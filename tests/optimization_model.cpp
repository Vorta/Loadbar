#include "model/continuity.hpp"
#include "model/layout.hpp"
#include "model/presentation.hpp"
#include "model/session_peaks.hpp"
#include "model/stream_key.hpp"

#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace loadbar;
using namespace std::chrono_literals;
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
void metric_views() {
    static_assert(std::is_trivially_copyable_v<MetricView>);
    static_assert(!std::is_constructible_v<MetricView, Metric &&>);
    const auto start = Clock::time_point{} + 10s;
    Metric source{75, Status::valid,           Unit::percent,          start,
                  1s, std::wstring(256, L'd'), std::wstring(256, L'i')};
    source.retained = ObservedValue{25, start - 1s, 200};
    const auto view = aged_metric(source, start + 4s, 1000);
    require(view.status == Status::stale && source.status == Status::valid &&
                view.detail.data() == source.detail.data() &&
                view.identity.data() == source.identity.data(),
            "Aging borrows diagnostics without mutating or copying the source");
    const auto shown = presented_metric(view);
    require(shown.status == Status::valid && shown.value == 25 && shown.timestamp == start - 1s &&
                shown.session_peak == 200 && shown.detail.data() == source.detail.data(),
            "Retained views preserve original age, peak and diagnostic lifetime");
    require(retention_text(view, start + 4s) == L"Last valid reading; 5 s old",
            "Retained age uses observation time rather than failure time");
    for (auto status : {Status::warming_up, Status::stale, Status::unavailable, Status::error}) {
        source.status = status;
        require(presented_metric(source).value == 25 && source.value == 75 &&
                    source.status == status,
                "All failures reuse last valid values while raw values remain untouched");
    }
    source.retained.reset();
    source.status = Status::warming_up;
    require(presented_metric(source).status == Status::valid &&
                presented_metric(source).value == 0 && compact_value(source) == L"0%",
            "Initial warmup is a zero presentation without altering provider status");
    source.status = Status::error;
    require(presented_metric(source).status == Status::error && compact_value(source) == L"—",
            "Never-observed errors retain their error presentation");
    source.status = Status::valid;
    source.value = std::numeric_limits<double>::quiet_NaN();
    require(presented_metric(source).status == Status::error,
            "Invalid values cannot become healthy rendering values");
    source.value = 10;
    auto other = source;
    other.detail = L"Different diagnostic";
    require(metric_changed(source, other, start, 1000) &&
                !visual_metric_changed(source, other, start, 1000),
            "Diagnostic changes remain observable without requiring repaint");
    other.status = Status::error;
    other.retained = ObservedValue{source.value, start};
    require(!visual_metric_changed(source, other, start, 1000),
            "Failure with identical retained value does not change graphics");
    other.retained->value = 11;
    require(visual_metric_changed(source, other, start, 1000),
            "Different retained value does change graphics");
    other = source;
    other.session_peak = 500;
    require(visual_metric_changed(source, other, start, 1000),
            "New rate ceiling invalidates graphics even when reading is unchanged");
    other = source;
    other.unit = Unit::bytes;
    require(visual_metric_changed(source, other, start, 1000),
            "Unit changes invalidate hover presentation");
    source.status = other.status = Status::error;
    source.value = std::numeric_limits<double>::quiet_NaN();
    other.value = std::numeric_limits<double>::infinity();
    require(!visual_metric_changed(source, other, start, 1000),
            "Unshown invalid payloads do not cause endless repaints");
    other.status = Status::unavailable;
    require(visual_metric_changed(source, other, start, 1000),
            "Different unretained status marks must repaint");
}
void deadlines() {
    Snapshot s;
    require(!next_stale_deadline(s, 1000) && !next_retention_deadline(s, {}, 1000),
            "Empty snapshots need no UI timer");
    const auto start = Clock::time_point{} + 10s;
    s.gauges[0] = {50, Status::valid, Unit::percent, start, 1s, {}};
    s.processors.push_back({{}, 0, true, {50, Status::valid, Unit::percent, start - 1s, 1s, {}}});
    require(next_stale_deadline(s, 1000) == start + 2s + Clock::duration{1} &&
                next_stale_deadline(s, 5000) == start + 14s + Clock::duration{1},
            "Next deadline is earliest metric and follows sampling threshold");
    require(!expire_snapshot(s, start + 2s, 1000) &&
                expire_snapshot(s, start + 2s + Clock::duration{1}, 1000) &&
                next_stale_deadline(s, 1000) == start + 3s + Clock::duration{1},
            "Strict threshold and remaining valid metric produce exact next transition");
    require(expire_snapshot(s, start + 4s, 1000) && !next_stale_deadline(s, 1000),
            "After every value expires staleness timer can be disarmed");
    s.gauges[0].retained = ObservedValue{50, start};
    require(next_retention_deadline(s, start + 4100ms, 1000) ==
                    start + 4500ms + Clock::duration{1} &&
                next_retention_deadline(s, start + 4500ms, 1000) ==
                    start + 4500ms + Clock::duration{1} &&
                next_retention_deadline(s, start + 4500ms + Clock::duration{1}, 1000) ==
                    start + 5500ms + Clock::duration{1},
            "Age text refreshes immediately after half-second rounding boundaries");
    require(next_retention_deadline(s, start - 1s, 1000) == start + 500ms + Clock::duration{1},
            "Future retained timestamps stay zero until first age boundary");
    s.gauges[0].status = Status::valid;
    require(!next_retention_deadline(s, start, 1000), "Current valid values need no age timer");
    s.gauges[0].timestamp = Clock::time_point::max();
    require(!next_stale_deadline(s, 1000) && !expire_snapshot(s, Clock::time_point::max(), 1000),
            "Extreme timestamps do not overflow status/deadline arithmetic");
    s.gauges[0].status = Status::warming_up;
    require(!next_stale_deadline(s, 1000), "Warmup retains existing non-expiring status semantics");
    s = {};
    s.disks.emplace_back();
    s.disks[0].write = {1, Status::valid, Unit::bytes_per_second, start, 1s, {}};
    require(next_stale_deadline(s, 1000) == start + 3s + Clock::duration{1},
            "Disk directions participate in deadline selection");
    s.disks.clear();
    s.gpu_memory_bytes = {1, Status::valid, Unit::bytes, start, 1s, {}};
    require(next_stale_deadline(s, 1000) == start + 3s + Clock::duration{1},
            "GPU memory bytes participate in deadline selection");
    s.gpu_memory_bytes.status = Status::error;
    s.ram_used_bytes = {1, Status::valid, Unit::bytes, start, 1s, {}};
    require(next_stale_deadline(s, 1000) == start + 3s + Clock::duration{1},
            "RAM bytes participate in deadline selection");
}
void borrowed_keys() {
    std::map<std::pair<std::wstring, Gauge>, int, StreamKeyLess> streams;
    const std::wstring identity(512, L'x');
    streams.emplace(std::pair{identity, Gauge::download}, 1);
    streams.emplace(std::pair{identity, Gauge::upload}, 2);
    const std::wstring padded = L"!" + identity + L"!";
    const auto view = std::wstring_view(padded).substr(1, identity.size());
    require(streams.find(std::pair{view, Gauge::download})->second == 1 &&
                streams.find(std::pair{view, Gauge::upload})->second == 2,
            "Borrowed nonterminated identities find their own direction without copying");
    std::map<std::pair<std::wstring, std::wstring>, int, StreamKeyLess> memory;
    memory.emplace(std::pair{identity, L"dedicated"}, 3);
    require(memory.find(std::pair{view, std::wstring_view(L"dedicated")})->second == 3 &&
                memory.find(std::pair{view, std::wstring_view(L"shared")}) == memory.end(),
            "Memory lookup preserves explicit scope identity");
    SessionPeaks peaks;
    const auto start = Clock::time_point{} + 1s;
    Metric metric{100, Status::valid, Unit::bytes_per_second, start, 1s, {}, identity};
    peaks.observe(Gauge::download, metric);
    metric.timestamp += 1s;
    metric.value = 20;
    peaks.observe(Gauge::download, metric);
    require(metric.session_peak == 100, "Borrowed session lookup preserves earlier peak");
    peaks.observe(Gauge::upload, metric);
    require(metric.session_peak == 20, "Independent directions have independent peaks");
}
void lazy_diagnostics() {
    const auto start = Clock::time_point{} + 10s;
    Rate rate;
    require(rate.sample(100, start).detail == L"First sample/reset",
            "First rate observation retains its diagnostic");
    const auto current = rate.sample(120, start + 1s);
    require(current.status == Status::valid && current.value == 20 && current.detail.empty(),
            "Successful rate calculation has no fallback diagnostic");
    require(rate.sample(1, start + 2s).detail == L"First sample/reset" &&
                rate.sample(2, start + 2s).detail == L"First sample/reset",
            "Counter resets and repeated timestamps retain warmup diagnostics");
    Device device;
    device.id = L"stable-disk";
    device.runtime_id = 1;
    Metric reading{25, Status::valid, Unit::bytes_per_second, start, 1s, L"Native detail"};
    std::vector<CounterItem> items{{L"10 C:", reading}, {L"1 D:", reading}};
    auto disk = disk_counter(items, device, start + 1s);
    require(disk.status == Status::valid && disk.timestamp == start &&
                disk.detail == L"Native detail" && disk.identity == device.id,
            "Disk selection preserves source diagnostics and time with stable device identity");
    items.push_back({L"1", reading});
    require(disk_counter(items, device, start).detail == L"Ambiguous physical disk counter",
            "Duplicate disk matches retain the ambiguity diagnostic");
    items.resize(1);
    require(disk_counter(items, device, start).detail == L"Physical disk counter absent",
            "Numeric disk prefixes are not confused with another device");
    Snapshot memory;
    set_physical_memory(memory, 100, 25, start);
    require(memory.gauges[0].status == Status::valid && memory.gauges[0].detail.empty() &&
                memory.ram_used_bytes.detail.empty(),
            "Valid memory readings do not retain fallback diagnostics");
    set_physical_memory(memory, 0, 0, start);
    require(memory.gauges[0].detail == L"Invalid physical memory capacity" &&
                memory.ram_used_bytes.detail == memory.gauges[0].detail,
            "Invalid memory capacities retain coherent diagnostics");
    DisplayContinuity continuity;
    Settings settings;
    settings.gpu_id = L"gpu";
    Snapshot first;
    first.gpu_id = settings.gpu_id;
    first.gpu_label = L"Old label";
    first.gpu_memory_label = L"Dedicated scope";
    continuity.observe(first, settings);
    continuity.observe(first, settings);
    first.gpu_label = L"New label";
    first.gpu_memory_label = L"Updated scope";
    continuity.observe(first, settings);
    Snapshot missing;
    continuity.observe(missing, settings);
    require(missing.gpu_id == settings.gpu_id && missing.gpu_label == L"New label" &&
                missing.gpu_memory_label == L"Updated scope",
            "Unchanged-selection shortcut still refreshes changed label and memory scope");
}

} // namespace
void run_optimization_model_tests() {
    metric_views();
    deadlines();
    borrowed_keys();
    lazy_diagnostics();
}
