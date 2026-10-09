#pragma once

#include "model/continuity.hpp"
#include "model/session_peaks.hpp"
#include "telemetry/discovery.hpp"
#include "telemetry/network.hpp"
#include "telemetry/pdh.hpp"
#include "telemetry/temperature.hpp"

#include <condition_variable>
#include <functional>
#include <map>
#include <span>
#include <thread>

namespace loadbar {
using DiskCounterRead = std::function<Result<std::vector<std::vector<CounterItem>>>()>;
// Narrow collection seam shared by the worker and deterministic provider-failure tests.
void collect_disk_readings(Snapshot &snapshot, const std::vector<Device> &devices,
                           Clock::time_point now, const DiskCounterRead &read_io,
                           const DiskCounterRead &read_idle, const Error *discovery_error = nullptr,
                           const HiddenDiskIds &hidden_disks = {});
void collect_gpu_readings(Snapshot &snapshot, const Device &device, Clock::time_point now,
                          const DiskCounterRead &read_engines,
                          const DiskCounterRead &read_dedicated,
                          const DiskCounterRead &read_shared);
// Discovery equality ignores sampled utilization: Catalog is immutable metadata.
[[nodiscard]] bool same_catalog(const Catalog &left, const Catalog &right);
class CpuSamples {
  public:
    void configure(const std::vector<Processor> &topology);
    void apply(std::vector<Processor> &output, const std::vector<CounterItem> &items,
               Clock::time_point now);
    [[nodiscard]] const std::vector<Processor> &topology() const noexcept {
        return topology_;
    }
    [[nodiscard]] const std::vector<Processor> &discovered() const noexcept {
        return discovered_;
    }

  private:
    void reindex();
    std::vector<Processor> discovered_, topology_;
    std::map<ProcessorId, std::size_t> indices_;
    std::vector<bool> seen_;
};
// Parsed names are immutable metadata, pruned on every successful enumeration.
// Large/transient process lists beyond the cache bound are still interpreted uncached.
class GpuSamples {
  public:
    static constexpr std::size_t kMaximumCachedNames = 8192;
    const std::vector<EngineSample> &read(const std::vector<CounterItem> &items,
                                          std::uint64_t selected_luid);
    [[nodiscard]] std::size_t cached_names() const noexcept {
        return identities_.size();
    }
    void deactivate();
    [[nodiscard]] Metric aggregate(std::uint64_t luid, std::wstring_view type,
                                   Clock::time_point now) {
        return aggregation_.aggregate(samples_, luid, type, now);
    }

  private:
    struct Entry {
        std::optional<EngineId> identity;
        bool seen{};
    };
    std::map<std::wstring, Entry, std::less<>> identities_;
    std::vector<EngineSample> samples_;
    EngineAggregation aggregation_;
    friend struct CollectorDiscoveryTests;
};
class CounterSource {
  public:
    CounterSource(std::vector<std::wstring> paths, Unit unit);
    // Borrowed until the next sample/reset of this source; callers copy readings into snapshots.
    Result<std::span<const std::vector<CounterItem>>> sample(Clock::time_point now);
    void reset();
    void deactivate();

  private:
    friend struct CollectorDiscoveryTests;
    std::vector<std::wstring> paths_;
    Unit unit_;
    PdhQuery query_;
    std::vector<std::vector<CounterItem>> arrays_;
    bool open_{};
    Clock::time_point retry_{};
    unsigned delay_{1};
    Error last_error_;
};
class Collector {
  public:
    using Discover = std::function<Discovery()>;
    Collector(SessionPeaks &peaks, DiskInventory &disks, CpuSamples &processors,
              Discover discover = {});
    void configure(const Settings &settings, bool reset = true, bool reset_gpu = false,
                   bool reset_network = false, bool reset_temperatures = false);
    Snapshot sample(Clock::time_point now);
    const Catalog &catalog() const noexcept {
        return catalog_;
    }

  private:
    friend struct CollectorDiscoveryTests;
    SessionPeaks &peaks_;
    DiskInventory &known_disks_;
    Discover discover_;
    std::optional<Error> disk_discovery_error_;
    Catalog catalog_;
    CpuSamples &cpu_samples_;
    GpuSamples gpu_samples_;
    std::optional<Device> gpu_, network_;
    bool gpu_visible_{true}, network_visible_{true}, disk_sampling_{};
    HiddenDiskIds hidden_disks_;
    CounterSource cpu_, disk_io_, disk_idle_, gpu_engines_, gpu_memory_, gpu_shared_;
    NetworkProvider network_provider_;
    TemperatureProviders temperatures_;
    void cpu(Snapshot &snapshot, Clock::time_point now);
    void disk(Snapshot &snapshot, Clock::time_point now);
    void network(Snapshot &snapshot, Clock::time_point now);
    void gpu(Snapshot &snapshot, Clock::time_point now);
};
struct Delivery {
    Snapshot snapshot;
    // Discovery metadata is immutable and shared by the worker, latest slot and UI.
    // Every normal delivery retains it, including when an earlier delivery is coalesced away.
    std::shared_ptr<const Catalog> catalog;
    bool worker_failed{};
    bool has_snapshot{true};
    std::uint64_t worker_id{};
};
[[nodiscard]] inline bool accept_delivery(const Delivery &value, std::uint64_t worker_id,
                                          std::uint64_t generation) noexcept {
    return value.worker_id == worker_id &&
           (value.worker_failed || value.snapshot.generation >= generation);
}
template <class Notify>
void publish_worker_failure(Latest<Delivery> &destination, std::uint64_t generation,
                            std::uint64_t worker_id, Notify &&notify) {
    if (!destination.update_pending(
            [&](Delivery &value) {
                value.worker_failed = true;
                value.worker_id = worker_id;
            },
            notify)) {
        Delivery failure;
        failure.worker_failed = true;
        failure.has_snapshot = false;
        failure.snapshot.generation = generation;
        failure.worker_id = worker_id;
        destination.publish(std::move(failure), notify);
    }
}
class SamplingWorker {
  public:
    SamplingWorker(HWND target, UINT message, Latest<Delivery> &destination, SessionPeaks &peaks,
                   DisplayContinuity &continuity, DiskInventory &disks, CpuSamples &processors,
                   Settings settings, std::uint64_t generation = 1, std::uint64_t worker_id = 1,
                   bool initially_paused = false);
    ~SamplingWorker();
    SamplingWorker(const SamplingWorker &) = delete;
    SamplingWorker &operator=(const SamplingWorker &) = delete;
    void configure(Settings settings, bool paused, bool reset = true);
    void stop();

  private:
    friend struct CollectorDiscoveryTests;
    void run(const std::stop_token &token) noexcept;
    HWND target_;
    UINT message_;
    Latest<Delivery> &destination_;
    SessionPeaks &peaks_;
    DisplayContinuity &continuity_;
    DiskInventory &known_disks_;
    CpuSamples &cpu_samples_;
    std::mutex mutex_;
    std::condition_variable_any changed_;
    Settings settings_;
    bool paused_{};
    std::uint64_t generation_{1}, reset_generation_{1}, gpu_generation_{1}, network_generation_{1};
    std::uint64_t temperature_generation_{1};
    const std::uint64_t worker_id_;
    std::jthread thread_;
};
} // namespace loadbar
