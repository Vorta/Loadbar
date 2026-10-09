#include "telemetry/collector.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <initializer_list>
#include <map>

namespace loadbar {
namespace {
Metric unavailable(Clock::time_point now, Unit unit, std::wstring_view reason) {
    return {0, Status::unavailable, unit, now, {}, std::wstring(reason)};
}
Metric failed(Clock::time_point now, Unit unit, const Error &error) {
    return {0,    error.code == ERROR_NOT_READY ? Status::warming_up : Status::error,
            unit, now,
            {},   error_text(error)};
}
Metric &gauge(Snapshot &snapshot, Gauge index) {
    return snapshot.gauges[static_cast<std::size_t>(index)];
}
} // namespace
bool same_catalog(const Catalog &left, const Catalog &right) {
    return left.disks == right.disks && left.gpus == right.gpus &&
           left.networks == right.networks && left.detail == right.detail &&
           std::ranges::equal(left.processors, right.processors,
                              [](const Processor &a, const Processor &b) {
                                  return a.id == b.id && a.core == b.core && a.mapped == b.mapped &&
                                         a.efficiency_class == b.efficiency_class;
                              });
}
void CpuSamples::configure(const std::vector<Processor> &topology) {
    discovered_ = topology;
    topology_ = topology;
    reindex();
}
void CpuSamples::reindex() {
    std::ranges::sort(topology_, [](const Processor &a, const Processor &b) {
        return a.core == b.core ? a.id < b.id : a.core < b.core;
    });
    indices_.clear();
    for (std::size_t index = 0; index < topology_.size(); ++index) {
        auto &processor = topology_[index];
        indices_.try_emplace(processor.id, index);
        processor.utilization.identity =
            std::format(L"processor:{},{}", processor.id.group, processor.id.number);
    }
}
void CpuSamples::apply(std::vector<Processor> &output, const std::vector<CounterItem> &items,
                       Clock::time_point now) {
    output = topology_;
    const auto previous_count = topology_.size();
    seen_.assign(output.size(), false);
    for (const auto &item : items) {
        const auto id = parse_processor(item.name);
        if (!id) {
            continue;
        }
        const auto known = indices_.find(*id);
        std::size_t index{};
        if (known != indices_.end()) {
            index = known->second;
        } else {
            // Missing/error arrays are not evidence of processor removal. Retain only identity
            // metadata; DisplayContinuity owns the last value. A completed topology scan prunes it.
            if (topology_.size() >= 65536) {
                throw std::length_error("CPU identity retention exhausted");
            }
            index = topology_.size();
            topology_.push_back({*id, 100000U + id->group * 64 + id->number, false, {}});
            topology_.back().utilization.identity =
                std::format(L"processor:{},{}", id->group, id->number);
            indices_.emplace(*id, index);
            output.push_back(topology_.back());
            seen_.push_back(false);
        }
        auto &metric = output[index].utilization;
        auto identity = std::move(metric.identity);
        metric = !seen_[index]
                     ? item.metric
                     : failed(now, Unit::percent, {L"Duplicate CPU counter", ERROR_DUP_NAME});
        metric.identity = std::move(identity);
        seen_[index] = true;
    }
    for (std::size_t index = 0; index < output.size(); ++index) {
        if (!seen_[index]) {
            auto &metric = output[index].utilization;
            auto identity = std::move(metric.identity);
            metric = unavailable(now, Unit::percent, L"Logical processor counter absent");
            metric.identity = std::move(identity);
        }
    }
    if (topology_.size() != previous_count) {
        reindex();
        std::ranges::sort(output, [](const Processor &a, const Processor &b) {
            return a.core == b.core ? a.id < b.id : a.core < b.core;
        });
    }
}
const std::vector<EngineSample> &GpuSamples::read(const std::vector<CounterItem> &items,
                                                  std::uint64_t selected_luid) {
    for (auto &[name, entry] : identities_) {
        (void)name;
        entry.seen = false;
    }
    std::size_t count{};
    for (const auto &item : items) {
        auto found = identities_.find(item.name);
        std::optional<EngineId> uncached;
        const std::optional<EngineId> *identity{};
        if (found == identities_.end() && identities_.size() < kMaximumCachedNames) {
            found = identities_.try_emplace(item.name, Entry{parse_engine(item.name), true}).first;
        }
        if (found != identities_.end()) {
            found->second.seen = true;
            identity = &found->second.identity;
        } else {
            uncached = parse_engine(item.name);
            identity = &uncached;
        }
        if (!*identity || (*identity)->luid != selected_luid ||
            ((*identity)->type != L"3D" && (*identity)->type != L"VideoDecode")) {
            continue;
        }
        if (count == samples_.size()) {
            samples_.emplace_back();
        }
        samples_[count].id = **identity;
        samples_[count].metric = item.metric;
        ++count;
    }
    samples_.resize(count);
    std::erase_if(identities_, [](const auto &entry) { return !entry.second.seen; });
    return samples_;
}
void GpuSamples::deactivate() {
    identities_.clear();
    std::vector<EngineSample>{}.swap(samples_);
    aggregation_.release();
}
CounterSource::CounterSource(std::vector<std::wstring> paths, Unit unit)
    : paths_(std::move(paths)), unit_(unit) {}
void CounterSource::reset() {
    query_.reset();
    open_ = false;
    retry_ = {};
    delay_ = 1;
}
void CounterSource::deactivate() {
    reset();
    query_.deactivate();
    std::vector<std::vector<CounterItem>>{}.swap(arrays_);
    last_error_ = {};
}
Result<std::span<const std::vector<CounterItem>>> CounterSource::sample(Clock::time_point now) {
    if (now < retry_) {
        return std::unexpected(last_error_);
    }
    auto result = open_ ? Result<void>{} : query_.open(paths_);
    if (result) {
        open_ = true;
        result = query_.collect(now);
    }
    if (!result) {
        last_error_ = result.error();
        retry_ = now + std::chrono::seconds(delay_);
        delay_ = std::min(delay_ * 2, 30U);
        query_.reset();
        open_ = false;
        return std::unexpected(last_error_);
    }
    arrays_.resize(paths_.size());
    for (std::size_t index = 0; index < paths_.size(); ++index) {
        auto values = query_.array(index, unit_, arrays_[index]);
        if (!values) {
            if (values.error().code != ERROR_NOT_READY) {
                last_error_ = values.error();
                retry_ = now + std::chrono::seconds(delay_);
                delay_ = std::min(delay_ * 2, 30U);
            }
            return std::unexpected(values.error());
        }
    }
    delay_ = 1;
    return arrays_;
}
Collector::Collector(SessionPeaks &peaks, DiskInventory &disks, CpuSamples &processors,
                     Discover discover)
    : peaks_(peaks), known_disks_(disks), discover_(std::move(discover)), cpu_samples_(processors),
      cpu_({L"\\Processor Information(*)\\% Processor Time"}, Unit::percent),
      disk_io_(
          {L"\\PhysicalDisk(*)\\Disk Read Bytes/sec", L"\\PhysicalDisk(*)\\Disk Write Bytes/sec"},
          Unit::bytes_per_second),
      disk_idle_({L"\\PhysicalDisk(*)\\% Idle Time"}, Unit::percent),
      gpu_engines_({L"\\GPU Engine(*)\\Utilization Percentage"}, Unit::percent),
      gpu_memory_({L"\\GPU Adapter Memory(*)\\Dedicated Usage"}, Unit::bytes),
      gpu_shared_({L"\\GPU Adapter Memory(*)\\Shared Usage"}, Unit::bytes) {}
void Collector::configure(const Settings &settings, bool reset, bool reset_gpu, bool reset_network,
                          bool reset_temperatures) {
    auto discovery = discover_ ? discover_() : discover_devices();
    auto &catalog = discovery.catalog;
    const bool disk_recovered = disk_discovery_error_ && !discovery.disk_error;
    if (discovery.disk_error) {
        // Failed/incomplete scans cannot establish disappearance or a safe counter binding.
        catalog.disks = known_disks_.devices;
        catalog.detail += L"Disk discovery failed: " + error_text(*discovery.disk_error) + L". ";
    } else {
        known_disks_.devices = catalog.disks;
    }
    disk_discovery_error_ = std::move(discovery.disk_error);
    const auto network = select_device(catalog.networks, settings.network_id);
    const auto gpu = select_device(catalog.gpus, settings.gpu_id);
    if (discovery.cpu_error) {
        catalog.processors = cpu_samples_.discovered();
        catalog.detail += L"CPU topology discovery failed: " + error_text(*discovery.cpu_error) +
                          L". Keeping known processor identities. ";
    }
    if (reset || !std::ranges::equal(catalog.processors, catalog_.processors,
                                     [](const Processor &a, const Processor &b) {
                                         return a.id == b.id && a.core == b.core &&
                                                a.mapped == b.mapped &&
                                                a.efficiency_class == b.efficiency_class;
                                     })) {
        cpu_.reset();
    }
    if (!discovery.cpu_error) {
        // A complete topology scan is the authority for removal, even when the mapped list is
        // unchanged: previously counter-only identities no longer present can be pruned.
        cpu_samples_.configure(catalog.processors);
    }
    const bool disk_sampling = std::ranges::any_of(
        catalog.disks, [&](const auto &disk) { return disk_visible(disk.id, settings); });
    if (reset || disk_recovered || catalog.disks != catalog_.disks ||
        disk_sampling != disk_sampling_) {
        disk_io_.reset();
        disk_idle_.reset();
    }
    if (reset || reset_network || network != network_ ||
        settings.network_visible != network_visible_) {
        network_provider_.reset();
    }
    if (reset || reset_gpu || gpu != gpu_ || settings.gpu_visible != gpu_visible_) {
        gpu_engines_.reset();
        gpu_memory_.reset();
        gpu_shared_.reset();
    }
    if (!disk_sampling) {
        disk_io_.deactivate();
        disk_idle_.deactivate();
    }
    if (!settings.gpu_visible) {
        gpu_engines_.deactivate();
        gpu_memory_.deactivate();
        gpu_shared_.deactivate();
        gpu_samples_.deactivate();
    }
    hidden_disks_ = settings.hidden_disks;
    disk_sampling_ = disk_sampling;
    catalog_ = std::move(catalog);
    network_ = network;
    gpu_ = gpu;
    network_visible_ = settings.network_visible;
    gpu_visible_ = settings.gpu_visible;
    temperatures_.configure(catalog_, gpu_, settings, reset || reset_temperatures,
                            disk_discovery_error_.has_value(), reset_gpu);
}
void Collector::cpu(Snapshot &snapshot, Clock::time_point now) {
    auto values = cpu_.sample(now);
    if (values) {
        cpu_samples_.apply(snapshot.processors, (*values)[0], now);
    } else {
        snapshot.processors = cpu_samples_.topology();
        for (auto &processor : snapshot.processors) {
            auto identity = std::move(processor.utilization.identity);
            processor.utilization = failed(now, Unit::percent, values.error());
            processor.utilization.identity = std::move(identity);
        }
    }
}
template <class ReadIo, class ReadIdle>
void collect_disk_impl(Snapshot &snapshot, const std::vector<Device> &devices,
                       Clock::time_point now, const ReadIo &read_io, const ReadIdle &read_idle,
                       const Error *discovery_error, const HiddenDiskIds &hidden_disks) {
    snapshot.disks.clear();
    snapshot.disks.reserve(devices.size());
    for (const auto &device : devices) {
        snapshot.disks.push_back({device.id, device.label});
        if (disk_hidden(hidden_disks, device.id)) {
            auto &reading = snapshot.disks.back();
            reading.active = unavailable(now, Unit::percent, L"Hidden — sampling paused");
            reading.active.identity = device.id;
            reading.read = reading.write = reading.active;
            reading.read.unit = reading.write.unit = Unit::bytes_per_second;
        }
    }
    if (discovery_error) {
        snapshot.disk_discovery = {
            0, Status::error, Unit::percent, now, {}, error_text(*discovery_error)};
        for (auto &reading : snapshot.disks) {
            if (disk_hidden(hidden_disks, reading.id)) {
                continue;
            }
            reading.active = snapshot.disk_discovery;
            reading.active.identity = reading.id;
            reading.read = reading.write = reading.active;
            reading.read.unit = reading.write.unit = Unit::bytes_per_second;
        }
        // Keep raw errors: DisplayContinuity supplies only the last valid presentation.
        // Never collect against potentially reassigned PhysicalDisk numbers until rediscovery.
        return;
    }
    snapshot.disk_discovery = {0,  Status::unavailable,           Unit::percent, now,
                               {}, L"No physical disk discovered"};
    if (std::ranges::none_of(
            devices, [&](const auto &device) { return !disk_hidden(hidden_disks, device.id); })) {
        return;
    }
    try {
        auto values = read_io();
        if (values && values->size() != 2) {
            values = std::unexpected(Error{L"Disk I/O counter array count", ERROR_INVALID_DATA});
        }
        for (std::size_t i = 0; i < snapshot.disks.size(); ++i) {
            auto &reading = snapshot.disks[i];
            if (disk_hidden(hidden_disks, reading.id)) {
                continue;
            }
            const auto &device = devices[i];
            reading.read = values ? disk_counter((*values)[0], device, now)
                                  : failed(now, Unit::bytes_per_second, values.error());
            reading.write = values ? disk_counter((*values)[1], device, now)
                                   : failed(now, Unit::bytes_per_second, values.error());
            reading.read.identity = reading.write.identity = device.id;
        }
    } catch (...) {
        for (auto &reading : snapshot.disks) {
            if (disk_hidden(hidden_disks, reading.id)) {
                continue;
            }
            reading.read = reading.write =
                failed(now, Unit::bytes_per_second,
                       {L"Disk provider exception", ERROR_UNHANDLED_EXCEPTION});
            reading.read.identity = reading.write.identity = reading.id;
        }
    }
    // A missing idle counter must not suppress successfully collected throughput.
    try {
        auto values = read_idle();
        if (values && values->size() != 1) {
            values = std::unexpected(Error{L"Disk idle counter array count", ERROR_INVALID_DATA});
        }
        for (std::size_t i = 0; i < snapshot.disks.size(); ++i) {
            auto &reading = snapshot.disks[i];
            if (disk_hidden(hidden_disks, reading.id)) {
                continue;
            }
            reading.active =
                values
                    ? disk_active_time(disk_counter((*values)[0], devices[i], now, Unit::percent))
                    : failed(now, Unit::percent, values.error());
            reading.active.identity = reading.id;
        }
    } catch (...) {
        for (auto &reading : snapshot.disks) {
            if (disk_hidden(hidden_disks, reading.id)) {
                continue;
            }
            reading.active = failed(now, Unit::percent,
                                    {L"Disk idle provider exception", ERROR_UNHANDLED_EXCEPTION});
            reading.active.identity = reading.id;
        }
    }
}
void collect_disk_readings(Snapshot &snapshot, const std::vector<Device> &devices,
                           Clock::time_point now, const DiskCounterRead &read_io,
                           const DiskCounterRead &read_idle, const Error *discovery_error,
                           const HiddenDiskIds &hidden_disks) {
    collect_disk_impl(snapshot, devices, now, read_io, read_idle, discovery_error, hidden_disks);
}
void Collector::disk(Snapshot &snapshot, Clock::time_point now) {
    collect_disk_impl(
        snapshot, catalog_.disks, now, [&] { return disk_io_.sample(now); },
        [&] { return disk_idle_.sample(now); },
        disk_discovery_error_ ? &*disk_discovery_error_ : nullptr, hidden_disks_);
}
void Collector::network(Snapshot &snapshot, Clock::time_point now) {
    if (!network_visible_) {
        snapshot.network_id = network_ ? network_->id : L"";
        snapshot.network_label = network_ ? network_->label : L"Network";
        for (auto kind : {Gauge::download, Gauge::upload}) {
            gauge(snapshot, kind) = unavailable(now, gauge_unit(kind), L"Hidden — sampling paused");
        }
        return;
    }
    if (!network_) {
        for (auto kind : {Gauge::download, Gauge::upload}) {
            gauge(snapshot, kind) = unavailable(
                now, gauge_unit(kind), L"Select an available, unambiguous device in Settings");
        }
        return;
    }
    snapshot.network_label = network_->label;
    snapshot.network_id = network_->id;
    const auto values = network_provider_.sample(network_->runtime_id);
    gauge(snapshot, Gauge::download) =
        values ? (*values)[0] : failed(now, Unit::bytes_per_second, values.error());
    gauge(snapshot, Gauge::upload) =
        values ? (*values)[1] : failed(now, Unit::bytes_per_second, values.error());
}
template <class ReadEngines, class ReadDedicated, class ReadShared>
void collect_gpu_impl(Snapshot &snapshot, const Device &device, Clock::time_point now,
                      const ReadEngines &read_engines, const ReadDedicated &read_dedicated,
                      const ReadShared &read_shared, GpuSamples &cache) {
    snapshot.gpu_label = device.label;
    snapshot.gpu_id = device.id;
    snapshot.gpu_memory_label = L"GPU VRAM";
    snapshot.gpu_memory_capacity = device.capacity;
    // Query failures are isolated: an engine failure must not suppress either memory source.
    const auto read = [](const auto &source) -> decltype(source()) {
        try {
            auto values = source();
            if (values && values->size() != 1) {
                return std::unexpected(Error{L"GPU counter array count", ERROR_INVALID_DATA});
            }
            return values;
        } catch (...) {
            return std::unexpected(Error{L"GPU provider exception", ERROR_UNHANDLED_EXCEPTION});
        }
    };
    const auto values = read(read_engines);
    if (values) {
        cache.read((*values)[0], device.runtime_id);
        gauge(snapshot, Gauge::gpu_3d) = cache.aggregate(device.runtime_id, L"3D", now);
        gauge(snapshot, Gauge::gpu_decode) =
            cache.aggregate(device.runtime_id, L"VideoDecode", now);
    } else {
        gauge(snapshot, Gauge::gpu_3d) = failed(now, Unit::percent, values.error());
        gauge(snapshot, Gauge::gpu_decode) = gauge(snapshot, Gauge::gpu_3d);
    }
    const auto memory = read(read_dedicated);
    auto &selected = gauge(snapshot, Gauge::gpu_memory);
    selected = memory ? memory_percentage((*memory)[0], device, device.capacity, now,
                                          &snapshot.gpu_memory_bytes)
                      : failed(now, Unit::percent, memory.error());
    if (selected.status != Status::valid) {
        const auto dedicated = selected;
        const auto shared = read(read_shared);
        selected = shared ? memory_percentage((*shared)[0], device, device.shared_capacity, now,
                                              &snapshot.gpu_memory_bytes)
                          : failed(now, Unit::percent, shared.error());
        if (selected.status == Status::valid) {
            snapshot.gpu_memory_label = L"GPU shared";
            snapshot.gpu_memory_capacity = device.shared_capacity;
        } else if (dedicated.status == Status::error) {
            selected.status = Status::error;
        }
        selected.detail +=
            L"; dedicated: " + status_text(dedicated.status) + L" " + dedicated.detail;
        selected.detail += L"; shared percentage uses the reported adapter shared-memory limit";
    }
    if (gauge(snapshot, Gauge::gpu_memory).status != Status::valid) {
        snapshot.gpu_memory_bytes = gauge(snapshot, Gauge::gpu_memory);
        snapshot.gpu_memory_bytes.unit = Unit::bytes;
    }
}
void collect_gpu_readings(Snapshot &snapshot, const Device &device, Clock::time_point now,
                          const DiskCounterRead &read_engines,
                          const DiskCounterRead &read_dedicated,
                          const DiskCounterRead &read_shared) {
    GpuSamples cache;
    collect_gpu_impl(snapshot, device, now, read_engines, read_dedicated, read_shared, cache);
}
void Collector::gpu(Snapshot &snapshot, Clock::time_point now) {
    if (!gpu_visible_) {
        snapshot.gpu_id = gpu_ ? gpu_->id : L"";
        snapshot.gpu_label = gpu_ ? gpu_->label : L"GPU";
        if (gpu_) {
            snapshot.gpu_memory_label = gpu_->integrated ? L"GPU shared" : L"GPU VRAM";
            snapshot.gpu_memory_capacity =
                gpu_->integrated ? gpu_->shared_capacity : gpu_->capacity;
        }
        for (auto kind : {Gauge::gpu_3d, Gauge::gpu_decode, Gauge::gpu_memory}) {
            gauge(snapshot, kind) = unavailable(now, gauge_unit(kind), L"Hidden — sampling paused");
        }
        return;
    }
    if (gpu_) {
        collect_gpu_impl(
            snapshot, *gpu_, now, [&] { return gpu_engines_.sample(now); },
            [&] { return gpu_memory_.sample(now); }, [&] { return gpu_shared_.sample(now); },
            gpu_samples_);
    } else {
        for (auto kind : {Gauge::gpu_3d, Gauge::gpu_decode, Gauge::gpu_memory}) {
            gauge(snapshot, kind) = unavailable(
                now, gauge_unit(kind), L"Select an available, unambiguous device in Settings");
        }
    }
}

Snapshot Collector::sample(Clock::time_point now) {
    Snapshot snapshot;
    snapshot.timestamp = now;
    const auto isolate = [&](auto &&collect, std::initializer_list<Gauge> affected) {
        try {
            collect();
        } catch (...) {
            for (auto item : affected) {
                gauge(snapshot, item) = failed(now, gauge_unit(item),
                                               {L"Provider exception", ERROR_UNHANDLED_EXCEPTION});
            }
        }
    };
    try {
        cpu(snapshot, now);
    } catch (...) {
        snapshot.processors = cpu_samples_.topology();
        for (auto &processor : snapshot.processors) {
            auto identity = std::move(processor.utilization.identity);
            processor.utilization =
                failed(now, Unit::percent, {L"CPU provider exception", ERROR_UNHANDLED_EXCEPTION});
            processor.utilization.identity = std::move(identity);
        }
    }
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        set_physical_memory(snapshot, memory.ullTotalPhys, memory.ullAvailPhys, now);
    } else {
        const auto error = GetLastError();
        gauge(snapshot, Gauge::ram) = failed(now, Unit::percent, {L"GlobalMemoryStatusEx", error});
        snapshot.ram_used_bytes = gauge(snapshot, Gauge::ram);
        snapshot.ram_used_bytes.unit = Unit::bytes;
    }
    snapshot.ram_used_bytes.identity = L"physical-memory";
    disk(snapshot, now);
    isolate([&] { network(snapshot, now); }, {Gauge::download, Gauge::upload});
    isolate([&] { gpu(snapshot, now); }, {Gauge::gpu_3d, Gauge::gpu_decode, Gauge::gpu_memory});
    snapshot.gpu_memory_bytes.identity = snapshot.gpu_id;
    const auto &memory_status = gauge(snapshot, Gauge::gpu_memory);
    if (memory_status.status != Status::valid) {
        snapshot.gpu_memory_bytes = memory_status;
        snapshot.gpu_memory_bytes.unit = Unit::bytes;
        snapshot.gpu_memory_bytes.identity = snapshot.gpu_id;
    }
    for (std::size_t index = 0; index < kGaugeCount; ++index) {
        snapshot.gauges[index].identity = index == 0  ? L"physical-memory"
                                          : index < 4 ? snapshot.gpu_id
                                                      : snapshot.network_id;
    }
    peaks_.observe(snapshot);
    temperatures_.sample(snapshot, now);
    return snapshot;
}
SamplingWorker::SamplingWorker(HWND target, UINT message, Latest<Delivery> &destination,
                               SessionPeaks &peaks, DisplayContinuity &continuity,
                               DiskInventory &disks, CpuSamples &processors, Settings settings,
                               std::uint64_t generation, std::uint64_t worker_id,
                               bool initially_paused)
    : target_(target), message_(message), destination_(destination), peaks_(peaks),
      continuity_(continuity), known_disks_(disks), cpu_samples_(processors),
      settings_(std::move(settings)), paused_(initially_paused), generation_(generation),
      worker_id_(worker_id), thread_([this](const std::stop_token &token) { run(token); }) {}
SamplingWorker::~SamplingWorker() {
    stop();
}
void SamplingWorker::configure(Settings settings, bool paused, bool reset) {
    {
        std::lock_guard lock(mutex_);
        // Preserve family resets even if several UI changes coalesce before collection.
        if (settings.gpu_id != settings_.gpu_id || settings.gpu_visible != settings_.gpu_visible) {
            ++gpu_generation_;
        }
        if (settings.network_id != settings_.network_id ||
            settings.network_visible != settings_.network_visible) {
            ++network_generation_;
        }
        if (settings.show_temperatures != settings_.show_temperatures) {
            ++temperature_generation_;
        }
        settings_ = std::move(settings);
        paused_ = paused;
        if (reset) {
            ++reset_generation_;
        }
        ++generation_;
    }
    changed_.notify_all();
}
void SamplingWorker::stop() {
    if (thread_.joinable()) {
        thread_.request_stop();
        changed_.notify_all();
        thread_.join();
    }
}
void SamplingWorker::run(const std::stop_token &token) noexcept {
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    try {
        Collector collector(peaks_, known_disks_, cpu_samples_);
        std::uint64_t applied{}, applied_reset{}, applied_gpu{}, applied_network{},
            applied_temperature{};
        std::shared_ptr<const Catalog> catalog;
        auto next_discovery = Clock::time_point{};
        Settings settings;
        std::uint64_t generation{}, reset_generation{}, gpu_generation{}, network_generation{};
        std::uint64_t temperature_generation{};
        bool copied_settings{};
        while (!token.stop_requested()) {
            {
                std::unique_lock lock(mutex_);
                changed_.wait(lock, token, [&] { return !paused_; });
                if (token.stop_requested()) {
                    break;
                }
                if (!copied_settings || generation != generation_) {
                    settings = settings_;
                    generation = generation_;
                    reset_generation = reset_generation_;
                    gpu_generation = gpu_generation_;
                    network_generation = network_generation_;
                    temperature_generation = temperature_generation_;
                    copied_settings = true;
                }
            }
            const auto begin = Clock::now();
            if (applied != generation || begin >= next_discovery) {
                collector.configure(settings, applied_reset != reset_generation,
                                    applied_gpu != gpu_generation,
                                    applied_network != network_generation,
                                    applied_temperature != temperature_generation);
                applied_gpu = gpu_generation;
                applied_network = network_generation;
                applied_reset = reset_generation;
                applied_temperature = temperature_generation;
                applied = generation;
                next_discovery = begin + std::chrono::seconds(30);
                if (!catalog || !same_catalog(*catalog, collector.catalog())) {
                    catalog = std::make_shared<const Catalog>(collector.catalog());
                }
            }
            auto snapshot = collector.sample(Clock::now());
            continuity_.observe(snapshot, settings);
            snapshot.generation = generation;
            if (token.stop_requested()) {
                break;
            }
            destination_.publish(Delivery{std::move(snapshot), catalog, false, true, worker_id_},
                                 [&] { return PostMessageW(target_, message_, 0, 0) != FALSE; });
            const auto finished = Clock::now();
            const auto period = std::chrono::milliseconds(settings.interval_ms);
            const auto deadline = begin + period > finished ? begin + period : finished + period;
            std::unique_lock lock(mutex_);
            changed_.wait_until(lock, token, deadline, [&] { return generation_ != generation; });
        }
    } catch (...) {
        try {
            std::uint64_t generation{};
            {
                std::lock_guard lock(mutex_);
                generation = generation_;
            }
            publish_worker_failure(destination_, generation, worker_id_,
                                   [&] { return PostMessageW(target_, message_, 0, 0) != FALSE; });
        } catch (...) {
            PostMessageW(target_, WM_APP + 20, static_cast<WPARAM>(worker_id_), 0);
        }
    }
    if (SUCCEEDED(initialized)) {
        CoUninitialize();
    }
}
} // namespace loadbar
