#include "override_registry.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace amigaport::detail {

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
