#pragma once

#include "amigaport/cpu_state.hpp"
#include "amigaport/memory.hpp"

#include <cstddef>
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
    /* A native override returned without completing its guest boundary: it
     * neither continued execution nor moved the PC off its own address, so
     * resuming would re-enter it forever. */
    UnterminatedNativeOverride,
};

struct ExecutionExit final {
    ExitReason reason{ExitReason::InstructionBudget};
    // A native callback changed the current CPU PC and asks the active run to
    // continue without unwinding its guest-call boundary.
    bool continue_execution{};
    // A native callback deliberately ends the run and hands control back to the
    // host, which owns what executes next. The callback owes no guest return or
    // PC, so its boundary is complete.
    bool hand_off_to_host{};
    ExecutionIdentity identity{};
    std::uint32_t instructions{};
    std::uint64_t cycles{};
    MemoryFault memory_fault{MemoryFault::None};
    std::uint16_t instruction_word{};
};

/* One retired instruction, recorded for post-mortem diagnostics. The ring is
 * always on: a hung or faulting guest is otherwise unobservable from the host,
 * and one packed store per instruction is negligible beside an interpreted
 * 68000 instruction. */
struct ExecutionTraceEntry final {
    GuestAddress pc{};
    std::uint16_t opcode{};
    std::uint8_t image_tag{};
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

    /* Copy the most recently retired instructions, oldest first, into
     * `destination`. Lock-free and safe to call from a fatal signal handler or
     * another thread while the guest is running; entries are individually
     * consistent, and a concurrent run may retire more between reads. */
    [[nodiscard]] std::size_t recent_execution(ExecutionTraceEntry *destination,
                                               std::size_t capacity) const noexcept;
    [[nodiscard]] static std::size_t recent_execution_capacity() noexcept;

  private:
    class Impl;
    struct ImplDeleter final {
        void operator()(Impl *implementation) const noexcept;
    };
    std::unique_ptr<Impl, ImplDeleter> impl_;
};

} // namespace amigaport
