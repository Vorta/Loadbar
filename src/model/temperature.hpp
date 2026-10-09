#pragma once

#include "model/model.hpp"
#include <span>

namespace loadbar {
enum class ThermalBand : std::uint8_t { normal, warm, hot, critical };
struct TemperatureReading {
    Metric metric{0, Status::unavailable, Unit::celsius, {}, {}, L"Temperature not reported"};
    // Describes the displayed observation, including when its value is retained.
    std::wstring sensor;
    std::optional<double> warning, critical;
};
struct TemperatureSensor {
    unsigned index{};
    double celsius{};
};
[[nodiscard]] bool valid_temperature(double value) noexcept;
[[nodiscard]] bool temperature_visible(const TemperatureReading &reading) noexcept;
[[nodiscard]] ThermalBand temperature_band(unsigned component, double value) noexcept;
[[nodiscard]] TemperatureReading choose_temperature(std::span<const TemperatureSensor> sensors,
                                                    bool prefer_composite, Clock::time_point now);
[[nodiscard]] std::wstring temperature_details(const TemperatureReading &reading,
                                               Clock::time_point now, unsigned interval_ms);
} // namespace loadbar
