#include "test_support.hpp"
#include "uae/m68k_embed.h"

#include <cstddef>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <type_traits>

static_assert(std::is_trivially_copyable_v<amigaport::CpuState>);

namespace {

constexpr amigaport::ImageTag main_image{1};
constexpr amigaport::ImageTag title_image{2};

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

struct RawMemory final {
    std::array<std::uint8_t, 16> bytes{};
    uae_m68k_context *nested_context{};
    uae_m68k_state *nested_state{};
    bool enter_nested{};
};

uae_m68k_memory_status raw_read(void *user, const uae_m68k_read_request *request,
                                std::uint32_t *value) {
    auto &memory = *static_cast<RawMemory *>(user);
    if (memory.enter_nested) {
        memory.enter_nested = false;
        const auto nested = uae_m68k_step(memory.nested_context, memory.nested_state);
        if (nested.status != UAE_M68K_STEP_OK) {
            return UAE_M68K_MEMORY_UNMAPPED;
        }
    }
    if (request->width != 2U || request->address + 1U >= memory.bytes.size()) {
        return UAE_M68K_MEMORY_UNMAPPED;
    }
    *value = (static_cast<std::uint32_t>(memory.bytes[request->address]) << 8U) |
             memory.bytes[request->address + 1U];
    return UAE_M68K_MEMORY_OK;
}

uae_m68k_memory_status raw_write(void *, const uae_m68k_write_request *) {
    return UAE_M68K_MEMORY_READ_ONLY;
}

void raw_load16(RawMemory &memory, amigaport::MemoryWrite<std::uint16_t> write) {
    memory.bytes.at(write.address) = static_cast<std::uint8_t>(write.value >> 8U);
    memory.bytes.at(write.address + 1U) = static_cast<std::uint8_t>(write.value);
}

void test_upstream_moveq_and_budget_exit() {
    VectorMemory memory(16);
    RecordingLogger logger;
    memory.load16({.address = 0, .value = 0x76FF}); // MOVEQ #-1,D3
    memory.load16({.address = 2, .value = 0x4E71}); // NOP

    amigaport::Executor executor({.max_instructions_per_slice = 8}, memory, logger);
    for (std::size_t index = 0; index < 8; ++index) {
        executor.state().data[index] = 0x11000000U + static_cast<std::uint32_t>(index);
        executor.state().address[index] = 0x22000000U + static_cast<std::uint32_t>(index);
    }
    executor.state().sr = 0x2713; // supervisor, mask 7, X, V, and C
    executor.state().user_stack_pointer = 0x00123456;
    executor.state().supervisor_stack_pointer = 0x00654320;
    executor.state().address[7] = executor.state().supervisor_stack_pointer;
    executor.state().pending_interrupt_level = 3;
    require(executor.execute({.value = 1}).reason == amigaport::ExitReason::NoImage,
            "executor ran before an image was activated");
    executor.replace_image(main_image);
    const auto result = executor.execute({.value = 2});

    require(result.reason == amigaport::ExitReason::InstructionBudget, "budget exit missing");
    require(result.instructions == 2, "instruction denominator is wrong");
    require(result.cycles == 8, "PUAE cycle count is wrong");
    require(executor.state().data[3] == 0xFFFFFFFFU, "PUAE MOVEQ result is wrong");
    require(executor.state().data[2] == 0x11000002U, "unrelated data register was lost");
    require(executor.state().address[6] == 0x22000006U, "address register was lost");
    require(executor.state().sr == 0x2718U, "PUAE MOVEQ status result is wrong");
    require(executor.state().user_stack_pointer == 0x00123456U, "USP was lost");
    require(executor.state().supervisor_stack_pointer == 0x00654320U, "SSP was lost");
    require(executor.state().pending_interrupt_level == 3U, "pending interrupt state was lost");
    require(executor.state().pc == 4U, "PUAE PC advancement is wrong");
}

void test_image_qualified_override_and_original_call() {
    VectorMemory memory(16);
    RecordingLogger logger;
    memory.load16({.address = 0, .value = 0x7007}); // MOVEQ #7,D0

    amigaport::Executor executor({.max_instructions_per_slice = 4}, memory, logger);
    executor.state().sr = 0x2000;
    executor.state().address[7] = 0;
    executor.state().supervisor_stack_pointer = 0;
    executor.replace_image(main_image);
    const auto identity = amigaport::ExecutionIdentity{.image = executor.image(), .address = 0};
    bool override_entered = false;
    executor.register_override(identity, [&](amigaport::Executor &runtime) {
        override_entered = true;
        return runtime.call_original({.value = 1});
    });

    const auto overridden = executor.call(0, {.value = 1});
    require(override_entered, "matching image-qualified override was not entered");
    require(overridden.reason == amigaport::ExitReason::NativeOverride, "override exit is untyped");
    require(executor.state().data[0] == 7U, "scoped original did not run upstream guest body");

    executor.state().data[0] = 0;
    executor.replace_image(title_image);
    const auto replaced = executor.call(0, {.value = 1});
    require(replaced.reason == amigaport::ExitReason::InstructionBudget,
            "stale-generation override remained active");
    require(executor.state().data[0] == 7U, "new image did not execute guest body");

    const auto replacement_identity =
        amigaport::ExecutionIdentity{.image = executor.image(), .address = 0};
    executor.register_override(replacement_identity, [&](amigaport::Executor &runtime) {
        return runtime.call_original({.value = 1});
    });
    executor.remove_override(replacement_identity);
    executor.state().data[0] = 0;
    const auto removed = executor.call(0, {.value = 1});
    require(removed.reason == amigaport::ExitReason::InstructionBudget,
            "removed override remained active");
    require(executor.state().data[0] == 7U, "removed override blocked guest execution");
}

void test_native_override_can_continue_without_unwinding_guest_call() {
    VectorMemory memory(16);
    RecordingLogger logger;
    memory.load16({.address = 2, .value = 0x7007}); // MOVEQ #7,D0

    amigaport::Executor executor({.max_instructions_per_slice = 4}, memory, logger);
    executor.state().sr = 0x2000;
    executor.state().address[7] = 0;
    executor.replace_image(main_image);
    const auto identity = amigaport::ExecutionIdentity{.image = executor.image(), .address = 0};
    executor.register_override(identity, [](amigaport::Executor &runtime) {
        runtime.state().pc = 2;
        runtime.state().prefetch_valid = false;
        amigaport::ExecutionExit result{};
        result.continue_execution = true;
        return result;
    });

    const auto result = executor.call(0, {.value = 1});
    require(result.reason == amigaport::ExitReason::InstructionBudget,
            "native continuation did not stay inside the guest run");
    require(result.instructions == 1U, "native continuation changed guest instruction accounting");
    require(executor.state().data[0] == 7U, "native continuation did not execute its target");
}

void test_nested_call_dispatches_guest_and_stops_at_its_caller_return() {
    VectorMemory memory(256);
    RecordingLogger logger;
    memory.load16({.address = 0x20, .value = 0x7007}); // MOVEQ #7,D0
    memory.load16({.address = 0x22, .value = 0x4E75}); // RTS
    memory.load16({.address = 0x40, .value = 0x4E71}); // NOP after the native call
    memory.load32({.address = 0x80, .value = 0x40});

    amigaport::Executor executor({.max_instructions_per_slice = 8}, memory, logger);
    executor.state().sr = 0x2700;
    executor.state().address[7] = 0x80;
    executor.state().supervisor_stack_pointer = 0x80;
    executor.replace_image(main_image);

    const auto entry = amigaport::ExecutionIdentity{.image = executor.image(), .address = 0};
    executor.register_override(entry, [&](amigaport::Executor &runtime) {
        const auto nested = runtime.call(0x20, {.value = 4});
        require(nested.reason == amigaport::ExitReason::ReturnToHost,
                "nested guest call did not stop at its guest return");
        require(runtime.state().pc == 0x40U, "nested guest call crossed its return boundary");
        amigaport::ExecutionExit result{};
        result.continue_execution = true;
        return result;
    });

    const auto result = executor.call(0, {.value = 1});
    require(result.reason == amigaport::ExitReason::InstructionBudget,
            "outer native call did not resume after nested guest call");
    require(executor.state().data[0] == 7U, "nested guest call did not execute its body");
    require(executor.state().address[7] == 0x84U,
            "nested guest call did not consume its guest return address");
}

void test_explicit_tail_transfer_does_not_consume_guest_stack() {
    VectorMemory memory(256);
    RecordingLogger logger;
    memory.load16({.address = 0x20, .value = 0x7007}); // MOVEQ #7,D0

    amigaport::Executor executor({.max_instructions_per_slice = 4}, memory, logger);
    executor.state().sr = 0x2700;
    executor.state().address[7] = 0;
    executor.replace_image(main_image);
    const auto entry = amigaport::ExecutionIdentity{.image = executor.image(), .address = 0};
    executor.register_override(entry, [&](amigaport::Executor &runtime) {
        const auto nested = runtime.call(0x20, amigaport::CallBoundary::TailTransfer, {.value = 1});
        require(nested.reason == amigaport::ExitReason::InstructionBudget,
                "explicit tail transfer did not remain in the guest run");
        require(runtime.state().pc == 0x22U, "explicit tail transfer did not advance the guest PC");
        amigaport::ExecutionExit result{};
        result.continue_execution = true;
        return result;
    });

    const auto result = executor.call(0, {.value = 1});
    require(result.reason == amigaport::ExitReason::InstructionBudget,
            "outer native call did not resume after the tail transfer");
    require(executor.state().data[0] == 7U, "tail transfer did not execute its target");
}

void test_native_stack_view_reconciles_before_guest_reentry() {
    VectorMemory memory(16);
    RecordingLogger logger;
    memory.load16({.address = 0, .value = 0x4E71}); // NOP

    amigaport::Executor executor({.max_instructions_per_slice = 4}, memory, logger);
    executor.state().sr = 0x2700;
    executor.state().address[7] = 0;
    executor.state().supervisor_stack_pointer = 0;
    executor.replace_image(main_image);
    const auto identity = amigaport::ExecutionIdentity{.image = executor.image(), .address = 0};
    executor.register_override(identity, [&](amigaport::Executor &runtime) {
        runtime.state().address[7] = 0x80;
        return runtime.call_original({.value = 1});
    });

    const auto result = executor.call(0, {.value = 1});
    require(result.reason == amigaport::ExitReason::NativeOverride,
            "stack-view override did not return through its native boundary");
    require(executor.state().supervisor_stack_pointer == 0x80U,
            "active A7 was not reconciled into the supervisor stack pointer");
    require(executor.state().address[7] == executor.state().supervisor_stack_pointer,
            "reconciled architectural stack state is inconsistent");
}

void test_original_subroutine_returns_at_guest_rts() {
    VectorMemory memory(256);
    RecordingLogger logger;
    memory.load16({.address = 0, .value = 0x7007}); // MOVEQ #7,D0
    memory.load16({.address = 2, .value = 0x4E75}); // RTS
    memory.load32({.address = 0x80, .value = 0x40});

    amigaport::Executor executor({.max_instructions_per_slice = 8}, memory, logger);
    executor.state().sr = 0x2700;
    executor.state().address[7] = 0x80;
    executor.state().supervisor_stack_pointer = 0x80;
    executor.replace_image(main_image);
    const auto identity = amigaport::ExecutionIdentity{.image = executor.image(), .address = 0};
    executor.register_override(identity, [&](amigaport::Executor &runtime) {
        return runtime.call_original_subroutine({.value = 4});
    });

    const auto result = executor.call(0, {.value = 4});
    require(result.reason == amigaport::ExitReason::NativeOverride,
            "original subroutine override did not return through native boundary");
    require(result.instructions == 2U, "original subroutine instruction count is wrong");
    require(executor.state().data[0] == 7U, "original subroutine body did not execute");
    require(executor.state().pc == 0x40U, "original subroutine did not stop at guest RTS");
    require(executor.state().address[7] == 0x84U,
            "guest RTS did not consume caller return address");
}

void test_interrupt_call_returns_through_guest_rte() {
    VectorMemory memory(512);
    RecordingLogger logger;
    memory.load16({.address = 0x100, .value = 0x7007}); // MOVEQ #7,D0
    memory.load16({.address = 0x102, .value = 0x4E73}); // RTE

    amigaport::Executor executor({.max_instructions_per_slice = 8}, memory, logger);
    executor.state().pc = 0x20;
    executor.state().sr = 0x2700;
    executor.state().address[7] = 0x180;
    executor.state().supervisor_stack_pointer = 0x180;
    executor.replace_image(main_image);

    const auto result = executor.call_interrupt(0x100, {.value = 4});
    require(result.reason == amigaport::ExitReason::ReturnToHost,
            "interrupt call did not stop at guest RTE");
    require(result.instructions == 2U, "interrupt call instruction count is wrong");
    require(executor.state().data[0] == 7U, "interrupt body did not execute");
    require(executor.state().pc == 0x20U, "RTE did not restore interrupted PC");
    require(executor.state().sr == 0x2700U, "RTE did not restore interrupted SR");
    require(executor.state().address[7] == 0x180U, "RTE did not restore interrupted stack");
}

void test_memory_branch_and_prefetch_paths() {
    VectorMemory memory(128);
    RecordingLogger logger;
    memory.load16({.address = 0, .value = 0x30BC}); // MOVE.W #$1234,(A0)
    memory.load16({.address = 2, .value = 0x1234});
    memory.load16({.address = 4, .value = 0x6002}); // BRA.s to 8
    memory.load16({.address = 6, .value = 0x7001}); // skipped
    memory.load16({.address = 8, .value = 0x7002}); // MOVEQ #2,D0
    memory.load16({.address = 10, .value = 0x4E71});

    amigaport::Executor executor({.max_instructions_per_slice = 8}, memory, logger);
    executor.state().sr = 0x2700;
    executor.state().address[0] = 0x40;
    executor.replace_image(main_image);
    const auto result = executor.execute({.value = 3});

    require(result.reason == amigaport::ExitReason::InstructionBudget,
            "mixed memory/branch slice did not reach its bound");
    require(memory.read16(0x40).value == 0x1234U, "PUAE memory write callback lost data");
    require(executor.state().data[0] == 2U, "PUAE branch or target execution was wrong");
    require(executor.state().pc == 10U, "PUAE branch PC is wrong");
    require(executor.state().prefetch_valid, "PUAE did not export valid prefetch state");
    require(executor.state().prefetch_address == 10U, "prefetch identity is stale");
    require(executor.state().instruction_register == 0x4E71U,
            "next prefetched instruction was not preserved");
}

void test_interrupt_entry_is_not_an_executed_instruction() {
    VectorMemory memory(512);
    RecordingLogger logger;
    constexpr std::uint8_t interrupt_level = 3;
    constexpr std::uint8_t vector = 24 + interrupt_level;
    memory.load16({.address = 0, .value = 0x4E71});
    memory.load16({.address = 2, .value = 0x4E71});
    memory.load32({.address = static_cast<amigaport::GuestAddress>(vector) * 4U, .value = 0xC0U});
    memory.load16({.address = 0xC0, .value = 0x4E71});
    memory.load16({.address = 0xC2, .value = 0x4E71});

    amigaport::Executor executor({.max_instructions_per_slice = 8}, memory, logger);
    executor.state().sr = 0x2000;
    executor.state().address[7] = 0xA0;
    executor.state().supervisor_stack_pointer = 0xA0;
    executor.state().pending_interrupt_level = interrupt_level;
    executor.state().stopped = true;
    executor.replace_image(main_image);
    const auto result = executor.execute({.value = 1});

    require(result.reason == amigaport::ExitReason::Exception, "interrupt did not exit by type");
    require(!executor.state().stopped, "accepted interrupt did not wake a stopped CPU");
    require(result.instructions == 0U, "interrupt inflated executed-instruction count");
    require(result.cycles == 44U, "68000 autovector cycle count is wrong");
    require(executor.state().pc == 0xC0U, "interrupt did not load autovector target");
    require(executor.state().address[7] == 0x9AU, "interrupt frame has wrong size");
    require((executor.state().sr & 0x0700U) == 0x0300U, "interrupt mask was not raised");
}

void test_precise_unsupported_and_memory_fault_exits() {
    VectorMemory memory(256);
    RecordingLogger logger;
    memory.load16({.address = 0, .value = 0x4AFC}); // ILLEGAL
    memory.load32({.address = 4U * 4U, .value = 0x80U});
    memory.load16({.address = 0x80, .value = 0x4E71});
    memory.load16({.address = 0x82, .value = 0x4E71});

    amigaport::Executor executor({.max_instructions_per_slice = 4}, memory, logger);
    executor.state().sr = 0x2000;
    executor.state().address[7] = 0x70;
    executor.state().supervisor_stack_pointer = 0x70;
    executor.replace_image(main_image);
    const auto exception = executor.execute({.value = 1});
    require(exception.reason == amigaport::ExitReason::Exception,
            "illegal opcode did not enter its exception vector");
    require(exception.instructions == 1, "exception instruction denominator is wrong");
    require(exception.instruction_word == 0x4AFC, "exception opcode bytes missing");
    require(executor.state().exception.active_vector ==
                amigaport::ExceptionVector::IllegalInstruction,
            "illegal opcode did not preserve exception identity");
    require(executor.state().pc == 0x80U, "illegal opcode did not load vector 4");
    require(executor.state().address[7] == 0x6AU, "illegal exception frame has wrong size");
    require(memory.read16(0x6A).value == 0x2000U, "exception frame lost saved SR");
    require(memory.read32(0x6C).value == 0U, "exception frame lost saved PC");
    require(logger.write_count == 0U, "architectural exception was logged as a runtime error");

    executor.state().pc = 5;
    executor.state().prefetch_valid = false;
    const auto invalid_state = [&]() {
        try {
            static_cast<void>(executor.execute({.value = 1}));
            return false;
        } catch (const std::invalid_argument &) {
            return true;
        }
    }();
    require(invalid_state, "odd PC was not rejected before fetch");

    executor.state().pc = 256;
    executor.state().prefetch_valid = false;
    const auto fault = executor.execute({.value = 1});
    require(fault.reason == amigaport::ExitReason::MemoryFault, "unmapped fetch did not exit");
    require(fault.memory_fault == amigaport::MemoryFault::Unmapped, "memory fault lost its reason");
    require(executor.state().exception.active_vector == amigaport::ExceptionVector::BusError,
            "memory fault lost its 68000 vector");
    require(logger.write_count == 1U, "memory fault did not use the injected logger");

    executor.state().pc = 0;
    executor.state().halted = true;
    const auto halted = executor.execute({.value = 1});
    require(halted.reason == amigaport::ExitReason::Halted, "halted CPU executed guest code");
}

void test_complete_68000_dispatch_population() {
    require(uae_m68k_legal_opcode_count() == 45815U,
            "PUAE 68000 dispatch table was not expanded across encoded opcodes");
}

void test_nested_context_execution_is_isolated() {
    RawMemory inner_memory;
    RawMemory outer_memory;
    raw_load16(inner_memory, {.address = 0, .value = 0x7007});
    raw_load16(inner_memory, {.address = 2, .value = 0x4E71});
    raw_load16(inner_memory, {.address = 4, .value = 0x4E71});
    raw_load16(outer_memory, {.address = 0, .value = 0x7003});
    raw_load16(outer_memory, {.address = 2, .value = 0x4E71});
    raw_load16(outer_memory, {.address = 4, .value = 0x4E71});
    const uae_m68k_memory callbacks{.read = &raw_read,
                                    .write = &raw_write,
                                    .acknowledge_interrupt = nullptr,
                                    .reset_devices = nullptr};
    uae_m68k_context *inner = uae_m68k_context_create(&callbacks, &inner_memory, nullptr);
    uae_m68k_context *outer = uae_m68k_context_create(&callbacks, &outer_memory, nullptr);
    require(inner != nullptr && outer != nullptr, "fork contexts could not be created");

    uae_m68k_state inner_state{.sr = 0x2700};
    uae_m68k_state outer_state{.sr = 0x2700};
    outer_memory.nested_context = inner;
    outer_memory.nested_state = &inner_state;
    outer_memory.enter_nested = true;
    const auto outer_result = uae_m68k_step(outer, &outer_state);

    require(outer_result.status == UAE_M68K_STEP_OK, "outer reentrant step failed");
    require(outer_state.data[0] == 3U, "outer context register state was corrupted");
    require(inner_state.data[0] == 7U, "nested context did not execute independently");
    require(outer_state.pc == 2U && inner_state.pc == 2U,
            "nested context corrupted either program counter");
    uae_m68k_context_destroy(outer);
    uae_m68k_context_destroy(inner);
}

} // namespace

void test_unterminated_native_override_fails_closed() {
    VectorMemory memory(16);
    RecordingLogger logger;

    amigaport::Executor executor({.max_instructions_per_slice = 4}, memory, logger);
    executor.state().sr = 0x2000;
    executor.state().address[7] = 0;
    executor.replace_image(main_image);
    const auto identity = amigaport::ExecutionIdentity{.image = executor.image(), .address = 0};
    executor.register_override(identity, [](amigaport::Executor &) {
        return amigaport::ExecutionExit{}; // completes no boundary at all
    });

    const auto result = executor.call(0, {.value = 4});
    require(result.reason == amigaport::ExitReason::UnterminatedNativeOverride,
            "an override that completed no boundary did not fail closed");
    require(result.identity.address == 0U, "unterminated override exit lost its address");
}

void test_native_continuation_reauthorizes_a_replaced_image() {
    VectorMemory memory(16);
    RecordingLogger logger;
    memory.load16({.address = 2, .value = 0x7007}); // MOVEQ #7,D0

    amigaport::Executor executor({.max_instructions_per_slice = 4}, memory, logger);
    executor.state().sr = 0x2000;
    executor.state().address[7] = 0;
    executor.replace_image(main_image);
    const auto identity = amigaport::ExecutionIdentity{.image = executor.image(), .address = 0};
    executor.register_override(identity, [](amigaport::Executor &runtime) {
        runtime.replace_image({.value = 9});
        runtime.state().pc = 2;
        runtime.state().prefetch_valid = false;
        amigaport::ExecutionExit result{};
        result.continue_execution = true;
        return result;
    });

    const auto result = executor.call(0, {.value = 1});
    require(result.reason == amigaport::ExitReason::InstructionBudget,
            "an override that replaced the image and continued did not keep running");
    require(executor.state().data[0] == 7U,
            "the continued run did not execute in the image the override chose");
}

void test_execution_trace_records_retired_instructions() {
    VectorMemory memory(16);
    RecordingLogger logger;
    memory.load16({.address = 0, .value = 0x7007}); // MOVEQ #7,D0
    memory.load16({.address = 2, .value = 0x7208}); // MOVEQ #8,D1

    amigaport::Executor executor({.max_instructions_per_slice = 2}, memory, logger);
    executor.state().sr = 0x2000;
    executor.state().address[7] = 0;
    executor.replace_image(main_image);
    (void)executor.call(0, {.value = 2});

    std::array<amigaport::ExecutionTraceEntry, 4> entries{};
    const std::size_t count = executor.recent_execution(entries.data(), entries.size());
    require(count == 2U, "the execution trace did not record both retired instructions");
    require(entries[0].pc == 0U && entries[0].opcode == 0x7007U,
            "the execution trace lost the first retired instruction");
    require(entries[1].pc == 2U && entries[1].opcode == 0x7208U,
            "the execution trace lost the second retired instruction");
    require(amigaport::Executor::recent_execution_capacity() >= count,
            "the reported trace capacity is smaller than what it returned");
}

void test_breakpoint_stops_before_the_instruction_and_resume_continues() {
    VectorMemory memory(16);
    RecordingLogger logger;
    memory.load16({.address = 0, .value = 0x7007}); // MOVEQ #7,D0
    memory.load16({.address = 2, .value = 0x7208}); // MOVEQ #8,D1
    memory.load16({.address = 4, .value = 0x7409}); // MOVEQ #9,D2

    amigaport::Executor executor({.max_instructions_per_slice = 8}, memory, logger);
    executor.state().sr = 0x2000;
    executor.state().address[7] = 0;
    executor.replace_image(main_image);

    require(executor.set_breakpoint(4), "a fresh breakpoint was refused");
    require(!executor.set_breakpoint(4), "the same breakpoint was accepted twice");
    std::array<amigaport::GuestAddress, 4> listed{};
    require(executor.breakpoints(listed.data(), listed.size()) == 1U,
            "the breakpoint set did not report exactly one address");
    require(listed[0] == 4U, "the breakpoint set reported the wrong address");

    const amigaport::ExecutionExit stopped = executor.execute({.value = 8});
    require(stopped.reason == amigaport::ExitReason::Breakpoint,
            "execution did not stop on the breakpoint");
    require(executor.state().pc == 4U, "the breakpoint ran the instruction it stopped on");
    require(executor.state().data[1] == 8U, "the instruction before the breakpoint did not run");
    require(executor.state().data[2] != 9U, "the breakpoint's own instruction ran");

    /* Resuming must make progress rather than stop on the same address again:
     * a run never breaks on its own first instruction. */
    const amigaport::ExecutionExit resumed = executor.execute({.value = 8});
    require(resumed.reason != amigaport::ExitReason::Breakpoint,
            "resuming stopped on the breakpoint it had just reported");
    require(executor.state().data[2] == 9U, "resuming did not execute past the breakpoint");

    /* A handler sees the CPU still ON the address, before anything unwinds. */
    executor.clear_breakpoints();
    require(executor.set_breakpoint(2), "a breakpoint for the handler was refused");
    std::uint32_t observed_pc = 0xFFFFFFFFU;
    executor.set_breakpoint_handler(
        [&observed_pc](amigaport::Executor &stopped) { observed_pc = stopped.state().pc; });
    executor.state().pc = 0;
    executor.state().data[1] = 0;
    const amigaport::ExecutionExit handled = executor.execute({.value = 8});
    require(handled.reason == amigaport::ExitReason::Breakpoint,
            "the handled breakpoint did not stop execution");
    require(observed_pc == 2U, "the breakpoint handler did not see the CPU on the address");
    require(executor.state().data[1] == 0U,
            "the breakpoint handler ran after the instruction it stopped on");
    executor.set_breakpoint_handler(nullptr);
    executor.clear_breakpoints();
    require(executor.set_breakpoint(4), "restoring the breakpoint for the clear checks failed");

    require(executor.clear_breakpoint(4), "clearing a set breakpoint reported nothing to clear");
    require(!executor.clear_breakpoint(4), "clearing an unset breakpoint reported success");
    executor.clear_breakpoints();
    require(executor.breakpoints(listed.data(), listed.size()) == 0U,
            "clear_breakpoints left addresses behind");
    require(amigaport::Executor::breakpoint_capacity() >= 1U,
            "the reported breakpoint capacity is unusable");
}

int main(int argc, char **argv) {
    try {
        // These controlled failures exercise this executable's terminal error
        // boundary. CTest requires failure exits, not abnormal termination.
        if (argc == 2 && std::string_view(argv[1]) == "--exercise-standard-failure") {
            throw std::runtime_error("controlled standard exception");
        }
        if (argc == 2 && std::string_view(argv[1]) == "--exercise-unknown-failure") {
            // Throwing a NON-exception type is the whole point here: it is what
            // exercises main's catch-all boundary, which a std::exception would
            // never reach.
            // NOLINTNEXTLINE(bugprone-std-exception-baseclass)
            throw main_image;
        }
        if (argc != 1) {
            throw std::invalid_argument("unrecognized test arguments");
        }
        test_upstream_moveq_and_budget_exit();
        test_image_qualified_override_and_original_call();
        test_native_override_can_continue_without_unwinding_guest_call();
        test_nested_call_dispatches_guest_and_stops_at_its_caller_return();
        test_explicit_tail_transfer_does_not_consume_guest_stack();
        test_native_stack_view_reconciles_before_guest_reentry();
        test_original_subroutine_returns_at_guest_rts();
        test_interrupt_call_returns_through_guest_rte();
        test_memory_branch_and_prefetch_paths();
        test_interrupt_entry_is_not_an_executed_instruction();
        test_precise_unsupported_and_memory_fault_exits();
        test_complete_68000_dispatch_population();
        test_nested_context_execution_is_isolated();
        test_unterminated_native_override_fails_closed();
        test_native_continuation_reauthorizes_a_replaced_image();
        test_execution_trace_records_retired_instructions();
        test_breakpoint_stops_before_the_instruction_and_resume_continues();
    } catch (const std::exception &error) {
        // Terminal test diagnostics must not throw while handling a failure.
        std::fputs("amigaport_tests: ", stderr);
        std::fputs(error.what(), stderr);
        std::fputc('\n', stderr);
        return EXIT_FAILURE;
    } catch (...) {
        std::fputs("amigaport_tests: unknown exception\n", stderr);
        return EXIT_FAILURE;
    }

    return std::puts("amigaport_tests: 13 scenarios passed") == EOF || std::fflush(stdout) == EOF
               ? EXIT_FAILURE
               : EXIT_SUCCESS;
}
