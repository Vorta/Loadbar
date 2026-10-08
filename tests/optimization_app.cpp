#include "app/tooltip_cache.hpp"

#include <chrono>
#include <stdexcept>

namespace {
void check(bool condition) {
    if (!condition) {
        throw std::runtime_error("Tooltip cache freshness regression");
    }
}
} // namespace

void run_optimization_app_tests() {
    using namespace std::chrono_literals;
    using loadbar::Clock;
    using loadbar::TooltipCache;
    using loadbar::TooltipKey;
    const auto now = Clock::time_point{} + 10s;
    const TooltipKey first{2, 3, 4, false};
    TooltipCache cache;
    check(!cache.fresh(first, now));
    cache.remember(first, now + 500ms);
    check(cache.fresh(first, now));
    check(cache.fresh(first, now + 499ms));
    check(!cache.fresh(first, now + 500ms));
    check(!cache.fresh(first, now + 2s));
    // Pointer movement inside a target does not invalidate text. A different target, layout,
    // accepted sample/status revision, or monitor fallback must do so immediately.
    check(!cache.fresh({3, 3, 4, false}, now));
    check(!cache.fresh({2, 4, 4, false}, now));
    check(!cache.fresh({2, 3, 5, false}, now));
    check(!cache.fresh({2, 3, 4, true}, now));
    cache.remember(first, std::nullopt);
    check(cache.fresh(first, now + 1h));
    check(!cache.fresh({2, 3, 5, false}, now + 1h));
}
