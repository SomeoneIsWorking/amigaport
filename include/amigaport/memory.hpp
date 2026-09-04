#pragma once

#include "amigaport/types.hpp"

#include <cstdint>

namespace amigaport {

enum class MemoryFault : std::uint8_t {
    None,
    Unmapped,
    Misaligned,
    ReadOnly,
};

template <typename T> struct MemoryRead final {
    T value{};
    MemoryFault fault{MemoryFault::None};

    [[nodiscard]] explicit operator bool() const noexcept { return fault == MemoryFault::None; }
};

template <typename T> struct MemoryWrite final {
    GuestAddress address{};
    T value{};
};

class Memory {
  public:
    virtual ~Memory() = default;

    [[nodiscard]] virtual MemoryRead<std::uint8_t> read8(GuestAddress address) = 0;
    [[nodiscard]] virtual MemoryRead<std::uint16_t> read16(GuestAddress address) = 0;
    [[nodiscard]] virtual MemoryRead<std::uint32_t> read32(GuestAddress address) = 0;
    [[nodiscard]] virtual MemoryFault write8(MemoryWrite<std::uint8_t> write) = 0;
    [[nodiscard]] virtual MemoryFault write16(MemoryWrite<std::uint16_t> write) = 0;
    [[nodiscard]] virtual MemoryFault write32(MemoryWrite<std::uint32_t> write) = 0;
};

} // namespace amigaport
