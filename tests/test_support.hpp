#pragma once

#include "amigaport/executor.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

class VectorMemory final : public amigaport::Memory {
  public:
    explicit VectorMemory(std::size_t size);

    void load16(amigaport::MemoryWrite<std::uint16_t> write);
    void load32(amigaport::MemoryWrite<std::uint32_t> write);
    [[nodiscard]] amigaport::MemoryRead<std::uint8_t>
    read8(amigaport::GuestAddress address) override;
    [[nodiscard]] amigaport::MemoryRead<std::uint16_t>
    read16(amigaport::GuestAddress address) override;
    [[nodiscard]] amigaport::MemoryRead<std::uint32_t>
    read32(amigaport::GuestAddress address) override;
    [[nodiscard]] amigaport::MemoryFault
    write8(amigaport::MemoryWrite<std::uint8_t> write) override;
    [[nodiscard]] amigaport::MemoryFault
    write16(amigaport::MemoryWrite<std::uint16_t> write) override;
    [[nodiscard]] amigaport::MemoryFault
    write32(amigaport::MemoryWrite<std::uint32_t> write) override;

  private:
    struct Range final {
        amigaport::GuestAddress address{};
        std::size_t width{};
    };

    [[nodiscard]] bool contains(Range range) const noexcept;

    std::vector<std::uint8_t> bytes_;
};

class RecordingLogger final : public amigaport::Logger {
  public:
    void write(amigaport::LogLevel level, std::string_view category,
               std::string_view message) noexcept override;

    std::size_t write_count{};
};
