#include "amigaport/executor.hpp"

#include "override_registry.hpp"
#include "puae_core.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace amigaport {
namespace {

constexpr std::size_t kExecutionTraceCapacity = 256U;

/* One trace slot is a single relaxed 64-bit word so a reader in a fatal signal
 * handler or another thread always observes a whole entry, never a torn one. */
constexpr std::uint64_t pack_trace_entry(GuestAddress pc, std::uint16_t opcode,
                                         std::uint32_t image_tag) noexcept {
    return static_cast<std::uint64_t>(pc) | (static_cast<std::uint64_t>(opcode) << 32U) |
           (static_cast<std::uint64_t>(image_tag & 0xFFU) << 48U) | (1ULL << 56U);
}

constexpr bool unpack_trace_entry(std::uint64_t packed, ExecutionTraceEntry &entry) noexcept {
    if ((packed >> 56U) == 0U) {
        return false;
    }
    entry = {.pc = static_cast<GuestAddress>(packed),
             .opcode = static_cast<std::uint16_t>(packed >> 32U),
             .image_tag = static_cast<std::uint8_t>(packed >> 48U)};
    return true;
}

} // namespace

/* Guest addresses the host wants execution to stop on.
 *
 * A debugger needs to stop the guest at an address without the host having to
 * know which run will reach it. The set is checked once per instruction, so the
 * common case — no breakpoints — must cost a single branch on a member already
 * in cache; hence the `count_` fast path and a flat open-addressed table rather
 * than a node-based container.
 *
 * A run never breaks on its FIRST instruction. Stopping before an address is
 * executed is what makes a breakpoint useful, but it also means resuming would
 * stop again immediately on the same address and never make progress. Skipping
 * the first instruction of each run is what lets "continue" continue.
 */
class Breakpoints final {
  public:
    static constexpr std::size_t kCapacity = 64U;

    [[nodiscard]] bool empty() const noexcept {
        return count_ == 0U;
    }
    [[nodiscard]] std::size_t size() const noexcept {
        return count_;
    }

    [[nodiscard]] bool contains(GuestAddress address) const noexcept {
        for (std::size_t probe = 0U; probe < count_; ++probe) {
            if (addresses_[probe] == address) {
                return true;
            }
        }
        return false;
    }

    /* False when the set is full, or the address is already in it. */
    bool add(GuestAddress address) noexcept {
        if (count_ >= kCapacity || contains(address)) {
            return false;
        }
        addresses_[count_++] = address;
        return true;
    }

    bool remove(GuestAddress address) noexcept {
        for (std::size_t probe = 0U; probe < count_; ++probe) {
            if (addresses_[probe] == address) {
                addresses_[probe] = addresses_[--count_];
                return true;
            }
        }
        return false;
    }

    void clear() noexcept {
        count_ = 0U;
    }

    [[nodiscard]] std::size_t copy(GuestAddress *destination, std::size_t capacity) const noexcept {
        if (destination == nullptr) {
            return 0U;
        }
        std::size_t wanted = std::min(capacity, count_);
        for (std::size_t index = 0U; index < wanted; ++index) {
            destination[index] = addresses_[index];
        }
        return wanted;
    }

  private:
    std::array<GuestAddress, kCapacity> addresses_{};
    std::size_t count_{0U};
};

class Executor::Impl final {
  public:
    struct SliceProgress final {
        std::uint32_t instructions{};
        std::uint64_t cycles{};
    };

    Impl(RuntimeConfig config_value, Memory &memory_value, Logger &logger_value)
        : config(config_value), memory(memory_value), logger(logger_value),
          core(memory_value, logger_value) {
        if (config.max_instructions_per_slice == 0U) {
            throw std::invalid_argument("max_instructions_per_slice must be nonzero");
        }
    }

    [[nodiscard]] ExecutionIdentity identity() const noexcept {
        return {.image = image, .address = cpu.pc};
    }

    class ActiveOverrideScope final {
      public:
        ActiveOverrideScope(Impl &impl, ExecutionIdentity identity)
            : impl_(impl), suppression_(impl.overrides, identity) {
            impl_.active_overrides.push_back(identity);
        }

        ~ActiveOverrideScope() {
            impl_.active_overrides.pop_back();
        }

        ActiveOverrideScope(const ActiveOverrideScope &) = delete;
        ActiveOverrideScope &operator=(const ActiveOverrideScope &) = delete;

      private:
        Impl &impl_;
        detail::OverrideRegistry::ScopedSuppression suppression_;
    };

    [[nodiscard]] ExecutionExit run(std::uint32_t requested_budget, bool stop_on_rte = false,
                                    std::optional<GuestAddress> stop_at_pc = std::nullopt) {
        std::uint32_t budget = requested_budget == 0U
                                   ? config.max_instructions_per_slice
                                   : std::min(requested_budget, config.max_instructions_per_slice);
        /* The run is authorized for the image it started in; a replacement from
         * anywhere else must end it. A native override that replaces the image
         * and asks to continue re-authorizes the run for its new image — that
         * act IS the transition, and unwinding it would strand the guest flow
         * that jumped into the new image. */
        ImageGeneration authorized_generation = image.generation;
        SliceProgress progress;

        while (progress.instructions < budget) {
            if (cpu.halted) {
                return make_exit(ExitReason::Halted, progress);
            }
            if (image.generation != authorized_generation) {
                return make_exit(ExitReason::ImageReplaced, progress);
            }
            if (stop_at_pc && cpu.pc == *stop_at_pc) {
                return make_exit(ExitReason::ReturnToHost, progress);
            }
            /* See Breakpoints: never on the first instruction of a run, or
             * resuming from a breakpoint could not make progress. */
            if (!breakpoints.empty() && progress.instructions > 0U &&
                breakpoints.contains(cpu.pc)) {
                /* Observed here, on the address, before anything unwinds. */
                if (breakpoint_handler) {
                    breakpoint_handler(owner());
                }
                return make_exit(ExitReason::Breakpoint, progress);
            }
            ExecutionIdentity current = identity();
            bool original_once = pending_original == current;
            if (original_once) {
                pending_original.reset();
            } else {
                if (NativeOverride *function = overrides.find(current); function != nullptr) {
                    ExecutionExit result = run_override(*function, current);
                    if (result.continue_execution) {
                        authorized_generation = image.generation;
                        continue;
                    }
                    return result;
                }
            }

            GuestAddress step_pc = cpu.pc;
            detail::CoreStep step = core.step(cpu);
            record_execution(step_pc, step.instruction_word);
            if (step.status == detail::CoreStep::Status::MemoryFault) {
                logger.write(LogLevel::Error, "cpu", "68000 memory access failed");
                ExecutionExit result = make_exit(ExitReason::MemoryFault, progress);
                result.memory_fault = step.memory_fault;
                cpu.exception = {.active_vector = step.memory_fault == MemoryFault::Misaligned
                                                      ? ExceptionVector::AddressError
                                                      : ExceptionVector::BusError,
                                 .fault_address = step.fault_address,
                                 .instruction_word = step.instruction_word};
                return result;
            }
            if (step.status == detail::CoreStep::Status::Exception) {
                if (step.instruction_executed) {
                    ++progress.instructions;
                    ++cpu.executed_instructions;
                }
                progress.cycles += step.cycles;
                cpu.elapsed_cycles += step.cycles;
                cpu.exception = {.active_vector = step.exception_vector,
                                 .instruction_word = step.instruction_word};
                ExecutionExit result = make_exit(ExitReason::Exception, progress);
                result.instruction_word = step.instruction_word;
                return result;
            }
            if (step.status == detail::CoreStep::Status::ReturnedFromInterrupt && stop_on_rte) {
                ++progress.instructions;
                ++cpu.executed_instructions;
                progress.cycles += step.cycles;
                cpu.elapsed_cycles += step.cycles;
                return make_exit(ExitReason::ReturnToHost, progress);
            }
            if (step.status == detail::CoreStep::Status::Halted) {
                return make_exit(ExitReason::Halted, progress);
            }

            ++progress.instructions;
            progress.cycles += step.cycles;
            ++cpu.executed_instructions;
            cpu.elapsed_cycles += step.cycles;
        }

        return make_exit(ExitReason::InstructionBudget, progress);
    }

    /* Run one native override and hold it to its boundary. A replacement that
     * returns with the PC still on its own address has consumed neither the
     * guest call nor a continuation, so resuming would re-enter it forever;
     * fail closed with the address instead of hanging the host. */
    [[nodiscard]] ExecutionExit run_override(NativeOverride &function, ExecutionIdentity current) {
        ActiveOverrideScope active_scope(*this, current);
        ExecutionExit result = function(owner());
        if (result.continue_execution) {
            return result;
        }
        if (result.hand_off_to_host) {
            /* The override ended the run on purpose; the host owns what happens
             * next, so this override's PC is not the executor's business. */
            result.reason = ExitReason::ReturnToHost;
            return result;
        }
        /* Every override must complete its guest boundary. A nested host
         * subroutine has its own synthetic return on A7. */
        if (cpu.pc == current.address) {
            logger.write(LogLevel::Error, "executor",
                         "native override returned without completing its guest boundary");
            result = make_exit(ExitReason::UnterminatedNativeOverride, {});
            result.identity = current;
            return result;
        }
        result.reason = ExitReason::NativeOverride;
        return result;
    }

    [[nodiscard]] ExecutionExit make_exit(ExitReason reason,
                                          SliceProgress progress) const noexcept {
        return {.reason = reason,
                .identity = identity(),
                .instructions = progress.instructions,
                .cycles = progress.cycles};
    }

    [[nodiscard]] ExecutionExit call_interrupt(GuestAddress address,
                                               InstructionBudget instruction_budget) {
        synchronize_active_stack_pointer();
        CpuState saved_state = cpu;
        if (saved_state.supervisor_stack_pointer < 6U) {
            return make_exit(ExitReason::MemoryFault, {});
        }

        GuestAddress frame_address = saved_state.supervisor_stack_pointer - 6U;
        auto saved_frame_sr = memory.read16(frame_address);
        auto saved_frame_pc = memory.read32(frame_address + 2U);
        if (!saved_frame_sr || !saved_frame_pc) {
            ExecutionExit result = make_exit(ExitReason::MemoryFault, {});
            result.memory_fault = !saved_frame_sr ? saved_frame_sr.fault : saved_frame_pc.fault;
            return result;
        }

        auto restore_frame = [&]() {
            (void)memory.write16({.address = frame_address, .value = saved_frame_sr.value});
            (void)memory.write32({.address = frame_address + 2U, .value = saved_frame_pc.value});
        };
        auto frame_sr_fault = memory.write16({.address = frame_address, .value = cpu.sr});
        if (frame_sr_fault != MemoryFault::None) {
            ExecutionExit result = make_exit(ExitReason::MemoryFault, {});
            result.memory_fault = frame_sr_fault;
            return result;
        }
        auto frame_pc_fault = memory.write32({.address = frame_address + 2U, .value = cpu.pc});
        if (frame_pc_fault != MemoryFault::None) {
            restore_frame();
            ExecutionExit result = make_exit(ExitReason::MemoryFault, {});
            result.memory_fault = frame_pc_fault;
            return result;
        }

        cpu.supervisor_stack_pointer = frame_address;
        cpu.address[7] = frame_address;
        cpu.sr = static_cast<std::uint16_t>(saved_state.sr | 0x2000U);
        cpu.pc = address;
        cpu.prefetch_valid = false;
        auto result = run(instruction_budget.value, true);
        if (result.reason != ExitReason::ReturnToHost) {
            /* Architectural state rolls back, but guest TIME does not: those
             * cycles were really spent, and a host that derives the video beam
             * from this counter must not see them un-happen. Carry the counter
             * across the restore. */
            std::uint64_t spent = cpu.elapsed_cycles;
            cpu = saved_state;
            cpu.elapsed_cycles = spent;
            restore_frame();
        }
        return result;
    }

    [[nodiscard]] Executor &owner() {
        if (owner_pointer == nullptr) {
            throw std::logic_error("executor owner is not bound");
        }
        return *owner_pointer;
    }

    void record_execution(GuestAddress pc, std::uint16_t opcode) noexcept {
        std::uint64_t index = trace_written.load(std::memory_order_relaxed);
        trace[index % kExecutionTraceCapacity].store(pack_trace_entry(pc, opcode, image.tag.value),
                                                     std::memory_order_relaxed);
        trace_written.store(index + 1U, std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t copy_recent_execution(ExecutionTraceEntry *destination,
                                                    std::size_t capacity) const noexcept {
        if (destination == nullptr || capacity == 0U) {
            return 0U;
        }
        std::uint64_t written = trace_written.load(std::memory_order_relaxed);
        std::uint64_t available = std::min<std::uint64_t>(written, kExecutionTraceCapacity);
        std::uint64_t wanted = std::min<std::uint64_t>(available, capacity);
        std::size_t count = 0U;
        for (std::uint64_t offset = wanted; offset > 0U; --offset) {
            std::uint64_t packed =
                trace[(written - offset) % kExecutionTraceCapacity].load(std::memory_order_relaxed);
            if (unpack_trace_entry(packed, destination[count])) {
                ++count;
            }
        }
        return count;
    }

    void synchronize_active_stack_pointer() noexcept {
        bool supervisor = (cpu.sr & 0x2000U) != 0U;
        if (supervisor) {
            cpu.supervisor_stack_pointer = cpu.address[7];
        } else {
            cpu.user_stack_pointer = cpu.address[7];
        }
    }

    RuntimeConfig config;
    Memory &memory;
    Logger &logger;
    CpuState cpu{};
    ImageIdentity image{};
    detail::OverrideRegistry overrides;
    Breakpoints breakpoints;
    BreakpointHandler breakpoint_handler;
    detail::PuaeCore core;
    std::vector<ExecutionIdentity> active_overrides;
    std::optional<ExecutionIdentity> pending_original;
    std::array<std::atomic<std::uint64_t>, kExecutionTraceCapacity> trace{};
    std::atomic<std::uint64_t> trace_written{0};
    Executor *owner_pointer{};
};

Executor::Executor(RuntimeConfig config, Memory &memory, Logger &logger)
    : impl_(new Impl(config, memory, logger)) {
    impl_->owner_pointer = this;
}

void Executor::ImplDeleter::operator()(Impl *implementation) const noexcept {
    delete implementation;
}

CpuState &Executor::state() noexcept {
    return impl_->cpu;
}
const CpuState &Executor::state() const noexcept {
    return impl_->cpu;
}
ImageIdentity Executor::image() const noexcept {
    return impl_->image;
}

std::size_t Executor::recent_execution(ExecutionTraceEntry *destination,
                                       std::size_t capacity) const noexcept {
    return impl_->copy_recent_execution(destination, capacity);
}

std::size_t Executor::recent_execution_capacity() noexcept {
    return kExecutionTraceCapacity;
}

ImageIdentity Executor::replace_image(ImageTag tag) {
    if (tag.value == 0U) {
        throw std::invalid_argument("replace_image requires a nonzero title-owned image tag");
    }
    if (impl_->image.generation == std::numeric_limits<ImageGeneration>::max()) {
        throw std::overflow_error("image generation exhausted");
    }
    impl_->image = {.tag = tag, .generation = impl_->image.generation + 1U};
    impl_->pending_original.reset();
    return impl_->image;
}

void Executor::register_override(ExecutionIdentity identity, NativeOverride function) {
    impl_->overrides.install(identity, std::move(function));
}

void Executor::remove_override(ExecutionIdentity identity) {
    impl_->overrides.remove(identity);
}

ExecutionExit Executor::execute(InstructionBudget instruction_budget) {
    if (impl_->image.tag.value == 0U) {
        return impl_->make_exit(ExitReason::NoImage, {});
    }
    impl_->synchronize_active_stack_pointer();
    if (!cpu_state_is_valid(impl_->cpu)) {
        throw std::invalid_argument("CPU state is not a valid 68000 architectural state");
    }
    return impl_->run(instruction_budget.value);
}

bool Executor::set_breakpoint(GuestAddress address) {
    return impl_->breakpoints.add(address);
}

bool Executor::clear_breakpoint(GuestAddress address) {
    return impl_->breakpoints.remove(address);
}

void Executor::clear_breakpoints() {
    impl_->breakpoints.clear();
}

std::size_t Executor::breakpoints(GuestAddress *destination, std::size_t capacity) const {
    return impl_->breakpoints.copy(destination, capacity);
}

std::size_t Executor::breakpoint_capacity() noexcept {
    return Breakpoints::kCapacity;
}

void Executor::set_breakpoint_handler(BreakpointHandler handler) {
    impl_->breakpoint_handler = std::move(handler);
}

ExecutionExit Executor::call(GuestAddress address, InstructionBudget instruction_budget) {
    return call(address, CallBoundary::GuestSubroutine, instruction_budget);
}

ExecutionExit Executor::call(GuestAddress address, CallBoundary boundary,
                             InstructionBudget instruction_budget, CallContinuation *continuation) {
    if (continuation != nullptr) {
        *continuation = {};
    }
    if (impl_->image.tag.value == 0U) {
        return impl_->make_exit(ExitReason::NoImage, {});
    }
    if (boundary == CallBoundary::HostSubroutine && impl_->active_overrides.empty()) {
        throw std::logic_error("host subroutine requires an active native override");
    }
    if (boundary == CallBoundary::HostSubroutine && continuation == nullptr) {
        throw std::invalid_argument("host subroutine requires a continuation token");
    }
    impl_->synchronize_active_stack_pointer();
    if (!cpu_state_is_valid(impl_->cpu)) {
        throw std::invalid_argument("CPU state is not a valid 68000 architectural state");
    }

    std::optional<GuestAddress> stop_at_pc = std::nullopt;
    if (!impl_->active_overrides.empty() && boundary == CallBoundary::HostSubroutine) {
        GuestAddress return_pc = impl_->cpu.pc;
        GuestAddress stack_pointer = impl_->cpu.address[7];
        if (stack_pointer < 4U) {
            ExecutionExit result = impl_->make_exit(ExitReason::MemoryFault, {});
            result.memory_fault = MemoryFault::Unmapped;
            return result;
        }
        MemoryFault fault =
            impl_->memory.write32({.address = stack_pointer - 4U, .value = return_pc});
        if (fault != MemoryFault::None) {
            ExecutionExit result = impl_->make_exit(ExitReason::MemoryFault, {});
            result.memory_fault = fault;
            return result;
        }
        impl_->cpu.address[7] = stack_pointer - 4U;
        stop_at_pc = return_pc;
    } else if (!impl_->active_overrides.empty() && boundary == CallBoundary::GuestSubroutine) {
        auto return_pc = impl_->memory.read32(impl_->cpu.address[7]);
        if (!return_pc) {
            ExecutionExit result = impl_->make_exit(ExitReason::MemoryFault, {});
            result.memory_fault = return_pc.fault;
            return result;
        }
        stop_at_pc = return_pc.value;
    }
    impl_->cpu.pc = address;
    impl_->cpu.prefetch_valid = false;
    if (continuation != nullptr) {
        *continuation = {.boundary = boundary,
                         .return_pc = stop_at_pc.value_or(0U),
                         .image = impl_->image,
                         .owner = impl_->active_overrides.empty() ? ExecutionIdentity{}
                                                                  : impl_->active_overrides.back(),
                         .valid = stop_at_pc.has_value()};
    }
    if (!impl_->active_overrides.empty()) {
        if (boundary == CallBoundary::HostSubroutine) {
            return impl_->run(instruction_budget.value, false, stop_at_pc);
        }
        detail::OverrideRegistry::ScopedSuppression suppression(impl_->overrides,
                                                                impl_->active_overrides.back());
        return impl_->run(instruction_budget.value, false, stop_at_pc);
    }

    return impl_->run(instruction_budget.value);
}

ExecutionExit Executor::continue_call(const CallContinuation &continuation,
                                      InstructionBudget instruction_budget) {
    if (!continuation.valid || continuation.boundary == CallBoundary::TailTransfer ||
        impl_->active_overrides.empty()) {
        throw std::invalid_argument("no active subroutine call to continue");
    }
    if (continuation.image != impl_->image) {
        return impl_->make_exit(ExitReason::ImageReplaced, {});
    }
    if (continuation.owner != impl_->active_overrides.back()) {
        throw std::logic_error("call continuation belongs to another native override");
    }
    impl_->synchronize_active_stack_pointer();
    if (!cpu_state_is_valid(impl_->cpu)) {
        throw std::invalid_argument("CPU state is not a valid 68000 architectural state");
    }
    if (continuation.boundary == CallBoundary::GuestSubroutine) {
        detail::OverrideRegistry::ScopedSuppression suppression(impl_->overrides,
                                                                impl_->active_overrides.back());
        return impl_->run(instruction_budget.value, false, continuation.return_pc);
    }
    return impl_->run(instruction_budget.value, false, continuation.return_pc);
}

ExecutionExit Executor::call_interrupt(GuestAddress address, InstructionBudget instruction_budget) {
    return impl_->call_interrupt(address, instruction_budget);
}

ExecutionExit Executor::call_original(InstructionBudget instruction_budget) {
    if (impl_->active_overrides.empty()) {
        throw std::logic_error("call_original requires an active native override");
    }
    detail::OverrideRegistry::ScopedSuppression suppression(impl_->overrides,
                                                            impl_->active_overrides.back());
    impl_->synchronize_active_stack_pointer();
    return impl_->run(instruction_budget.value);
}

ExecutionExit Executor::call_original_subroutine(InstructionBudget instruction_budget) {
    if (impl_->active_overrides.empty()) {
        throw std::logic_error("call_original_subroutine requires an active native override");
    }
    impl_->synchronize_active_stack_pointer();
    auto return_pc = impl_->memory.read32(impl_->cpu.address[7]);
    if (!return_pc) {
        ExecutionExit result = impl_->make_exit(ExitReason::MemoryFault, {});
        result.memory_fault = return_pc.fault;
        return result;
    }
    detail::OverrideRegistry::ScopedSuppression suppression(impl_->overrides,
                                                            impl_->active_overrides.back());
    return impl_->run(instruction_budget.value, false, return_pc.value);
}

ExecutionExit Executor::continue_original() {
    if (impl_->active_overrides.empty()) {
        throw std::logic_error("continue_original requires an active native override");
    }
    ExecutionIdentity current = impl_->active_overrides.back();
    if (impl_->image != current.image || impl_->cpu.pc != current.address ||
        impl_->pending_original.has_value()) {
        throw std::logic_error("continue_original requires the current override PC");
    }
    impl_->pending_original = current;
    ExecutionExit result{};
    result.reason = ExitReason::NativeOverride;
    result.continue_execution = true;
    result.identity = current;
    return result;
}

} // namespace amigaport
