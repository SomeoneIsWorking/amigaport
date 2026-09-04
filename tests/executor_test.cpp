#include "test_support.hpp"
#include "uae/m68k_embed.h"

#include <cstddef>
#include <array>
#include <cstdlib>
#include <iostream>
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

void raw_load16(RawMemory &memory, std::uint32_t address, std::uint16_t value) {
    memory.bytes.at(address) = static_cast<std::uint8_t>(value >> 8U);
    memory.bytes.at(address + 1U) = static_cast<std::uint8_t>(value);
}

void test_upstream_moveq_and_budget_exit() {
    VectorMemory memory(16);
    RecordingLogger logger;
    memory.load16(0, 0x76FF); // MOVEQ #-1,D3
    memory.load16(2, 0x4E71); // NOP

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
    memory.load16(0, 0x7007); // MOVEQ #7,D0

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

void test_memory_branch_and_prefetch_paths() {
    VectorMemory memory(128);
    RecordingLogger logger;
    memory.load16(0, 0x30BC); // MOVE.W #$1234,(A0)
    memory.load16(2, 0x1234);
    memory.load16(4, 0x6002); // BRA.s to 8
    memory.load16(6, 0x7001); // skipped
    memory.load16(8, 0x7002); // MOVEQ #2,D0
    memory.load16(10, 0x4E71);

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
    memory.load16(0, 0x4E71);
    memory.load16(2, 0x4E71);
    memory.load32(static_cast<amigaport::GuestAddress>(vector) * 4U, 0xC0U);
    memory.load16(0xC0, 0x4E71);
    memory.load16(0xC2, 0x4E71);

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
    memory.load16(0, 0x4AFC); // ILLEGAL
    memory.load32(4U * 4U, 0x80U);
    memory.load16(0x80, 0x4E71);
    memory.load16(0x82, 0x4E71);

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
    raw_load16(inner_memory, 0, 0x7007);
    raw_load16(inner_memory, 2, 0x4E71);
    raw_load16(inner_memory, 4, 0x4E71);
    raw_load16(outer_memory, 0, 0x7003);
    raw_load16(outer_memory, 2, 0x4E71);
    raw_load16(outer_memory, 4, 0x4E71);
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

int main() {
    try {
        test_upstream_moveq_and_budget_exit();
        test_image_qualified_override_and_original_call();
        test_memory_branch_and_prefetch_paths();
        test_interrupt_entry_is_not_an_executed_instruction();
        test_precise_unsupported_and_memory_fault_exits();
        test_complete_68000_dispatch_population();
        test_nested_context_execution_is_isolated();
    } catch (const std::exception &error) {
        std::cerr << "amigaport_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "amigaport_tests: 7 scenarios passed\n";
    return EXIT_SUCCESS;
}
