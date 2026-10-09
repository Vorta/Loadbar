#include "model/presentation.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <map>

namespace loadbar {
Color component_icon_color(unsigned component) noexcept {
    constexpr std::array colors{kCpuIcon, rgb(0x8EB6DC), rgb(0xB1AADB), rgb(0x96BD96),
                                rgb(0xD79FAF)};
    return component < colors.size() ? colors[component] : kSecondary;
}
TemperatureAppearance temperature_appearance(unsigned component, double celsius) noexcept {
    TemperatureAppearance result{component_icon_color(component), kSecondary};
    if (component >= 4 || !valid_temperature(celsius)) {
        return result;
    }
    constexpr std::array colors{kSecondary, rgb(0xF2B840), rgb(0xFF623A), rgb(0xFF3456)};
    result.text = colors[static_cast<std::size_t>(temperature_band(component, celsius))];
    const auto blend = static_cast<float>(std::clamp((celsius - 60) / 30, 0.0, 1.0));
    result.icon = {std::lerp(result.icon.r, result.text.r, blend),
                   std::lerp(result.icon.g, result.text.g, blend),
                   std::lerp(result.icon.b, result.text.b, blend)};
    result.glow_alpha = .55F * static_cast<float>(std::clamp((celsius - 90) / 5, 0.0, 1.0));
    return result;
}
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
std::wstring peak_text(MetricView metric, ByteFormat format) {
    if (format == ByteFormat::tooltip && metric.session_peak) {
        MetricView peak;
        peak.value = *metric.session_peak;
        peak.status = Status::valid;
        peak.unit = Unit::bytes_per_second;
        return L"Linear scale; peak since launch " + metric_text(peak, format);
    }
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
std::wstring readout_quantity(MetricView source) {
    const auto metric = presented_metric(source);
    if (metric.status != Status::valid || !std::isfinite(metric.value) || metric.value < 0) {
        return L"—";
    }
    if (metric.unit == Unit::percent) {
        return std::format(L"{:.0f}%", metric.value);
    }
    constexpr std::array units{L"B", L"K", L"M", L"G", L"T", L"P", L"E"};
    double value = metric.value;
    std::size_t unit{};
    while (value >= 1024 && unit + 1 < units.size()) {
        value /= 1024;
        ++unit;
    }
    if (value == 0) {
        return L"0";
    }
    if (value < 0.1) {
        return std::format(L"<0.1{}", units[unit]);
    }
    // At most one decimal. Promote rounded boundaries instead of showing 1024K.
    value = value < 100 ? std::round(value * 10) / 10 : std::round(value);
    if (value >= 1024 && unit + 1 < units.size()) {
        value /= 1024;
        ++unit;
    }
    if (value >= 100 || (value >= 10 && std::floor(value) == value)) {
        return std::format(L"{:.0f}{}", value, units[unit]);
    }
    return std::format(L"{:.1f}{}", value, units[unit]);
}
Readout component_readout(unsigned kind, std::size_t disk, const Snapshot &s,
                          const Settings &settings, Clock::time_point now) {
    Readout result;
    const auto shown = [&](MetricView m) {
        return presented_metric(aged_metric(m, now, settings.interval_ms));
    };
    const auto part = [&](Gauge gauge, std::wstring_view prefix = L"") {
        return ReadoutPart{std::wstring(prefix) +
                               readout_quantity(shown(s.gauges[static_cast<std::size_t>(gauge)])),
                           gauge_palette(gauge).text};
    };
    if (kind == 0) {
        result.primary = {L"—", kPrimary};
        double total{}, p_total{}, e_total{};
        unsigned highest{}, lowest = std::numeric_limits<unsigned>::max();
        bool classified = true;
        for (const auto &cpu : s.processors) {
            if (!cpu.efficiency_class) {
                classified = false;
            } else {
                highest = std::max(highest, *cpu.efficiency_class);
                lowest = std::min(lowest, *cpu.efficiency_class);
            }
        }
        std::size_t p_count{}, e_count{};
        for (const auto &cpu : s.processors) {
            const auto metric = shown(cpu.utilization);
            if (metric.status != Status::valid || !std::isfinite(metric.value)) {
                return result;
            }
            total += metric.value;
            if (cpu.efficiency_class && *cpu.efficiency_class == highest) {
                p_total += metric.value;
                ++p_count;
            } else {
                e_total += metric.value;
                ++e_count;
            }
        }
        if (!s.processors.empty()) {
            result.primary.text =
                std::format(L"{:.0f}%", total / static_cast<double>(s.processors.size()));
        }
        if (classified && highest > lowest && p_count && e_count) {
            result.secondary[0] = {std::format(L"P{:.0f}", p_total / static_cast<double>(p_count)),
                                   kSecondary};
            result.secondary[1] = {std::format(L"E{:.0f}", e_total / static_cast<double>(e_count)),
                                   kSecondary};
        }
    } else if (kind == 1) {
        result.primary = part(Gauge::ram);
        const auto used = shown(s.ram_used_bytes);
        const auto percent = shown(s.gauges[static_cast<std::size_t>(Gauge::ram)]);
        std::wstring capacity = L"—";
        if (used.status == Status::valid && percent.status == Status::valid &&
            used.unit == Unit::bytes && std::isfinite(used.value) && used.value >= 0 &&
            s.ram_total_bytes > 0 && used.value <= static_cast<double>(s.ram_total_bytes)) {
            constexpr std::array units{L"G", L"T", L"P", L"E"};
            double divisor = 1073741824.0;
            std::size_t unit{};
            while (static_cast<double>(s.ram_total_bytes) / divisor >= 1024 &&
                   unit + 1 < units.size()) {
                divisor *= 1024;
                ++unit;
            }
            const auto number = [](double value, bool total) {
                if (value > 0 && value < 0.1) {
                    return std::wstring(L"<0.1");
                }
                return value >= 100 || (total && std::floor(value) == value)
                           ? std::format(L"{:.0f}", value)
                           : std::format(L"{:.1f}", value);
            };
            capacity = number(used.value / divisor, false) + L"/" +
                       number(static_cast<double>(s.ram_total_bytes) / divisor, true) +
                       units.at(unit);
        }
        result.secondary[0] = {std::move(capacity), gauge_palette(Gauge::ram).text};
    } else if (kind == 2) {
        result.primary = part(Gauge::gpu_3d);
        if (gpu_memory_visible(s)) {
            result.secondary[0] = {readout_quantity(shown(s.gpu_memory_bytes)),
                                   gauge_palette(Gauge::gpu_memory).text};
        }
        result.secondary[1] = part(Gauge::gpu_decode, L"▶");
    } else if (kind == 3 && disk < s.disks.size()) {
        result.primary = {readout_quantity(shown(s.disks[disk].active)),
                          gauge_palette(Gauge::disk_active).text};
        result.secondary[0] = {L"R" + readout_quantity(shown(s.disks[disk].read)),
                               gauge_palette(Gauge::disk_read).text};
        result.secondary[1] = {L"W" + readout_quantity(shown(s.disks[disk].write)),
                               gauge_palette(Gauge::disk_write).text};
    } else if (kind == 4) {
        result.primary = part(Gauge::download, L"↓");
        result.secondary[0] = part(Gauge::upload, L"↑");
    }
    return result;
}
} // namespace loadbar
