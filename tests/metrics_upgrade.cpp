#include "model/layout.hpp"
#include "model/presentation.hpp"
#include "model/session_peaks.hpp"
#include "telemetry/collector.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <pdhmsg.h>
#include <stdexcept>
#include <tuple>

namespace {
using namespace loadbar;
using namespace std::chrono_literals;
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
Metric rate(double value, std::wstring id, int second, Status status = Status::valid) {
    return {
        value, status, Unit::bytes_per_second, Clock::time_point{} + std::chrono::seconds(second),
        1s,    {},     std::move(id)};
}
void tooltip_format_tests() {
    constexpr double mb = 1048576.0, gb = 1073741824.0;
    for (const auto unit : {Unit::bytes, Unit::bytes_per_second}) {
        const std::wstring suffix = unit == Unit::bytes ? L"" : L"/s";
        for (const auto &[value, expected] : {std::pair{0.0, L"0.0 MB"},
                                              {-0.0, L"0.0 MB"},
                                              {1.0, L"<0.1 MB"},
                                              {0.099 * mb, L"<0.1 MB"},
                                              {0.1 * mb, L"0.1 MB"},
                                              {512 * mb, L"512.0 MB"},
                                              {1023.9 * mb, L"1023.9 MB"},
                                              {gb, L"1.0 GB"},
                                              {8 * gb, L"8.0 GB"},
                                              {2048 * gb, L"2048.0 GB"}}) {
            Metric m{value, Status::valid, unit, {}, {}, {}};
            require(metric_text(m, ByteFormat::tooltip) == expected + suffix,
                    "Tooltip uses compact binary MB/GB without hiding small positive values");
        }
        for (const auto value : {-1.0, std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()}) {
            Metric m{value, Status::valid, unit, {}, {}, {}};
            require(metric_text(m, ByteFormat::tooltip) == L"Error",
                    "Invalid tooltip quantity is never shown as a valid zero");
        }
        Metric m{0, Status::warming_up, unit, {}, {}, {}};
        require(metric_text(m, ByteFormat::tooltip).starts_with(L"0.0 MB" + suffix),
                "Initial warmup uses compact zero with raw status retained");
        for (const auto status : {Status::unavailable, Status::error, Status::stale}) {
            m.status = status;
            require(metric_text(m, ByteFormat::tooltip) == status_text(status),
                    "Never-observed failures keep their status");
            m.retained = ObservedValue{512 * mb, Clock::now() - 10s};
            const auto text = metric_text(m, ByteFormat::tooltip);
            require(text.starts_with(L"512.0 MB" + suffix) &&
                        text.find(status_text(status)) != std::wstring::npos &&
                        text.find(L"Last valid reading;") != std::wstring::npos,
                    "Retained tooltip quantity preserves status and observation age");
            m.retained.reset();
        }
    }
    auto m = rate(mb, L"test", 1);
    require(metric_text(m) == L"1.0 MiB/s" && compact_value(m) == L"1.0 MiB/s",
            "Non-tooltip rate formatting is unchanged");
    m.session_peak = 8 * gb;
    require(peak_text(m, ByteFormat::tooltip) == L"Linear scale; peak since launch 8.0 GB/s",
            "Session peaks use the same compact rate convention");
    m.session_peak = 1;
    require(peak_text(m, ByteFormat::tooltip).ends_with(L"<0.1 MB/s"), "Tiny peak stays positive");
    m.session_peak = 0;
    require(peak_text(m, ByteFormat::tooltip).ends_with(L"0.0 MB/s"), "Zero peak stays zero");
    m.session_peak.reset();
    require(peak_text(m, ByteFormat::tooltip).ends_with(L"unavailable"),
            "Missing peak stays absent");

    Snapshot s;
    s.gpu_label = L"Fixture GPU";
    s.gpu_memory_label = L"Shared memory";
    s.gpu_memory_capacity = static_cast<std::uint64_t>(8 * gb);
    s.gpu_memory_bytes = {512 * mb, Status::valid, Unit::bytes, {}, {}, {}};
    s.gauges[3] = {6.25, Status::valid, Unit::percent, {}, {}, {}};
    s.network_label = L"Fixture NIC";
    s.gauges[4] = s.gauges[5] = rate(mb, L"nic", 0);
    s.gauges[4].session_peak = s.gauges[5].session_peak = 8 * gb;
    s.disks.push_back({L"disk", L"Fixture disk", s.gauges[4], s.gauges[5]});
    const auto layout = make_snapshot_layout(1920, 60, Edge::top, s, 1, {});
    for (const auto &block : layout.blocks) {
        if (block.kind < 2) {
            continue;
        }
        const auto tip = metric_tooltip(layout, block.bounds.x + 1, block.bounds.y + 1, s, {}, {});
        require(tip.find(L"1024-based") != std::wstring::npos &&
                    tip.find(L"bytes") == std::wstring::npos,
                "Every byte-based device tooltip explains its compact units");
        if (block.kind == 2) {
            require(tip.find(L"Shared memory") != std::wstring::npos &&
                        tip.find(L"512.0 MB / 8.0 GB") != std::wstring::npos,
                    "GPU tooltip preserves selected memory scope and matching capacity");
        } else {
            require(tip.find(L"1.0 MB/s") != std::wstring::npos &&
                        tip.find(L"peak since launch 8.0 GB/s") != std::wstring::npos,
                    "Disk/network tooltips compact both current rates and peaks");
        }
    }
}
void memory_tests() {
    Snapshot snapshot;
    constexpr auto gib = 1073741824ULL;
    const auto total = static_cast<std::uint64_t>(63.4 * gib);
    const auto used = 47 * gib;
    set_physical_memory(snapshot, total, total - used, {});
    require(ram_summary(snapshot, {}, 1000) == L"47.0/63.4GB 74%",
            "RAM capacity and percentage agree");
    require(snapshot.gauges[0].identity == snapshot.ram_used_bytes.identity &&
                snapshot.gauges[0].timestamp == snapshot.ram_used_bytes.timestamp &&
                snapshot.ram_used_bytes.unit == Unit::bytes,
            "RAM uses one observation and identity");
    auto changed = snapshot;
    changed.ram_total_bytes *= 2;
    changed.ram_used_bytes.value *= 2;
    require(ram_changed(snapshot, changed, {}, 1000),
            "Capacity change repaints even at equal percent");
    require(!ram_changed(snapshot, snapshot, {}, 1000), "Identical RAM does not repaint");
    const auto layout = make_layout(1920, 40, loadbar::Edge::top, {}, 1);
    const auto box = layout.blocks[1].bounds;
    const auto tip = metric_tooltip(layout, box.x + 1, box.y + 1, snapshot, {}, {});
    require(tip.find(L"47.0 GB / 63.4 GB") != std::wstring::npos &&
                tip.find(L"1024-based") != std::wstring::npos &&
                tip.find(L"bytes") == std::wstring::npos,
            "RAM tooltip states binary GB");
    require(expire_snapshot(snapshot, Clock::time_point{} + 4s, 1000) &&
                snapshot.gauges[0].status == Status::stale &&
                snapshot.ram_used_bytes.status == Status::stale &&
                ram_summary(snapshot, Clock::time_point{} + 4s, 1000) == L"—",
            "Both RAM representations stale together");
    for (const auto status : {Status::warming_up, Status::unavailable, Status::error}) {
        set_snapshot_status(changed, status, L"Transition");
        require(changed.ram_used_bytes.status == status &&
                    ram_summary(changed, {}, 1000) ==
                        (status == Status::warming_up ? L"0.0/126.8GB 0%" : L"—"),
                "Unprimed warm-up displays zero; unobserved errors stay unavailable");
    }
    for (auto available : {0ULL, total, total + 1}) {
        set_physical_memory(snapshot, total, available, {});
        require(snapshot.gauges[0].status == (available <= total ? Status::valid : Status::error),
                "RAM availability bounds");
    }
    set_physical_memory(snapshot, 0, 0, {});
    require(snapshot.gauges[0].status == Status::error && snapshot.ram_total_bytes == 0 &&
                ram_summary(snapshot, {}, 1000) == L"—",
            "Zero RAM capacity is not healthy zero");
    set_physical_memory(snapshot, 16 * gib, 8 * gib, {});
    require(ram_summary(snapshot, {}, 1000) == L"8.0/16.0GB 50%", "RAM exact binary capacity");
}
void disk_tests() {
    const auto now = Clock::time_point{} + 5s;
    for (double idle : {0.0, 50.0, 100.0}) {
        auto raw =
            interpret_counter(idle, PDH_CSTATUS_VALID_DATA, false, now, 1500ms, Unit::percent);
        raw.identity = L"disk-a";
        const auto active = disk_active_time(raw);
        require(active.status == Status::valid && active.value == 100 - idle &&
                    active.interval == 1500ms && active.identity == raw.identity &&
                    graphic_fraction(Gauge::disk_active, active) == (100 - idle) / 100,
                "Disk active is linear 100-idle, not a rate");
    }
    for (double idle : {-1.0, 100.01, std::numeric_limits<double>::infinity(),
                        std::numeric_limits<double>::quiet_NaN()}) {
        const auto active = disk_active_time({idle, Status::valid, Unit::percent, now, 1s, {}});
        require(active.status == Status::error && !active.detail.empty(),
                "Invalid idle is an error, not clamped to healthy");
    }
    require(disk_active_time(
                interpret_counter(0, PDH_CSTATUS_INVALID_DATA, true, now, 1s, Unit::percent))
                    .status == Status::warming_up,
            "Idle primes before interpreting zero");
    const auto absent = disk_active_time(
        interpret_counter(0, PDH_CSTATUS_NO_INSTANCE, false, now, 1s, Unit::percent));
    require(absent.status == Status::unavailable && absent.detail.find(L"0x") != std::wstring::npos,
            "Native idle failure retains status and code");
    const std::vector<Device> devices{{DeviceKind::disk, L"disk-a", L"Disk 0", 0},
                                      {DeviceKind::disk, L"disk-b", L"Disk 1", 1}};
    const auto reading = rate(100, L"", 5);
    const Metric idle{20, Status::valid, Unit::percent, now, 1s, {}};
    const DiskCounterRead io = [&] {
        return std::vector<std::vector<CounterItem>>{
            {{L"1 D:", reading}, {L"_Total", reading}, {L"0 C:", reading}},
            {{L"0 C:", reading}, {L"1 D:", reading}}};
    };
    const DiskCounterRead idle_source = [&] {
        return std::vector<std::vector<CounterItem>>{
            {{L"1 D:", idle}, {L"10 E:", idle}, {L"0 C:", idle}, {L"_Total", idle}}};
    };
    const DiskCounterRead failed = []() -> Result<std::vector<std::vector<CounterItem>>> {
        return std::unexpected(Error{L"Injected unavailable query", ERROR_NOT_SUPPORTED});
    };
    const DiskCounterRead throws = []() -> Result<std::vector<std::vector<CounterItem>>> {
        throw std::runtime_error("Injected provider exception");
    };
    Snapshot snapshot;
    collect_disk_readings(snapshot, devices, now, io, idle_source);
    require(snapshot.disks.size() == 2 && snapshot.disks[1].active.identity == L"disk-b" &&
                snapshot.disks[0].active.value == 80,
            "Active matches reordered exact physical identities");
    for (const auto &failure : {failed, throws}) {
        collect_disk_readings(snapshot, devices, now, io, failure);
        require(snapshot.disks[0].active.status == Status::error &&
                    snapshot.disks[1].read.status == Status::valid &&
                    snapshot.disks[1].write.status == Status::valid,
                "Idle query failure/exception leaves both disk rates valid");
        collect_disk_readings(snapshot, devices, now, failure, idle_source);
        require(snapshot.disks[1].active.status == Status::valid &&
                    snapshot.disks[0].read.status == Status::error,
                "I/O query failure/exception leaves disk active valid");
    }
    collect_disk_readings(snapshot, devices, now, io, [&] {
        return std::vector<std::vector<CounterItem>>{{{L"1 D:", idle}, {L"1 D:", idle}}};
    });
    require(snapshot.disks[0].active.status == Status::unavailable &&
                snapshot.disks[1].active.status == Status::error &&
                snapshot.disks[0].read.status == Status::valid,
            "Absent and duplicate idle rows remain isolated");
    collect_disk_readings(snapshot, devices, now, io, idle_source);
    require(expire_snapshot(snapshot, now + 4s, 1000) &&
                snapshot.disks[1].active.status == Status::stale,
            "Active ages with other disk metrics");
    set_snapshot_status(snapshot, Status::warming_up, L"Resume");
    require(snapshot.disks[0].active.status == Status::warming_up &&
                snapshot.disks[0].active.unit == Unit::percent,
            "Active resets after resume without losing its unit");
}
void peak_tests() {
    SessionPeaks peaks;
    auto m = rate(0, L"disk-a", 1);
    peaks.observe(Gauge::disk_read, m);
    require(m.session_peak == 0 && graphic_fraction(Gauge::disk_read, m) == 0,
            "Observed idle peak is zero and empty");
    for (const auto &[time, value, peak, fraction] : {std::tuple{2, 100.0, 100.0, 1.0},
                                                      {3, 50.0, 100.0, 0.5},
                                                      {4, 200.0, 200.0, 1.0},
                                                      {5, 0.0, 200.0, 0.0},
                                                      {6, 1.0, 200.0, 0.005}}) {
        m = rate(value, L"disk-a", time);
        peaks.observe(Gauge::disk_read, m);
        require(m.session_peak == peak && graphic_fraction(Gauge::disk_read, m) == fraction,
                "Peak rises, never shrinks, with no rate floor");
    }
    for (auto direction : {Gauge::disk_write, Gauge::download, Gauge::upload}) {
        m = rate(10, L"disk-a", 1);
        peaks.observe(direction, m);
        require(m.session_peak == 10, "Each direction has an independent peak");
    }
    m = rate(20, L"disk-b", 1);
    peaks.observe(Gauge::disk_read, m);
    require(m.session_peak == 20, "Each physical device has an independent peak");
    for (auto status : {Status::warming_up, Status::stale, Status::unavailable, Status::error}) {
        m = rate(9999, L"disk-a", 7, status);
        peaks.observe(Gauge::disk_read, m);
        require(m.session_peak == 200 && m.status == status &&
                    graphic_fraction(Gauge::disk_read, m) == 0,
                "Resume/disconnect/error retains peak without learning invalid values");
    }
    for (double value : {-1.0, std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN()}) {
        m = rate(value, L"disk-a", 8);
        peaks.observe(Gauge::disk_read, m);
        require(m.session_peak == 200 && m.status == Status::error,
                "Nonfinite/negative rates cannot poison peaks");
    }
    for (int second : {5, 6}) {
        m = rate(9999, L"disk-a", second);
        peaks.observe(Gauge::disk_read, m);
        require(m.session_peak == 200 && m.status == Status::error,
                "Out-of-order/duplicate samples cannot raise peaks");
    }
    m = rate(9999, L"disk-a", 9);
    m.interval = 0s;
    peaks.observe(Gauge::disk_read, m);
    require(m.session_peak == 200 && m.status == Status::error, "Zero interval cannot raise peak");
    // Production collector replacement borrows application-owned state, never creates a new scale.
    {
        DiskInventory disks;
        CpuSamples processors;
        Collector replacement(peaks, disks, processors);
    }
    m = rate(100, L"disk-a", 10);
    peaks.observe(Gauge::disk_read, m);
    require(m.session_peak == 200, "Returning device/replacement collector keeps lifetime peak");
    auto changed = m;
    changed.session_peak = 400;
    require(metric_changed(m, changed, m.timestamp, 1000) &&
                graphic_fraction(Gauge::disk_read, changed) == 0.25,
            "Equal rate with changed peak invalidates its visual");
    Latest<Snapshot> handoff;
    int notifications{};
    for (int time : {11, 12}) {
        Snapshot s;
        s.disks = {{L"disk-a", L"Test disk", rate(time == 11 ? 1000 : 100, L"disk-a", time),
                    rate(1, L"disk-a", time)}};
        peaks.observe(s);
        handoff.publish(std::move(s), [&] {
            ++notifications;
            return true;
        });
    }
    const auto delivered = handoff.take();
    require(notifications == 1 && delivered && delivered->disks[0].read.value == 100 &&
                delivered->disks[0].read.session_peak == 1000,
            "Coalesced-away snapshot still establishes peak");
    SessionPeaks next_launch;
    m = rate(5, L"disk-a", 13);
    next_launch.observe(Gauge::disk_read, m);
    require(m.session_peak == 5, "New process state starts a new peak");
    for (std::size_t i = 0; i < SessionPeaks::kMaximumStreams; ++i) {
        m = rate(1, L"bounded-" + std::to_wstring(i), 1);
        next_launch.observe(Gauge::upload, m);
    }
    require(m.status == Status::error && !m.session_peak,
            "Bounded peak storage reports exhaustion without evicting old peaks");
}
void migration_tests() {
    for (auto version : {2, 3}) {
        const auto old = std::wstring(L"Loadbar ") + std::to_wstring(version) +
                         (version == 2 ? L" 1 1" : L" 1") +
                         L" 48 750 10000 20000 30000 40000 \"monitor\" " +
                         (version == 2 ? L"\"retired-disk\" " : L"") + L"\"gpu\" \"nic\" 2 1 1 1 1";
        const auto migrated = decode_settings(old);
        require(migrated && migrated->edge == Edge::left && migrated->thickness == 48 &&
                    migrated->alignment == Alignment::end && migrated->gpu_id == L"gpu" &&
                    migrated->network_id == L"nic",
                "Legacy scale retirement retains placement/devices/appearance");
        require(migrated && encode_settings(*migrated).starts_with(L"Loadbar 12 ") &&
                    decode_settings(encode_settings(*migrated)) == migrated,
                "Migrated choices round trip in v11 without persisted peaks");
    }
    for (const auto *malformed :
         {L"Loadbar 4 0 40 1000 \"\" \"\" \"\" 0 2 0 0",
          L"Loadbar 3 0 40 1000 999 2000 3000 4000 \"\" \"\" \"\" 0 1 0 0 0",
          L"Loadbar 4 0 40 1000 \"\" \"\" \"\" 3 0 0 0"}) {
        require(!decode_settings(malformed),
                "Invalid old/new enumeration and scale settings remain rejected");
    }
}
} // namespace
void run_metric_upgrade_tests() {
    tooltip_format_tests();
    memory_tests();
    disk_tests();
    peak_tests();
    migration_tests();
    std::cout << "RAM, disk active-time, session-peak and migration tests passed\n";
}
