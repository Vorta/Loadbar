#pragma once

#include "model/model.hpp"
#include "model/settings.hpp"

#include <array>
#include <compare>
#include <mutex>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace loadbar {
struct ProcessorId {
    unsigned group{}, number{};
    auto operator<=>(const ProcessorId &) const = default;
};
struct Processor {
    ProcessorId id;
    unsigned core{};
    bool mapped{};
    Metric utilization;
    std::optional<unsigned> efficiency_class;
};
enum class DeviceKind : std::uint8_t { disk, gpu, network };
struct Device {
    DeviceKind kind{};
    std::wstring id;
    std::wstring label;
    std::uint64_t runtime_id{};
    std::uint64_t capacity{};
    std::uint64_t shared_capacity{};
    bool integrated{};
    bool memory_scope_known{};
    bool preferred{};
    bool operator==(const Device &) const = default;
};
struct Catalog {
    std::vector<Processor> processors;
    std::vector<Device> disks;
    std::vector<Device> gpus;
    std::vector<Device> networks;
    std::wstring detail;
};
struct CounterItem {
    std::wstring name;
    Metric metric;
};
[[nodiscard]] Metric memory_percentage(const std::vector<CounterItem> &items, const Device &device,
                                       std::uint64_t capacity, Clock::time_point now,
                                       Metric *used_bytes = nullptr);
enum class Gauge : std::uint8_t {
    ram,
    gpu_3d,
    gpu_decode,
    gpu_memory,
    download,
    upload,
    disk_read,
    disk_write,
    disk_active,
    count
};
// Fixed families live in gauges; each physical disk owns its metrics below.
constexpr std::size_t kGaugeCount = static_cast<std::size_t>(Gauge::disk_read);
struct DiskReading {
    std::wstring id, label;
    Metric read{0, Status::unavailable, Unit::bytes_per_second, {}, {}, L"Disk counter absent"};
    Metric write{read};
    Metric active{0, Status::unavailable, Unit::percent, {}, {}, L"Disk idle counter absent"};
};
[[nodiscard]] std::size_t disk_widget_count(std::span<const DiskReading> disks,
                                            const Settings &settings);
[[nodiscard]] Metric disk_counter(const std::vector<CounterItem> &items, const Device &disk,
                                  Clock::time_point now, Unit unit = Unit::bytes_per_second);
[[nodiscard]] Metric disk_active_time(Metric idle);
struct Snapshot {
    Clock::time_point timestamp{};
    std::vector<Processor> processors;
    std::array<Metric, kGaugeCount> gauges{};
    std::vector<DiskReading> disks;
    // Also exposes a failed cold-start scan when no physical disk identity is known yet.
    Metric disk_discovery{0,  Status::unavailable,           Unit::percent, {},
                          {}, L"No physical disk discovered"};
    std::wstring gpu_label{L"Select GPU"};
    std::wstring network_label{L"Select network"};
    std::wstring gpu_id, network_id;
    std::wstring gpu_memory_label{L"GPU memory"};
    std::uint64_t generation{};
    Metric gpu_memory_bytes{0, Status::unavailable, Unit::bytes, {}, {}, {}};
    std::uint64_t gpu_memory_capacity{};
    Metric ram_used_bytes{0, Status::unavailable, Unit::bytes, {}, {}, {}};
    std::uint64_t ram_total_bytes{};
};
void set_physical_memory(Snapshot &snapshot, std::uint64_t total, std::uint64_t available,
                         Clock::time_point now);
[[nodiscard]] const wchar_t *gauge_name(Gauge gauge) noexcept;
[[nodiscard]] constexpr bool is_rate_gauge(Gauge gauge) noexcept {
    return gauge == Gauge::disk_read || gauge == Gauge::disk_write || gauge == Gauge::download ||
           gauge == Gauge::upload;
}
[[nodiscard]] constexpr Unit gauge_unit(Gauge gauge) noexcept {
    return is_rate_gauge(gauge) ? Unit::bytes_per_second : Unit::percent;
}
[[nodiscard]] constexpr bool gauge_visible(Gauge gauge, const Settings &settings) noexcept {
    if (gauge >= Gauge::gpu_3d && gauge <= Gauge::gpu_memory) {
        return settings.gpu_visible;
    }
    if (gauge == Gauge::download || gauge == Gauge::upload) {
        return settings.network_visible;
    }
    return true;
}
[[nodiscard]] MetricView aged_metric(MetricView metric, Clock::time_point now,
                                     unsigned interval_ms) noexcept;
// The exact next status transition; absent once no valid observations remain.
[[nodiscard]] std::optional<Clock::time_point>
next_stale_deadline(const Snapshot &snapshot, unsigned interval_ms,
                    const Settings &visibility = {}) noexcept;
// Next rounded-seconds retained-age text change. Only needed while details are visible.
[[nodiscard]] std::optional<Clock::time_point>
next_retention_deadline(const Snapshot &snapshot, Clock::time_point now, unsigned interval_ms,
                        const Settings &visibility = {}) noexcept;
[[nodiscard]] bool expire_snapshot(Snapshot &snapshot, Clock::time_point now, unsigned interval_ms,
                                   const Settings &visibility = {});
void set_snapshot_status(Snapshot &snapshot, Status status, std::wstring_view detail);
[[nodiscard]] std::optional<ProcessorId> parse_processor(std::wstring_view name);
struct EngineId {
    std::uint64_t luid{};
    unsigned physical{}, engine{}, process{};
    std::wstring type;
    auto operator<=>(const EngineId &) const = default;
};
struct EngineSample {
    EngineId id;
    Metric metric;
};
[[nodiscard]] std::optional<EngineId> parse_engine(std::wstring_view name);
[[nodiscard]] std::optional<std::pair<std::uint64_t, unsigned>>
parse_adapter(std::wstring_view name);
[[nodiscard]] Metric aggregate_engines(const std::vector<EngineSample> &samples, std::uint64_t luid,
                                       std::wstring_view type, Clock::time_point now);
// Worker-owned scratch: no owning engine/string copies and no borrowed samples after a call.
class EngineAggregation {
  public:
    [[nodiscard]] Metric aggregate(const std::vector<EngineSample> &samples, std::uint64_t luid,
                                   std::wstring_view type, Clock::time_point now);
    void release() noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept {
        return selected_.capacity();
    }

  private:
    std::vector<const EngineSample *> selected_;
};

// Notification generation and slot mutation share the same mutex. The UI closes publication
// before destroying its HWND; the worker never retains a naked notification target.
template <class T> class Latest {
  public:
    template <class Notify> void publish(T value, Notify &&notify) {
        std::lock_guard lock(mutex_);
        if (closed_) {
            return;
        }
        value_ = std::move(value);
        if (!pending_) {
            pending_ = notify();
        }
    }
    std::optional<T> take() {
        std::lock_guard lock(mutex_);
        pending_ = false;
        return std::exchange(value_, std::nullopt);
    }
    // Failure publication marks the newest pending observation instead of replacing it
    // with an empty snapshot. The mutation must not call providers, graphics or Shell APIs.
    template <class Mutate, class Notify> bool update_pending(Mutate &&mutate, Notify &&notify) {
        std::lock_guard lock(mutex_);
        if (closed_) {
            return true;
        }
        if (!value_) {
            return false;
        }
        mutate(*value_);
        if (!pending_) {
            pending_ = notify();
        }
        return true;
    }
    void close() {
        std::lock_guard lock(mutex_);
        closed_ = true;
        value_.reset();
        pending_ = false;
    }

  private:
    std::mutex mutex_;
    std::optional<T> value_;
    bool pending_{}, closed_{};
};
} // namespace loadbar
