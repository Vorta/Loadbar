#pragma once

#include "model/snapshot.hpp"

namespace loadbar {
struct Color {
    float r{}, g{}, b{}, a{1};
    bool operator==(const Color &) const = default;
};
constexpr Color rgb(unsigned value, float alpha = 1) noexcept {
    return {static_cast<float>((value >> 16U) & 255U) / 255,
            static_cast<float>((value >> 8U) & 255U) / 255, static_cast<float>(value & 255U) / 255,
            alpha};
}
inline constexpr auto kBackground = rgb(0x0E1116);
inline constexpr auto kBorder = rgb(0x1B2029);
inline constexpr auto kDivider = rgb(0x232832);
inline constexpr auto kPrimary = rgb(0xE8EDF4);
inline constexpr auto kSecondary = rgb(0x9AA4B2);
inline constexpr auto kCpuIcon = rgb(0xAEB7C4);
struct TemperatureAppearance {
    Color icon, text;
    float glow_alpha{};
    bool operator==(const TemperatureAppearance &) const = default;
};
[[nodiscard]] Color component_icon_color(unsigned component) noexcept;
[[nodiscard]] TemperatureAppearance temperature_appearance(unsigned component,
                                                           double celsius) noexcept;
struct Palette {
    Color hue, track, text;
};
[[nodiscard]] Palette gauge_palette(Gauge gauge) noexcept;
[[nodiscard]] Color heat_color(double percent) noexcept;
[[nodiscard]] Color tone(Color track, Color hue, double fraction) noexcept;
[[nodiscard]] double graphic_fraction(Gauge gauge, MetricView metric);
[[nodiscard]] bool glows(Gauge gauge, MetricView metric);
[[nodiscard]] bool metric_changed(MetricView before, MetricView after, Clock::time_point now,
                                  unsigned interval_ms);
// Ignore diagnostics that cannot affect a graphic or its hover number.
[[nodiscard]] bool visual_metric_changed(MetricView before, MetricView after, Clock::time_point now,
                                         unsigned interval_ms) noexcept;
[[nodiscard]] std::wstring peak_text(MetricView metric,
                                     ByteFormat format = ByteFormat::adaptive_binary);
[[nodiscard]] std::wstring ram_capacity_text(const Snapshot &snapshot, Clock::time_point now,
                                             unsigned interval_ms);
[[nodiscard]] std::wstring ram_summary(const Snapshot &snapshot, Clock::time_point now,
                                       unsigned interval_ms);
[[nodiscard]] bool ram_changed(const Snapshot &before, const Snapshot &after, Clock::time_point now,
                               unsigned interval_ms);
[[nodiscard]] std::wstring compact_value(MetricView metric);
[[nodiscard]] std::wstring cpu_summary(const Snapshot &snapshot, const Settings &settings,
                                       Clock::time_point now);
} // namespace loadbar
