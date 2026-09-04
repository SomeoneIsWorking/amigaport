#pragma once

#include "amigaport/types.hpp"

#include <array>
#include <cstdint>

namespace amigaport {

enum class ExceptionVector : std::uint8_t {
    None = 0,
    BusError = 2,
    AddressError = 3,
    IllegalInstruction = 4,
    DivideByZero = 5,
    Check = 6,
    TrapOverflow = 7,
    PrivilegeViolation = 8,
    Trace = 9,
    LineA = 10,
    LineF = 11,
    SpuriousInterrupt = 24,
};

struct ExceptionState final {
    ExceptionVector active_vector{ExceptionVector::None};
    GuestAddress fault_address{};
    std::uint16_t instruction_word{};
    std::uint16_t frame_status{};
};

struct CpuState final {
    std::array<std::uint32_t, 8> data{};
    std::array<std::uint32_t, 8> address{};
    GuestAddress pc{};
    std::uint16_t sr{0x2700};
    std::uint32_t user_stack_pointer{};
    std::uint32_t supervisor_stack_pointer{};
    std::uint16_t instruction_register{};
    std::uint16_t prefetch_word{};
    std::uint8_t pending_interrupt_level{};
    bool stopped{};
    bool halted{};
    ExceptionState exception{};
    std::uint64_t executed_instructions{};
    std::uint64_t elapsed_cycles{};
};

[[nodiscard]] bool cpu_state_is_valid(const CpuState &state) noexcept;

} // namespace amigaport
