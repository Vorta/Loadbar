#include "model/continuity.hpp"
#include "model/geometry.hpp"
#include "model/layout.hpp"
#include "model/presentation.hpp"
#include "telemetry/collector.hpp"

#include <cmath>
#include <format>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace loadbar;
using namespace std::chrono_literals;
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
Snapshot sample(int second = 1) {
    Snapshot result;
    result.timestamp = Clock::time_point{} + std::chrono::seconds(second);
    result.gpu_id = L"gpu-a";
    result.gpu_label = L"Test GPU";
    result.gpu_memory_label = L"GPU VRAM";
    result.network_id = L"nic-a";
    result.network_label = L"Test NIC";
    for (std::size_t i = 0; i < kGaugeCount; ++i) {
        result.gauges[i] = {50,
                            Status::valid,
                            gauge_unit(static_cast<Gauge>(i)),
                            result.timestamp,
                            1s,
                            {},
                            i < 4 ? result.gpu_id : result.network_id};
    }
    set_physical_memory(result, 64ULL * 1073741824, 16ULL * 1073741824, result.timestamp);
    result.gpu_memory_capacity = 8ULL * 1073741824;
    result.gpu_memory_bytes = {
        4.0 * 1073741824, Status::valid, Unit::bytes, result.timestamp, 1s, {}};
    result.processors = {
        {{0, 0}, 0, true, {20, Status::valid, Unit::percent, result.timestamp, 1s, {}}},
        {{0, 1}, 0, true, {80, Status::valid, Unit::percent, result.timestamp, 1s, {}}}};
    Metric rate{100, Status::valid, Unit::bytes_per_second, result.timestamp, 1s, {}, L"disk-a"};
    result.disks = {{L"disk-a",
                     L"Test disk",
                     rate,
                     rate,
                     {35, Status::valid, Unit::percent, result.timestamp, 1s, {}, L"disk-a"}}};
    return result;
}
void reading_tests() {
    DisplayContinuity state;
    auto initial = sample();
    set_snapshot_status(initial, Status::warming_up, L"First observation");
    state.observe(initial);
    require(presented_metric(initial.processors[0].utilization).value == 0 &&
                initial.processors[0].utilization.status == Status::warming_up &&
                !initial.processors[0].utilization.retained &&
                ram_summary(initial, initial.timestamp, 1000) == L"0.0/64.0GB 0%",
            "Initial warm-up shows zero without manufacturing valid raw samples");
    for (auto status : {Status::error, Status::unavailable, Status::stale}) {
        set_snapshot_status(initial, status, L"Never worked");
        state.observe(initial);
        require(presented_metric(initial.gauges[1]).status == status && !initial.gauges[1].retained,
                "Never-valid failed metrics remain unavailable");
    }
    SessionPeaks peaks;
    auto first = sample();
    peaks.observe(first);
    state.observe(first);
    const auto layout = make_layout(1920, 40, loadbar::Edge::top, first.processors, 1, {}, 1);
    require(core_metric(layout.cores[0], first, first.timestamp, 1000).value == 20 &&
                core_metric(layout.cores[1], first, first.timestamp, 1000).value == 80,
            "Logical processors start with their own values");
    for (auto status : {Status::error, Status::unavailable, Status::stale, Status::warming_up}) {
        auto failed = sample(2);
        failed.ram_total_bytes = failed.gpu_memory_capacity = 0;
        failed.processors[1].utilization.value = 9999;
        failed.disks[0].read.value = 1e15;
        set_snapshot_status(failed, status, L"Injected failure");
        peaks.observe(failed);
        state.observe(failed);
        require(failed.processors[1].utilization.status == status &&
                    failed.processors[1].utilization.value == 9999 &&
                    core_metric(layout.cores[0], failed, failed.timestamp, 1000).value == 20 &&
                    core_metric(layout.cores[1], failed, failed.timestamp, 1000).value == 80 &&
                    cpu_summary(failed, {}, failed.timestamp) == L"50%",
                "Repeated failures preserve raw status/value and display prior CPU values");
        require(ram_summary(failed, failed.timestamp, 1000) == L"48.0/64.0GB 75%" &&
                    presented_metric(failed.gauges[3]).value == 50 &&
                    presented_metric(failed.gpu_memory_bytes).value == 4.0 * 1073741824 &&
                    failed.gpu_memory_capacity == 8ULL * 1073741824,
                "Failed memory holds a coherent percent/bytes/capacity tuple");
        require(failed.disks[0].read.session_peak == 100 &&
                    presented_metric(failed.disks[0].read).value == 100 &&
                    graphic_fraction(Gauge::disk_read, failed.disks[0].read) == 1 &&
                    presented_metric(failed.disks[0].active).value == 35 &&
                    presented_metric(failed.gauges[4]).value == 50,
                "Disk and NIC values hold without poisoning independent rate peaks");
        require(retention_text(failed.gauges[0], first.timestamp + 10s).find(L"10 s old") !=
                    std::wstring::npos,
                "Diagnostics use age of the original valid observation");
    }
}
void recovery_tests() {
    DisplayContinuity state;
    auto first = sample();
    state.observe(first);
    const auto layout = make_layout(1920, 40, loadbar::Edge::top, first.processors, 1, {}, 1);
    auto partial = sample(3);
    partial.processors[0].utilization.value = 95;
    partial.processors[1].utilization.status = Status::error;
    state.observe(partial);
    require(core_metric(layout.cores[0], partial, partial.timestamp, 1000).value == 95,
            "A missing sibling does not suppress a fresh sibling");
    partial.processors[0].utilization.value = std::numeric_limits<double>::quiet_NaN();
    state.observe(partial);
    require(partial.processors[0].utilization.status == Status::error &&
                presented_metric(partial.processors[0].utilization).value == 95,
            "Non-finite observations cannot replace the last valid reading");
    auto idle = sample(4);
    idle.processors[0].utilization.value = 0;
    state.observe(idle);
    require(expire_snapshot(idle, idle.timestamp + 4s, 1000) &&
                presented_metric(idle.processors[0].utilization).value == 0 &&
                idle.processors[0].utilization.status == Status::stale,
            "Valid idle zero is retained when a UI-side stale timer fires");
    set_snapshot_status(idle, Status::warming_up, L"Resume");
    require(presented_metric(idle.processors[1].utilization).value == 80,
            "UI-side re-prime keeps observations while providers restart");
    auto wrong_memory = sample(5);
    wrong_memory.ram_used_bytes.value = 1;
    state.observe(wrong_memory);
    require(wrong_memory.gauges[0].status == Status::error &&
                ram_summary(wrong_memory, wrong_memory.timestamp, 1000) == L"48.0/64.0GB 75%",
            "Inconsistent memory cannot mix new capacity and old usage");
    auto new_scope = sample(6);
    new_scope.gpu_memory_label = L"GPU shared";
    set_snapshot_status(new_scope, Status::error, L"New scope absent");
    state.observe(new_scope);
    require(new_scope.gauges[3].retained && new_scope.gpu_memory_label == L"GPU VRAM" &&
                new_scope.gpu_memory_capacity == 8ULL * 1073741824,
            "An unavailable new scope retains the last observation with its original scope");
    auto new_device = sample(7);
    new_device.gpu_id = L"gpu-b";
    new_device.network_id = L"nic-b";
    new_device.disks[0].id = L"disk-b";
    set_snapshot_status(new_device, Status::warming_up, L"New device");
    state.observe(new_device);
    require(!new_device.gauges[1].retained && !new_device.gauges[4].retained &&
                !new_device.disks[0].read.retained,
            "Device changes do not borrow another device's reading");
}
void coalescing_tests() {
    SessionPeaks peaks;
    DisplayContinuity shared_state;
    auto good = sample();
    peaks = SessionPeaks{};
    peaks.observe(good);
    shared_state.observe(good);
    Latest<Snapshot> delivery;
    int posts{};
    delivery.publish(good, [&] {
        ++posts;
        return true;
    });
    auto lost = sample(2);
    lost.gpu_id.clear();
    lost.network_id.clear();
    set_snapshot_status(lost, Status::unavailable, L"Device disappeared");
    // Peaks cannot resolve an absent NIC. Continuity retains both its value and scale.
    lost.gauges[4].session_peak.reset();
    shared_state.observe(lost);
    delivery.publish(lost, [&] {
        ++posts;
        return true;
    });
    const auto received = delivery.take();
    require(received && posts == 1 && received->network_id == L"nic-a" &&
                graphic_fraction(Gauge::download, received->gauges[4]) == 1 &&
                presented_metric(received->gauges[1]).value == 50,
            "Coalesced valid delivery still primes continuity and missing devices retain "
            "identity/scale");
    Settings selected;
    selected.network_id = L"another-nic";
    lost.network_id.clear();
    shared_state.observe(lost, selected);
    require(!lost.gauges[4].retained, "Unresolved explicit selection cannot inherit the old NIC");
    DisplayContinuity next_launch;
    auto cold = sample(9);
    set_snapshot_status(cold, Status::warming_up, L"New process");
    next_launch.observe(cold);
    require(!cold.gauges[1].retained, "Retained samples are not persisted across launches");
}
void gpu_fallback_tests() {
    Device device{DeviceKind::gpu, L"gpu-a", L"Test GPU", 42, 100, 200, false, true};
    DisplayContinuity continuity;
    Settings settings;
    settings.gpu_id = device.id;
    int second{};
    const auto collect = [&](Status dedicated_status, Status shared_status, double dedicated_value,
                             double shared_value, bool fail_engines = false) {
        Snapshot snapshot;
        snapshot.processors.push_back({{0, 0}, 0, true, {}});
        const auto now = Clock::time_point{} + std::chrono::seconds(++second);
        const auto rows = [&](Status status,
                              double value) -> Result<std::vector<std::vector<CounterItem>>> {
            return std::vector<std::vector<CounterItem>>{
                {{L"luid_0x0_0x2a_phys_0", {value, status, Unit::bytes, now, {}, L"test"}}}};
        };
        collect_gpu_readings(
            snapshot, device, now,
            [&]() -> Result<std::vector<std::vector<CounterItem>>> {
                if (fail_engines) {
                    throw std::runtime_error("injected engine failure");
                }
                return std::vector<std::vector<CounterItem>>(1);
            },
            [&] { return rows(dedicated_status, dedicated_value); },
            [&] { return rows(shared_status, shared_value); });
        continuity.observe(snapshot, settings);
        return snapshot;
    };
    auto snapshot = collect(Status::unavailable, Status::unavailable, 0, 0);
    require(!gpu_memory_visible(snapshot), "No memory source omits the never-observed meter");
    for (bool horizontal : {true, false}) {
        const auto layout = make_snapshot_layout(
            horizontal ? 1400.0F : 60.0F, horizontal ? 60.0F : 1400.0F,
            (horizontal ? loadbar::Edge::top : loadbar::Edge::right), snapshot, 1, settings);
        require(layout.fits, "GPU fallback layout fits before accessing blocks");
        const auto &gpu = layout.blocks[2];
        require(layout.fits && gpu.types[0] == Gauge::gpu_3d && gpu.types[1] == Gauge::gpu_decode &&
                    gpu.types[2] == Gauge::count,
                "Absent GPU memory produces the two-engine layout");
        const float major = horizontal ? gpu.meters[0].height : gpu.meters[0].width;
        const float minor = horizontal ? gpu.meters[1].height : gpu.meters[1].width;
        require(std::abs(major / minor - 14.0F / 6) < 0.001F, "Engine-only GPU uses 14/6 split");
    }
    snapshot = collect(Status::unavailable, Status::valid, 0, 100, true);
    require(gpu_memory_visible(snapshot) && snapshot.gpu_memory_label == L"GPU shared" &&
                snapshot.gpu_memory_capacity == 200 && snapshot.gauges[3].value == 50 &&
                snapshot.gauges[1].status == Status::error,
            "Engine query failure does not block shared memory fallback");
    const auto observed = snapshot.gauges[3].timestamp;
    snapshot = collect(Status::error, Status::error, 0, 0);
    const auto &retained = snapshot.gauges[3].retained;
    require(gpu_memory_visible(snapshot) && snapshot.gpu_memory_label == L"GPU shared" &&
                snapshot.gpu_memory_capacity == 200 &&
                presented_metric(snapshot.gauges[3]).value == 50 && retained &&
                retained->timestamp == observed,
            "Dual failure preserves last displayed memory scope/capacity/age");
    snapshot = collect(Status::valid, Status::valid, 25, 100);
    require(snapshot.gpu_memory_label == L"GPU VRAM" && snapshot.gauges[3].value == 25 &&
                snapshot.gpu_memory_capacity == 100,
            "Fresh dedicated memory wins over shared memory");
    snapshot = collect(Status::unavailable, Status::valid, 0, 80);
    require(snapshot.gpu_memory_label == L"GPU shared" && snapshot.gauges[3].value == 40 &&
                snapshot.gpu_memory_bytes.value == 80,
            "Fresh shared memory wins over retained dedicated memory");
    snapshot = collect(Status::valid, Status::valid, 0, 100);
    require(gpu_memory_visible(snapshot) && snapshot.gpu_memory_label == L"GPU VRAM" &&
                snapshot.gauges[3].value == 0,
            "Observed dedicated idle zero is valid and visible");
    device.id = settings.gpu_id = L"gpu-b";
    snapshot = collect(Status::unavailable, Status::unavailable, 0, 0);
    require(!gpu_memory_visible(snapshot), "Another GPU cannot inherit the memory meter");
    device.id = settings.gpu_id = L"gpu-a";
    snapshot = collect(Status::warming_up, Status::warming_up, 0, 0);
    require(gpu_memory_visible(snapshot) && snapshot.gpu_memory_label == L"GPU VRAM",
            "Reconnect/re-prime preserves the adapter's last selected scope");
}
void gpu_fault_tests() {
    for (bool integrated : {false, true}) {
        for (int stage = 0; stage < 3; ++stage) {
            DisplayContinuity state;
            auto first = sample();
            first.gpu_memory_label = integrated ? L"GPU shared" : L"GPU VRAM";
            state.observe(first);
            Device device{DeviceKind::gpu,   L"gpu-a",          L"Test GPU", 1,
                          8ULL * 1073741824, 8ULL * 1073741824, integrated,  true};
            auto failed = sample(2);
            int call{};
            const DiskCounterRead read = [&]() -> Result<std::vector<std::vector<CounterItem>>> {
                if (call++ == stage) {
                    throw std::runtime_error("Injected GPU query exception");
                }
                return std::vector<std::vector<CounterItem>>(1);
            };
            collect_gpu_readings(failed, device, failed.timestamp, read, read, read);
            state.observe(failed);
            require(failed.gauges[3].status == (stage == 0 ? Status::unavailable : Status::error) &&
                        failed.gpu_memory_label == first.gpu_memory_label &&
                        presented_metric(failed.gauges[3]).value == 50 &&
                        presented_metric(failed.gpu_memory_bytes).value == 4.0 * 1073741824 &&
                        failed.gpu_memory_capacity == first.gpu_memory_capacity,
                    "GPU query exceptions preserve stable scope and coherent retained memory");
        }
    }
}
void selection_and_failure_tests() {
    DisplayContinuity state;
    SessionPeaks peaks;
    Settings a, b, c;
    a.gpu_id = L"gpu-a";
    a.network_id = L"nic-a";
    b.gpu_id = L"gpu-b";
    b.network_id = L"nic-b";
    c.gpu_id = L"gpu-c";
    c.network_id = L"nic-c";
    auto first = sample();
    peaks.observe(first);
    state.observe(first, a);
    auto other = sample(2);
    other.gpu_id = b.gpu_id;
    other.network_id = b.network_id;
    other.gpu_memory_label = L"GPU shared";
    other.gpu_memory_capacity *= 2;
    other.gpu_memory_bytes.value *= 2;
    other.gauges[1].value = 90;
    other.gauges[4].value = 350;
    state.observe(other, b);
    auto gone = sample(3);
    gone.gpu_id.clear();
    gone.network_id.clear();
    gone.gpu_memory_label = L"GPU memory";
    gone.gpu_memory_capacity = 0;
    set_snapshot_status(gone, Status::unavailable, L"Saved device disconnected");
    state.observe(gone, a);
    require(gone.gpu_id == a.gpu_id && gone.network_id == a.network_id &&
                presented_metric(gone.gauges[1]).value == 50 &&
                presented_metric(gone.gauges[4]).value == 50 &&
                gone.gpu_memory_label == L"GPU VRAM" &&
                gone.gpu_memory_capacity == first.gpu_memory_capacity,
            "A to B to disconnected A restores A's own values, metadata and memory scope");
    gone.gpu_id.clear();
    gone.network_id.clear();
    state.observe(gone, c);
    require(!gone.gauges[1].retained && !gone.gauges[3].retained && !gone.gauges[4].retained,
            "A never-seen explicit C stays unavailable");
    Latest<Delivery> slot;
    int posts{};
    const auto notify = [&] {
        ++posts;
        return true;
    };
    auto older = sample(4);
    older.processors[0].utilization.value = 10;
    state.observe(older, a);
    slot.publish(Delivery{older}, notify);
    require(slot.take().has_value(), "UI consumes initial reading");
    auto newer = sample(5);
    newer.processors[0].utilization.value = 20;
    state.observe(newer, a);
    const auto catalog = std::make_shared<const Catalog>();
    slot.publish(Delivery{newer, catalog}, notify);
    publish_worker_failure(slot, 2, 1, notify);
    auto failure = slot.take();
    if (!failure) {
        throw std::runtime_error("Expected pending worker-failure delivery");
    }
    require(failure && failure->worker_failed && failure->has_snapshot &&
                failure->catalog == catalog && posts == 2,
            "Fatal worker delivery preserves pending snapshot/catalog and coalesces notification");
    require(
        accept_delivery(*failure, 1, 99) && !accept_delivery(*failure, 2, 1),
        "Current-worker failure survives configuration advance; retired-worker failure is ignored");
    set_snapshot_status(failure->snapshot, Status::error, L"Worker stopped");
    require(failure->snapshot.processors[0].utilization.status == Status::error &&
                presented_metric(failure->snapshot.processors[0].utilization).value == 20,
            "Failure presents newest completed 20, not UI's older 10");
    publish_worker_failure(slot, 3, 1, notify);
    failure = slot.take();
    require(failure && failure->worker_failed && !failure->has_snapshot &&
                failure->snapshot.generation == 3,
            "Failure without pending observation tells UI to retain its own latest snapshot");
    slot.publish(Delivery{newer, catalog}, [] { return false; });
    publish_worker_failure(slot, 4, 1, notify);
    failure = slot.take();
    require(failure && failure->has_snapshot && failure->worker_failed,
            "Failure retries a failed notification while preserving its observation");
    slot.publish(Delivery{newer, catalog}, [] { return false; });
    // WM_APP fallback consumes the same pending delivery if failure publication itself throws.
    failure = slot.take();
    require(failure && failure->snapshot.processors[0].utilization.value == 20,
            "Fallback notification can consume the newest pending observation");
    slot.publish(Delivery{newer, catalog, false, true, 2}, notify);
    const auto replacement = slot.take();
    require(replacement && accept_delivery(*replacement, 2, 0) &&
                !accept_delivery(*replacement, 1, 0) && !accept_delivery(*replacement, 2, 99),
            "Healthy deliveries match worker lifetime and configuration separately");
    slot.close();
    const int before_close = posts;
    publish_worker_failure(slot, 5, 1, notify);
    require(!slot.take() && posts == before_close, "Shutdown prevents late failure publication");
}
class Shell final : public ShellPort {
  public:
    int adds{}, removes{}, sets{};
    bool add() override {
        ++adds;
        return true;
    }
    void remove() noexcept override {
        ++removes;
    }
    Rect query(Rect rect, Edge) override {
        return rect;
    }
    Rect set(Rect rect, Edge) override {
        ++sets;
        return rect;
    }
};
void thickness_tests() {
    const Rect monitor{-1920, -200, 0, 880};
    for (auto edge : {Edge::top, Edge::bottom, Edge::left, Edge::right}) {
        for (unsigned dpi : {96U, 120U, 144U, 192U}) {
            Shell shell;
            {
                AppBar bar(shell);
                Settings settings;
                settings.edge = edge;
                for (double request : {5, 6, 5, 30, 5}) {
                    const auto migrated =
                        decode_settings(std::format(L"Loadbar 5 {} {} 1000 \"\" \"\" \"\" 0",
                                                    static_cast<unsigned>(edge), request));
                    require(migrated && migrated->thickness == 40,
                            "Old small requests migrate to the new minimum");
                    if (!migrated) {
                        throw std::runtime_error("Expected migrated settings");
                    }
                    settings = *migrated;
                    const auto size = resolve_thickness(settings, monitor, dpi, 40);
                    if (!size) {
                        throw std::runtime_error("Expected a readable minimum thickness");
                    }
                    require(size && size->effective == 40 &&
                                size->adjustment == ThicknessAdjustment::none,
                            "Migrated requests use the readable minimum");
                    require(bar.position(monitor, edge, size->pixels).has_value(),
                            "Apply valid minimum");
                }
                require(shell.sets == 1, "The user's repeated small requests share one rectangle");
                for (double request : {40, 80, 120, 80}) {
                    settings.thickness = request;
                    const auto size = resolve_thickness(settings, monitor, dpi, 40);
                    require(size && size->effective == request &&
                                bar.position(monitor, edge, size->pixels),
                            "Growing and shrinking requests apply repeatedly");
                }
                require(shell.sets == 4 && shell.adds == 1,
                        "Only changed sizes renegotiate without extra registrations");
            }
            require(shell.removes == 1, "Repeated Apply balances one registration");
        }
    }
    Settings settings;
    settings.edge = Edge::top;
    settings.thickness = 400;
    const auto capped = resolve_thickness(settings, monitor, 144, 40);
    require(capped && capped->effective == 180 &&
                capped->adjustment == ThicknessAdjustment::maximum,
            "Large requests retain the monitor safety cap");
    settings.thickness = 41;
    const auto rounded = resolve_thickness(settings, monitor, 144, 40);
    require(rounded && rounded->pixels == 62 && rounded->effective == 62 / 1.5 &&
                rounded->adjustment == ThicknessAdjustment::rounding,
            "Whole DIPs still require physical pixel rounding at mixed DPI");
    const auto larger = resolve_thickness(settings, monitor, 144, 80);
    require(larger && larger->effective == 80, "Topology changes can raise the readable minimum");
    auto processors = sample().processors;
    const auto compact = make_layout(1920, 40, loadbar::Edge::top, processors, 1);
    const auto thick = make_layout(1920, 80, loadbar::Edge::top, processors, 1);
    require(compact.fits && thick.fits &&
                compact.blocks[1].graphic.height < thick.blocks[1].graphic.height &&
                compact.blocks[1].icon.height < thick.blocks[1].icon.height,
            "Extra horizontal thickness enlarges graphics and icons together");
}
void appearance_tests() {
    require(gauge_palette(Gauge::disk_active).hue == rgb(0x65C55B) &&
                gauge_palette(Gauge::disk_read).hue == rgb(0xB9DF9B) &&
                gauge_palette(Gauge::disk_write).hue == rgb(0x36884D),
            "Exact disk hues");
    const auto old = decode_settings(L"Loadbar 4 2 6 1000 \"monitor\" \"gpu\" \"nic\" 1 1 1 1");
    require(old && encode_settings(*old) ==
                       L"Loadbar 10 2 40 1000 \"monitor\" \"gpu\" \"nic\" 1 1 1 0 0 0 1 1",
            "Migration retires appearance switches and raises old sizes to 40 DIPs");
    require(!decode_settings(L"Loadbar 5 2 6 1000 \"\" \"\" \"\" 1 0 0 0") &&
                !decode_settings(L"Loadbar 5 2 6 1000 \"\" \"\" \"\" 3"),
            "New schema rejects trailing retired fields and invalid alignment");
}
} // namespace
void run_continuity_tests() {
    reading_tests();
    recovery_tests();
    coalescing_tests();
    gpu_fault_tests();
    gpu_fallback_tests();
    selection_and_failure_tests();
    thickness_tests();
    appearance_tests();
    std::cout << "Continuity, GPU faults, repeated thickness and fixed appearance tests passed\n";
}
