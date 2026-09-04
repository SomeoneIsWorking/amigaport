#pragma once

#include "amigaport/executor.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class VectorMemory final : public amigaport::Memory {
  public:
    explicit VectorMemory(std::size_t size);

    void load16(amigaport::GuestAddress address, std::uint16_t value);
    [[nodiscard]] amigaport::MemoryRead<std::uint8_t>
    read8(amigaport::GuestAddress address) override;
    [[nodiscard]] amigaport::MemoryRead<std::uint16_t>
    read16(amigaport::GuestAddress address) override;
    [[nodiscard]] amigaport::MemoryRead<std::uint32_t>
    read32(amigaport::GuestAddress address) override;
    [[nodiscard]] amigaport::MemoryFault write8(amigaport::GuestAddress address,
                                                std::uint8_t value) override;
    [[nodiscard]] amigaport::MemoryFault write16(amigaport::GuestAddress address,
                                                 std::uint16_t value) override;
    [[nodiscard]] amigaport::MemoryFault write32(amigaport::GuestAddress address,
                                                 std::uint32_t value) override;

  private:
    [[nodiscard]] bool contains(amigaport::GuestAddress address, std::size_t width) const noexcept;

    std::vector<std::uint8_t> bytes_;
};

class RecordingLogger final : public amigaport::Logger {
  public:
    void write(amigaport::LogLevel level, std::string_view category,
               std::string_view message) noexcept override;

    std::vector<std::string> messages;
};
