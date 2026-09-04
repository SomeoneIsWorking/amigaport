#pragma once

#include "amigaport/cpu_state.hpp"

#include <cstdint>

namespace amigaport::detail {

struct CoreStep final {
    bool supported{};
    std::uint64_t cycles{};
};

class PuaeCore final {
  public:
    [[nodiscard]] CoreStep step(CpuState &state, std::uint16_t instruction_word);
};

} // namespace amigaport::detail
