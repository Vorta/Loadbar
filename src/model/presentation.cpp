#include "model/presentation.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

namespace loadbar {
bool metric_changed(MetricView before, MetricView after, Clock::time_point now,
                    unsigned interval_ms) {
    return before.value != after.value ||
           aged_metric(before, now, interval_ms).status !=
               aged_metric(after, now, interval_ms).status ||
           before.detail != after.detail || before.identity != after.identity ||
           before.session_peak != after.session_peak ||
           before.retained.has_value() != after.retained.has_value() ||
           (before.retained && after.retained && before.retained->value != after.retained->value);
}
bool visual_metric_changed(MetricView before, MetricView after, Clock::time_point now,
                           unsigned interval_ms) noexcept {
    const auto a = presented_metric(aged_metric(before, now, interval_ms));
    const auto b = presented_metric(aged_metric(after, now, interval_ms));
    return a.status != b.status ||
           (a.status == Status::valid &&
            (a.value != b.value || a.unit != b.unit || a.session_peak != b.session_peak));
}
Palette gauge_palette(Gauge gauge) noexcept {
    constexpr std::array<Palette, static_cast<std::size_t>(Gauge::count)> palettes{
        {{rgb(0x55ABF7), rgb(0x1A2129), rgb(0x93CAFF)},
         {rgb(0x9C81F7), rgb(0x201F28), rgb(0xBFB1FF)},
         {rgb(0xD3BEFD), rgb(0x201F28), rgb(0xDECEFE)},
         {rgb(0x6D5CBF), rgb(0x201F28), rgb(0xA199E6)},
         {rgb(0xEC5775), rgb(0x281D20), rgb(0xF78C9C)},
         {rgb(0xA93F75), rgb(0x281D20), rgb(0xDB7AA7)},
         {rgb(0xB9DF9B), rgb(0x1B231B), rgb(0xB9DF9B)},
         {rgb(0x36884D), rgb(0x1B231B), rgb(0x66BA7F)},
         {rgb(0x65C55B), rgb(0x1B231B), rgb(0x98E090)}}};
    const auto index = static_cast<std::size_t>(gauge);
    return index < palettes.size() ? palettes[index] : Palette{};
}
Color tone(Color track, Color hue, double fraction) noexcept {
    const auto t = static_cast<float>(0.7 + 0.3 * fill_fraction(fraction, 1));
    return {track.r + (hue.r - track.r) * t, track.g + (hue.g - track.g) * t,
            track.b + (hue.b - track.b) * t, hue.a};
}
Color heat_color(double percent) noexcept {
    constexpr std::array stops{0.0, 30.0, 60.0, 82.0, 100.0};
    constexpr std::array colors{rgb(0x3A4856), rgb(0x34B6A6), rgb(0xF2B840), rgb(0xFF623A),
                                rgb(0xFF3456)};
    const double value = fill_fraction(percent, 100) * 100;
    for (std::size_t i = 1; i < stops.size(); ++i) {
        if (value <= stops[i]) {
            const auto t = static_cast<float>((value - stops[i - 1]) / (stops[i] - stops[i - 1]));
            const auto a = colors[i - 1], b = colors[i];
            return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1};
        }
    }
    return colors.back();
}
double graphic_fraction(Gauge gauge, MetricView source) {
    const auto metric = presented_metric(source);
    if (metric.status != Status::valid) {
        return 0;
    }
    if (gauge == Gauge::gpu_decode && metric.value <= 0.5) {
        return 0;
    }
    return is_rate_gauge(gauge) ? fill_fraction(metric.value, metric.session_peak.value_or(0))
                                : fill_fraction(metric.value, 100);
}
bool glows(Gauge gauge, MetricView source) {
    const auto metric = presented_metric(source);
    return metric.status == Status::valid && std::isfinite(metric.value) && metric.value > 90 &&
           (gauge == Gauge::ram || gauge == Gauge::gpu_3d || gauge == Gauge::gpu_memory);
}
std::wstring compact_value(MetricView source) {
    const auto metric = presented_metric(source);
    if (metric.status != Status::valid) {
        return L"—";
    }
    if (!std::isfinite(metric.value) || metric.value < 0) {
        return L"—";
    }
    if (metric.unit == Unit::percent) {
        return std::format(L"{:.0f}%", metric.value);
    }
    if (metric.unit == Unit::bytes) {
        return std::format(L"{:.0f} GiB", metric.value / 1073741824.0);
    }
    constexpr const wchar_t *units[]{L"B/s", L"KiB/s", L"MiB/s", L"GiB/s"};
    double value = metric.value;
    unsigned index{};
    while (value >= 1024 && index < 3) {
        value /= 1024;
        ++index;
    }
    return value < 10 ? std::format(L"{:.1f} {}", value, units[index])
                      : std::format(L"{:.0f} {}", value, units[index]);
}
std::wstring peak_text(MetricView metric) {
    return metric.session_peak
               ? std::format(L"Linear scale; peak since launch {} B/s", *metric.session_peak)
               : L"Linear scale; peak since launch unavailable";
}
std::wstring ram_capacity_text(const Snapshot &snapshot, Clock::time_point now,
                               unsigned interval_ms) {
    const auto used = presented_metric(aged_metric(snapshot.ram_used_bytes, now, interval_ms));
    const auto percent = presented_metric(
        aged_metric(snapshot.gauges[static_cast<std::size_t>(Gauge::ram)], now, interval_ms));
    if (used.status != Status::valid || percent.status != Status::valid ||
        used.unit != Unit::bytes || !std::isfinite(used.value) || used.value < 0 ||
        snapshot.ram_total_bytes == 0 ||
        used.value > static_cast<double>(snapshot.ram_total_bytes)) {
        return L"—";
    }
    // Windows-style RAM GB uses 1024^3 bytes; rate units remain explicitly binary.
    return std::format(L"{:.1f}/{:.1f}GB", used.value / 1073741824.0,
                       static_cast<double>(snapshot.ram_total_bytes) / 1073741824.0);
}
std::wstring ram_summary(const Snapshot &snapshot, Clock::time_point now, unsigned interval_ms) {
    const auto capacity = ram_capacity_text(snapshot, now, interval_ms);
    return capacity == L"—"
               ? compact_value(aged_metric(snapshot.gauges[0], now, interval_ms))
               : capacity + L" " + compact_value(aged_metric(snapshot.gauges[0], now, interval_ms));
}
bool ram_changed(const Snapshot &before, const Snapshot &after, Clock::time_point now,
                 unsigned interval_ms) {
    return before.ram_total_bytes != after.ram_total_bytes ||
           metric_changed(before.ram_used_bytes, after.ram_used_bytes, now, interval_ms) ||
           metric_changed(before.gauges[0], after.gauges[0], now, interval_ms);
}
std::wstring cpu_summary(const Snapshot &snapshot, const Settings &settings,
                         Clock::time_point now) {
    if (snapshot.processors.empty()) {
        return L"CPU —";
    }
    double total{};
    std::map<unsigned, std::pair<double, unsigned>> classes;
    bool classified = true;
    for (const auto &cpu : snapshot.processors) {
        const auto metric =
            presented_metric(aged_metric(cpu.utilization, now, settings.interval_ms));
        if (metric.status != Status::valid || !std::isfinite(metric.value)) {
            return L"CPU — incomplete";
        }
        total += metric.value;
        if (cpu.efficiency_class) {
            auto &[sum, count] = classes[*cpu.efficiency_class];
            sum += metric.value;
            ++count;
        } else {
            classified = false;
        }
    }
    auto result = std::format(L"{:.0f}%", total / static_cast<double>(snapshot.processors.size()));
    if (classified && classes.size() > 1) {
        const auto [p_sum, p_count] = classes.rbegin()->second;
        double e_sum{};
        unsigned e_count{};
        for (auto it = classes.begin(); it != std::prev(classes.end()); ++it) {
            e_sum += it->second.first;
            e_count += it->second.second;
        }
        result += std::format(L"  P {:.0f}%  E {:.0f}%", p_sum / p_count, e_sum / e_count);
    }
    return result;
}
} // namespace loadbar
