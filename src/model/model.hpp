#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace loadbar {
using Clock = std::chrono::steady_clock;
struct ActivityState {
    bool locked{}, suspended{};
    [[nodiscard]] bool paused() const noexcept {
        return locked || suspended;
    }
};
enum class Status : std::uint8_t { valid, warming_up, stale, unavailable, error };
enum class Unit : std::uint8_t { percent, bytes_per_second, bytes, celsius };
struct ObservedValue {
    double value{};
    Clock::time_point timestamp{};
    std::optional<double> session_peak;
    bool operator==(const ObservedValue &) const = default;
};
struct Metric {
    double value{};
    Status status{Status::unavailable};
    Unit unit{Unit::percent};
    Clock::time_point timestamp{};
    std::chrono::duration<double> interval{};
    std::wstring detail;
    std::wstring identity;
    // Maximum valid rate observed for this device/direction during this process.
    // Empty means no valid observation; zero is an observed idle rate.
    std::optional<double> session_peak;
    // Presentation only: never an input to providers, rate baselines, or peaks.
    std::optional<ObservedValue> retained;
};
// A short-lived, non-owning presentation view. Keep the source Metric alive and unchanged
// while its diagnostic strings are used; numeric/status transformations never copy strings.
struct MetricView {
    double value{};
    Status status{Status::unavailable};
    Unit unit{Unit::percent};
    Clock::time_point timestamp{};
    std::chrono::duration<double> interval{};
    std::wstring_view detail;
    std::wstring_view identity;
    std::optional<double> session_peak;
    std::optional<ObservedValue> retained;

    MetricView() = default;
    MetricView(const Metric &metric) noexcept
        : value(metric.value), status(metric.status), unit(metric.unit),
          timestamp(metric.timestamp), interval(metric.interval), detail(metric.detail),
          identity(metric.identity), session_peak(metric.session_peak), retained(metric.retained) {}
    MetricView(Metric &&) = delete;
    MetricView(const Metric &&) = delete;
};
class Rate {
  public:
    Metric sample(std::uint64_t counter, Clock::time_point now);
    void reset() noexcept;

  private:
    std::optional<std::uint64_t> previous_;
    Clock::time_point time_{};
};
[[nodiscard]] double fill_fraction(double value, double ceiling) noexcept;
[[nodiscard]] std::wstring status_text(Status status);
enum class ByteFormat : std::uint8_t { adaptive_binary, tooltip };
[[nodiscard]] std::wstring metric_text(MetricView metric,
                                       ByteFormat format = ByteFormat::adaptive_binary);
// A disposable rendering view. The source metric retains its actual provider status.
[[nodiscard]] MetricView presented_metric(MetricView metric) noexcept;
[[nodiscard]] std::wstring retention_text(MetricView metric, Clock::time_point now);
[[nodiscard]] Metric percentage(double used, double total, Clock::time_point now);
} // namespace loadbar
