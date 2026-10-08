#include "model/continuity.hpp"

#include <cmath>
#include <format>

namespace loadbar {
namespace {
bool usable(const Metric &metric) {
    return metric.status == Status::valid && std::isfinite(metric.value) && metric.value >= 0;
}
} // namespace
void DisplayContinuity::observe(Metric &metric, Gauge kind) {
    metric.retained.reset();
    if (metric.identity.empty()) {
        return;
    }
    const auto key = std::pair{std::wstring_view(metric.identity), kind};
    auto found = values_.find(key);
    if (metric.status == Status::valid && (!usable(metric) || metric.unit != gauge_unit(kind))) {
        metric.status = Status::error;
        metric.detail += L"; invalid display observation";
    }
    if (usable(metric)) {
        const ObservedValue value{metric.value, metric.timestamp, metric.session_peak};
        if (found != values_.end()) {
            if (metric.timestamp >= found->second.timestamp) {
                found->second = value;
            }
        } else if (values_.size() < kMaximumStreams) {
            found = values_.emplace(key, value).first;
        } else {
            metric.detail += L"; display retention capacity reached";
        }
        metric.retained = value;
    }
    if (found != values_.end()) {
        metric.retained = found->second;
        if (!metric.session_peak) {
            metric.session_peak = found->second.session_peak;
        }
    }
}
void DisplayContinuity::memory(Metric &percent, Metric &bytes, std::uint64_t &capacity,
                               std::wstring_view scope) {
    percent.retained.reset();
    bytes.retained.reset();
    if (percent.identity.empty() || percent.identity != bytes.identity) {
        return;
    }
    const auto key = std::pair{std::wstring_view(percent.identity), scope};
    auto found = memories_.find(key);
    const bool coherent =
        usable(percent) && usable(bytes) && percent.unit == Unit::percent &&
        bytes.unit == Unit::bytes && capacity > 0 && bytes.value <= static_cast<double>(capacity) &&
        percent.timestamp == bytes.timestamp && percent.interval == bytes.interval &&
        std::abs(percent.value - 100 * bytes.value / static_cast<double>(capacity)) < 0.000001;
    if (coherent) {
        const Memory value{
            {percent.value, percent.timestamp}, {bytes.value, bytes.timestamp}, capacity};
        if (found != memories_.end()) {
            if (percent.timestamp >= found->second.percent.timestamp) {
                found->second = value;
            }
        } else if (memories_.size() < kMaximumMemoryScopes) {
            found = memories_.emplace(key, value).first;
        } else {
            percent.detail += L"; display retention capacity reached";
        }
        percent.retained = value.percent;
        bytes.retained = value.bytes;
    } else if (percent.status == Status::valid || bytes.status == Status::valid) {
        percent.status = bytes.status = Status::error;
        percent.detail += L"; inconsistent memory observation";
        bytes.detail += L"; inconsistent memory observation";
    }
    if (found != memories_.end() && !coherent) {
        percent.retained = found->second.percent;
        bytes.retained = found->second.bytes;
        capacity = found->second.capacity;
    }
}
void DisplayContinuity::observe(Snapshot &snapshot, const Settings &settings) {
    const auto selection = [](Selection &previous, std::map<std::wstring, Selection> &known,
                              const std::wstring &preference, std::wstring &id, std::wstring &label,
                              std::wstring *scope) {
        const auto saved = known.find(preference);
        const Selection *retained =
            !preference.empty() && saved != known.end()                       ? &saved->second
            : previous.preference == preference && !previous.identity.empty() ? &previous
                                                                              : nullptr;
        if (id.empty() && retained) {
            id = retained->identity;
            label = retained->label;
            if (scope) {
                *scope = retained->scope;
            }
        }
        if (!id.empty()) {
            const std::wstring_view current_scope = scope ? std::wstring_view(*scope) : L"";
            if (previous.preference != preference || previous.identity != id ||
                previous.label != label || previous.scope != current_scope) {
                previous.preference = preference;
                previous.identity = id;
                previous.label = label;
                previous.scope = current_scope;
            }
            const auto entry = known.find(id);
            if (entry != known.end()) {
                if (entry->second != previous) {
                    entry->second = previous;
                }
            } else if (known.size() < kMaximumMemoryScopes) {
                known.emplace(id, previous);
            }
        } else {
            previous.preference = preference;
            previous.identity.clear();
            previous.label.clear();
            previous.scope.clear();
        }
    };
    selection(gpu_, known_gpus_, settings.gpu_id, snapshot.gpu_id, snapshot.gpu_label,
              &snapshot.gpu_memory_label);
    selection(network_, known_networks_, settings.network_id, snapshot.network_id,
              snapshot.network_label, nullptr);
    for (auto &cpu : snapshot.processors) {
        if (cpu.utilization.identity.empty()) {
            cpu.utilization.identity = std::format(L"processor:{},{}", cpu.id.group, cpu.id.number);
        }
        observe(cpu.utilization, Gauge::count);
    }
    for (std::size_t i = 0; i < snapshot.gauges.size(); ++i) {
        auto &metric = snapshot.gauges[i];
        metric.identity = i == 0  ? L"physical-memory"
                          : i < 4 ? snapshot.gpu_id
                                  : snapshot.network_id;
        if (i != 0 && i != static_cast<std::size_t>(Gauge::gpu_memory)) {
            observe(metric, static_cast<Gauge>(i));
        }
    }
    snapshot.ram_used_bytes.identity = L"physical-memory";
    snapshot.gpu_memory_bytes.identity = snapshot.gpu_id;
    memory(snapshot.gauges[0], snapshot.ram_used_bytes, snapshot.ram_total_bytes,
           L"physical-memory");
    memory(snapshot.gauges[3], snapshot.gpu_memory_bytes, snapshot.gpu_memory_capacity,
           snapshot.gpu_memory_label);
    for (auto &disk : snapshot.disks) {
        disk.read.identity = disk.write.identity = disk.active.identity = disk.id;
        observe(disk.read, Gauge::disk_read);
        observe(disk.write, Gauge::disk_write);
        observe(disk.active, Gauge::disk_active);
    }
}
} // namespace loadbar
