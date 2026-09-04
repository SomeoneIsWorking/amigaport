#pragma once

#include <compare>
#include <cstdint>
#include <string_view>

namespace amigaport {

using GuestAddress = std::uint32_t;
using ImageGeneration = std::uint64_t;

struct ImageTag final {
    std::uint32_t value{};

    auto operator<=>(const ImageTag &) const = default;
};

struct ImageIdentity final {
    ImageTag tag{};
    ImageGeneration generation{};

    auto operator<=>(const ImageIdentity &) const = default;
};

struct ExecutionIdentity final {
    ImageIdentity image{};
    GuestAddress address{};

    auto operator<=>(const ExecutionIdentity &) const = default;
};

enum class LogLevel : std::uint8_t {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
};

class Logger {
  public:
    virtual ~Logger() = default;
    virtual void write(LogLevel level, std::string_view category,
                       std::string_view message) noexcept = 0;
};

struct RuntimeConfig final {
    std::uint32_t max_instructions_per_slice{10'000};
};

struct InstructionBudget final {
    std::uint32_t value{};
};

} // namespace amigaport
