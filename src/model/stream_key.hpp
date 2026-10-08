#pragma once

#include <string_view>

namespace loadbar {
// Stored keys own their strings. Lookup pairs borrow the current observation instead of
// allocating the same identity on every sample; the two forms share one lexical ordering.
struct StreamKeyLess {
    using is_transparent = void;
    template <class Left, class Right>
    bool operator()(const Left &left, const Right &right) const noexcept {
        const std::wstring_view a(left.first), b(right.first);
        return a < b || (a == b && left.second < right.second);
    }
};
} // namespace loadbar
