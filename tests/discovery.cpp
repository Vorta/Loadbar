#include "model/layout.hpp"
#include "telemetry/collector.hpp"

#include <stdexcept>

namespace {
using namespace loadbar;
using namespace std::chrono_literals;
void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
void enumeration_tests() {
    int token{};
    unsigned closes{}, reads{};
    DWORD end_error = ERROR_NO_MORE_ITEMS;
    bool open_failure{}, endless{}, read_failure{};
    std::vector<Device> devices{{DeviceKind::disk, L"disk-b", L"Disk 1", 1},
                                {DeviceKind::disk, L"disk-a", L"Disk 0", 0}};
    const DiskDiscoveryApi api{
        [&]() -> HDEVINFO {
            if (open_failure) {
                SetLastError(ERROR_ACCESS_DENIED);
                return INVALID_HANDLE_VALUE;
            }
            return &token;
        },
        [&](HDEVINFO handle, DWORD index, SP_DEVICE_INTERFACE_DATA *data) -> BOOL {
            require(handle == &token && data->cbSize == sizeof(*data), "Sized disk enumeration");
            if (!endless && index >= devices.size()) {
                SetLastError(end_error);
                return FALSE;
            }
            data->Reserved = endless ? 0 : index;
            return TRUE;
        },
        [&](HDEVINFO, SP_DEVICE_INTERFACE_DATA &data) -> Result<Device> {
            ++reads;
            if (read_failure && data.Reserved == 1) {
                return std::unexpected(Error{L"Injected identity failure", ERROR_NOT_READY});
            }
            return devices.at(data.Reserved);
        },
        [&](HDEVINFO handle) {
            require(handle == &token, "Close the owned enumeration handle");
            ++closes;
        }};
    open_failure = true;
    auto found = discover_disks(api);
    require(!found && found.error().code == ERROR_ACCESS_DENIED && closes == 0 && reads == 0,
            "Class-list failure is not successful empty enumeration");
    open_failure = false;
    found = discover_disks(api);
    require(found && found->size() == 2 && (*found)[0].id == L"disk-a" && closes == 1,
            "Completed disk discovery sorts physical numbers and releases its handle");
    end_error = ERROR_GEN_FAILURE;
    found = discover_disks(api);
    require(!found && found.error().code == ERROR_GEN_FAILURE && closes == 2,
            "Unexpected enumeration termination rejects a partial inventory");
    end_error = ERROR_NO_MORE_ITEMS;
    read_failure = true;
    found = discover_disks(api);
    require(!found && found.error().code == ERROR_NOT_READY && closes == 3,
            "Per-device identity failure rejects incomplete inventory and closes it");
    read_failure = false;
    const auto original = devices;
    devices[1] = devices[0];
    found = discover_disks(api);
    require(found && found->size() == 1, "Exact duplicate mappings are deduplicated");
    devices[1].id = L"different-disk";
    require(!discover_disks(api), "Conflicting stable identities on one physical number fail");
    devices[1] = devices[0];
    devices[1].runtime_id = 9;
    require(!discover_disks(api), "One stable identity cannot name two physical numbers");
    devices = original;
    devices[0].id.clear();
    require(!discover_disks(api), "Empty identities fail discovery");
    devices[0].id.assign(1024, L'x');
    require(!discover_disks(api), "Unbounded identities fail discovery");
    devices[0].id = std::wstring(L"bad\0identity", 12);
    require(!discover_disks(api), "Embedded identity terminators fail discovery");
    devices = original;
    endless = true;
    found = discover_disks(api);
    require(!found && found.error().code == ERROR_MORE_DATA,
            "Enumeration bound is an error, never a successfully truncated inventory");
    endless = false;
    devices.clear();
    found = discover_disks(api);
    require(found && found->empty(), "Only completed empty discovery establishes absence");
}
void topology_api_tests() {
    DWORD failure = ERROR_ACCESS_DENIED;
    bool malformed{};
    const CpuTopologyRead read = [&](LOGICAL_PROCESSOR_RELATIONSHIP relationship,
                                     PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX buffer,
                                     PDWORD size) -> BOOL {
        require(relationship == RelationProcessorCore, "Read physical-core relationships");
        if (failure == ERROR_ACCESS_DENIED || (buffer && failure != ERROR_SUCCESS)) {
            SetLastError(failure);
            return FALSE;
        }
        const auto needed = static_cast<DWORD>(sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX));
        if (!buffer) {
            *size = needed;
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        require(*size >= needed, "Topology output buffer was sized first");
        *buffer = {};
        buffer->Size = malformed ? 1 : needed;
        buffer->Relationship = RelationProcessorCore;
        buffer->Processor.GroupCount = 1;
        buffer->Processor.EfficiencyClass = 3;
        buffer->Processor.GroupMask[0] = {3, 2, {}};
        *size = needed;
        return TRUE;
    };
    auto found = discover_processors(read);
    require(!found && found.error().code == ERROR_ACCESS_DENIED,
            "Topology sizing failure is not successful empty discovery");
    failure = ERROR_GEN_FAILURE;
    found = discover_processors(read);
    require(!found && found.error().code == ERROR_GEN_FAILURE,
            "Topology data failure preserves its native error");
    failure = ERROR_SUCCESS;
    found = discover_processors(read);
    require(found && found->size() == 2 && (*found)[0].id == ProcessorId{2, 0} &&
                (*found)[1].id == ProcessorId{2, 1} && (*found)[0].core == (*found)[1].core,
            "Successful topology preserves group identity and physical siblings");
    malformed = true;
    require(!discover_processors(read), "Malformed topology never commits a partial inventory");
}
} // namespace

namespace loadbar {
struct CollectorDiscoveryTests {
    static void temperature_coalescing() {
        SessionPeaks peaks;
        DiskInventory disks;
        CpuSamples processors;
        DisplayContinuity continuity;
        Latest<Delivery> deliveries;
        Settings settings;
        // Start paused so this exercises real worker configuration without hardware reads.
        SamplingWorker worker(nullptr, WM_APP, deliveries, peaks, continuity, disks, processors,
                              settings, 1, 1, true);
        settings.show_temperatures = false;
        worker.configure(settings, true, false);
        settings.show_temperatures = true;
        worker.configure(settings, true, false);
        {
            std::lock_guard lock(worker.mutex_);
            require(worker.settings_.show_temperatures && worker.temperature_generation_ == 3 &&
                        worker.generation_ == 3,
                    "Coalesced off/on keeps a temperature reset even with unchanged final state");
            require(worker.reset_generation_ == 1 && worker.gpu_generation_ == 1 &&
                        worker.network_generation_ == 1,
                    "Temperature transitions cannot reset unrelated worker families");
        }
        settings.show_hover_info = false;
        worker.configure(settings, true, false);
        {
            std::lock_guard lock(worker.mutex_);
            require(worker.temperature_generation_ == 3,
                    "Unrelated presentation changes preserve temperature bindings");
        }
        worker.stop();
    }

    static void drive_visibility() {
        SessionPeaks peaks;
        DiskInventory inventory;
        CpuSamples processors;
        DisplayContinuity continuity;
        Discovery next;
        next.catalog.disks = {{DeviceKind::disk, L"a", L"Disk A", 0},
                              {DeviceKind::disk, L"b", L"Disk B", 1}};
        Collector collector(peaks, inventory, processors, [&] { return next; });
        Settings settings;
        collector.configure(settings);
        auto now = Clock::time_point{} + 1s;
        double rate = 100;
        unsigned io_calls{}, idle_calls{};
        const DiskCounterRead io = [&] {
            ++io_calls;
            Metric value{rate, Status::valid, Unit::bytes_per_second, now, 1s, {}};
            return std::vector<std::vector<CounterItem>>{{{L"0 C:", value}, {L"1 D:", value}},
                                                         {{L"0 C:", value}, {L"1 D:", value}}};
        };
        const DiskCounterRead idle = [&] {
            ++idle_calls;
            Metric value{25, Status::valid, Unit::percent, now, 1s, {}};
            return std::vector<std::vector<CounterItem>>{{{L"0 C:", value}, {L"1 D:", value}}};
        };
        Snapshot snapshot;
        const auto sample = [&] {
            collect_disk_readings(snapshot, collector.catalog().disks, now, io, idle, nullptr,
                                  settings.hidden_disks);
            peaks.observe(snapshot);
            continuity.observe(snapshot, settings);
        };
        sample();
        const auto first = now;
        const auto sentinel = now + 1000s;
        for (auto *source : {&collector.cpu_, &collector.disk_io_, &collector.disk_idle_,
                             &collector.gpu_engines_}) {
            source->retry_ = sentinel;
        }
        hide_disk(settings.hidden_disks, L"a");
        collector.configure(settings, false);
        require(collector.disk_io_.retry_ == sentinel && collector.disk_idle_.retry_ == sentinel,
                "Hiding one disk preserves shared queries for the visible disk");
        now += 1s;
        rate = 10000;
        sample();
        require(io_calls == 2 && idle_calls == 2 && snapshot.disks[1].read.value == rate &&
                    snapshot.disks[0].read.status == Status::unavailable &&
                    snapshot.disks[0].read.session_peak == 100 &&
                    presented_metric(snapshot.disks[0].read).value == 100 &&
                    presented_metric(snapshot.disks[0].read).timestamp == first,
                "Hidden disk ignores shared array values while preserving original observation and "
                "peak");
        collector.disk_io_.arrays_.resize(2);
        collector.disk_io_.arrays_[0].resize(100);
        collector.disk_io_.query_.buffer_.resize(4096);
        collector.disk_idle_.arrays_.resize(1);
        collector.disk_idle_.query_.buffer_.resize(4096);
        hide_disk(settings.hidden_disks, L"b");
        collector.configure(settings, false);
        require(collector.disk_io_.retry_ == Clock::time_point{} &&
                    collector.disk_idle_.retry_ == Clock::time_point{} &&
                    collector.cpu_.retry_ == sentinel && collector.gpu_engines_.retry_ == sentinel,
                "Hiding every disk resets only disk queries");
        require(collector.disk_io_.arrays_.capacity() == 0 &&
                    collector.disk_io_.query_.buffer_.capacity() == 0 &&
                    collector.disk_idle_.arrays_.capacity() == 0 &&
                    collector.disk_idle_.query_.buffer_.capacity() == 0,
                "All-hidden disks release raw and copied provider buffers");
        now += 1s;
        sample();
        collector.disk(snapshot, now);
        require(io_calls == 2 && idle_calls == 2 && !collector.disk_io_.open_ &&
                    !collector.disk_idle_.open_,
                "All hidden skips and closes both disk queries");
        next.catalog.disks.push_back({DeviceKind::disk, L"c", L"New drive", 2});
        collector.configure(settings, false);
        require(collector.disk_sampling_ && collector.catalog().disks.size() == 3 &&
                    !disk_hidden(settings.hidden_disks, L"c"),
                "Discovery continues and a new drive automatically resumes shared sampling");
        next.catalog.disks.pop_back();
        collector.configure(settings, false);
        show_disk(settings.hidden_disks, L"a");
        collector.configure(settings, false);
        require(collector.disk_sampling_ && !collector.disk_io_.open_ &&
                    collector.disk_io_.retry_ == Clock::time_point{},
                "Showing a drive reopens from unprimed query state");
        const DiskCounterRead warming = []() -> Result<std::vector<std::vector<CounterItem>>> {
            return std::unexpected(Error{L"Priming", ERROR_NOT_READY});
        };
        collect_disk_readings(snapshot, collector.catalog().disks, now, warming, warming, nullptr,
                              settings.hidden_disks);
        peaks.observe(snapshot);
        continuity.observe(snapshot, settings);
        require(snapshot.disks[0].read.status == Status::warming_up &&
                    presented_metric(snapshot.disks[0].read).value == 100 &&
                    snapshot.disks[0].read.session_peak == 100,
                "Query warmup after showing retains pre-hide reading and peak");
        now += 1s;
        rate = 20;
        sample();
        require(snapshot.disks[0].read.value == 20 && snapshot.disks[0].read.session_peak == 100 &&
                    snapshot.disks[1].read.session_peak == 10000,
                "Resumed device has independent original peak");
        next.disk_error = Error{L"Discovery failed", ERROR_GEN_FAILURE};
        next.catalog.disks.clear();
        collector.configure(settings, false);
        collector.disk(snapshot, now);
        require(snapshot.disks.size() == 2 && snapshot.disks[0].read.status == Status::error &&
                    snapshot.disks[1].read.detail == L"Hidden — sampling paused",
                "Discovery failure retains identities and preserves explicit hidden status");
        next.disk_error.reset();
        collector.configure(settings, false);
        next.catalog.disks = {{DeviceKind::disk, L"b", L"Reconnected B", 9}};
        collector.configure(settings, false);
        collector.disk(snapshot, now);
        require(!collector.disk_sampling_ && snapshot.disks[0].id == L"b" &&
                    snapshot.disks[0].read.detail == L"Hidden — sampling paused",
                "Stable hidden identity survives reconnect with a different disk number");
        snapshot = {};
        snapshot.disks.push_back({L"b", L"Hidden B"});
        snapshot.disks[0].read = {100, Status::valid, Unit::bytes_per_second, now, 1s, {}};
        require(next_stale_deadline(snapshot, 1000).has_value() &&
                    !next_stale_deadline(snapshot, 1000, settings) &&
                    !expire_snapshot(snapshot, now + 10s, 1000, settings),
                "Hidden disks do not schedule expiry or freshness repaint");
        snapshot.disks[0].read.status = Status::unavailable;
        snapshot.disks[0].read.retained = ObservedValue{100, now};
        require(next_retention_deadline(snapshot, now + 10s, 1000).has_value() &&
                    !next_retention_deadline(snapshot, now + 10s, 1000, settings),
                "Hidden retained disk observations do not schedule age updates");
    }

    static void visibility() {
        SessionPeaks peaks;
        DiskInventory disks;
        CpuSamples processors;
        DisplayContinuity continuity;
        Discovery next;
        next.catalog.gpus = {{DeviceKind::gpu, L"gpu", L"GPU", 10, 1000}};
        next.catalog.networks = {{DeviceKind::network, L"nic", L"NIC", 20}};
        next.catalog.disks = {{DeviceKind::disk, L"disk", L"Disk", 0}};
        Collector collector(peaks, disks, processors, [&] { return next; });
        Settings settings;
        settings.gpu_id = L"gpu";
        settings.network_id = L"nic";
        collector.configure(settings);
        auto now = Clock::time_point{} + 1s;
        unsigned reads{};
        InterfaceCounters counters{1000, 100, true};
        collector.network_provider_ = NetworkProvider(
            [&](std::uint64_t luid) -> Result<InterfaceCounters> {
                require(luid == 20, "Sample only selected interface");
                ++reads;
                return counters;
            },
            [&] { return now; });
        Snapshot snapshot;
        collector.network(snapshot, now);
        require(reads == 1 && snapshot.gauges[4].status == Status::warming_up,
                "Network initially primes a real baseline");
        now += 1s;
        counters.received += 200;
        counters.sent += 50;
        collector.network(snapshot, now);
        snapshot.gauges[4].identity = snapshot.gauges[5].identity = L"nic";
        peaks.observe(snapshot);
        continuity.observe(snapshot, settings);
        require(snapshot.gauges[4].value == 200 && snapshot.gauges[4].session_peak == 200,
                "Visible interface establishes its session peak");
        const auto sentinel = now + 1000s;
        for (auto *source :
             {&collector.cpu_, &collector.disk_io_, &collector.disk_idle_, &collector.gpu_engines_,
              &collector.gpu_memory_, &collector.gpu_shared_}) {
            source->retry_ = sentinel;
        }
        for (const bool enabled : {false, true}) {
            settings.show_temperatures = enabled;
            collector.configure(settings, false, false, false, true);
            for (auto *source :
                 {&collector.cpu_, &collector.disk_io_, &collector.disk_idle_,
                  &collector.gpu_engines_, &collector.gpu_memory_, &collector.gpu_shared_}) {
                require(source->retry_ == sentinel,
                        "Temperature switch leaves every utilization/rate counter primed");
            }
            now += 1s;
            counters.received += 20;
            collector.network(snapshot, now);
            snapshot.gauges[4].identity = snapshot.gauges[5].identity = L"nic";
            peaks.observe(snapshot);
            require(snapshot.gauges[4].status == Status::valid && snapshot.gauges[4].value == 20 &&
                        snapshot.gauges[4].session_peak == 200,
                    "Temperature switch preserves the network baseline and session peak");
        }
        const std::vector<CounterItem> gpu_rows{
            {L"pid_1_luid_0x00000000_0x0000000A_phys_0_eng_0_engtype_3D",
             {50, Status::valid, Unit::percent, now, 1s, {}}}};
        collector.gpu_samples_.read(gpu_rows, 10);
        require(collector.gpu_samples_.aggregate(10, L"3D", now).value == 50,
                "GPU scratch fixture establishes valid data");
        for (auto *source :
             {&collector.gpu_engines_, &collector.gpu_memory_, &collector.gpu_shared_}) {
            source->arrays_.push_back(gpu_rows);
            source->query_.buffer_.resize(4096);
            source->reset();
            require(!source->arrays_.empty() && source->query_.buffer_.capacity() >= 4096,
                    "Ordinary reset keeps reusable allocations");
        }
        settings.gpu_visible = false;
        collector.configure(settings, false);
        for (auto *source :
             {&collector.gpu_engines_, &collector.gpu_memory_, &collector.gpu_shared_}) {
            require(source->arrays_.capacity() == 0 && source->query_.buffer_.capacity() == 0,
                    "GPU Hide releases raw and copied counter arrays");
        }
        require(collector.gpu_samples_.cached_names() == 0 &&
                    collector.gpu_samples_.samples_.capacity() == 0 &&
                    collector.gpu_samples_.aggregation_.capacity() == 0,
                "GPU Hide releases parsed identities, sample storage and aggregation workspace");
        now += 1s;
        counters.received += 20;
        collector.network(snapshot, now);
        require(snapshot.gauges[4].status == Status::valid && snapshot.gauges[4].value == 20,
                "GPU hide does not re-prime network baseline");
        settings.network_visible = false;
        collector.configure(settings, false);
        require(collector.cpu_.retry_ == sentinel && collector.disk_io_.retry_ == sentinel &&
                    collector.disk_idle_.retry_ == sentinel &&
                    collector.gpu_engines_.retry_ == Clock::time_point{},
                "Visibility changes reset only their own family, preserving CPU/disk queries");
        // A hidden GPU must not even attempt opening PDH. Open state is observable without
        // hardware.
        const auto before = reads;
        Snapshot hidden;
        for (int tick = 0; tick < 4; ++tick) {
            now += 1s;
            collector.network(hidden, now);
            collector.gpu(hidden, now);
            continuity.observe(hidden, settings);
        }
        require(reads == before && !collector.gpu_engines_.open_ && !collector.gpu_memory_.open_ &&
                    !collector.gpu_shared_.open_ &&
                    collector.gpu_engines_.retry_ == Clock::time_point{} &&
                    hidden.gauges[1].detail == L"Hidden — sampling paused",
                "Hidden families do not read NIC or open GPU counters");
        settings.network_visible = true;
        collector.configure(settings, false);
        now += 1s;
        counters.received += 10000;
        Snapshot resumed;
        collector.network(resumed, now);
        resumed.gauges[4].identity = resumed.gauges[5].identity = L"nic";
        peaks.observe(resumed);
        continuity.observe(resumed, settings);
        require(resumed.gauges[4].status == Status::warming_up &&
                    presented_metric(resumed.gauges[4]).value == 200 &&
                    resumed.gauges[4].session_peak == 200,
                "Show re-primes rates and restores last valid reading and peak, excluding hidden "
                "traffic");
        now += 1s;
        counters.received += 40;
        collector.network(resumed, now);
        resumed.gauges[4].identity = resumed.gauges[5].identity = L"nic";
        peaks.observe(resumed);
        require(resumed.gauges[4].status == Status::valid && resumed.gauges[4].value == 40 &&
                    resumed.gauges[4].session_peak == 200,
                "Fresh visible interval resumes independently with original session scale");
    }

    static void cpu_continuity() {
        SessionPeaks peaks;
        DiskInventory disks;
        CpuSamples processors;
        DisplayContinuity continuity;
        Discovery next;
        next.catalog.processors = {{{0, 0}, 0, true, {}, 2}};
        const auto now = Clock::time_point{} + 1s;
        const Metric reading{73, Status::valid, Unit::percent, now, 1s, {}};
        Collector collector(peaks, disks, processors, [&] { return next; });
        collector.configure({});
        Snapshot initial;
        processors.apply(initial.processors, {{L"0,0", reading}, {L"1,2", reading}}, now);
        continuity.observe(initial);
        require(initial.processors.size() == 2 && !initial.processors.back().mapped,
                "Unmapped PDH processor remains honestly identified");
        next.catalog.processors.clear();
        next.cpu_error = Error{L"Injected topology failure", ERROR_GEN_FAILURE};
        collector.configure({}, false);
        require(collector.catalog().processors.size() == 1 && processors.topology().size() == 2 &&
                    collector.catalog().detail.find(L"Injected topology failure") !=
                        std::wstring::npos,
                "Failed topology scan preserves mapped and counter-only processor metadata");
        Snapshot missing;
        processors.apply(missing.processors, {}, now + 30s);
        continuity.observe(missing);
        for (const auto &cpu : missing.processors) {
            require(cpu.utilization.status == Status::unavailable &&
                        presented_metric(cpu.utilization).value == 73 &&
                        presented_metric(cpu.utilization).timestamp == now,
                    "Absent arrays preserve every known CPU reading and its original age");
        }
        require(missing.processors.size() == 2, "An empty counter array cannot remove CPU cells");
        {
            Collector retry(peaks, disks, processors, [&] { return next; });
            retry.configure({});
            // Force the real query-error branch without opening PDH or using hardware.
            retry.cpu_.retry_ = now + 1000s;
            retry.cpu_.last_error_ = {L"Injected CPU query failure", ERROR_GEN_FAILURE};
            retry.cpu(missing, now + 60s);
            continuity.observe(missing);
            require(missing.processors.size() == 2 &&
                        missing.processors.back().utilization.status == Status::error &&
                        presented_metric(missing.processors.back().utilization).value == 73,
                    "Worker Retry and query error retain fallback CPU identity and reading");
        }
        next.cpu_error.reset();
        next.catalog.processors = {{{0, 1}, 0, true, {}, 1}};
        collector.configure({}, false);
        require(processors.topology().size() == 1 &&
                    processors.topology()[0].id == ProcessorId{0, 1},
                "Successful changed topology establishes removal of old processor identities");
        processors.apply(missing.processors, {{L"0,1", reading}}, now + 90s);
        require(missing.processors.size() == 1 && missing.processors[0].utilization.value == 73,
                "Recovered topology accepts new samples");
        CpuSamples cold_processors;
        next.catalog.processors.clear();
        next.cpu_error = Error{L"Cold CPU topology failure", ERROR_NOT_READY};
        Collector cold(peaks, disks, cold_processors, [&] { return next; });
        cold.configure({});
        require(cold.catalog().processors.empty() &&
                    cold.catalog().detail.find(L"Cold CPU topology failure") != std::wstring::npos,
                "Cold topology failure remains an exposed diagnostic rather than false absence");
        cold_processors.apply(missing.processors, {{L"2,3", reading}}, now);
        continuity.observe(missing);
        cold.configure({}, false);
        cold_processors.apply(missing.processors, {}, now + 30s);
        continuity.observe(missing);
        require(missing.processors.size() == 1 && !missing.processors[0].mapped &&
                    presented_metric(missing.processors[0].utilization).value == 73,
                "Cold mapping failure can use and retain explicitly unmapped counter identities");
    }
    static void run() {
        const auto now = Clock::time_point{} + 1s;
        SessionPeaks peaks;
        DisplayContinuity continuity;
        DiskInventory inventory;
        CpuSamples processors;
        Discovery next;
        next.catalog.disks = {{DeviceKind::disk, L"disk-a", L"Disk 0", 0},
                              {DeviceKind::disk, L"disk-b", L"Disk 1", 1}};
        const auto devices = next.catalog.disks;
        Collector collector(peaks, inventory, processors, [&] { return next; });
        collector.configure({});
        require(inventory.devices == devices && collector.catalog().disks == devices,
                "Completed discovery commits the durable worker inventory");
        unsigned calls{};
        const Metric rate{100, Status::valid, Unit::bytes_per_second, now, 1s, {}};
        const Metric idle{25, Status::valid, Unit::percent, now, 1s, {}};
        const DiskCounterRead io = [&] {
            ++calls;
            return std::vector<std::vector<CounterItem>>{{{L"0 C:", rate}, {L"1 D:", rate}},
                                                         {{L"0 C:", rate}, {L"1 D:", rate}}};
        };
        const DiskCounterRead active = [&] {
            ++calls;
            return std::vector<std::vector<CounterItem>>{{{L"0 C:", idle}, {L"1 D:", idle}}};
        };
        const auto valid_storage = std::make_unique<Snapshot>();
        auto &valid = *valid_storage;
        collect_disk_readings(valid, devices, now, io, active);
        peaks.observe(valid);
        continuity.observe(valid);
        require(calls == 2 && valid.disks[0].active.value == 75, "Prime real disk interpretation");
        // Seed retry state without opening PDH. Failure must not collect or reset queries;
        // successful rediscovery must clear both sources' old retry/priming state.
        for (auto *source : {&collector.disk_io_, &collector.disk_idle_}) {
            source->retry_ = now + 1000s;
            source->delay_ = 8;
            source->last_error_ = {L"Counter unexpectedly sampled", ERROR_INVALID_DATA};
        }
        next.catalog.disks.clear();
        next.disk_error = Error{L"Injected disk discovery", ERROR_NOT_READY};
        collector.configure({}, false);
        require(inventory.devices == devices && collector.catalog().disks == devices &&
                    collector.catalog().detail.find(L"Injected disk discovery") !=
                        std::wstring::npos,
                "Failed discovery retains all disk identities and exposes the native failure");
        require(collector.disk_io_.delay_ == 8 && collector.disk_idle_.delay_ == 8,
                "An unchanged failed inventory does not rebuild disk queries");
        const auto failed_storage = std::make_unique<Snapshot>();
        auto &failed = *failed_storage;
        collector.disk(failed, now + 30s);
        require(failed.disks.size() == 2 && failed.disks[0].read.status == Status::error &&
                    failed.disks[0].read.detail.find(L"Injected disk discovery") !=
                        std::wstring::npos,
                "Production disk path publishes discovery errors, including ERROR_NOT_READY");
        if (!collector.disk_discovery_error_) {
            throw std::runtime_error("Expected the collector's discovery error");
        }
        collect_disk_readings(failed, collector.catalog().disks, now + 30s, io, active,
                              &*collector.disk_discovery_error_);
        require(calls == 2, "Untrusted discovery never invokes either disk counter source");
        peaks.observe(failed);
        continuity.observe(failed);
        for (std::size_t i = 0; i < devices.size(); ++i) {
            for (const auto *metric :
                 {&failed.disks[i].active, &failed.disks[i].read, &failed.disks[i].write}) {
                const auto shown = presented_metric(*metric);
                require(metric->status == Status::error && shown.status == Status::valid &&
                            shown.timestamp == now &&
                            shown.value == (metric->unit == Unit::percent ? 75 : 100),
                        "Discovery failures retain values and original observation age");
            }
            require(failed.disks[i].read.session_peak == valid.disks[i].read.session_peak &&
                        failed.disks[i].read.identity == devices[i].id,
                    "Failure never updates peaks or changes disk identity");
        }
        {
            Collector retry(peaks, inventory, processors, [&] { return next; });
            retry.configure({});
            Snapshot after_retry;
            retry.disk(after_retry, now + 60s);
            continuity.observe(after_retry);
            require(after_retry.disks.size() == 2 &&
                        presented_metric(after_retry.disks[0].read).value == 100,
                    "New collector after worker Retry preserves known disks through failure");
        }
        next.disk_error.reset();
        next.catalog.disks = devices;
        collector.configure({}, false);
        require(collector.disk_io_.retry_ == Clock::time_point{} &&
                    collector.disk_idle_.retry_ == Clock::time_point{} &&
                    collector.disk_io_.delay_ == 1 && collector.disk_idle_.delay_ == 1,
                "Recovery re-primes both queries even with identical device metadata");
        collect_disk_readings(failed, devices, now + 90s, io, active);
        require(failed.disk_discovery.status == Status::unavailable && calls == 4 &&
                    failed.disks[0].read.status == Status::valid,
                "Recovered collection clears the discovery error and resumes counters");
        next.catalog.disks.clear();
        collector.configure({}, false);
        require(inventory.devices.empty() && collector.catalog().disks.empty(),
                "Successful empty enumeration commits real removal");
        next.disk_error = Error{L"Cold disk discovery", ERROR_ACCESS_DENIED};
        // Keep the second large provider fixture off the test runner's stack.
        auto cold = std::make_unique<Collector>(peaks, inventory, processors, [&] { return next; });
        cold->configure({});
        Snapshot absent;
        cold->disk(absent, now + 120s);
        continuity.observe(absent);
        Layout::Block block;
        block.kind = 3;
        const auto placeholder = block_metric(block, Gauge::disk_read, absent);
        require(absent.disks.empty() && placeholder.status == Status::error &&
                    placeholder.detail.find(L"Cold disk discovery") != std::wstring::npos &&
                    placeholder.unit == Unit::bytes_per_second,
                "Cold failure is visible as error and cannot resurrect confirmed-removed disks");
    }
};
} // namespace loadbar

void discovery_tests() {
    enumeration_tests();
    topology_api_tests();
    loadbar::CollectorDiscoveryTests::run();
    loadbar::CollectorDiscoveryTests::visibility();
    loadbar::CollectorDiscoveryTests::drive_visibility();
    loadbar::CollectorDiscoveryTests::cpu_continuity();
    loadbar::CollectorDiscoveryTests::temperature_coalescing();
}
