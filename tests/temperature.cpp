#include "telemetry/temperature.hpp"
#include "model/continuity.hpp"
#include "model/layout.hpp"
#include "platform/windows.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <winioctl.h>

void vendor_temperature_tests();
namespace {
void check(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
bool same_box(loadbar::Box a, loadbar::Box b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
void asus_tests() {
    using namespace loadbar;
    const auto start = Clock::now();
    const auto at = [&](int seconds) { return start + std::chrono::seconds(seconds); };
    std::array<std::uint32_t, 4> raw{0x10060, 0, 0, 0};
    auto bytes = std::as_bytes(std::span(raw));
    check(parse_asus_temperature(bytes, start).metric.value == 96, "ASUS Celsius encoding");
    check(parse_asus_temperature(bytes.first(3), start).metric.status == Status::error,
          "ASUS short response rejected");
    for (const auto value : {0U, 0x10000U, 0x20060U, 0xffffffffU}) {
        raw[0] = value;
        check(parse_asus_temperature(bytes, start).metric.status == Status::unavailable,
              "ASUS missing or unknown firmware encoding is not a reading");
    }
    raw[0] = 0x10097;
    check(parse_asus_temperature(bytes, start).metric.status == Status::error,
          "ASUS temperature sanity bound");
    unsigned queries{}, opens{}, cancellations{}, drains{};
    bool asynchronous{}, cancelled{}, done{}, missing{}, cancel_delayed{};
    void *output{};
    AsusTemperatureIo io;
    io.open = [&] {
        ++opens;
        if (missing) {
            SetLastError(ERROR_FILE_NOT_FOUND);
            return INVALID_HANDLE_VALUE;
        }
        return CreateEventW(nullptr, TRUE, FALSE, nullptr);
    };
    const auto write = [&](DWORD *size) {
        raw[0] = 0x10060;
        std::memcpy(output, raw.data(), sizeof(raw));
        *size = sizeof(raw);
        return TRUE;
    };
    io.query = [&](HANDLE, DWORD control, void *input, DWORD size, void *buffer, DWORD capacity,
                   DWORD *returned, OVERLAPPED *) {
        const std::array<std::uint32_t, 4> expected{0x53545344, 8, 0x00120094, 0};
        check(control == 0x0022240c && size == sizeof(expected) &&
                  std::memcmp(input, expected.data(), size) == 0 && capacity == 16,
              "Only the reviewed read-only ASUS request is issued");
        ++queries;
        output = buffer;
        cancelled = false;
        if (asynchronous) {
            SetLastError(ERROR_IO_PENDING);
            return FALSE;
        }
        return write(returned);
    };
    io.complete = [&](HANDLE, OVERLAPPED *, DWORD *size, BOOL wait) {
        if (wait) {
            ++drains;
            check(cancelled, "ASUS shutdown cancels before draining");
        }
        if (cancelled && !cancel_delayed) {
            SetLastError(ERROR_OPERATION_ABORTED);
            return FALSE;
        }
        if (!done) {
            SetLastError(ERROR_IO_INCOMPLETE);
            return FALSE;
        }
        return write(size);
    };
    io.cancel = [&](HANDLE, OVERLAPPED *) {
        ++cancellations;
        cancelled = true;
        return TRUE;
    };
    {
        AsusTemperature cpu(io);
        check(cpu.sample(start, 250).metric.value == 96, "ASUS production request parsing");
        static_cast<void>(cpu.sample(start + std::chrono::milliseconds(250), 250));
        check(queries == 1 && opens == 1, "ASUS handle cached and cadence capped");
        asynchronous = true;
        static_cast<void>(cpu.sample(at(1), 1000));
        static_cast<void>(cpu.sample(at(2), 1000));
        check(queries == 2 && cancellations == 0, "ASUS pending requests never overlap");
        done = true;
        check(cpu.sample(at(2), 1000).metric.timestamp == at(1),
              "ASUS delayed completion retains observation age");
        done = false;
        static_cast<void>(cpu.sample(at(3), 1000));
        check(cpu.sample(at(5), 1000).metric.status == Status::error && cancellations == 1,
              "ASUS timeout is cancelled and reported");
        static_cast<void>(cpu.sample(at(6), 1000));
        static_cast<void>(cpu.sample(at(7), 1000));
        check(queries == 3, "ASUS cancellation completion does not bypass timeout backoff");
        cpu.reset();
        static_cast<void>(cpu.sample(at(8), 1000));
        cpu.reset();
        static_cast<void>(cpu.sample(at(9), 1000));
        check(queries == 4, "ASUS reset reaps pending request before rebinding");
        static_cast<void>(cpu.sample(at(10), 1000));
    }
    check(drains == 1, "ASUS pending buffers survive shutdown drain");
    {
        AsusTemperature cpu(io);
        cpu.set_enabled(false);
        auto before_opens = opens;
        auto before_queries = queries;
        static_cast<void>(cpu.sample(at(20), 1000));
        check(opens == before_opens && queries == before_queries,
              "Disabled ASUS cold start does not even open its interface");
        cpu.set_enabled(true);
        static_cast<void>(cpu.sample(at(21), 1000));
        check(queries == before_queries + 1, "Enabled ASUS resumes collection");
        cancel_delayed = true;
        cpu.set_enabled(false);
        before_opens = opens;
        before_queries = queries;
        static_cast<void>(cpu.sample(at(22), 1000));
        cpu.set_enabled(true);
        static_cast<void>(cpu.sample(at(23), 1000));
        check(opens == before_opens && queries == before_queries,
              "ASUS re-enable waits for cancelled I/O buffers to finish");
        cancel_delayed = false;
        static_cast<void>(cpu.sample(at(24), 1000));
        static_cast<void>(cpu.sample(at(25), 1000));
        check(opens == before_opens + 1 && queries == before_queries + 1,
              "ASUS resumes promptly after cancellation completes");
        cpu.set_enabled(false);
        static_cast<void>(cpu.sample(at(26), 1000));
        before_queries = queries;
        static_cast<void>(cpu.sample(at(60), 1000));
        check(queries == before_queries, "Paused ASUS never restarts after draining");
    }
    missing = true;
    AsusTemperature absent(io);
    const auto previous_opens = opens;
    const auto &unavailable = absent.sample(start, 1000).metric;
    check(unavailable.status == Status::unavailable,
          "Absent ASUS driver is unavailable without elevation");
    check(unavailable.detail ==
              error_text({L"CPU temperature is not available", ERROR_FILE_NOT_FOUND}),
          "Missing CPU interface uses generic wording and preserves the native error");
    check(Snapshot{}.cpu_temperature.metric.detail == L"CPU temperature is not available",
          "Initial CPU temperature availability message is vendor-neutral");
    static_cast<void>(absent.sample(at(1), 1000));
    check(opens == previous_opens + 1, "ASUS absent interface backoff");
}
void provider_tests() {
    using namespace loadbar;
    unsigned queries{}, gpu_queries{}, opens{}, cancellations{}, cpu_opens{}, power_checks{};
    bool sleeping{}, asynchronous{}, cancelled{}, complete{}, fail_open{}, fail_query{},
        fail_power{}, cancel_delayed{};
    HANDLE last_handle{};
    void *pending_buffer{};
    DWORD pending_size{};
    std::map<HANDLE, std::wstring> identities;
    TemperatureIo io;
    io.cpu.open = [&] {
        ++cpu_opens;
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    };
    io.open_disk = [&](const Device &device) {
        ++opens;
        if (fail_open) {
            SetLastError(ERROR_ACCESS_DENIED);
            return INVALID_HANDLE_VALUE;
        }
        const auto handle = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        identities[handle] = device.id;
        return handle;
    };
    io.power_state = [&](HANDLE, BOOL *awake) {
        ++power_checks;
        if (fail_power) {
            SetLastError(ERROR_DEVICE_NOT_CONNECTED);
            return FALSE;
        }
        *awake = sleeping ? FALSE : TRUE;
        return TRUE;
    };
    const auto write = [&](void *buffer, DWORD size, DWORD *bytes) {
        STORAGE_TEMPERATURE_DATA_DESCRIPTOR descriptor{};
        descriptor.Size = descriptor.Version = sizeof(descriptor);
        descriptor.InfoCount = 1;
        descriptor.TemperatureInfo[0].Temperature = 49;
        check(size >= sizeof(descriptor), "Query buffer fits storage descriptor");
        std::memcpy(buffer, &descriptor, sizeof(descriptor));
        *bytes = sizeof(descriptor);
        return TRUE;
    };
    io.query_disk = [&](HANDLE handle, void *buffer, DWORD size, DWORD *bytes, OVERLAPPED *) {
        check(identities.contains(handle), "Temperature queries use an opened identity");
        ++queries;
        last_handle = handle;
        if (fail_query) {
            SetLastError(ERROR_DEVICE_NOT_CONNECTED);
            return FALSE;
        }
        if (asynchronous) {
            pending_buffer = buffer;
            pending_size = size;
            cancelled = false;
            SetLastError(ERROR_IO_PENDING);
            return FALSE;
        }
        return write(buffer, size, bytes);
    };
    io.complete_disk = [&](HANDLE, OVERLAPPED *, DWORD *bytes, BOOL wait) {
        check(!wait || cancelled, "Shutdown cancels before draining");
        if (cancelled && !cancel_delayed) {
            SetLastError(ERROR_OPERATION_ABORTED);
            return FALSE;
        }
        if (!complete) {
            SetLastError(ERROR_IO_INCOMPLETE);
            return FALSE;
        }
        return write(pending_buffer, pending_size, bytes);
    };
    io.cancel_disk = [&](HANDLE, OVERLAPPED *) {
        ++cancellations;
        cancelled = true;
        return TRUE;
    };
    io.read_gpu = [&](const Device &device, Clock::time_point now) {
        ++gpu_queries;
        check(device.id == L"gpu-a", "GPU query uses selected device identity");
        const std::array sensors{TemperatureSensor{0, 58}};
        return choose_temperature(sensors, false, now);
    };
    Catalog catalog;
    Device disk;
    disk.kind = DeviceKind::disk;
    disk.id = L"disk-a";
    disk.device_path = L"fake interface A";
    catalog.disks.push_back(disk);
    disk.id = L"disk-b";
    disk.device_path = L"fake interface B";
    catalog.disks.push_back(disk);
    Device gpu;
    gpu.kind = DeviceKind::gpu;
    gpu.id = L"gpu-a";
    Settings settings;
    settings.interval_ms = 250;
    Snapshot snapshot;
    snapshot.disks = {{L"disk-a", L"A"}, {L"disk-b", L"B"}};
    const auto start = Clock::now();
    const auto at = [&](int seconds) { return start + std::chrono::seconds(seconds); };
    TemperatureProviders provider(io);
    settings.show_temperatures = false;
    provider.configure(catalog, gpu, settings, true, false);
    provider.sample(snapshot, start);
    check(queries == 0 && gpu_queries == 0 && opens == 0 && cpu_opens == 0 && power_checks == 0 &&
              provider.disk_request_count() == 0,
          "Disabled cold start allocates no drive requests and makes no sensor calls");
    settings.show_temperatures = true;
    provider.configure(catalog, gpu, settings, true, false);
    provider.sample(snapshot, start);
    check(queries == 2 && gpu_queries == 1 && opens == 2,
          "All visible drives and selected GPU collected");
    provider.sample(snapshot, start + std::chrono::milliseconds(250));
    check(queries == 2 && gpu_queries == 1, "Temperature cadence capped at one Hz");
    sleeping = true;
    provider.sample(snapshot, at(1));
    check(queries == 2 && gpu_queries == 2 &&
              snapshot.disks[0].temperature.metric.status == Status::unavailable,
          "Sleeping drives skipped without blocking GPU");
    sleeping = false;
    settings.hidden_disks = {L"disk-b"};
    settings.gpu_visible = false;
    provider.configure(catalog, gpu, settings, false, false, true);
    check(provider.disk_request_count() == 1,
          "Idle hidden drive releases its request buffer immediately");
    provider.sample(snapshot, at(2));
    check(queries == 3 && gpu_queries == 2 &&
              snapshot.disks[1].temperature.metric.status != Status::valid,
          "Hidden families are not collected");
    provider.configure(catalog, gpu, settings, false, true);
    provider.sample(snapshot, at(3));
    check(queries == 3, "Failed disk rediscovery pauses temperature bindings");
    settings.hidden_disks.clear();
    provider.configure(catalog, gpu, settings, false, false);
    provider.sample(snapshot, at(4));
    check(queries == 5, "Recovery and newly visible disks resume");

    settings.hidden_disks = {L"disk-b"};
    asynchronous = true;
    provider.configure(catalog, gpu, settings, true, false);
    provider.sample(snapshot, at(5));
    check(queries == 6 && snapshot.disks[0].temperature.metric.status == Status::warming_up,
          "Pending temperature request preserves availability status");
    provider.sample(snapshot, at(6));
    check(queries == 6 && cancellations == 0, "A slow disk does not overlap its requests");
    complete = true;
    provider.sample(snapshot, at(6));
    check(snapshot.disks[0].temperature.metric.value == 49 &&
              snapshot.disks[0].temperature.metric.timestamp == at(5),
          "Delayed completion uses original query age");
    complete = false;
    provider.sample(snapshot, at(7));
    provider.sample(snapshot, at(9));
    check(cancellations == 1 && snapshot.disks[0].temperature.metric.status == Status::error,
          "Timed out disk request is cancelled");
    provider.sample(snapshot, at(10));
    const auto before = queries;
    provider.sample(snapshot, at(11));
    check(queries == before, "Timeout backs off instead of retry storm");
    fail_open = true;
    provider.configure(catalog, gpu, settings, true, false);
    provider.sample(snapshot, at(12));
    check(snapshot.disks[0].temperature.metric.status == Status::error,
          "Access failure is reported");
    fail_open = false;
    asynchronous = false;
    fail_query = true;
    provider.sample(snapshot, at(42));
    const auto failed_opens = opens;
    fail_query = false;
    provider.sample(snapshot, at(44));
    check(opens == failed_opens + 1 && snapshot.disks[0].temperature.metric.status == Status::valid,
          "Disconnected drive handle is reopened without identity or catalog changes");
    fail_power = true;
    provider.sample(snapshot, at(45));
    const auto power_opens = opens;
    fail_power = false;
    provider.sample(snapshot, at(75));
    check(opens == power_opens + 1 && snapshot.disks[0].temperature.metric.status == Status::valid,
          "Power query failure discards a stale drive handle");
    DisplayContinuity continuity;
    continuity.observe(snapshot, settings);
    settings.show_temperatures = false;
    provider.configure(catalog, gpu, settings, false, false);
    check(provider.disk_request_count() == 0,
          "Disabling releases every completed drive request buffer");
    const auto before_disabled = std::array{queries, gpu_queries, opens, cpu_opens, power_checks};
    for (int tick = 76; tick < 96; ++tick) {
        provider.sample(snapshot, at(tick));
        continuity.observe(snapshot, settings);
    }
    check(before_disabled == std::array{queries, gpu_queries, opens, cpu_opens, power_checks},
          "Disabled temperature providers issue no retries, opens, power checks or reads");
    const auto &retained = snapshot.disks[0].temperature.metric.retained;
    check(snapshot.disks[0].temperature.metric.status == Status::unavailable && retained &&
              retained->timestamp == at(75),
          "Paused temperature retains its original observation age");
    settings.show_temperatures = true;
    asynchronous = true;
    provider.configure(catalog, gpu, settings, false, false);
    provider.sample(snapshot, at(100));
    const auto pending_queries = queries;
    const auto pending_handle = last_handle;
    cancel_delayed = true;
    settings.show_temperatures = false;
    provider.configure(catalog, gpu, settings, false, false);
    provider.sample(snapshot, at(101));
    check(provider.disk_request_count() == 1 && queries == pending_queries,
          "Disabled drive owns pending cancelled buffer until completion");
    settings.show_temperatures = true;
    provider.configure(catalog, gpu, settings, false, false);
    provider.sample(snapshot, at(102));
    check(queries == pending_queries, "Rapid re-enable cannot overlap cancelled drive I/O");
    cancel_delayed = false;
    provider.sample(snapshot, at(103));
    DWORD flags{};
    check(!GetHandleInformation(pending_handle, &flags),
          "Cancelled drive handle closes as soon as its completion is drained");
    provider.sample(snapshot, at(104));
    check(queries == pending_queries + 1,
          "Re-enabled drive resumes promptly without waiting for rediscovery");
    cancel_delayed = true;
    settings.show_temperatures = false;
    provider.configure(catalog, gpu, settings, false, false);
    check(provider.disk_request_count() == 1, "Pending drive request remains owned");
    cancel_delayed = false;
    provider.sample(snapshot, at(105));
    check(provider.disk_request_count() == 0,
          "Disabled request is freed on completion without waiting for rediscovery");
}
void fallback_tests() {
    using namespace loadbar;
    const auto start = Clock::now();
    const auto at = [&](int seconds) { return start + std::chrono::seconds(seconds); };
    unsigned windows_reads{}, vendor_reads{};
    bool windows_valid{}, vendor_valid{true};
    const auto reading = [](Clock::time_point now, bool valid, std::wstring_view sensor) {
        TemperatureReading value;
        value.metric = {67, valid ? Status::valid : Status::unavailable, Unit::celsius, now};
        value.sensor = sensor;
        return value;
    };
    TemperatureIo io;
    io.cpu.open = [] {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    };
    io.read_gpu = [&](const Device &, Clock::time_point now) {
        ++windows_reads;
        return reading(now, windows_valid, L"Windows main");
    };
    io.read_vendor_gpu = [&](const Device &, Clock::time_point now) {
        ++vendor_reads;
        return reading(now, vendor_valid, L"Vendor GPU core");
    };
    TemperatureProviders provider(io);
    Device gpu;
    gpu.id = L"gpu-a";
    Settings settings;
    settings.interval_ms = 250;
    Snapshot snapshot;
    snapshot.gpu_id = gpu.id;
    DisplayContinuity continuity;
    provider.configure({}, gpu, settings, true, false);
    provider.sample(snapshot, start);
    check(windows_reads == 1 && vendor_reads == 1 &&
              snapshot.gpu_temperature.metric.status == Status::valid,
          "Vendor fallback follows unsuccessful Windows read");
    provider.sample(snapshot, start + std::chrono::milliseconds(250));
    provider.sample(snapshot, at(1));
    check(windows_reads == 1 && vendor_reads == 2,
          "Working vendor source cached without repeated Windows queries");
    vendor_valid = false;
    windows_valid = true;
    provider.sample(snapshot, at(2));
    check(windows_reads == 2 && vendor_reads == 3 &&
              snapshot.gpu_temperature.sensor == L"Windows main",
          "Failed vendor falls back to recovered Windows source");
    continuity.observe(snapshot);
    windows_valid = false;
    provider.sample(snapshot, at(3));
    continuity.observe(snapshot);
    check(snapshot.gpu_temperature.metric.retained &&
              snapshot.gpu_temperature.metric.retained->timestamp == at(2) &&
              snapshot.gpu_temperature.sensor == L"Windows main",
          "Exhausted fallbacks retain original source and age through continuity");
    provider.sample(snapshot, at(4));
    check(windows_reads == 3 && vendor_reads == 4, "Failed sources back off");
    settings.gpu_visible = false;
    provider.configure({}, gpu, settings, false, false);
    provider.sample(snapshot, at(6));
    check(windows_reads == 3 && vendor_reads == 4, "Hidden GPU pauses both sources");
    settings.gpu_visible = true;
    windows_valid = true;
    provider.configure({}, gpu, settings, false, false);
    provider.sample(snapshot, at(7));
    check(windows_reads == 4 && vendor_reads == 4, "Re-enable tries Windows first");
    settings.interval_ms = 5000;
    provider.configure({}, gpu, settings, true, false);
    provider.sample(snapshot, at(8));
    provider.sample(snapshot, at(9));
    check(windows_reads == 5 && vendor_reads == 4, "Configured slower cadence respected");
    settings.show_temperatures = false;
    provider.configure({}, gpu, settings, false, false);
    provider.sample(snapshot, at(30));
    check(windows_reads == 5 && vendor_reads == 4,
          "Global temperature switch pauses Windows and vendor GPU sources");
    settings.show_temperatures = true;
    provider.configure({}, gpu, settings, false, false);
    provider.sample(snapshot, at(31));
    check(windows_reads == 6 && vendor_reads == 4,
          "Global re-enable restarts GPU temperature selection with Windows first");
}
} // namespace
void temperature_tests() {
    vendor_temperature_tests();
    asus_tests();
    provider_tests();
    fallback_tests();
    using namespace loadbar;
    const auto now = Clock::now();
    const std::array sensors{TemperatureSensor{1, 74}, TemperatureSensor{0, 49},
                             TemperatureSensor{2, 65}};
    const auto drive = choose_temperature(sensors, true, now);
    check(drive.metric.value == 49 && temperature_visible(drive), "Drive prefers composite sensor");
    const auto gpu = choose_temperature(sensors, false, now);
    check(gpu.metric.value == 74, "Linked GPU chooses hottest main sensor");
    {
        Snapshot timers;
        timers.cpu_temperature = timers.ram_temperature = timers.gpu_temperature = gpu;
        timers.disks = {{L"drive", L"Drive"}};
        timers.disks[0].temperature = drive;
        Settings disabled;
        disabled.show_temperatures = false;
        check(next_stale_deadline(timers, 1000).has_value() &&
                  !next_stale_deadline(timers, 1000, disabled),
              "Disabled temperatures cannot schedule staleness wakeups");
        check(!expire_snapshot(timers, now + std::chrono::seconds(10), 1000, disabled),
              "Disabled temperature status transitions do not trigger UI work");
        DisplayContinuity retention;
        retention.observe(timers);
        set_snapshot_status(timers, Status::error, L"Injected failure");
        check(next_retention_deadline(timers, now + std::chrono::seconds(10), 1000).has_value() &&
                  !next_retention_deadline(timers, now + std::chrono::seconds(10), 1000, disabled),
              "Disabled retained temperatures do not schedule age updates");
    }
    check(choose_temperature({}, true, now).metric.status == Status::unavailable,
          "Empty sensor array is unavailable");
    const std::array duplicate{TemperatureSensor{0, 12}, TemperatureSensor{0, 13}};
    check(choose_temperature(duplicate, true, now).metric.status == Status::error,
          "Duplicate sensor IDs rejected");
    check(!valid_temperature(std::numeric_limits<double>::quiet_NaN()) && !valid_temperature(1000),
          "Temperature sanity checks");
    check(valid_temperature(-10) && valid_temperature(0), "Signed and zero temperature accepted");
    TemperatureReading warm;
    warm.metric.status = Status::warming_up;
    check(!temperature_visible(warm), "Temperature warmup must not synthesize zero");
    const Metric cold{-10, Status::valid, Unit::celsius};
    check(metric_text(cold).find(L"-10.0°C") != std::wstring::npos, "Signed Celsius formatting");
    constexpr std::array<std::array<double, 3>, 4> bands{
        {{70, 80, 90}, {55, 65, 75}, {65, 75, 85}, {50, 60, 70}}};
    for (unsigned component = 0; component < bands.size(); ++component) {
        check(temperature_band(component, bands[component][0] - 0.1) == ThermalBand::normal,
              "Normal thermal band");
        for (unsigned band = 0; band < 3; ++band) {
            check(temperature_band(component, bands[component][band]) ==
                      static_cast<ThermalBand>(band + 1),
                  "Thermal threshold boundary");
        }
    }
    STORAGE_TEMPERATURE_DATA_DESCRIPTOR descriptor{};
    descriptor.Version = sizeof(descriptor);
    descriptor.Size = sizeof(descriptor);
    descriptor.InfoCount = 1;
    descriptor.WarningTemperature = 70;
    descriptor.CriticalTemperature = 85;
    descriptor.TemperatureInfo[0].Temperature = 49;
    auto bytes = std::as_bytes(std::span(&descriptor, 1));
    auto parsed = parse_storage_temperature(bytes, now);
    check(parsed.metric.value == 49 && parsed.warning == 70 && parsed.critical == 85,
          "Windows descriptor parsing");
    check(parse_storage_temperature(bytes.first(5), now).metric.status == Status::error,
          "Truncated storage header");
    descriptor.InfoCount = 128;
    check(parse_storage_temperature(bytes, now).metric.status == Status::error,
          "Hostile storage sensor count");
    descriptor.InfoCount = 1;
    descriptor.Size += 4;
    check(parse_storage_temperature(bytes, now).metric.status == Status::error,
          "Truncated storage payload");
    descriptor.Size = sizeof(descriptor);
    descriptor.TemperatureInfo[0].Temperature = std::numeric_limits<SHORT>::min();
    check(parse_storage_temperature(bytes, now).metric.status == Status::unavailable,
          "Missing storage sentinel is not a temperature");
    descriptor.TemperatureInfo[0].Temperature = -8;
    check(parse_storage_temperature(bytes, now).metric.value == -8,
          "Storage temperature preserves sign");

    Snapshot snapshot;
    snapshot.gpu_id = L"gpu-a";
    snapshot.gpu_temperature = gpu;
    snapshot.disks.push_back({L"drive-a", L"Drive A"});
    snapshot.disks[0].temperature = drive;
    DisplayContinuity continuity;
    continuity.observe(snapshot);
    snapshot.gpu_temperature = warm;
    snapshot.disks[0].temperature = {};
    continuity.observe(snapshot);
    check(temperature_visible(snapshot.gpu_temperature) &&
              snapshot.gpu_temperature.metric.retained &&
              snapshot.gpu_temperature.metric.retained->value == 74,
          "GPU last-valid retention");
    check(snapshot.gpu_temperature.sensor == gpu.sensor &&
              snapshot.gpu_temperature.metric.retained &&
              snapshot.gpu_temperature.metric.retained->timestamp == now,
          "Temperature scope and age retained");
    check(snapshot.disks[0].temperature.metric.retained &&
              snapshot.disks[0].temperature.metric.retained->value == 49,
          "Drive failure continuity");
    snapshot.gpu_id = L"gpu-b";
    snapshot.gpu_temperature = {};
    continuity.observe(snapshot);
    check(!temperature_visible(snapshot.gpu_temperature), "GPU selection cannot leak temperature");
    snapshot.gpu_id = L"gpu-a";
    continuity.observe(snapshot);
    check(temperature_visible(snapshot.gpu_temperature), "GPU reconnect retains observation");

    Settings settings;
    Snapshot fixture;
    for (unsigned i = 0; i < 8; ++i) {
        fixture.processors.push_back(
            {{0, i}, i / 2, true, {30, Status::valid, Unit::percent, now}});
    }
    fixture.disks = {{L"drive-a", L"A"}, {L"drive-b", L"B"}};
    for (auto edge : {Edge::top, Edge::bottom, Edge::left, Edge::right}) {
        for (float thickness : {40.0F, 60.0F, 200.0F}) {
            const bool horizontal = edge == Edge::top || edge == Edge::bottom;
            const float width = horizontal ? 1400 : thickness;
            const float height = horizontal ? thickness : 1400;
            const auto before = make_snapshot_layout(width, height, edge, fixture, 1, settings);
            auto current = fixture;
            current.gpu_temperature = gpu;
            current.disks[0].temperature = drive;
            const auto after = make_snapshot_layout(width, height, edge, current, 1, settings);
            check(before.fits && after.fits, "Temperature layout fits all edges");
            check(before.cores.size() == 8 && after.cores.size() == 8,
                  "Temperature keeps every CPU thread");
            for (std::size_t b = 0; b < before.blocks.size(); ++b) {
                const auto &old = before.blocks[b];
                const auto &changed = after.blocks[b];
                const bool visible = changed.kind == 2 || (changed.kind == 3 && changed.disk == 0);
                check(same_box(old.graphic, changed.graphic), "Temperature does not resize meters");
                if (visible) {
                    check(changed.icon.y < old.icon.y && changed.icon.width == old.icon.width &&
                              changed.icon.height == old.icon.height,
                          "Icon moves up without scaling");
                    check(changed.temperature.width > 0 &&
                              changed.temperature.y >= changed.icon.y + changed.icon.height,
                          "Temperature below icon");
                    check(changed.bounds.x >= 0 && changed.bounds.y >= 0 &&
                              changed.bounds.x + changed.bounds.width <= width + 0.01F &&
                              changed.bounds.y + changed.bounds.height <= height + 0.01F,
                          "Temperature bounds fit client");
                    check(metric_tooltip(after, changed.temperature.x + 1,
                                         changed.temperature.y + 1, current, settings, now)
                                  .find(L"Temperature:") != std::wstring::npos,
                          "Temperature hit target and details");
                } else {
                    check(same_box(old.icon, changed.icon) && changed.temperature.width == 0,
                          "Never-observed and network icons unchanged");
                }
            }
        }
    }
}
