#include "telemetry/temperature.hpp"
#include "platform/windows.hpp"
#include "telemetry/vendor_temperature.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <limits>
#include <map>
#include <winioctl.h>
#include <winternl.h>

// The graphics thunk header requires NTSTATUS from winternl.h first.
#include <d3dkmthk.h>

namespace loadbar {
namespace {
TemperatureReading missing(Clock::time_point now, std::wstring text,
                           Status status = Status::unavailable) {
    TemperatureReading result;
    result.metric = {0, status, Unit::celsius, now, {}, std::move(text)};
    return result;
}
TemperatureReading failed(Clock::time_point now, std::wstring_view operation, DWORD code) {
    return missing(now, error_text({std::wstring(operation), code}),
                   code == ERROR_NOT_SUPPORTED || code == ERROR_INVALID_FUNCTION
                       ? Status::unavailable
                       : Status::error);
}
struct DiskRequest {
    explicit DiskRequest(const TemperatureIo &functions) : io(functions) {}
    const TemperatureIo &io; // State owns callbacks and outlives every pending request.
    Device device;
    Handle handle, event;
    OVERLAPPED overlap{};
    std::array<std::byte, 4096> buffer{};
    bool enabled{}, pending{}, cancelled{};
    Clock::time_point next{}, started{};
    unsigned failures{};
    TemperatureReading reading;
    ~DiskRequest() {
        if (pending) {
            // The OVERLAPPED and buffers must outlive completion, including failed cancellation.
            // This runs on the joined worker, after the UI has released its reservation.
            io.cancel_disk(handle.get(), &overlap);
            DWORD ignored{};
            io.complete_disk(handle.get(), &overlap, &ignored, TRUE);
        }
    }
    void cancel() noexcept {
        if (pending && !cancelled) {
            io.cancel_disk(handle.get(), &overlap);
            cancelled = true;
        }
    }
    void finish(Clock::time_point now, DWORD bytes, DWORD error, unsigned interval) {
        pending = false;
        reading =
            error == ERROR_SUCCESS
                ? parse_storage_temperature(
                      std::span(buffer).first(std::min<std::size_t>(bytes, buffer.size())), started)
                : failed(now, L"Read drive temperature", error);
        if (bytes > buffer.size() && error == ERROR_SUCCESS) {
            reading = failed(now, L"Drive temperature buffer size", ERROR_INVALID_DATA);
        }
        failures = reading.metric.status == Status::valid ? 0 : std::min(failures + 1, 5U);
        if (error != ERROR_SUCCESS) {
            // A reconnected device can keep its interface path but invalidate an open handle.
            // Completion has finished, so reopening on the next retry is now safe.
            handle.reset();
            event.reset();
        }
        next = now + std::chrono::milliseconds(std::max(interval, (1U << failures) * 1000U));
    }
    void sample(Clock::time_point now, unsigned interval) {
        if (pending) {
            DWORD bytes{};
            if (io.complete_disk(handle.get(), &overlap, &bytes, FALSE)) {
                if (enabled && !cancelled) {
                    finish(now, bytes, ERROR_SUCCESS, interval);
                } else {
                    pending = false;
                }
            } else {
                const auto error = GetLastError();
                if (error != ERROR_IO_INCOMPLETE) {
                    if (enabled && !cancelled) {
                        finish(now, 0, error, interval);
                    } else {
                        pending = false;
                    }
                } else if (!cancelled && now - started >= std::chrono::seconds(2)) {
                    cancel();
                    reading = failed(now, L"Drive temperature request timed out", ERROR_TIMEOUT);
                    next = now + std::chrono::seconds(30);
                }
            }
            if (!pending && cancelled) {
                handle.reset();
                event.reset();
                cancelled = false;
            }
            // Never start another request in the completion cycle.
            return;
        }
        if (!enabled) {
            handle.reset();
            event.reset();
            return;
        }
        if (now < next) {
            return;
        }
        next = now + std::chrono::seconds(30);
        if (!handle) {
            handle.reset(io.open_disk(device));
            if (!handle) {
                reading = failed(now, L"Open drive temperature interface", GetLastError());
                return;
            }
            event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!event) {
                reading = failed(now, L"Create drive temperature event", GetLastError());
                handle.reset();
                return;
            }
        }
        BOOL awake{};
        if (!io.power_state(handle.get(), &awake)) {
            reading = failed(now, L"Check drive power state", GetLastError());
            handle.reset();
            event.reset();
            return;
        }
        if (!awake) {
            reading = missing(now, L"Drive is sleeping; temperature query skipped");
            next = now + std::chrono::milliseconds(interval);
            return;
        }
        overlap = {};
        overlap.hEvent = event.get();
        ResetEvent(event.get());
        cancelled = false;
        DWORD bytes{};
        started = now;
        if (io.query_disk(handle.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &bytes,
                          &overlap)) {
            finish(now, bytes, ERROR_SUCCESS, interval);
        } else {
            const auto error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                pending = true;
                reading = missing(now, L"Drive temperature request pending", Status::warming_up);
            } else {
                finish(now, 0, error, interval);
            }
        }
    }
};
struct GpuHandle {
    D3DKMT_HANDLE value{};
    ~GpuHandle() {
        reset();
    }
    void reset() noexcept {
        if (value) {
            D3DKMT_CLOSEADAPTER close{value};
            // Teardown cannot recover a lost adapter or safely retry an invalidated handle.
            static_cast<void>(D3DKMTCloseAdapter(&close));
            value = 0;
        }
    }
    template <class T> NTSTATUS query(KMTQUERYADAPTERINFOTYPE kind, T &data) const {
        D3DKMT_QUERYADAPTERINFO request{value, kind, &data, sizeof(data)};
        return D3DKMTQueryAdapterInfo(&request);
    }
};
} // namespace
TemperatureReading parse_storage_temperature(std::span<const std::byte> bytes,
                                             Clock::time_point now) {
    constexpr auto header_size = offsetof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR, TemperatureInfo);
    if (bytes.size() < header_size) {
        return failed(now, L"Drive temperature descriptor header", ERROR_INVALID_DATA);
    }
    STORAGE_TEMPERATURE_DATA_DESCRIPTOR header{};
    std::memcpy(&header, bytes.data(), header_size);
    if (header.Version < sizeof(header) || header.Size < header_size ||
        header.Size > bytes.size() || header.InfoCount > 128 ||
        header.InfoCount > (header.Size - header_size) / sizeof(STORAGE_TEMPERATURE_INFO)) {
        return failed(now, L"Drive temperature descriptor bounds", ERROR_INVALID_DATA);
    }
    std::array<TemperatureSensor, 128> sensors{};
    std::size_t count{};
    for (unsigned i = 0; i < header.InfoCount; ++i) {
        STORAGE_TEMPERATURE_INFO info{};
        std::memcpy(&info, bytes.data() + header_size + i * sizeof(info), sizeof(info));
        if (static_cast<USHORT>(info.Temperature) != STORAGE_TEMPERATURE_VALUE_NOT_REPORTED) {
            sensors[count++] = {info.Index, static_cast<double>(info.Temperature)};
        }
    }
    auto result = choose_temperature(std::span(sensors).first(count), true, now);
    const auto limit = [](SHORT value) -> std::optional<double> {
        return static_cast<USHORT>(value) != STORAGE_TEMPERATURE_VALUE_NOT_REPORTED && value > 0 &&
                       valid_temperature(value)
                   ? std::optional<double>(value)
                   : std::nullopt;
    };
    result.warning = limit(header.WarningTemperature);
    result.critical = limit(header.CriticalTemperature);
    return result;
}
struct TemperatureProviders::State {
    TemperatureIo io;
    AsusTemperature cpu{io.cpu};
    VendorGpuTemperature vendor{};
    std::map<std::wstring, std::unique_ptr<DiskRequest>, std::less<>> disks;
    std::optional<Device> gpu;
    GpuHandle adapter;
    std::vector<D3DKMT_ADAPTER_PERFDATACAPS> caps;
    TemperatureReading gpu_reading;
    Clock::time_point gpu_next{};
    unsigned interval{1000};
    bool gpu_enabled{};
    bool enabled{true};
    bool vendor_active{};
    unsigned gpu_failures{};
    void sample_gpu(Clock::time_point now) {
        if (!gpu_enabled || !gpu || now < gpu_next) {
            return;
        }
        // Also bound retries if an allocation/provider exception reaches the family boundary.
        gpu_next = now + std::chrono::seconds(30);
        const auto vendor_read = [&] {
            return io.read_vendor_gpu ? io.read_vendor_gpu(*gpu, now) : vendor.sample(*gpu, now);
        };
        const bool was_vendor = vendor_active;
        if (was_vendor) {
            gpu_reading = vendor_read();
        } else {
            sample_windows_gpu(*gpu, now);
        }
        if (gpu_reading.metric.status != Status::valid) {
            const auto previous = gpu_reading;
            if (was_vendor) {
                vendor_active = false;
                sample_windows_gpu(*gpu, now);
            } else {
                gpu_reading = vendor_read();
                vendor_active = gpu_reading.metric.status == Status::valid;
            }
            if (gpu_reading.metric.status != Status::valid) {
                gpu_reading.metric.detail =
                    previous.metric.detail + L"; " + gpu_reading.metric.detail;
                if (previous.metric.status == Status::error) {
                    gpu_reading.metric.status = Status::error;
                }
            }
        }
        if (gpu_reading.metric.status == Status::valid) {
            gpu_failures = 0;
            gpu_next = now + std::chrono::milliseconds(interval);
            if (vendor_active) {
                adapter.reset();
                caps.clear();
            }
        } else {
            gpu_failures = std::min(gpu_failures + 1, 5U);
            gpu_next = now + std::chrono::milliseconds(
                                 std::max(interval, std::min(30U, 1U << gpu_failures) * 1000U));
        }
    }
    void sample_windows_gpu(const Device &selected_device, Clock::time_point now) {
        if (io.read_gpu) {
            gpu_reading = io.read_gpu(selected_device, now);
            return;
        }
        if (!adapter.value) {
            D3DKMT_OPENADAPTERFROMLUID open{};
            open.AdapterLuid.LowPart = static_cast<DWORD>(selected_device.runtime_id);
            open.AdapterLuid.HighPart = static_cast<LONG>(selected_device.runtime_id >> 32U);
            auto status = D3DKMTOpenAdapterFromLuid(&open);
            if (status < 0) {
                gpu_reading = failed(now, L"Open GPU temperature adapter (NTSTATUS)",
                                     static_cast<DWORD>(status));
                return;
            }
            adapter.value = open.hAdapter;
            D3DKMT_PHYSICAL_ADAPTER_COUNT count{};
            status = adapter.query(KMTQAITYPE_PHYSICALADAPTERCOUNT, count);
            if (status < 0 || !count.Count || count.Count > 64) {
                gpu_reading = status < 0
                                  ? failed(now, L"Query GPU physical-adapter count (NTSTATUS)",
                                           static_cast<DWORD>(status))
                                  : failed(now, L"GPU physical-adapter count", ERROR_INVALID_DATA);
                adapter.reset();
                return;
            }
            caps.assign(count.Count, {});
            for (UINT i = 0; i < count.Count; ++i) {
                caps[i].PhysicalAdapterIndex = i;
                if (adapter.query(KMTQAITYPE_ADAPTERPERFDATA_CAPS, caps[i]) < 0) {
                    caps[i] = {};
                }
            }
        }
        std::array<TemperatureSensor, 64> sensors{};
        std::size_t count{};
        for (UINT i = 0; i < caps.size(); ++i) {
            D3DKMT_ADAPTER_PERFDATA data{};
            data.PhysicalAdapterIndex = i;
            data.Temperature = std::numeric_limits<ULONG>::max();
            const auto status = adapter.query(KMTQAITYPE_ADAPTERPERFDATA, data);
            if (status < 0) {
                gpu_reading =
                    failed(now, L"Query GPU temperature (NTSTATUS)", static_cast<DWORD>(status));
                adapter.reset();
                return;
            }
            // Some drivers return an entirely zeroed optional performance record. Do not
            // claim a genuine zero-temperature observation without thermal capabilities.
            if (data.Temperature == std::numeric_limits<ULONG>::max() ||
                (!data.Temperature && !caps[i].TemperatureMax && !caps[i].TemperatureWarning)) {
                gpu_reading = missing(now, L"GPU driver did not report a temperature");
                return;
            }
            sensors[count++] = {i, static_cast<double>(data.Temperature) / 10};
        }
        gpu_reading = choose_temperature(std::span(sensors).first(count), false, now);
        if (gpu_reading.metric.status == Status::valid) {
            const auto selected = std::ranges::max_element(std::span(sensors).first(count), {},
                                                           &TemperatureSensor::celsius);
            const auto &limits = caps[selected->index];
            if (limits.TemperatureWarning > 0 &&
                valid_temperature(limits.TemperatureWarning / 10.0)) {
                gpu_reading.warning = limits.TemperatureWarning / 10.0;
            }
            if (limits.TemperatureMax > 0 && valid_temperature(limits.TemperatureMax / 10.0)) {
                gpu_reading.critical = limits.TemperatureMax / 10.0;
            }
        }
    }
};
TemperatureProviders::TemperatureProviders(TemperatureIo io) {
    if (!io.open_disk) {
        io.open_disk = [](const Device &device) {
            return CreateFileW(device.device_path.c_str(), 0,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        };
    }
    if (!io.power_state) {
        io.power_state = GetDevicePowerState;
    }
    if (!io.query_disk) {
        io.query_disk = [](HANDLE handle, void *buffer, DWORD size, DWORD *bytes,
                           OVERLAPPED *overlap) {
            // The input is copied by the I/O manager before DeviceIoControl returns.
            STORAGE_PROPERTY_QUERY query{
                StorageDeviceTemperatureProperty, PropertyStandardQuery, {0}};
            return DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
                                   buffer, size, bytes, overlap);
        };
    }
    if (!io.complete_disk) {
        io.complete_disk = GetOverlappedResult;
    }
    if (!io.cancel_disk) {
        io.cancel_disk = CancelIoEx;
    }
    state_ = std::make_unique<State>(std::move(io));
}
TemperatureProviders::~TemperatureProviders() = default;
std::size_t TemperatureProviders::disk_request_count() const noexcept {
    return state_->disks.size();
}
void TemperatureProviders::configure(const Catalog &catalog, const std::optional<Device> &gpu,
                                     const Settings &settings, bool reset,
                                     bool disk_discovery_failed, bool reset_gpu) {
    auto &state = *state_;
    state.interval = std::max(1000U, settings.interval_ms);
    state.enabled = settings.show_temperatures;
    state.cpu.set_enabled(state.enabled);
    if (reset) {
        state.cpu.reset();
    }
    const bool gpu_enabled = state.enabled && settings.gpu_visible;
    if (reset || reset_gpu || state.gpu != gpu || state.gpu_enabled != gpu_enabled) {
        state.adapter.reset();
        std::vector<D3DKMT_ADAPTER_PERFDATACAPS>().swap(state.caps);
        state.vendor.reset();
        state.vendor_active = false;
        state.gpu_failures = 0;
        state.gpu_next = {};
        state.gpu_reading =
            missing({}, L"GPU temperature awaiting observation", Status::warming_up);
    }
    state.gpu = gpu;
    state.gpu_enabled = gpu_enabled;
    for (auto &[id, disk] : state.disks) {
        const auto found = std::ranges::find(catalog.disks, id, &Device::id);
        const bool enabled = state.enabled && found != catalog.disks.end() &&
                             disk_visible(id, settings) && !disk_discovery_failed;
        if (reset || !enabled || (found != catalog.disks.end() && disk->device != *found)) {
            disk->cancel();
            disk->enabled = false;
            disk->sample(Clock::now(), state.interval);
            disk->next = {};
            disk->reading =
                missing({}, L"Drive temperature awaiting observation", Status::warming_up);
        }
        // Device metadata is not an I/O buffer. A cancelled request keeps its handle and
        // OVERLAPPED until completion, then a later sample opens the current device path.
        if (found != catalog.disks.end()) {
            disk->device = *found;
        }
        disk->enabled = enabled;
    }
    // Cancelled requests are owned until completion; no freed/reused OVERLAPPED buffers.
    std::erase_if(state.disks, [&](const auto &entry) {
        return !entry.second->pending &&
               (!entry.second->enabled ||
                std::ranges::find(catalog.disks, entry.first, &Device::id) == catalog.disks.end());
    });
    if (state.enabled && !disk_discovery_failed) {
        for (const auto &device : catalog.disks) {
            if (state.disks.size() >= 1024) {
                break;
            }
            if (device.device_path.empty() || !disk_visible(device.id, settings) ||
                state.disks.contains(device.id)) {
                continue;
            }
            auto disk = std::make_unique<DiskRequest>(state.io);
            disk->device = device;
            disk->enabled = true;
            state.disks.emplace(device.id, std::move(disk));
        }
    }
}
void TemperatureProviders::sample(Snapshot &snapshot, Clock::time_point now) {
    auto &state = *state_;
    try {
        const auto &cpu = state.cpu.sample(now, state.interval);
        snapshot.cpu_temperature = state.enabled ? cpu : missing({}, L"Disabled — sampling paused");
    } catch (...) {
        snapshot.cpu_temperature =
            missing(now, L"CPU temperature provider exception", Status::error);
    }
    try {
        state.sample_gpu(now);
        snapshot.gpu_temperature =
            state.gpu_enabled && state.gpu
                ? state.gpu_reading
                : missing(now, !state.enabled      ? L"Disabled — sampling paused"
                               : state.gpu_enabled ? L"No selected GPU"
                                                   : L"Hidden — sampling paused");
    } catch (...) {
        snapshot.gpu_temperature =
            missing(now, L"GPU temperature provider exception", Status::error);
    }
    if (!state.enabled) {
        snapshot.ram_temperature = missing({}, L"Disabled — sampling paused");
    }
    for (auto &[id, request] : state.disks) {
        try {
            request->sample(now, state.interval);
        } catch (...) {
            request->reading = missing(now, L"Drive temperature provider exception", Status::error);
        }
    }
    std::erase_if(state.disks, [](const auto &entry) {
        return !entry.second->enabled && !entry.second->pending;
    });
    for (auto &disk : snapshot.disks) {
        const auto found = state.disks.find(disk.id);
        disk.temperature =
            state.enabled && found != state.disks.end() && found->second->enabled
                ? found->second->reading
                : missing(
                      {},
                      state.enabled
                          ? L"Drive hidden, removed, or discovery unavailable — temperature paused"
                          : L"Disabled — sampling paused");
    }
}
} // namespace loadbar
