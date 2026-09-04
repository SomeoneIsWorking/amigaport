#include "amigaport/cpu_state.hpp"

namespace amigaport {

bool cpu_state_is_valid(const CpuState &state) noexcept {
    constexpr std::uint16_t reserved_status_bits = 0x58E0;
    const bool supervisor = (state.sr & 0x2000U) != 0U;
    const bool stack_is_consistent = supervisor ? state.address[7] == state.supervisor_stack_pointer
                                                : state.address[7] == state.user_stack_pointer;
    return (state.pc & 1U) == 0U && (state.sr & reserved_status_bits) == 0U &&
           state.pending_interrupt_level <= 7U && stack_is_consistent;
}

} // namespace amigaport
