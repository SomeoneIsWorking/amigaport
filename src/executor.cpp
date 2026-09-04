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
    Impl(RuntimeConfig config_value, Memory &memory_value, Logger &logger_value)
        : config(config_value), memory(memory_value), logger(logger_value) {
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

    [[nodiscard]] ExecutionExit run(std::uint32_t requested_budget) {
        const std::uint32_t budget =
            requested_budget == 0U ? config.max_instructions_per_slice
                                   : std::min(requested_budget, config.max_instructions_per_slice);
        const ImageGeneration starting_generation = image.generation;
        std::uint32_t executed{};
        std::uint64_t cycles{};

        while (executed < budget) {
            if (cpu.halted || cpu.stopped) {
                return make_exit(ExitReason::Halted, executed, cycles);
            }
            if (image.generation != starting_generation) {
                return make_exit(ExitReason::ImageReplaced, executed, cycles);
            }

            const ExecutionIdentity current = identity();
            if (NativeOverride *function = overrides.find(current); function != nullptr) {
                ActiveOverrideScope active_scope(*this, current);
                ExecutionExit result = (*function)(owner());
                result.reason = ExitReason::NativeOverride;
                return result;
            }

            const auto fetch = memory.read16(cpu.pc);
            if (!fetch) {
                logger.write(LogLevel::Error, "cpu", "instruction fetch failed");
                ExecutionExit result = make_exit(ExitReason::MemoryFault, executed, cycles);
                result.memory_fault = fetch.fault;
                cpu.exception = {.active_vector = fetch.fault == MemoryFault::Misaligned
                                                      ? ExceptionVector::AddressError
                                                      : ExceptionVector::BusError,
                                 .fault_address = cpu.pc};
                return result;
            }

            cpu.instruction_register = fetch.value;
            const detail::CoreStep step = core.step(cpu, fetch.value);
            if (!step.supported) {
                logger.write(LogLevel::Error, "cpu", "unsupported 68000 instruction");
                ExecutionExit result =
                    make_exit(ExitReason::UnsupportedInstruction, executed, cycles);
                result.instruction_word = fetch.value;
                cpu.exception = {.active_vector = ExceptionVector::IllegalInstruction,
                                 .fault_address = cpu.pc,
                                 .instruction_word = fetch.value};
                return result;
            }

            ++executed;
            cycles += step.cycles;
            ++cpu.executed_instructions;
            cpu.elapsed_cycles += step.cycles;
        }

        return make_exit(ExitReason::InstructionBudget, executed, cycles);
    }

    [[nodiscard]] ExecutionExit make_exit(ExitReason reason, std::uint32_t instructions,
                                          std::uint64_t cycles) const noexcept {
        return {.reason = reason,
                .identity = identity(),
                .instructions = instructions,
                .cycles = cycles};
    }

    [[nodiscard]] Executor &owner() {
        if (owner_pointer == nullptr) {
            throw std::logic_error("executor owner is not bound");
        }
        return *owner_pointer;
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
    : impl_(std::make_unique<Impl>(config, memory, logger)) {
    impl_->owner_pointer = this;
}

Executor::~Executor() = default;

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
        return impl_->make_exit(ExitReason::NoImage, 0, 0);
    }
    if (!cpu_state_is_valid(impl_->cpu)) {
        throw std::invalid_argument("CPU state is not a valid 68000 architectural state");
    }
    return impl_->run(instruction_budget.value);
}

ExecutionExit Executor::call(GuestAddress address, InstructionBudget instruction_budget) {
    impl_->cpu.pc = address;
    return execute(instruction_budget);
}

ExecutionExit Executor::call_original(InstructionBudget instruction_budget) {
    if (impl_->active_overrides.empty()) {
        throw std::logic_error("call_original requires an active native override");
    }
    detail::OverrideRegistry::ScopedSuppression suppression(impl_->overrides,
                                                            impl_->active_overrides.back());
    return impl_->run(instruction_budget.value);
}

} // namespace amigaport
