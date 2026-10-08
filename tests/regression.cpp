#include "model/geometry.hpp"
#include "model/layout.hpp"
#include "model/snapshot.hpp"
#include "telemetry/collector.hpp"
#include "telemetry/discovery.hpp"
#include "telemetry/pdh.hpp"
#include "ui/control_ids.hpp"
#include <pdhmsg.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
void check(bool condition, const char *explanation) {
    if (!condition) {
        throw std::runtime_error(explanation);
    }
}
class FakeShell final : public loadbar::ShellPort {
  public:
    int registrations{}, removals{}, positions{};
    bool allow{true}, corrupt{}, fail{};
    std::function<void()> during_query;
    bool add() override {
        ++registrations;
        return allow;
    }
    void remove() noexcept override {
        ++removals;
    }
    loadbar::Rect query(loadbar::Rect rectangle, loadbar::Edge edge) override {
        if (during_query) {
            during_query();
        }
        if (fail) {
            throw std::runtime_error("Injected Shell failure");
        }
        if (edge == loadbar::Edge::bottom) {
            rectangle.bottom -= 40;
        }
        return rectangle;
    }
    loadbar::Rect set(loadbar::Rect rectangle, loadbar::Edge) override {
        ++positions;
        return corrupt ? loadbar::Rect{} : rectangle;
    }
};
void arithmetic() {
    using namespace loadbar;
    const Clock::time_point start{};
    Snapshot staggered;
    staggered.processors.push_back(
        {{0, 0}, 0, true, {10, Status::valid, Unit::percent, start, {}, {}}});
    auto &nic = staggered.gauges[static_cast<std::size_t>(Gauge::download)];
    nic = {100, Status::valid, Unit::bytes_per_second, start + std::chrono::milliseconds(200), {},
           {}};
    check(expire_snapshot(staggered, start + std::chrono::milliseconds(3050), 1000) &&
              staggered.processors[0].utilization.status == Status::stale &&
              nic.status == Status::valid,
          "CPU expiration does not prematurely expire newer NIC observation");
    check(expire_snapshot(staggered, start + std::chrono::milliseconds(3250), 1000) &&
              nic.status == Status::stale,
          "A later NIC stale transition is still reported after CPU became stale");
    check(!expire_snapshot(staggered, start + std::chrono::seconds(5), 1000),
          "Already expired metrics do not cause periodic repaints");
    auto observation = start;
    std::uint64_t octets{};
    NetworkProvider network(
        [&](std::uint64_t) -> Result<InterfaceCounters> {
            observation += std::chrono::seconds(1);
            octets += 200;
            return InterfaceCounters{octets, octets / 2, true};
        },
        [&] { return observation; });
    const auto first_network = network.sample(42);
    check(first_network && (*first_network)[0].status == Status::warming_up,
          "First fake network read primes");
    observation += std::chrono::seconds(1); // A preceding provider delays this observation.
    const auto second_network = network.sample(42);
    check(second_network && (*second_network)[0].value == 100 && (*second_network)[1].value == 50 &&
              (*second_network)[0].timestamp == start + std::chrono::seconds(3),
          "NIC rates use completed read timestamps including preceding-provider delay");
    Rate rate;
    check(rate.sample(1ULL << 63U, start).status == Status::warming_up, "First 64-bit sample");
    check(rate.sample((1ULL << 63U) + 150, start + std::chrono::milliseconds(1500)).value == 100,
          "Actual elapsed interval");
    check(rate.sample(5, start + std::chrono::seconds(2)).status == Status::warming_up,
          "Counter reset");
    check(rate.sample(5, start + std::chrono::seconds(3)).status == Status::valid,
          "Healthy zero is valid");
    check(rate.sample(10, start + std::chrono::seconds(3)).status == Status::warming_up,
          "Zero interval");
    check(rate.sample(20, start).status == Status::warming_up, "Invalid ordering");
    rate.reset();
    check(rate.sample(100, start).status == Status::warming_up, "Resume/reconnect baseline");
    check(percentage(1, 0, start).status == Status::error, "Zero percentage denominator");
    check(percentage(11, 10, start).status == Status::error, "Invalid used capacity");
    check(percentage(std::numeric_limits<double>::infinity(), 10, start).status == Status::error,
          "Infinite input");
    check(fill_fraction(std::numeric_limits<double>::quiet_NaN(), 100) == 0, "NaN fill guarded");
    for (double value : {0.0, 50.0, 100.0}) {
        check(fill_fraction(value, 100) == value / 100, "Percentage fill scale");
    }
    check(fill_fraction(2048, 1024) == 1, "Rate visual clipping");
    Metric bytes{1024, Status::valid, Unit::bytes_per_second, start, {}, {}};
    check(metric_text(bytes) == L"1.0 KiB/s", "Binary bytes, not bits");
    check(aged_metric(bytes, start + std::chrono::seconds(3), 1000).status == Status::valid,
          "Stale boundary");
    check(aged_metric(bytes, start + std::chrono::seconds(4), 1000).status == Status::stale,
          "Staleness");
    bytes.status = Status::unavailable;
    check(metric_text(bytes) == L"Unavailable", "No healthy zero for unavailable");
}
void settings() {
    using namespace loadbar;
    Settings value;
    value.monitor_id = L"display\\path\"quoted";
    value.gpu_id = L"disk Ω";
    value.edge = Edge::left;
    value.thickness = 96;
    check(decode_settings(encode_settings(value)) == value, "Settings identity/Unicode round trip");
    check(!decode_settings(encode_settings(value) + L" junk"), "Reject trailing content");
    check(!decode_settings(L"Loadbar 2 0 0 5 1000"), "Reject unknown schema");
    check(!decode_settings(std::wstring(9000, L'x')), "Bound settings input");
    value.interval_ms = 249;
    check(!valid_settings(value), "Interval minimum");
    value.interval_ms = 5001;
    check(!valid_settings(value), "Interval maximum");
    value.interval_ms = 250;
    check(valid_settings(value), "Interval boundary");
    value.thickness = std::numeric_limits<double>::infinity();
    check(!valid_settings(value), "Finite thickness");
    value.thickness = 96;
    value.gpu_id = L"disk\n";
    check(!valid_settings(value), "No controls in identity");
    value.gpu_id.clear();
    check(!decode_settings(L"Loadbar 3 0 40 1000 10000 10000 0 10000 \"\" \"\" \"\" 0 0 0 0 0"),
          "Malformed legacy ceiling is rejected before migration");
}
void geometry() {
    using namespace loadbar;
    const std::vector<Monitor> displays{{L"primary", L"", {0, 0, 1920, 1080}, 96, true},
                                        {L"", L"", {-1080, 0, 0, 1920}, 144, false}};
    const auto automatic = select_monitor(displays, L"");
    check(automatic && automatic->index == 0 && !automatic->fallback,
          "Automatic placement never matches an unresolved secondary identity");
    const auto missing = select_monitor(displays, L"missing");
    check(missing && missing->index == 0 && missing->fallback,
          "Missing monitor falls back to primary");
    check(fit_window({100, 100, 1700, 1620}, {0, 0, 3840, 2160}) == Rect{100, 100, 1700, 1620},
          "DPI suggestion preserves requested logical size");
    check(fit_window({-2000, -100, -400, 1420}, {-1080, 0, 0, 1920}) == Rect{-1080, 0, 0, 1520},
          "Settings DPI rectangle fits destination work area");
    check(!fit_window({}, {0, 0, 1920, 1080}), "Invalid DPI bounds rejected");
    const Rect monitor{-1920, -400, 0, 680};
    Settings value;
    for (const auto edge : {Edge::left, Edge::right, Edge::top, Edge::bottom}) {
        value.edge = edge;
        for (unsigned dpi : {96U, 120U, 144U, 192U}) {
            const auto pixels = thickness_pixels(value, monitor, dpi, 32);
            if (!pixels) {
                throw std::runtime_error("Mixed-DPI geometry");
            }
            const auto rectangle = edge_rectangle(monitor, edge, *pixels);
            check(rectangle && valid_rect(*rectangle), "All edge rectangles");
        }
        check(resolve_edge(edge, {-100, -100, 980, 1820}) == edge,
              "Explicit edge survives rotation");
    }
    value.edge = Edge::right;
    value.thickness = 80;
    check(thickness_pixels(value, monitor, 144, 32) == 120, "DIP conversion");
    check(!thickness_pixels(value, monitor, 96, 1000), "Reject unreadable oversized minimum");
    check(!valid_rect({std::numeric_limits<int>::min(), 0, std::numeric_limits<int>::max(), 1}),
          "Signed rectangle overflow");
    FakeShell shell;
    {
        AppBar bar(shell);
        shell.during_query = [&] { (void)bar.position(monitor, Edge::bottom, 100); };
        const auto position = bar.position(monitor, Edge::bottom, 100);
        check(position && position->bottom == 640 && position->top == 540,
              "Restore Shell-adjusted thickness");
        check(shell.positions == 1, "No recursive negotiation");
        check(bar.position(monitor, Edge::bottom, 100).has_value() && shell.positions == 1,
              "Unchanged negotiation does not send another SETPOS");
        bar.shell_restarted();
        check(bar.position(monitor, Edge::bottom, 100).has_value(), "Explorer recovery");
        check(shell.registrations == 2, "One registration per Shell generation");
        bar.remove();
        bar.remove();
        check(shell.removals == 1, "Idempotent removal");
        shell.allow = false;
        check(!bar.position(monitor, Edge::bottom, 100), "Registration failure");
    }
    check(shell.removals == 1, "Failed registration not removed twice");
    FakeShell equal_geometry;
    {
        AppBar bar(equal_geometry);
        const Rect square{0, 0, 100, 100};
        check(bar.position(square, Edge::left, 100).has_value(),
              "Full-width left test reservation");
        check(bar.position(square, Edge::right, 100).has_value() && equal_geometry.positions == 2,
              "Edge changes are sent even when rectangles match");
        bar.remove();
        check(bar.position(square, Edge::right, 100).has_value() && equal_geometry.positions == 3,
              "Removal invalidates unchanged-placement cache");
    }
    FakeShell partial;
    bool caught{};
    try {
        AppBar bar(partial);
        partial.fail = true;
        (void)bar.position(monitor, Edge::left, 100);
    } catch (const std::runtime_error &) {
        caught = true;
    }
    check(caught, "Injected Shell failure reached exception boundary");
    check(partial.removals == 1, "Partial startup exception releases registration");
    FakeShell invalid;
    {
        AppBar bar(invalid);
        invalid.corrupt = true;
        check(!bar.position(monitor, Edge::top, 100), "Reject invalid returned geometry");
    }
    check(invalid.removals == 1, "Invalid geometry cleanup");
}
void processors_and_gpu() {
    using namespace loadbar;
    ActivityState activity;
    activity.locked = true;
    activity.suspended = true;
    activity.suspended = false;
    check(activity.paused(), "Resume while locked remains paused");
    activity.locked = false;
    check(!activity.paused(), "Unlock resumes after power resume");
    check(parse_processor(L"2,0") == ProcessorId{2, 0}, "Processor group parsing");
    for (const auto *name : {L"_Total", L"0,_Total", L"0,64", L"1,-1", L"0,1junk", L"0,1,2"}) {
        check(!parse_processor(name), "Reject aggregate/malformed CPU instance");
    }
    const Clock::time_point now{};
    const auto counter = [&](double value, DWORD status, bool priming) {
        return interpret_counter(value, status, priming, now, std::chrono::seconds(1),
                                 Unit::percent);
    };
    check(counter(0, PDH_CSTATUS_INVALID_DATA, true).status == Status::warming_up,
          "PDH first invalid observation primes");
    check(counter(0, PDH_CSTATUS_INVALID_DATA, false).status == Status::unavailable,
          "PDH invalid status never becomes healthy zero");
    check(counter(0, PDH_CSTATUS_NEW_DATA, false).status == Status::valid,
          "Documented PDH new data accepts valid idle");
    check(counter(50, PDH_CSTATUS_VALID_DATA, true).status == Status::warming_up,
          "New/reconnected instance primes");
    check(counter(std::numeric_limits<double>::quiet_NaN(), PDH_CSTATUS_VALID_DATA, false).status ==
              Status::error,
          "Reject PDH non-finite values");
    Device gpu;
    gpu.runtime_id = 42;
    gpu.memory_scope_known = true;
    std::vector<CounterItem> memory{
        {L"luid_0x0_0x2a_phys_0", {50, Status::valid, Unit::bytes, now, {}, {}}},
        {L"luid_0x0_0x63_phys_0", {100, Status::valid, Unit::bytes, now, {}, {}}}};
    check(memory_percentage(memory, gpu, 100, now).value == 50, "Matching adapter memory scope");
    gpu.memory_scope_known = false;
    check(memory_percentage(memory, gpu, 100, now).status == Status::unavailable,
          "Unverified capacity is unavailable");
    gpu.memory_scope_known = true;
    memory.push_back(memory.front());
    check(memory_percentage(memory, gpu, 100, now).status == Status::unavailable,
          "Duplicate memory rows are ambiguous");
    memory.back().name = L"luid_0x0_0x2a_phys_1";
    check(memory_percentage(memory, gpu, 100, now).status == Status::unavailable,
          "Physical nodes cannot share one DXGI denominator");
    Metric valid{30, Status::valid, Unit::percent, now, std::chrono::seconds(1), {}};
    std::vector<EngineSample> rows{{{42, 0, 0, 1, L"3D"}, valid},
                                   {{42, 0, 0, 2, L"3D"}, valid},
                                   {{42, 0, 1, 3, L"3D"}, valid},
                                   {{42, 0, 2, 4, L"VideoDecode"}, valid},
                                   {{99, 0, 0, 1, L"3D"}, valid}};
    check(aggregate_engines(rows, 42, L"3D", now).value == 60,
          "Processes additive only within same engine/adapter");
    check(aggregate_engines(rows, 42, L"VideoDecode", now).value == 30, "Decode distinct from 3D");
    std::ranges::reverse(rows);
    check(aggregate_engines(rows, 42, L"3D", now).value == 60, "GPU reorder stable");
    rows.push_back(rows.back());
    check(aggregate_engines(rows, 42, L"3D", now).status == Status::error, "Duplicate engine row");
    rows.pop_back();
    rows.back().metric.status = Status::unavailable;
    check(aggregate_engines(rows, 42, L"3D", now).status == Status::unavailable,
          "Partial engine enumeration is not healthy zero");
    rows.back().metric = valid;
    rows.back().metric.interval = std::chrono::seconds(2);
    check(aggregate_engines(rows, 42, L"3D", now).status == Status::error,
          "Mixed intervals rejected");
    rows.back().metric = valid;
    rows.back().metric.timestamp += std::chrono::seconds(1);
    check(aggregate_engines(rows, 42, L"3D", now).status == Status::error,
          "Mixed sample timestamps rejected");
    rows.back().metric = std::move(valid);
    rows.back().metric.value = 150;
    const auto anomaly = aggregate_engines(rows, 42, L"3D", now);
    check(anomaly.value == 180 && !anomaly.detail.empty(), "Preserve raw GPU anomalies");
    check(aggregate_engines({}, 42, L"3D", now).status == Status::unavailable,
          "Empty provider is unavailable");
    const auto adapter = parse_adapter(L"luid_0x00000001_0xffffffff_phys_2");
    check(adapter && adapter->first == 0x1ffffffffULL && adapter->second == 2,
          "Memory physical-adapter identity");
    check(!parse_adapter(L"luid_0x1_0x2_phys_0#1"), "Reject duplicate/ambiguous memory rows");
}
void layout_and_selection() {
    using namespace loadbar;
    Snapshot unmapped;
    unmapped.processors.push_back({{2, 7}, 100135, false, {}});
    const auto unmapped_layout = make_layout(1200, 200, loadbar::Edge::top, unmapped.processors, 1);
    check(unmapped_layout.fits && core_label(unmapped_layout.cores[0]) == L"G2:7",
          "Unmapped lane label uses actual group and logical processor identity");
    const auto &unmapped_lane = unmapped_layout.cores[0].label;
    const auto unmapped_tip =
        metric_tooltip(unmapped_layout, unmapped_lane.x + 1, unmapped_lane.y + 1, unmapped, {}, {});
    check(unmapped_tip.find(L"G2:7") == 0 && unmapped_tip.find(L"100135") == std::wstring::npos,
          "Unmapped tooltip never exposes synthetic grouping key as physical core");
    Snapshot portrait;
    portrait.processors.reserve(24);
    for (unsigned index = 0; index < 24; ++index) {
        portrait.processors.push_back(
            {{0, index}, index / 2, true, {50, Status::valid, Unit::percent, {}, {}, {}}});
    }
    const auto thickness = minimum_thickness(540, 240, loadbar::Edge::top, portrait.processors, 1);
    check(thickness <= 240, "200% portrait horizontal bar fits within safety maximum");
    const auto compact =
        make_layout(540, static_cast<float>(thickness), loadbar::Edge::top, portrait.processors, 1);
    check(compact.fits && compact.cores.size() == 24, "Compact horizontal grid retains every LP");
    const auto &first_lane = compact.cores[0].label;
    const auto &second_lane = compact.cores[1].label;
    const auto initial_tip =
        metric_tooltip(compact, first_lane.x + 1, first_lane.y + 1, portrait, {}, {});
    const auto moved_tip =
        metric_tooltip(compact, second_lane.x + 1, second_lane.y + 1, portrait, {}, {});
    check(initial_tip != moved_tip, "Moving inside one tooltip window changes the metric identity");
    portrait.processors[0].utilization.value = 75;
    check(metric_tooltip(compact, first_lane.x + 1, first_lane.y + 1, portrait, {}, {}) !=
              initial_tip,
          "Stationary tooltip reflects a new sample");
    check(metric_tooltip(compact, first_lane.x + 1, first_lane.y + 1, portrait, {},
                         Clock::time_point{} + std::chrono::seconds(4))
                  .find(L"Stale") != std::wstring::npos,
          "Stationary tooltip reflects stale transition");
    check(command_activates(0, true), "Button click activates");
    check(!command_activates(6, true) && !command_activates(7, true),
          "Button focus and blur do not activate Apply/Cancel/Retry/Exit");
    check(command_activates(0, false) && command_activates(1, false),
          "Menu and accelerator commands activate");
    const auto catalog = std::make_shared<const Catalog>();
    Latest<Delivery> deliveries;
    deliveries.publish(Delivery{Snapshot{}, catalog}, [] { return true; });
    Snapshot newer;
    newer.generation = 2;
    deliveries.publish(Delivery{std::move(newer), catalog}, [] { return true; });
    const auto delivered = deliveries.take();
    check(delivered && delivered->snapshot.generation == 2 && delivered->catalog == catalog,
          "Coalesced samples retain coherent discovery metadata");
    std::vector<Processor> processors;
    processors.reserve(136);
    for (unsigned index = 0; index < 128; ++index) {
        processors.push_back({{index / 64, index % 64}, index / 2, true, {}});
    }
    // Mixed SMT/core counts: the last physical cores have one lane rather than two.
    for (unsigned index = 128; index < 136; ++index) {
        processors.push_back({{2, index - 128}, index - 64, true, {}});
    }
    for (bool horizontal : {false, true}) {
        for (float scale : {1.0F, 1.25F, 1.5F, 2.0F, 2.25F}) {
            const auto layout = make_layout(
                horizontal ? 3840.0F : 800.0F, horizontal ? 800.0F : 3840.0F,
                (horizontal ? loadbar::Edge::top : loadbar::Edge::right), processors, scale);
            check(layout.fits && layout.cores.size() == processors.size(),
                  "All processors visible at text scale");
            std::set<std::size_t> indices;
            for (const auto &core : layout.cores) {
                check(core.label.height >= 6 && core.label.width >= 6, "Readable core size");
                indices.insert(core.processor);
            }
            check(indices.size() == processors.size(), "No hidden/duplicated logical processors");
        }
    }
    check(!make_layout(40, 40, loadbar::Edge::top, processors, 1).fits,
          "Too-small layout is rejected");
    Device a;
    a.id = L"a";
    a.preferred = true;
    Device b;
    b.id = L"b";
    b.preferred = true;
    check(!select_device({a, b}, L""), "Ambiguous auto selection is exposed");
    check(select_device({a, b}, L"b") == b, "Explicit persistent selection");
    check(!select_device({a}, L"b"), "Disappearing device never silently switches");
    check(!select_device({a, a}, L"a"), "Colliding identity requires resolution");
    struct FakeProvider {
        int value{};
        int sample() {
            return ++value;
        }
    } provider;
    Latest<int> latest;
    int posts{};
    for (int i = 0; i < 1000; ++i) {
        latest.publish(provider.sample(), [&] {
            ++posts;
            return true;
        });
    }
    check(posts == 1 && latest.take() == 1000, "Bounded latest-value handoff");
    latest.publish(1, [] { return false; });
    latest.publish(2, [&] {
        ++posts;
        return true;
    });
    check(posts == 2 && latest.take() == 2, "Failed notification can retry");
    latest.close();
    latest.publish(3, [&] {
        ++posts;
        return true;
    });
    check(posts == 2 && !latest.take(), "Late publication after close is dropped");
}
} // namespace
void run_regressions() {
    arithmetic();
    settings();
    geometry();
    processors_and_gpu();
    layout_and_selection();
    std::cout << "Regression groups passed: arithmetic/status, settings, geometry/lifecycle, "
                 "CPU/GPU, layout/handoff\n";
}
