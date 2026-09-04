#include "test_support.hpp"

VectorMemory::VectorMemory(std::size_t size) : bytes_(size) {}

void VectorMemory::load16(amigaport::MemoryWrite<std::uint16_t> write) {
    bytes_.at(write.address) = static_cast<std::uint8_t>(write.value >> 8U);
    bytes_.at(write.address + 1U) = static_cast<std::uint8_t>(write.value);
}

void VectorMemory::load32(amigaport::MemoryWrite<std::uint32_t> write) {
    load16({.address = write.address, .value = static_cast<std::uint16_t>(write.value >> 16U)});
    load16({.address = write.address + 2U, .value = static_cast<std::uint16_t>(write.value)});
}

amigaport::MemoryRead<std::uint8_t> VectorMemory::read8(amigaport::GuestAddress address) {
    return contains({.address = address, .width = 1U})
               ? amigaport::MemoryRead<std::uint8_t>{.value = bytes_[address]}
               : amigaport::MemoryRead<std::uint8_t>{.fault = amigaport::MemoryFault::Unmapped};
}

amigaport::MemoryRead<std::uint16_t> VectorMemory::read16(amigaport::GuestAddress address) {
    if ((address & 1U) != 0U) {
        return {.fault = amigaport::MemoryFault::Misaligned};
    }
    if (!contains({.address = address, .width = 2U})) {
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

amigaport::MemoryFault VectorMemory::write8(amigaport::MemoryWrite<std::uint8_t> write) {
    if (!contains({.address = write.address, .width = 1U})) {
        return amigaport::MemoryFault::Unmapped;
    }
    bytes_[write.address] = write.value;
    return amigaport::MemoryFault::None;
}

amigaport::MemoryFault VectorMemory::write16(amigaport::MemoryWrite<std::uint16_t> write) {
    if ((write.address & 1U) != 0U) {
        return amigaport::MemoryFault::Misaligned;
    }
    if (!contains({.address = write.address, .width = 2U})) {
        return amigaport::MemoryFault::Unmapped;
    }
    bytes_[write.address] = static_cast<std::uint8_t>(write.value >> 8U);
    bytes_[write.address + 1U] = static_cast<std::uint8_t>(write.value);
    return amigaport::MemoryFault::None;
}

amigaport::MemoryFault VectorMemory::write32(amigaport::MemoryWrite<std::uint32_t> write) {
    if (!contains({.address = write.address, .width = 4U})) {
        return amigaport::MemoryFault::Unmapped;
    }
    if (const auto fault = write16(
            {.address = write.address, .value = static_cast<std::uint16_t>(write.value >> 16U)});
        fault != amigaport::MemoryFault::None) {
        return fault;
    }
    return write16(
        {.address = write.address + 2U, .value = static_cast<std::uint16_t>(write.value)});
}

bool VectorMemory::contains(Range range) const noexcept {
    return range.address <= bytes_.size() && range.width <= bytes_.size() - range.address;
}

void RecordingLogger::write(amigaport::LogLevel, std::string_view, std::string_view) noexcept {
    ++write_count;
}
