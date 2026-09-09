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
        ActiveOverrideScope(Impl &impl, ExecutionIdentity identity) : impl_(impl) {
            impl_.active_overrides.push_back(identity);
        }

        ~ActiveOverrideScope() { impl_.active_overrides.pop_back(); }

        ActiveOverrideScope(const ActiveOverrideScope &) = delete;
        ActiveOverrideScope &operator=(const ActiveOverrideScope &) = delete;

      private:
        Impl &impl_;
    };

    [[nodiscard]] ExecutionExit run(std::uint32_t requested_budget, bool stop_on_rte = false,
                                    std::optional<GuestAddress> stop_at_pc = std::nullopt) {
        const std::uint32_t budget =
            requested_budget == 0U ? config.max_instructions_per_slice
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
            const ExecutionIdentity current = identity();
            if (NativeOverride *function = overrides.find(current); function != nullptr) {
                ExecutionExit result = run_override(*function, current);
                if (result.continue_execution) {
                    authorized_generation = image.generation;
                    continue;
                }
                return result;
            }

            const GuestAddress step_pc = cpu.pc;
            const detail::CoreStep step = core.step(cpu);
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
        const CpuState saved_state = cpu;
        if (saved_state.supervisor_stack_pointer < 6U) {
            return make_exit(ExitReason::MemoryFault, {});
        }

        const GuestAddress frame_address = saved_state.supervisor_stack_pointer - 6U;
        const auto saved_frame_sr = memory.read16(frame_address);
        const auto saved_frame_pc = memory.read32(frame_address + 2U);
        if (!saved_frame_sr || !saved_frame_pc) {
            ExecutionExit result = make_exit(ExitReason::MemoryFault, {});
            result.memory_fault = !saved_frame_sr ? saved_frame_sr.fault : saved_frame_pc.fault;
            return result;
        }

        const auto restore_frame = [&]() {
            (void)memory.write16({.address = frame_address, .value = saved_frame_sr.value});
            (void)memory.write32({.address = frame_address + 2U, .value = saved_frame_pc.value});
        };
        const auto frame_sr_fault = memory.write16({.address = frame_address, .value = cpu.sr});
        if (frame_sr_fault != MemoryFault::None) {
            ExecutionExit result = make_exit(ExitReason::MemoryFault, {});
            result.memory_fault = frame_sr_fault;
            return result;
        }
        const auto frame_pc_fault =
            memory.write32({.address = frame_address + 2U, .value = cpu.pc});
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
        const auto result = run(instruction_budget.value, true);
        if (result.reason != ExitReason::ReturnToHost) {
            cpu = saved_state;
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
        const std::uint64_t index = trace_written.load(std::memory_order_relaxed);
        trace[index % kExecutionTraceCapacity].store(pack_trace_entry(pc, opcode, image.tag.value),
                                                     std::memory_order_relaxed);
        trace_written.store(index + 1U, std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t copy_recent_execution(ExecutionTraceEntry *destination,
                                                    std::size_t capacity) const noexcept {
        if (destination == nullptr || capacity == 0U) {
            return 0U;
        }
        const std::uint64_t written = trace_written.load(std::memory_order_relaxed);
        const std::uint64_t available = std::min<std::uint64_t>(written, kExecutionTraceCapacity);
        const std::uint64_t wanted = std::min<std::uint64_t>(available, capacity);
        std::size_t count = 0U;
        for (std::uint64_t offset = wanted; offset > 0U; --offset) {
            const std::uint64_t packed =
                trace[(written - offset) % kExecutionTraceCapacity].load(std::memory_order_relaxed);
            if (unpack_trace_entry(packed, destination[count])) {
                ++count;
            }
        }
        return count;
    }

    void synchronize_active_stack_pointer() noexcept {
        const bool supervisor = (cpu.sr & 0x2000U) != 0U;
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
    detail::PuaeCore core;
    std::vector<ExecutionIdentity> active_overrides;
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

CpuState &Executor::state() noexcept { return impl_->cpu; }
const CpuState &Executor::state() const noexcept { return impl_->cpu; }
ImageIdentity Executor::image() const noexcept { return impl_->image; }

std::size_t Executor::recent_execution(ExecutionTraceEntry *destination,
                                       std::size_t capacity) const noexcept {
    return impl_->copy_recent_execution(destination, capacity);
}

std::size_t Executor::recent_execution_capacity() noexcept { return kExecutionTraceCapacity; }

ImageIdentity Executor::replace_image(ImageTag tag) {
    if (tag.value == 0U) {
        throw std::invalid_argument("replace_image requires a nonzero title-owned image tag");
    }
    if (impl_->image.generation == std::numeric_limits<ImageGeneration>::max()) {
        throw std::overflow_error("image generation exhausted");
    }
    impl_->image = {.tag = tag, .generation = impl_->image.generation + 1U};
    return impl_->image;
}

void Executor::register_override(ExecutionIdentity identity, NativeOverride function) {
    impl_->overrides.install(identity, std::move(function));
}

void Executor::remove_override(ExecutionIdentity identity) { impl_->overrides.remove(identity); }

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

ExecutionExit Executor::call(GuestAddress address, InstructionBudget instruction_budget) {
    impl_->cpu.pc = address;
    impl_->cpu.prefetch_valid = false;
    return execute(instruction_budget);
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
    const auto return_pc = impl_->memory.read32(impl_->cpu.address[7]);
    if (!return_pc) {
        ExecutionExit result = impl_->make_exit(ExitReason::MemoryFault, {});
        result.memory_fault = return_pc.fault;
        return result;
    }
    detail::OverrideRegistry::ScopedSuppression suppression(impl_->overrides,
                                                            impl_->active_overrides.back());
    return impl_->run(instruction_budget.value, false, return_pc.value);
}

} // namespace amigaport
