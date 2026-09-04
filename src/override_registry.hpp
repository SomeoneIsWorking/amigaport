#pragma once

#include "amigaport/executor.hpp"

#include <map>
#include <optional>
#include <vector>

namespace amigaport::detail {

class OverrideRegistry final {
  public:
    void install(ExecutionIdentity identity, NativeOverride function);
    void remove(ExecutionIdentity identity);
    [[nodiscard]] NativeOverride *find(ExecutionIdentity identity);

    class ScopedSuppression final {
      public:
        ScopedSuppression(OverrideRegistry &registry, ExecutionIdentity identity);
        ~ScopedSuppression();

        ScopedSuppression(const ScopedSuppression &) = delete;
        ScopedSuppression &operator=(const ScopedSuppression &) = delete;

      private:
        OverrideRegistry &registry_;
    };

  private:
    [[nodiscard]] bool is_suppressed(ExecutionIdentity identity) const;

    std::map<ExecutionIdentity, NativeOverride> overrides_;
    std::vector<ExecutionIdentity> suppressions_;
};

} // namespace amigaport::detail
