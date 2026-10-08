#include "model/session_peaks.hpp"

#include <algorithm>
#include <cmath>

namespace loadbar {
void SessionPeaks::observe(Gauge direction, Metric &metric) {
    metric.session_peak.reset();
    if (!is_rate_gauge(direction)) {
        return;
    }
    const auto key = std::pair{std::wstring_view(metric.identity), direction};
    auto found = peaks_.find(key);
    if (found != peaks_.end()) {
        metric.session_peak = found->second.value;
    }
    if (metric.status != Status::valid) {
        return;
    }
    if (metric.identity.empty() || metric.identity.size() > 1024 ||
        metric.unit != Unit::bytes_per_second || !std::isfinite(metric.value) || metric.value < 0 ||
        !std::isfinite(metric.interval.count()) || metric.interval.count() <= 0 ||
        (found != peaks_.end() && metric.timestamp <= found->second.last)) {
        metric.status = Status::error;
        metric.detail = L"Invalid rate observation for session peak";
        return;
    }
    if (found == peaks_.end()) {
        if (peaks_.size() >= kMaximumStreams) {
            metric.status = Status::error;
            metric.detail = L"Session peak unavailable: device/direction limit reached";
            return;
        }
        found = peaks_.emplace(key, Peak{}).first;
    }
    found->second.value = std::max(found->second.value, metric.value);
    found->second.last = metric.timestamp;
    metric.session_peak = found->second.value;
}
void SessionPeaks::observe(Snapshot &snapshot) {
    for (auto &disk : snapshot.disks) {
        observe(Gauge::disk_read, disk.read);
        observe(Gauge::disk_write, disk.write);
    }
    observe(Gauge::download, snapshot.gauges[static_cast<std::size_t>(Gauge::download)]);
    observe(Gauge::upload, snapshot.gauges[static_cast<std::size_t>(Gauge::upload)]);
}
} // namespace loadbar
