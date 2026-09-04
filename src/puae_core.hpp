#pragma once

#include "amigaport/cpu_state.hpp"
#include "amigaport/memory.hpp"

#include <cstdint>

namespace amigaport::detail {

struct CoreStep final {
    enum class Status : std::uint8_t { Completed, MemoryFault, Exception, Halted } status{};
    std::uint64_t cycles{};
    MemoryFault memory_fault{};
    GuestAddress fault_address{};
    std::uint16_t instruction_word{};
    ExceptionVector exception_vector{};
    bool instruction_executed{};
};

class PuaeCore final {
  public:
    PuaeCore(Memory &memory, Logger &logger);
    ~PuaeCore();

    PuaeCore(const PuaeCore &) = delete;
    PuaeCore &operator=(const PuaeCore &) = delete;

    [[nodiscard]] CoreStep step(CpuState &state);

  private:
    class Impl;
    Impl *impl_;
};

} // namespace amigaport::detail
