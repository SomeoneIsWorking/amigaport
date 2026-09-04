#include "test_support.hpp"

#include <cstddef>
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
    executor.state().sr = 0x2013; // supervisor, X, V, and C
    executor.state().user_stack_pointer = 0x00123456;
    executor.state().supervisor_stack_pointer = 0x00654320;
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
    require(executor.state().sr == 0x2018U, "PUAE MOVEQ status result is wrong");
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

void test_precise_unsupported_and_memory_fault_exits() {
    VectorMemory memory(4);
    RecordingLogger logger;
    memory.load16(0, 0x4AFC); // ILLEGAL, not yet in the bounded upstream slice

    amigaport::Executor executor({.max_instructions_per_slice = 4}, memory, logger);
    executor.state().sr = 0x2000;
    executor.replace_image(main_image);
    const auto unsupported = executor.execute({.value = 1});
    require(unsupported.reason == amigaport::ExitReason::UnsupportedInstruction,
            "unsupported opcode did not fail closed");
    require(unsupported.instructions == 0, "unsupported opcode inflated denominator");
    require(unsupported.instruction_word == 0x4AFC, "unsupported opcode bytes missing");
    require(executor.state().exception.active_vector ==
                amigaport::ExceptionVector::IllegalInstruction,
            "unsupported opcode did not preserve exception identity");
    require(logger.messages.size() == 1U, "unsupported opcode did not use the injected logger");

    executor.state().pc = 5;
    const auto invalid_state = [&]() {
        try {
            static_cast<void>(executor.execute({.value = 1}));
            return false;
        } catch (const std::invalid_argument &) {
            return true;
        }
    }();
    require(invalid_state, "odd PC was not rejected before fetch");

    executor.state().pc = 4;
    const auto fault = executor.execute({.value = 1});
    require(fault.reason == amigaport::ExitReason::MemoryFault, "unmapped fetch did not exit");
    require(fault.memory_fault == amigaport::MemoryFault::Unmapped, "memory fault lost its reason");
    require(executor.state().exception.active_vector == amigaport::ExceptionVector::BusError,
            "memory fault lost its 68000 vector");
    require(logger.messages.size() == 2U, "memory fault did not use the injected logger");

    executor.state().pc = 0;
    executor.state().halted = true;
    const auto halted = executor.execute({.value = 1});
    require(halted.reason == amigaport::ExitReason::Halted, "halted CPU executed guest code");
}

} // namespace

int main() {
    try {
        test_upstream_moveq_and_budget_exit();
        test_image_qualified_override_and_original_call();
        test_precise_unsupported_and_memory_fault_exits();
    } catch (const std::exception &error) {
        std::cerr << "amigaport_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "amigaport_tests: 3 scenarios passed\n";
    return EXIT_SUCCESS;
}
