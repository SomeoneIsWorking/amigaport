#include "test_support.hpp"

VectorMemory::VectorMemory(std::size_t size) : bytes_(size) {}

void VectorMemory::load16(amigaport::GuestAddress address, std::uint16_t value) {
    bytes_.at(address) = static_cast<std::uint8_t>(value >> 8U);
    bytes_.at(address + 1U) = static_cast<std::uint8_t>(value);
}

void VectorMemory::load32(amigaport::GuestAddress address, std::uint32_t value) {
    load16(address, static_cast<std::uint16_t>(value >> 16U));
    load16(address + 2U, static_cast<std::uint16_t>(value));
}

amigaport::MemoryRead<std::uint8_t> VectorMemory::read8(amigaport::GuestAddress address) {
    return contains(address, 1U)
               ? amigaport::MemoryRead<std::uint8_t>{.value = bytes_[address]}
               : amigaport::MemoryRead<std::uint8_t>{.fault = amigaport::MemoryFault::Unmapped};
}

amigaport::MemoryRead<std::uint16_t> VectorMemory::read16(amigaport::GuestAddress address) {
    if ((address & 1U) != 0U) {
        return {.fault = amigaport::MemoryFault::Misaligned};
    }
    if (!contains(address, 2U)) {
        return {.fault = amigaport::MemoryFault::Unmapped};
    }
    return {.value = static_cast<std::uint16_t>((bytes_[address] << 8U) | bytes_[address + 1U])};
}

amigaport::MemoryRead<std::uint32_t> VectorMemory::read32(amigaport::GuestAddress address) {
    const auto high = read16(address);
    const auto low = read16(address + 2U);
    if (!high) {
        return {.fault = high.fault};
    }
    if (!low) {
        return {.fault = low.fault};
    }
    return {.value = (static_cast<std::uint32_t>(high.value) << 16U) | low.value};
}

amigaport::MemoryFault VectorMemory::write8(amigaport::GuestAddress address, std::uint8_t value) {
    if (!contains(address, 1U)) {
        return amigaport::MemoryFault::Unmapped;
    }
    bytes_[address] = value;
    return amigaport::MemoryFault::None;
}

amigaport::MemoryFault VectorMemory::write16(amigaport::GuestAddress address, std::uint16_t value) {
    if ((address & 1U) != 0U) {
        return amigaport::MemoryFault::Misaligned;
    }
    if (!contains(address, 2U)) {
        return amigaport::MemoryFault::Unmapped;
    }
    bytes_[address] = static_cast<std::uint8_t>(value >> 8U);
    bytes_[address + 1U] = static_cast<std::uint8_t>(value);
    return amigaport::MemoryFault::None;
}

amigaport::MemoryFault VectorMemory::write32(amigaport::GuestAddress address, std::uint32_t value) {
    if (!contains(address, 4U)) {
        return amigaport::MemoryFault::Unmapped;
    }
    if (const auto fault = write16(address, static_cast<std::uint16_t>(value >> 16U));
        fault != amigaport::MemoryFault::None) {
        return fault;
    }
    return write16(address + 2U, static_cast<std::uint16_t>(value));
}

bool VectorMemory::contains(amigaport::GuestAddress address, std::size_t width) const noexcept {
    return address <= bytes_.size() && width <= bytes_.size() - address;
}

void RecordingLogger::write(amigaport::LogLevel, std::string_view, std::string_view) noexcept {
    ++write_count;
}
