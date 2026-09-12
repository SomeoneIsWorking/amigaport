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
    /* Execution reached an address the host asked to stop on. The PC is left
     * ON that address, not past it, so the instruction has not run yet. */
    Breakpoint,
};

/* A host call enters guest code without pushing a synthetic return address.
 * The consumer therefore states whether the current guest stack already owns
 * a subroutine return or whether the target is a tail transfer. */
enum class CallBoundary : std::uint8_t {
    GuestSubroutine,
    TailTransfer,
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

/* Called the instant a breakpoint is reached, while the CPU is still exactly
 * on the address and nothing has unwound. The host inspects (or blocks) here;
 * the run ends with ExitReason::Breakpoint once it returns. */
using BreakpointHandler = std::function<void(Executor &)>;

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

    /* Stop execution when the PC reaches `address`, before the instruction
     * there runs, exiting with ExitReason::Breakpoint. A run never breaks on
     * its own first instruction, so resuming from a breakpoint continues past
     * it instead of stopping on it forever.
     *
     * set_breakpoint returns false if the address is already set or the set is
     * full (breakpoint_capacity); clear_breakpoint returns false if it was not
     * set. `breakpoints` copies out the current addresses in no order. */
    bool set_breakpoint(GuestAddress address);
    bool clear_breakpoint(GuestAddress address);
    void clear_breakpoints();
    [[nodiscard]] std::size_t breakpoints(GuestAddress *destination, std::size_t capacity) const;
    [[nodiscard]] static std::size_t breakpoint_capacity() noexcept;

    /* Where a breakpoint is observed. Without a handler the hit is only
     * reported through the exit, by which time the run has returned to its
     * caller and — if the breakpoint was inside a nested call such as an
     * interrupt delivery — the CPU has moved on. A host that wants the
     * registers AT the address must set this. */
    void set_breakpoint_handler(BreakpointHandler handler);

    [[nodiscard]] ExecutionExit execute(InstructionBudget instruction_budget = {});
    [[nodiscard]] ExecutionExit call(GuestAddress address,
                                     InstructionBudget instruction_budget = {});
    [[nodiscard]] ExecutionExit call(GuestAddress address, CallBoundary boundary,
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
