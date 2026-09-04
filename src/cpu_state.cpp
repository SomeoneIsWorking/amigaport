#include "amigaport/cpu_state.hpp"

namespace amigaport {

bool cpu_state_is_valid(const CpuState &state) noexcept {
    constexpr std::uint16_t reserved_status_bits = 0x58E0;
    return (state.pc & 1U) == 0U && (state.sr & reserved_status_bits) == 0U &&
           state.pending_interrupt_level <= 7U;
}

} // namespace amigaport
