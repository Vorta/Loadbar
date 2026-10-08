#include "telemetry/collector.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>

namespace {
using namespace loadbar;
using namespace std::chrono_literals;
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
Metric valid(double value) {
    return {value, Status::valid, Unit::percent, Clock::time_point{} + 1s, 1s, {}};
}
void instance_tracking() {
    CounterInstances instances;
    instances.begin();
    require(!instances.contains(L"a"), "First observation primes");
    instances.observe(L"a");
    require(!instances.contains(L"a"), "Duplicate row cannot prime itself");
    instances.finish();
    instances.begin();
    require(instances.contains(L"a"), "Previous valid instance retained");
    instances.observe(L"a");
    instances.observe(L"b");
    instances.finish();
    require(instances.size() == 2, "Current names retained");
    instances.begin();
    instances.observe(L"b");
    instances.finish();
    require(!instances.contains(L"a") && instances.contains(L"b") && instances.size() == 1,
            "Disappeared or invalid instances pruned");
    instances.begin();
    require(!instances.contains(L"a"), "Reappearing instance primes again");
    instances.observe(L"a");
    instances.finish();
    instances.clear();
    require(instances.size() == 0 && !instances.contains(L"a"), "Failure resets priming");
}
void cpu_lookup() {
    CpuSamples cpu;
    cpu.configure({{{1, 0}, 2, true, {}, 8}, {{0, 1}, 0, true, {}, 4}, {{0, 0}, 0, true, {}, 4}});
    std::vector<Processor> output;
    const auto now = Clock::time_point{} + 1s;
    cpu.apply(output,
              {{L"1,0", valid(80)},
               {L"0,0", valid(20)},
               {L"0,_Total", valid(100)},
               {L"_Total", valid(100)},
               {L"0,1", valid(40)}},
              now);
    require(output.size() == 3 && output[0].id == ProcessorId{0, 0} &&
                output[1].id == ProcessorId{0, 1} && output[2].id == ProcessorId{1, 0},
            "CPU lookup preserves deterministic physical core/group ordering");
    require(output[0].utilization.value == 20 && output[1].utilization.value == 40 &&
                output[2].utilization.value == 80 && output[2].efficiency_class == 8 &&
                output[0].utilization.identity == L"processor:0,0",
            "CPU values, heterogeneity and stable identities preserved");
    cpu.apply(output,
              {{L"0,0", valid(10)}, {L"0,0", valid(30)}, {L"2,7", valid(60)}, {L"2,7", valid(70)}},
              now);
    require(output.size() == 4 && output[0].utilization.status == Status::error &&
                output[1].utilization.status == Status::unavailable && !output.back().mapped &&
                output.back().utilization.status == Status::error &&
                output.back().utilization.identity == L"processor:2,7",
            "Duplicate, absent and unmatched CPU counters remain honest");
    cpu.apply(output, {}, now);
    require(output.size() == 4 && output[0].utilization.status == Status::unavailable &&
                !output.back().mapped && output.back().utilization.status == Status::unavailable,
            "Missing counters retain known fallback identities until completed topology discovery");
    cpu.configure({{{3, 5}, 0, true, {}, 9}});
    cpu.apply(output, {{L"3,5", valid(65)}}, now);
    require(output.size() == 1 && output[0].utilization.value == 65 &&
                output[0].utilization.identity == L"processor:3,5",
            "Topology change rebuilds indices and identity cache");
}
std::wstring engine(unsigned process, unsigned adapter, std::wstring_view type = L"3D") {
    return std::format(L"pid_{}_luid_0x00000000_0x{:08X}_phys_0_eng_0_engtype_{}", process, adapter,
                       type);
}
void gpu_cache() {
    GpuSamples cache;
    const auto now = Clock::time_point{} + 1s;
    std::vector<CounterItem> items{
        {engine(1, 10), valid(20)},           {engine(2, 10), valid(30)},
        {engine(3, 11), valid(90)},           {engine(4, 10, L"VideoDecode"), valid(40)},
        {engine(5, 10, L"Copy"), valid(100)}, {L"bad", valid(90)}};
    auto &samples = cache.read(items, 10);
    require(samples.size() == 3 && aggregate_engines(samples, 10, L"3D", now).value == 50 &&
                aggregate_engines(samples, 10, L"VideoDecode", now).value == 40,
            "Cache filters only irrelevant adapters/types, retains additive contributions");
    auto *storage = samples.data();
    std::ranges::reverse(items);
    const auto &reordered = cache.read(items, 10);
    require(reordered.data() == storage && aggregate_engines(reordered, 10, L"3D", now).value == 50,
            "Reordered names reuse samples and preserve totals");
    items = {{engine(1, 10), valid(20)}, {engine(1, 10), valid(20)}};
    require(aggregate_engines(cache.read(items, 10), 10, L"3D", now).status == Status::error &&
                cache.cached_names() == 1,
            "Duplicate rows remain detectable and vanished identities are pruned");
    items[0].metric.status = Status::warming_up;
    items.resize(1);
    require(aggregate_engines(cache.read(items, 10), 10, L"3D", now).status == Status::warming_up,
            "Cached identity never changes fresh metric status");
    require(cache.read(items, 11).empty(), "Adapter selection changes filter immediately");
    items.clear();
    for (std::size_t i = 0; i < GpuSamples::kMaximumCachedNames + 2; ++i) {
        items.push_back({engine(static_cast<unsigned>(i), 10), valid(0.001)});
    }
    require(cache.read(items, 10).size() == items.size() &&
                cache.cached_names() <= GpuSamples::kMaximumCachedNames,
            "Cache bound never drops uncached valid rows");
    require(cache.read({}, 10).empty() && cache.cached_names() == 0,
            "Successful empty enumeration clears cache and samples");
}
void network_backoff() {
    auto now = Clock::time_point{};
    unsigned calls{};
    bool fail = true, connected = true;
    std::uint64_t bytes{};
    NetworkProvider provider(
        [&](std::uint64_t) -> Result<InterfaceCounters> {
            ++calls;
            now += 100ms; // Deadline must begin after the slow observation.
            if (fail) {
                return std::unexpected(Error{L"Injected NIC error", ERROR_GEN_FAILURE});
            }
            return InterfaceCounters{bytes, bytes / 2, connected};
        },
        [&] { return now; });
    for (auto delay : {1s, 2s, 4s, 8s, 16s, 30s, 30s}) {
        const auto before = calls;
        const auto result = provider.sample(1);
        require(!result && result.error().code == ERROR_GEN_FAILURE && calls == before + 1,
                "NIC error performs exactly one scheduled read");
        now += delay - 1ms;
        const auto held = provider.sample(1);
        require(!held && held.error().operation == L"Injected NIC error" && calls == before + 1,
                "Backoff preserves native error and suppresses reads before deadline");
        now += 1ms;
    }
    fail = false;
    auto result = provider.sample(1);
    require(result && (*result)[0].status == Status::warming_up,
            "First success after failure only primes");
    now += 900ms;
    bytes = 200;
    result = provider.sample(1);
    require(result && (*result)[0].value == 200 && (*result)[1].value == 100,
            "Recovery uses completed observations and excludes failure interval");
    fail = true;
    require(!provider.sample(1), "Start a new failure episode");
    const auto before = calls;
    require(!provider.sample(2) && calls == before + 1,
            "Interface identity change cancels the old retry deadline");
    provider.reset();
    fail = false;
    require(provider.sample(2).value()[0].status == Status::warming_up && calls == before + 2,
            "Explicit reset immediately retries and primes");
    connected = false;
    require(provider.sample(2).value()[0].status == Status::unavailable,
            "Successful disconnected reads remain unavailable");
    connected = true;
    require(provider.sample(2).value()[0].status == Status::warming_up,
            "Reconnect primes without API-error backoff");
}
void aggregation_workspace() {
    EngineAggregation workspace;
    const auto now = Clock::time_point{} + 1s;
    std::vector<EngineSample> rows;
    rows.reserve(513);
    for (unsigned i = 0; i < 512; ++i) {
        rows.push_back({{10, i % 2, i % 4, i, L"3D"}, valid(0.01)});
    }
    const auto first = workspace.aggregate(rows, 10, L"3D", now);
    const auto capacity = workspace.capacity();
    std::ranges::reverse(rows);
    const auto reordered = workspace.aggregate(rows, 10, L"3D", now);
    require(first.status == Status::valid && reordered.value == first.value &&
                workspace.capacity() == capacity,
            "GPU aggregation reuses scratch and sums only matching hardware engines");
    rows.push_back(rows.back());
    require(workspace.aggregate(rows, 10, L"3D", now).status == Status::error,
            "Reused aggregation still detects duplicate process/engine identities");
    rows.clear();
    require(workspace.aggregate(rows, 10, L"3D", now).status == Status::unavailable,
            "Workspace retains no references to earlier sample storage");
    workspace.release();
    require(workspace.capacity() == 0, "Explicit GPU deactivation releases aggregation capacity");
}
void catalog_equivalence() {
    Catalog a;
    a.processors = {{{0, 0}, 0, true, {}, 1}};
    a.disks = {{DeviceKind::disk, L"disk", L"Disk", 1}};
    a.gpus = {{DeviceKind::gpu, L"gpu", L"GPU", 2}};
    a.networks = {{DeviceKind::network, L"nic", L"NIC", 3}};
    auto b = a;
    b.processors[0].utilization = valid(80);
    require(same_catalog(a, b), "Catalog compares metadata rather than sample values");
    b.processors[0].efficiency_class = 2;
    require(!same_catalog(a, b), "Heterogeneity changes catalog");
    b = a;
    b.disks[0].runtime_id = 7;
    require(!same_catalog(a, b), "Disk rebinding changes catalog");
    b = a;
    b.gpus[0].capacity = 4096;
    require(!same_catalog(a, b), "GPU capacity changes catalog");
    b = a;
    b.networks[0].preferred = true;
    require(!same_catalog(a, b), "Default route preference changes catalog");
    b = a;
    b.detail = L"Discovery partial";
    require(!same_catalog(a, b), "Discovery diagnostic changes catalog");
}
} // namespace
void telemetry_optimization_tests() {
    instance_tracking();
    cpu_lookup();
    gpu_cache();
    catalog_equivalence();
    network_backoff();
    aggregation_workspace();
}
