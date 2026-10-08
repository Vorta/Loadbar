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
        Snapshot valid;
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
        Snapshot failed;
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
        Collector cold(peaks, inventory, processors, [&] { return next; });
        cold.configure({});
        Snapshot absent;
        cold.disk(absent, now + 120s);
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
    loadbar::CollectorDiscoveryTests::cpu_continuity();
}
