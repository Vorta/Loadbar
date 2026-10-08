#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace loadbar {
inline constexpr double kMinimumThicknessDips = 40;
inline constexpr double kMaximumThicknessDips = 640;

enum class Edge : std::uint8_t { automatic, left, top, right, bottom };
enum class Alignment : std::uint8_t { start, center, end };
struct Settings {
    Edge edge{Edge::automatic};
    double thickness{kMinimumThicknessDips};
    std::uint32_t interval_ms{1000};
    std::wstring monitor_id;
    // Only populated while decoding v1/v2; resolved once against the chosen monitor.
    std::optional<double> legacy_percent;
    std::wstring gpu_id;
    std::wstring network_id;
    Alignment alignment{Alignment::start};
    bool operator==(const Settings &) const = default;
};
[[nodiscard]] bool valid_settings(const Settings &settings);
[[nodiscard]] bool collection_changed(const Settings &before, const Settings &after);
[[nodiscard]] std::wstring encode_settings(const Settings &settings);
[[nodiscard]] std::optional<Settings> decode_settings(std::wstring_view text);
} // namespace loadbar
