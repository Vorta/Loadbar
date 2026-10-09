#include "model/temperature.hpp"
#include "model/snapshot.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace loadbar {
bool valid_temperature(double value) noexcept {
    // Sanity bounds for these desktop-component sources, not operating-temperature limits.
    return std::isfinite(value) && value >= -99 && value <= 250;
}
bool temperature_visible(const TemperatureReading &reading) noexcept {
    const auto value = presented_metric(reading.metric);
    return value.status == Status::valid && value.unit == Unit::celsius &&
           valid_temperature(value.value);
}
ThermalBand temperature_band(unsigned component, double value) noexcept {
    constexpr std::array<std::array<double, 3>, 4> thresholds{
        {{70, 80, 90}, {55, 65, 75}, {65, 75, 85}, {50, 60, 70}}};
    if (component >= thresholds.size() || !valid_temperature(value)) {
        return ThermalBand::normal;
    }
    const auto &limits = thresholds[component];
    return value >= limits[2]   ? ThermalBand::critical
           : value >= limits[1] ? ThermalBand::hot
           : value >= limits[0] ? ThermalBand::warm
                                : ThermalBand::normal;
}
TemperatureReading choose_temperature(std::span<const TemperatureSensor> sensors,
                                      bool prefer_composite, Clock::time_point now) {
    TemperatureReading result;
    result.metric.timestamp = now;
    const TemperatureSensor *selected{};
    for (std::size_t i = 0; i < sensors.size(); ++i) {
        if (!valid_temperature(sensors[i].celsius)) {
            result.metric.status = Status::error;
            result.metric.detail = L"Invalid temperature observation";
            return result;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (sensors[j].index == sensors[i].index) {
                result.metric.status = Status::error;
                result.metric.detail = L"Duplicate temperature sensor index";
                return result;
            }
        }
        if (!selected || (prefer_composite && sensors[i].index == 0) ||
            (!(prefer_composite && selected->index == 0) &&
             sensors[i].celsius > selected->celsius)) {
            selected = &sensors[i];
        }
    }
    if (selected) {
        result.metric.value = selected->celsius;
        result.metric.status = Status::valid;
        result.metric.detail.clear();
        result.sensor = prefer_composite && selected->index == 0
                            ? L"Device sensor 0 (composite where reported)"
                            : std::format(L"{} {}",
                                          prefer_composite ? L"Hottest reported sensor"
                                                           : L"Main GPU sensor, physical adapter",
                                          selected->index);
    }
    return result;
}
std::wstring temperature_details(const TemperatureReading &reading, Clock::time_point now,
                                 unsigned interval_ms) {
    const auto metric = aged_metric(reading.metric, now, std::max(1000U, interval_ms));
    auto text = L"Temperature: " + metric_text(metric);
    if (!reading.sensor.empty()) {
        text += L"; " + reading.sensor;
    }
    if (!metric.detail.empty()) {
        text += L"; " + std::wstring(metric.detail);
    }
    if (reading.warning) {
        text += std::format(L"; device warning {:.1f}°C", *reading.warning);
    }
    if (reading.critical) {
        text += std::format(L"; device critical {:.1f}°C", *reading.critical);
    }
    return text;
}
} // namespace loadbar
