#include "model/snapshot.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace loadbar {
void set_physical_memory(Snapshot &snapshot, std::uint64_t total, std::uint64_t available,
                         Clock::time_point now) {
    Metric percent{0, Status::error, Unit::percent, now, {}, {}};
    snapshot.ram_total_bytes = 0;
    if (total > 0 && available <= total) {
        percent =
            percentage(static_cast<double>(total - available), static_cast<double>(total), now);
        snapshot.ram_total_bytes = total;
    } else {
        percent.detail = L"Invalid physical memory capacity";
    }
    percent.identity = L"physical-memory";
    snapshot.gauges[static_cast<std::size_t>(Gauge::ram)] = percent;
    snapshot.ram_used_bytes = percent;
    snapshot.ram_used_bytes.unit = Unit::bytes;
    snapshot.ram_used_bytes.value =
        percent.status == Status::valid ? static_cast<double>(total - available) : 0;
}
Metric disk_active_time(Metric idle) {
    if (idle.status != Status::valid) {
        return idle;
    }
    if (idle.unit != Unit::percent || !std::isfinite(idle.value) || idle.value < 0 ||
        idle.value > 100) {
        idle.status = Status::error;
        idle.detail = std::format(L"Invalid PhysicalDisk % Idle Time: {}", idle.value);
        return idle;
    }
    idle.value = 100 - idle.value;
    return idle;
}
Metric disk_counter(const std::vector<CounterItem> &items, const Device &disk,
                    Clock::time_point now, Unit unit) {
    const auto number = std::to_wstring(disk.runtime_id);
    const Metric *found = nullptr;
    for (const auto &item : items) {
        if (!item.name.starts_with(number) ||
            (item.name.size() != number.size() && item.name[number.size()] != L' ')) {
            continue;
        }
        if (found) {
            return {0, Status::error, unit, now, {}, L"Ambiguous physical disk counter", disk.id};
        }
        found = &item.metric;
    }
    if (!found) {
        return {0, Status::unavailable, unit, now, {}, L"Physical disk counter absent", disk.id};
    }
    auto result = *found;
    result.identity = disk.id;
    return result;
}
void set_snapshot_status(Snapshot &snapshot, Status status, std::wstring_view detail) {
    const auto apply = [&](Metric &metric) {
        metric.status = status;
        metric.detail = detail;
    };
    for (auto &cpu : snapshot.processors) {
        apply(cpu.utilization);
    }
    for (auto &metric : snapshot.gauges) {
        apply(metric);
    }
    for (auto &disk : snapshot.disks) {
        apply(disk.read);
        apply(disk.write);
        apply(disk.active);
    }
    apply(snapshot.gpu_memory_bytes);
    apply(snapshot.ram_used_bytes);
}
const wchar_t *gauge_name(Gauge gauge) noexcept {
    constexpr const wchar_t *names[]{L"RAM",        L"GPU 3D",     L"GPU decode",
                                     L"GPU memory", L"Net down",   L"Net up",
                                     L"Disk read",  L"Disk write", L"Disk active"};
    const auto index = static_cast<std::size_t>(gauge);
    return index < static_cast<std::size_t>(Gauge::count) ? names[index] : L"Unknown";
}
namespace {
Clock::duration stale_after(unsigned interval_ms) noexcept {
    return std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(
        std::max(3000ULL, static_cast<unsigned long long>(interval_ms) * 3)));
}
template <class SnapshotType, class Apply> void visit_metrics(SnapshotType &snapshot, Apply apply) {
    for (auto &processor : snapshot.processors) {
        apply(processor.utilization);
    }
    for (auto &metric : snapshot.gauges) {
        apply(metric);
    }
    for (auto &disk : snapshot.disks) {
        apply(disk.read);
        apply(disk.write);
        apply(disk.active);
    }
    apply(snapshot.gpu_memory_bytes);
    apply(snapshot.ram_used_bytes);
}
std::optional<Clock::time_point> after(Clock::time_point start, Clock::duration delay) noexcept {
    if (start > Clock::time_point::max() - delay) {
        return std::nullopt;
    }
    return start + delay;
}
Status status_at(MetricView metric, Clock::time_point now, unsigned interval_ms) noexcept {
    if (metric.status != Status::valid) {
        return metric.status;
    }
    const auto deadline = after(metric.timestamp, stale_after(interval_ms));
    return deadline && now > *deadline ? Status::stale : metric.status;
}
} // namespace
MetricView aged_metric(MetricView metric, Clock::time_point now, unsigned interval_ms) noexcept {
    auto result = metric;
    result.status = status_at(metric, now, interval_ms);
    return result;
}
std::optional<Clock::time_point> next_stale_deadline(const Snapshot &snapshot,
                                                     unsigned interval_ms) noexcept {
    std::optional<Clock::time_point> result;
    const auto delay = stale_after(interval_ms) + Clock::duration{1};
    visit_metrics(snapshot, [&](const Metric &metric) {
        if (metric.status == Status::valid) {
            const auto candidate = after(metric.timestamp, delay);
            if (candidate && (!result || *candidate < *result)) {
                result = candidate;
            }
        }
    });
    return result;
}
std::optional<Clock::time_point> next_retention_deadline(const Snapshot &snapshot,
                                                         Clock::time_point now,
                                                         unsigned interval_ms) noexcept {
    std::optional<Clock::time_point> result;
    const auto second = std::chrono::duration_cast<Clock::duration>(std::chrono::seconds(1));
    const auto half = second / 2;
    visit_metrics(snapshot, [&](const Metric &metric) {
        if (!metric.retained || status_at(metric, now, interval_ms) == Status::valid) {
            return;
        }
        const auto observed = metric.retained->timestamp;
        std::optional<Clock::time_point> candidate;
        if (now <= observed) {
            candidate = after(observed, half + Clock::duration{1});
        } else {
            // Subtract only subsecond remainders so malformed extreme timestamps cannot
            // overflow the clock's signed representation. Age changes at half seconds.
            auto delay =
                half - (now.time_since_epoch() % second - observed.time_since_epoch() % second);
            if (delay < Clock::duration::zero()) {
                delay += second;
            } else if (delay >= second) {
                delay -= second;
            }
            candidate = after(now, delay + Clock::duration{1});
        }
        if (candidate && (!result || *candidate < *result)) {
            result = candidate;
        }
    });
    return result;
}
bool expire_snapshot(Snapshot &snapshot, Clock::time_point now, unsigned interval_ms) {
    bool changed{};
    visit_metrics(snapshot, [&](Metric &metric) {
        const auto status = status_at(metric, now, interval_ms);
        changed = changed || metric.status != status;
        metric.status = status;
    });
    return changed;
}
namespace {
std::optional<unsigned> number(std::wstring_view text, unsigned base = 10) {
    if (text.empty()) {
        return std::nullopt;
    }
    unsigned value{};
    for (wchar_t ch : text) {
        unsigned digit{};
        if (ch >= L'0' && ch <= L'9') {
            digit = static_cast<unsigned>(ch - L'0');
        } else if (ch >= L'a' && ch <= L'f') {
            digit = static_cast<unsigned>(ch - L'a') + 10;
        } else if (ch >= L'A' && ch <= L'F') {
            digit = static_cast<unsigned>(ch - L'A') + 10;
        } else {
            return std::nullopt;
        }
        if (digit >= base || value > (std::numeric_limits<unsigned>::max() - digit) / base) {
            return std::nullopt;
        }
        value = value * base + digit;
    }
    return value;
}
std::optional<unsigned> take_number(std::wstring_view &text, std::wstring_view prefix,
                                    unsigned base = 10) {
    if (!text.starts_with(prefix)) {
        return std::nullopt;
    }
    text.remove_prefix(prefix.size());
    const auto end = text.find(L'_');
    const auto value = number(text.substr(0, end), base);
    if (!value) {
        return std::nullopt;
    }
    text.remove_prefix(end == std::wstring_view::npos ? text.size() : end);
    return value;
}
} // namespace
std::optional<ProcessorId> parse_processor(std::wstring_view name) {
    const auto comma = name.find(L',');
    if (comma == std::wstring_view::npos) {
        return std::nullopt;
    }
    const auto group = number(name.substr(0, comma));
    const auto index = number(name.substr(comma + 1));
    if (!group || !index || *index >= 64 || *group > 65535) {
        return std::nullopt;
    }
    return ProcessorId{*group, *index};
}
std::optional<EngineId> parse_engine(std::wstring_view name) {
    const auto pid = take_number(name, L"pid_");
    const auto high = take_number(name, L"_luid_0x", 16);
    const auto low = take_number(name, L"_0x", 16);
    const auto physical = take_number(name, L"_phys_");
    const auto engine = take_number(name, L"_eng_");
    if (!pid || !high || !low || !physical || !engine || !name.starts_with(L"_engtype_")) {
        return std::nullopt;
    }
    name.remove_prefix(9);
    if (name.empty() || name.size() > 64 ||
        name.find_first_of(L"#\\/") != std::wstring_view::npos) {
        return std::nullopt;
    }
    return EngineId{(static_cast<std::uint64_t>(*high) << 32U) | *low, *physical, *engine, *pid,
                    std::wstring(name)};
}
std::optional<std::pair<std::uint64_t, unsigned>> parse_adapter(std::wstring_view name) {
    const auto high = take_number(name, L"luid_0x", 16);
    const auto low = take_number(name, L"_0x", 16);
    const auto physical = take_number(name, L"_phys_");
    if (!high || !low || !physical || !name.empty()) {
        return std::nullopt;
    }
    return std::pair{(static_cast<std::uint64_t>(*high) << 32U) | *low, *physical};
}
Metric aggregate_engines(const std::vector<EngineSample> &samples, std::uint64_t luid,
                         std::wstring_view type, Clock::time_point now) {
    std::map<std::pair<unsigned, unsigned>, double> totals;
    std::set<EngineId> seen;
    std::optional<std::chrono::duration<double>> interval;
    for (const auto &sample : samples) {
        if (sample.id.luid != luid || sample.id.type != type) {
            continue;
        }
        if (!seen.insert(sample.id).second) {
            return {
                0, Status::error, Unit::percent, now, {}, L"Duplicate GPU process/engine instance"};
        }
        if (sample.metric.status != Status::valid) {
            return sample.metric;
        }
        if (!std::isfinite(sample.metric.value) || sample.metric.value < 0 ||
            sample.metric.timestamp != now || (interval && *interval != sample.metric.interval)) {
            return {0, Status::error, Unit::percent, now, {}, L"Invalid GPU value/interval"};
        }
        interval = sample.metric.interval;
        totals[{sample.id.physical, sample.id.engine}] += sample.metric.value;
    }
    if (totals.empty()) {
        return {0, Status::unavailable, Unit::percent, now, {}, L"No valid engine observations"};
    }
    double busiest{};
    for (const auto &[key, value] : totals) {
        (void)key;
        if (!std::isfinite(value)) {
            return {0, Status::error, Unit::percent, now, {}, L"GPU sum overflow"};
        }
        busiest = std::max(busiest, value);
    }
    return {busiest,
            Status::valid,
            Unit::percent,
            now,
            interval.value_or(std::chrono::duration<double>{}),
            busiest > 100 ? L"Raw utilization exceeds 100%; visual fill clipped" : L""};
}
Metric memory_percentage(const std::vector<CounterItem> &items, const Device &device,
                         std::uint64_t capacity, Clock::time_point now, Metric *used_bytes) {
    if (used_bytes) {
        *used_bytes = {0, Status::unavailable, Unit::bytes, now, {}, {}};
    }
    if (!device.memory_scope_known || !capacity) {
        return Metric{0,
                      Status::unavailable,
                      Unit::percent,
                      now,
                      {},
                      L"Adapter memory capacity/physical scope could not be verified"};
    }
    std::optional<Metric> value;
    for (const auto &item : items) {
        const auto identity = parse_adapter(item.name);
        if (!identity || identity->first != device.runtime_id) {
            continue;
        }
        if (identity->second != 0 || value) {
            return Metric{0,
                          Status::unavailable,
                          Unit::percent,
                          now,
                          {},
                          L"Ambiguous adapter memory node scope"};
        }
        value = item.metric;
    }
    if (!value) {
        return Metric{0,
                      Status::unavailable,
                      Unit::percent,
                      now,
                      {},
                      L"Selected adapter memory counter absent"};
    }
    if (value->status != Status::valid) {
        value->unit = Unit::percent;
        return *value;
    }
    auto result = percentage(value->value, static_cast<double>(capacity), now);
    result.interval = value->interval;
    if (used_bytes && result.status == Status::valid) {
        *used_bytes = *value;
        used_bytes->unit = Unit::bytes;
        used_bytes->timestamp = now;
    }
    return result;
}
} // namespace loadbar
