#include "override_registry.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace amigaport::detail {

std::size_t OverrideRegistry::Hash::operator()(ExecutionIdentity identity) const noexcept {
    std::size_t value = static_cast<std::size_t>(identity.image.tag.value);
    value ^= static_cast<std::size_t>(identity.image.generation) + 0x9E3779B9u + (value << 6U) +
             (value >> 2U);
    value ^=
        static_cast<std::size_t>(identity.address) + 0x9E3779B9u + (value << 6U) + (value >> 2U);
    return value;
}

void OverrideRegistry::install(ExecutionIdentity identity, NativeOverride function) {
    if (!function) {
        throw std::invalid_argument("native override must be callable");
    }
    overrides_.insert_or_assign(identity, std::move(function));
}

void OverrideRegistry::remove(ExecutionIdentity identity) { overrides_.erase(identity); }

NativeOverride *OverrideRegistry::find(ExecutionIdentity identity) {
    if (is_suppressed(identity)) {
        return nullptr;
    }
    const auto iterator = overrides_.find(identity);
    return iterator == overrides_.end() ? nullptr : &iterator->second;
}

OverrideRegistry::ScopedSuppression::ScopedSuppression(OverrideRegistry &registry,
                                                       ExecutionIdentity identity)
    : registry_(registry) {
    registry_.suppressions_.push_back(identity);
}

OverrideRegistry::ScopedSuppression::~ScopedSuppression() { registry_.suppressions_.pop_back(); }

bool OverrideRegistry::is_suppressed(ExecutionIdentity identity) const {
    return std::ranges::find(suppressions_, identity) != suppressions_.end();
}

} // namespace amigaport::detail
