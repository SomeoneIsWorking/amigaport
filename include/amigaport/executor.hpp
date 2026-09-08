#pragma once

#include "amigaport/cpu_state.hpp"
#include "amigaport/memory.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace amigaport {

enum class ExitReason : std::uint8_t {
    NoImage,
    InstructionBudget,
    NativeOverride,
    ReturnToHost,
    MemoryFault,
    UnsupportedInstruction,
    Exception,
    Halted,
    ImageReplaced,
};

struct ExecutionExit final {
    ExitReason reason{ExitReason::InstructionBudget};
    // A native callback changed the current CPU PC and asks the active run to
    // continue without unwinding its guest-call boundary.
    bool continue_execution{};
    ExecutionIdentity identity{};
    std::uint32_t instructions{};
    std::uint64_t cycles{};
    MemoryFault memory_fault{MemoryFault::None};
    std::uint16_t instruction_word{};
};

class Executor;

using NativeOverride = std::function<ExecutionExit(Executor &)>;

class Executor final {
  public:
    Executor(RuntimeConfig config, Memory &memory, Logger &logger);
    ~Executor() = default;

    Executor(const Executor &) = delete;
    Executor &operator=(const Executor &) = delete;
    Executor(Executor &&) = delete;
    Executor &operator=(Executor &&) = delete;

    [[nodiscard]] CpuState &state() noexcept;
    [[nodiscard]] const CpuState &state() const noexcept;
    [[nodiscard]] ImageIdentity image() const noexcept;

    ImageIdentity replace_image(ImageTag tag);
    void register_override(ExecutionIdentity identity, NativeOverride function);
    void remove_override(ExecutionIdentity identity);

    [[nodiscard]] ExecutionExit execute(InstructionBudget instruction_budget = {});
    [[nodiscard]] ExecutionExit call(GuestAddress address,
                                     InstructionBudget instruction_budget = {});
    [[nodiscard]] ExecutionExit call_interrupt(GuestAddress address,
                                               InstructionBudget instruction_budget = {});
    [[nodiscard]] ExecutionExit call_original(InstructionBudget instruction_budget = {});
    [[nodiscard]] ExecutionExit call_original_subroutine(InstructionBudget instruction_budget = {});

  private:
    class Impl;
    struct ImplDeleter final {
        void operator()(Impl *implementation) const noexcept;
    };
    std::unique_ptr<Impl, ImplDeleter> impl_;
};

} // namespace amigaport
