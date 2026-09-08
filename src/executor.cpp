#include "amigaport/executor.hpp"

#include "override_registry.hpp"
#include "puae_core.hpp"

#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace amigaport {

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
        const ImageGeneration starting_generation = image.generation;
        SliceProgress progress;

        while (progress.instructions < budget) {
            if (cpu.halted) {
                return make_exit(ExitReason::Halted, progress);
            }
            if (image.generation != starting_generation) {
                return make_exit(ExitReason::ImageReplaced, progress);
            }
            if (stop_at_pc && cpu.pc == *stop_at_pc) {
                return make_exit(ExitReason::ReturnToHost, progress);
            }
            const ExecutionIdentity current = identity();
            if (NativeOverride *function = overrides.find(current); function != nullptr) {
                ActiveOverrideScope active_scope(*this, current);
                ExecutionExit result = (*function)(owner());
                if (result.continue_execution)
                    continue;
                result.reason = ExitReason::NativeOverride;
                return result;
            }

            const detail::CoreStep step = core.step(cpu);
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
