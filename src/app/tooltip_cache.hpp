#pragma once

#include "model/model.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace loadbar {
struct TooltipKey {
    std::size_t target{};
    std::uint64_t layout{}, observation{};
    bool fallback{};
    bool operator==(const TooltipKey &) const = default;
};

// Contains no views into snapshots or layout storage. Observation advances on every accepted
// sample and status/configuration change, independently of the worker configuration generation.
class TooltipCache {
  public:
    [[nodiscard]] bool fresh(TooltipKey key, Clock::time_point now) const noexcept {
        return key_ == key && (!expires_ || now < *expires_);
    }
    void remember(TooltipKey key, std::optional<Clock::time_point> expires) noexcept {
        key_ = key;
        expires_ = expires;
    }

  private:
    std::optional<TooltipKey> key_;
    std::optional<Clock::time_point> expires_;
};
} // namespace loadbar
