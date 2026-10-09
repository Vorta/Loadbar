#include "model/model.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace loadbar {
Metric Rate::sample(std::uint64_t counter, Clock::time_point now) {
    Metric result{0, Status::warming_up, Unit::bytes_per_second, now, {}, {}};
    if (previous_ && now > time_ && counter >= *previous_) {
        result.interval = now - time_;
        result.value = static_cast<double>(counter - *previous_) / result.interval.count();
        result.status = std::isfinite(result.value) ? Status::valid : Status::error;
    } else {
        result.detail = L"First sample/reset";
    }
    previous_ = counter;
    time_ = now;
    return result;
}
void Rate::reset() noexcept {
    previous_.reset();
}
double fill_fraction(double value, double ceiling) noexcept {
    if (!std::isfinite(value) || !std::isfinite(ceiling) || ceiling <= 0) {
        return 0;
    }
    return std::clamp(value / ceiling, 0.0, 1.0);
}
std::wstring status_text(Status status) {
    switch (status) {
    case Status::valid:
        return L"Valid";
    case Status::warming_up:
        return L"Warming up";
    case Status::stale:
        return L"Stale";
    case Status::unavailable:
        return L"Unavailable";
    case Status::error:
        return L"Error";
    }
    return L"Error";
}
std::wstring metric_text(MetricView metric, ByteFormat format) {
    const auto shown = presented_metric(metric);
    if (metric.status != Status::valid && shown.status == Status::valid) {
        return metric_text(shown, format) + L" — " + status_text(metric.status) + L"; " +
               retention_text(metric, Clock::now());
    }
    if (metric.status != Status::valid) {
        return status_text(metric.status);
    }
    if (!std::isfinite(metric.value) || metric.value < 0) {
        return status_text(Status::error);
    }
    if (metric.unit == Unit::percent) {
        return std::format(L"{:.1f}%", metric.value);
    }
    if (format == ByteFormat::tooltip) {
        constexpr double mb = 1048576.0, gb = 1073741824.0;
        const bool large = metric.value >= gb;
        const auto unit = large ? L"GB" : L"MB";
        const auto suffix = metric.unit == Unit::bytes_per_second ? L"/s" : L"";
        if (metric.value > 0 && metric.value < 0.1 * mb) {
            return std::format(L"<0.1 {}{}", unit, suffix);
        }
        return std::format(L"{:.1f} {}{}", std::max(0.0, metric.value) / (large ? gb : mb), unit,
                           suffix);
    }
    constexpr const wchar_t *rate_units[]{L"B/s", L"KiB/s", L"MiB/s", L"GiB/s"};
    constexpr const wchar_t *byte_units[]{L"B", L"KiB", L"MiB", L"GiB"};
    double value = metric.value;
    std::size_t index = 0;
    while (value >= 1024 && index < 3) {
        value /= 1024;
        ++index;
    }
    return std::format(L"{:.1f} {}", value,
                       metric.unit == Unit::bytes ? byte_units[index] : rate_units[index]);
}
MetricView presented_metric(MetricView metric) noexcept {
    auto result = metric;
    if (metric.status == Status::valid && std::isfinite(metric.value) && metric.value >= 0) {
        return result;
    }
    if (metric.retained && std::isfinite(metric.retained->value) && metric.retained->value >= 0) {
        result.value = metric.retained->value;
        result.timestamp = metric.retained->timestamp;
        if (!result.session_peak) {
            result.session_peak = metric.retained->session_peak;
        }
        result.status = Status::valid;
    } else if (metric.status == Status::warming_up) {
        result.value = 0;
        result.status = Status::valid;
    } else if (metric.status == Status::valid) {
        result.status = Status::error;
    }
    return result;
}
std::wstring retention_text(MetricView metric, Clock::time_point now) {
    if (metric.status == Status::valid) {
        return {};
    }
    if (metric.retained) {
        const auto age =
            std::max(0.0, std::chrono::duration<double>(now - metric.retained->timestamp).count());
        return std::format(L"Last valid reading; {:.0f} s old", age);
    }
    return metric.status == Status::warming_up ? L"Initial warm-up: showing zero" : L"";
}
Metric percentage(double used, double total, Clock::time_point now) {
    if (!std::isfinite(used) || !std::isfinite(total) || total <= 0 || used < 0 || used > total) {
        return {0, Status::error, Unit::percent, now, {}, L"Invalid percentage scope/value"};
    }
    return {100 * used / total, Status::valid, Unit::percent, now, {}, {}};
}
} // namespace loadbar
