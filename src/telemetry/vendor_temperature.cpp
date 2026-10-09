#include "telemetry/vendor_temperature.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <format>
#include <igcl_api.h>
#include <limits>
#include <nvapi.h>
#include <set>
#include <temperature_abi.hpp>
#include <winternl.h>

// The graphics thunk header requires NTSTATUS from winternl.h first.
#include <d3dkmthk.h>

namespace loadbar {
namespace {
constexpr auto kCapabilityRetryInterval = std::chrono::minutes(5);
TemperatureReading failure(Clock::time_point now, std::wstring_view operation, std::int64_t code,
                           Status status = Status::error) {
    TemperatureReading result;
    result.metric = {0,   status, Unit::celsius,
                     now, {},     std::format(L"{} (status {})", operation, code)};
    return result;
}
TemperatureReading observation(double value, Clock::time_point now, std::wstring_view sensor) {
    if (!valid_temperature(value)) {
        return failure(now, L"Invalid vendor temperature", ERROR_INVALID_DATA);
    }
    TemperatureReading result;
    result.metric = {value, Status::valid, Unit::celsius, now};
    result.sensor = sensor;
    return result;
}
std::uint64_t luid_value(LUID luid) noexcept {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(luid.HighPart)) << 32U) |
           luid.LowPart;
}
Result<unsigned> physical_count(const Device &device) {
    D3DKMT_OPENADAPTERFROMLUID open{};
    open.AdapterLuid.LowPart = static_cast<DWORD>(device.runtime_id);
    open.AdapterLuid.HighPart = std::bit_cast<LONG>(static_cast<DWORD>(device.runtime_id >> 32U));
    auto status = D3DKMTOpenAdapterFromLuid(&open);
    if (status < 0) {
        return std::unexpected(Error{L"Open physical GPU topology", static_cast<DWORD>(status)});
    }
    D3DKMT_PHYSICAL_ADAPTER_COUNT count{};
    D3DKMT_QUERYADAPTERINFO query{open.hAdapter, KMTQAITYPE_PHYSICALADAPTERCOUNT, &count,
                                  sizeof(count)};
    status = D3DKMTQueryAdapterInfo(&query);
    D3DKMT_CLOSEADAPTER close{open.hAdapter};
    static_cast<void>(D3DKMTCloseAdapter(&close));
    if (status < 0 || !count.Count || count.Count > 64) {
        return std::unexpected(Error{L"Query physical GPU topology",
                                     status < 0 ? static_cast<DWORD>(status) : ERROR_INVALID_DATA});
    }
    return count.Count;
}
template <class T> struct AdlxRelease {
    void operator()(T *value) const noexcept {
        if (value) {
            value->vtable->base.release(value);
        }
    }
};
template <class T> using AdlxPtr = std::unique_ptr<T, AdlxRelease<T>>;
struct IntelSensor {
    ctl_temp_handle_t handle{};
};
} // namespace
struct VendorGpuTemperature::State {
    const DriverLibraryIo &io;
    Device device;
    HMODULE module{};
    unsigned physical{};
    bool initialized{}, bound{}, missing_capability{};
    // Only the selected vendor is initialized. Destruction order is interfaces -> API -> DLL.
    decltype(&NvAPI_Initialize) nv_init{};
    decltype(&NvAPI_Unload) nv_close{};
    decltype(&NvAPI_EnumLogicalGPUs) nv_devices{};
    decltype(&NvAPI_GPU_GetLogicalGpuInfo) nv_info{};
    decltype(&NvAPI_GPU_GetThermalSettings) nv_temperature{};
    std::vector<NvPhysicalGpuHandle> nv_gpus;
    adlx::Initialize amd_init{};
    adlx::Terminate amd_close{};
    adlx::System *amd_system{}; // Owned by ADLX; invalidated by ADLXTerminate, not Release.
    AdlxPtr<adlx::Performance> amd_performance;
    std::vector<AdlxPtr<adlx::Gpu>> amd_gpus;
    ctl_pfnInit_t intel_init{};
    ctl_pfnClose_t intel_close{};
    ctl_pfnEnumerateDevices_t intel_devices{};
    ctl_pfnGetDeviceProperties_t intel_info{};
    ctl_pfnEnumTemperatureSensors_t intel_sensors{};
    ctl_pfnTemperatureGetProperties_t intel_properties{};
    ctl_pfnTemperatureGetState_t intel_temperature{};
    ctl_api_handle_t intel_api{};
    std::vector<IntelSensor> intel_gpu_sensors;
    State(const DriverLibraryIo &functions, Device selected)
        : io(functions), device(std::move(selected)) {}
    ~State() {
        amd_gpus.clear();
        amd_performance.reset();
        if (initialized) {
            if (device.vendor_id == 0x10de) {
                static_cast<void>(nv_close());
            } else if (device.vendor_id == 0x1002) {
                static_cast<void>(amd_close());
            } else if (device.vendor_id == 0x8086) {
                static_cast<void>(intel_close(intel_api));
            }
        }
        if (module) {
            io.free(module);
        }
    }
    template <class T> T symbol(const char *name) {
        return reinterpret_cast<T>(io.symbol(module, name));
    }
    TemperatureReading unavailable_capability(Clock::time_point now, std::wstring_view operation,
                                              std::int64_t code) {
        // Only explicit missing APIs or successfully queried sensor absence use the long
        // retry interval. Identity, enumeration and provider failures remain transient.
        missing_capability = true;
        return failure(now, operation, code, Status::unavailable);
    }
    TemperatureReading bind(Clock::time_point now) {
        const wchar_t *library{};
        switch (device.vendor_id) {
        case 0x10de:
            library = L"nvapi64.dll";
            break;
        case 0x1002:
            library = L"amdadlx64.dll";
            break;
        case 0x8086:
            library = L"ControlLib.dll";
            break;
        default:
            return failure(now, L"No temperature fallback for this GPU vendor", ERROR_NOT_SUPPORTED,
                           Status::unavailable);
        }
        const auto count = io.physical_count(device);
        if (!count || !*count || *count > 64) {
            return failure(now, L"Cannot verify physical GPU temperature scope",
                           count ? ERROR_INVALID_DATA : count.error().code);
        }
        physical = *count;
        module = io.load(library);
        if (!module) {
            const auto code = GetLastError();
            if (code == ERROR_MOD_NOT_FOUND || code == ERROR_DLL_NOT_FOUND ||
                code == ERROR_PROC_NOT_FOUND) {
                return unavailable_capability(now, std::wstring(library) + L" unavailable", code);
            }
            return failure(now, std::wstring(library) + L" unavailable", code, Status::unavailable);
        }
        if (device.vendor_id == 0x10de) {
            return bind_nvidia(now);
        }
        if (device.vendor_id == 0x1002) {
            return bind_amd(now);
        }
        return bind_intel(now);
    }
    TemperatureReading bind_nvidia(Clock::time_point now) {
        using Query = void *(__cdecl *)(unsigned);
        const auto query = symbol<Query>("nvapi_QueryInterface");
        if (!query) {
            return unavailable_capability(now, L"NVAPI entry point missing", ERROR_PROC_NOT_FOUND);
        }
        // Published IDs in the pinned NVIDIA nvapi_interface.h. No undocumented calls.
        nv_init = reinterpret_cast<decltype(nv_init)>(query(0x0150e828));
        nv_close = reinterpret_cast<decltype(nv_close)>(query(0xd22bdd7e));
        nv_devices = reinterpret_cast<decltype(nv_devices)>(query(0x48b3ea59));
        nv_info = reinterpret_cast<decltype(nv_info)>(query(0x842b066e));
        nv_temperature = reinterpret_cast<decltype(nv_temperature)>(query(0xe3640a56));
        if (!nv_init || !nv_close || !nv_devices || !nv_info || !nv_temperature) {
            return unavailable_capability(now, L"NVAPI temperature interface missing",
                                          ERROR_PROC_NOT_FOUND);
        }
        auto status = nv_init();
        if (status != NVAPI_OK) {
            return failure(now, L"Initialize NVAPI", status);
        }
        initialized = true;
        std::array<NvLogicalGpuHandle, NVAPI_MAX_LOGICAL_GPUS> logical{};
        NvU32 count{};
        status = nv_devices(logical.data(), &count);
        if (status != NVAPI_OK || count > logical.size()) {
            return failure(now, L"Enumerate NVAPI GPUs",
                           status != NVAPI_OK ? status : ERROR_INVALID_DATA);
        }
        bool matched{};
        for (NvU32 i = 0; i < count; ++i) {
            LUID luid{};
            NV_LOGICAL_GPU_DATA info{};
            info.version = NV_LOGICAL_GPU_DATA_VER;
            info.pOSAdapterId = &luid;
            status = nv_info(logical[i], &info);
            if (status != NVAPI_OK) {
                return failure(now, L"Read NVAPI GPU identity", status);
            }
            if (luid_value(luid) != device.runtime_id) {
                continue;
            }
            if (matched || info.physicalGpuCount != physical ||
                info.physicalGpuCount > NVAPI_MAX_PHYSICAL_GPUS) {
                return failure(now, L"Ambiguous or incomplete NVAPI GPU scope", ERROR_INVALID_DATA);
            }
            matched = true;
            for (NvU32 n = 0; n < info.physicalGpuCount; ++n) {
                const auto handle = info.physicalGpuHandles[n];
                if (!handle || std::ranges::find(nv_gpus, handle) != nv_gpus.end()) {
                    return failure(now, L"Duplicate NVAPI physical GPU", ERROR_INVALID_DATA);
                }
                nv_gpus.push_back(handle);
            }
        }
        if (!matched) {
            return failure(now, L"Selected GPU absent from NVAPI", ERROR_NOT_FOUND,
                           Status::unavailable);
        }
        bound = true;
        return read_nvidia(now);
    }
    TemperatureReading read_nvidia(Clock::time_point now) {
        double hottest = -std::numeric_limits<double>::infinity();
        bool missing_sensor{};
        for (const auto gpu : nv_gpus) {
            NV_GPU_THERMAL_SETTINGS sensors{};
            sensors.version = NV_GPU_THERMAL_SETTINGS_VER;
            const auto status = nv_temperature(gpu, NVAPI_THERMAL_TARGET_ALL, &sensors);
            if (status != NVAPI_OK || sensors.count > NVAPI_MAX_THERMAL_SENSORS_PER_GPU) {
                return failure(now, L"Read NVAPI GPU temperature",
                               status != NVAPI_OK ? status : ERROR_INVALID_DATA);
            }
            bool found{};
            for (NvU32 i = 0; i < sensors.count; ++i) {
                const auto &sensor = sensors.sensor[i];
                if (sensor.target != NVAPI_THERMAL_TARGET_GPU) {
                    continue;
                }
                if (!valid_temperature(sensor.currentTemp)) {
                    return failure(now, L"NVAPI invalid GPU temperature", ERROR_INVALID_DATA);
                }
                found = true;
                hottest = std::max(hottest, static_cast<double>(sensor.currentTemp));
            }
            missing_sensor |= !found;
        }
        if (missing_sensor) {
            return unavailable_capability(now, L"NVAPI GPU sensor unavailable",
                                          ERROR_NOT_SUPPORTED);
        }
        return observation(hottest, now, L"NVIDIA NVAPI GPU core sensor (hottest physical member)");
    }
    TemperatureReading bind_amd(Clock::time_point now) {
        amd_init = symbol<adlx::Initialize>("ADLXInitialize");
        amd_close = symbol<adlx::Terminate>("ADLXTerminate");
        if (!amd_init || !amd_close) {
            return unavailable_capability(now, L"ADLX temperature interface missing",
                                          ERROR_PROC_NOT_FOUND);
        }
        auto status = amd_init(adlx::kVersion, &amd_system);
        if ((status != adlx::kOk && status != adlx::kInitialized) || !amd_system) {
            return failure(now, L"Initialize ADLX", status);
        }
        initialized = true;
        adlx::List *raw_list{};
        status = amd_system->vtable->gpus(amd_system, &raw_list);
        AdlxPtr<adlx::List> list(raw_list);
        if (status != adlx::kOk || !list) {
            return failure(now, L"Enumerate ADLX GPUs", status);
        }
        adlx::Performance *raw_performance{};
        status = amd_system->vtable->performance(amd_system, &raw_performance);
        amd_performance.reset(raw_performance);
        if (status != adlx::kOk || !amd_performance) {
            return failure(now, L"Get ADLX current metrics interface", status);
        }
        const auto count = list->vtable->size(list.get());
        const auto begin = list->vtable->begin(list.get());
        const auto end = list->vtable->end(list.get());
        if (count > 256 || end < begin || end - begin != count) {
            return failure(now, L"ADLX GPU list bounds", ERROR_INVALID_DATA);
        }
        std::set<int> identities;
        bool missing_sensor{};
        for (auto i = begin; i < end; ++i) {
            adlx::Gpu *raw_gpu{};
            status = list->vtable->at(list.get(), i, &raw_gpu);
            AdlxPtr<adlx::Gpu> gpu(raw_gpu);
            if (status != adlx::kOk || !gpu) {
                return failure(now, L"Read ADLX GPU", status);
            }
            void *extended{};
            status = gpu->vtable->base.query(gpu.get(), L"IADLXGPU2", &extended);
            AdlxPtr<adlx::Gpu> gpu2(static_cast<adlx::Gpu *>(extended));
            if (status != adlx::kOk || !gpu2) {
                return failure(now, L"ADLX LUID interface unavailable", status,
                               Status::unavailable);
            }
            LUID luid{};
            status = gpu2->vtable->luid(gpu2.get(), &luid);
            if (status != adlx::kOk) {
                return failure(now, L"Read ADLX GPU identity", status);
            }
            if (luid_value(luid) != device.runtime_id) {
                continue;
            }
            int identity{};
            status = gpu->vtable->unique_id(gpu.get(), &identity);
            if (status != adlx::kOk || !identities.insert(identity).second) {
                return failure(now, L"Ambiguous ADLX GPU identity",
                               status != adlx::kOk ? status : ERROR_INVALID_DATA);
            }
            adlx::Support *raw_support{};
            status =
                amd_performance->vtable->supported(amd_performance.get(), gpu.get(), &raw_support);
            AdlxPtr<adlx::Support> support(raw_support);
            if (status != adlx::kOk || !support) {
                return failure(now, L"Query ADLX temperature support", status);
            }
            unsigned char supported{};
            status = support->vtable->temperature(support.get(), &supported);
            if (status != adlx::kOk) {
                return failure(now, L"ADLX edge temperature unavailable", status,
                               Status::unavailable);
            }
            missing_sensor |= !supported;
            amd_gpus.push_back(std::move(gpu));
        }
        if (amd_gpus.size() != physical) {
            return failure(now, L"Selected GPU absent or incomplete in ADLX", ERROR_NOT_FOUND,
                           Status::unavailable);
        }
        if (missing_sensor) {
            return unavailable_capability(now, L"ADLX edge temperature unavailable",
                                          ERROR_NOT_SUPPORTED);
        }
        bound = true;
        return read_amd(now);
    }
    TemperatureReading read_amd(Clock::time_point now) {
        double hottest = -std::numeric_limits<double>::infinity();
        for (const auto &gpu : amd_gpus) {
            adlx::Metrics *raw{};
            auto status = amd_performance->vtable->current(amd_performance.get(), gpu.get(), &raw);
            AdlxPtr<adlx::Metrics> metrics(raw);
            if (status != adlx::kOk || !metrics) {
                return failure(now, L"Read ADLX current GPU metrics", status);
            }
            double value = std::numeric_limits<double>::quiet_NaN();
            status = metrics->vtable->temperature(metrics.get(), &value);
            if (status != adlx::kOk || !valid_temperature(value)) {
                return failure(now, L"Read ADLX GPU edge temperature",
                               status != adlx::kOk ? status : ERROR_INVALID_DATA);
            }
            hottest = std::max(hottest, value);
        }
        return observation(hottest, now,
                           L"AMD ADLX GPU edge temperature (hottest physical member)");
    }
    TemperatureReading bind_intel(Clock::time_point now) {
        intel_init = symbol<ctl_pfnInit_t>("ctlInit");
        intel_close = symbol<ctl_pfnClose_t>("ctlClose");
        intel_devices = symbol<ctl_pfnEnumerateDevices_t>("ctlEnumerateDevices");
        intel_info = symbol<ctl_pfnGetDeviceProperties_t>("ctlGetDeviceProperties");
        intel_sensors = symbol<ctl_pfnEnumTemperatureSensors_t>("ctlEnumTemperatureSensors");
        intel_properties = symbol<ctl_pfnTemperatureGetProperties_t>("ctlTemperatureGetProperties");
        intel_temperature = symbol<ctl_pfnTemperatureGetState_t>("ctlTemperatureGetState");
        if (!intel_init || !intel_close || !intel_devices || !intel_info || !intel_sensors ||
            !intel_properties || !intel_temperature) {
            return unavailable_capability(now, L"IGCL temperature interface missing",
                                          ERROR_PROC_NOT_FOUND);
        }
        ctl_init_args_t args{};
        args.Size = sizeof(args);
        args.AppVersion = CTL_IMPL_VERSION;
        args.flags = CTL_INIT_FLAG_USE_LEVEL_ZERO;
        auto status = intel_init(&args, &intel_api);
        if (status != CTL_RESULT_SUCCESS || !intel_api) {
            return failure(now, L"Initialize IGCL telemetry", status);
        }
        initialized = true;
        // Windows linked-adapter indexing is not exposed by this IGCL interface. Do not
        // reinterpret a logical-adapter maximum as verified physical-member coverage.
        if (physical != 1) {
            return failure(now, L"IGCL linked GPU temperature scope unavailable",
                           ERROR_NOT_SUPPORTED, Status::unavailable);
        }
        std::uint32_t count{};
        status = intel_devices(intel_api, &count, nullptr);
        if (status != CTL_RESULT_SUCCESS || !count || count > 256) {
            return failure(now, L"Enumerate IGCL GPU count", status);
        }
        std::vector<ctl_device_adapter_handle_t> devices(count);
        const auto capacity = count;
        status = intel_devices(intel_api, &count, devices.data());
        if (status != CTL_RESULT_SUCCESS || count > capacity) {
            return failure(now, L"Enumerate IGCL GPUs",
                           status != CTL_RESULT_SUCCESS ? status : ERROR_INVALID_DATA);
        }
        ctl_device_adapter_handle_t selected{};
        for (std::uint32_t i = 0; i < count; ++i) {
            LUID luid{};
            ctl_device_adapter_properties_t properties{.device_type = CTL_DEVICE_TYPE_MAX};
            properties.Size = sizeof(properties);
            properties.pDeviceID = &luid;
            properties.device_id_size = sizeof(luid);
            status = intel_info(devices[i], &properties);
            if (status != CTL_RESULT_SUCCESS) {
                return failure(now, L"Read IGCL GPU identity", status);
            }
            if (properties.device_type != CTL_DEVICE_TYPE_GRAPHICS ||
                properties.pci_vendor_id != 0x8086 || luid_value(luid) != device.runtime_id ||
                properties.device_id_size != sizeof(luid)) {
                continue;
            }
            if (selected) {
                return failure(now, L"Ambiguous IGCL GPU identity", ERROR_INVALID_DATA);
            }
            selected = devices[i];
        }
        if (!selected) {
            return failure(now, L"Selected GPU absent from IGCL", ERROR_NOT_FOUND,
                           Status::unavailable);
        }
        count = 0;
        status = intel_sensors(selected, &count, nullptr);
        if (status != CTL_RESULT_SUCCESS) {
            return failure(now, L"Enumerate IGCL temperature sensors", status);
        }
        if (!count) {
            return unavailable_capability(now, L"IGCL reports no temperature sensors",
                                          ERROR_NOT_SUPPORTED);
        }
        if (count > 256) {
            return failure(now, L"IGCL temperature sensor count", ERROR_INVALID_DATA);
        }
        std::vector<ctl_temp_handle_t> sensors(count);
        const auto sensor_capacity = count;
        status = intel_sensors(selected, &count, sensors.data());
        if (status != CTL_RESULT_SUCCESS || count > sensor_capacity) {
            return failure(now, L"Enumerate IGCL temperature sensors",
                           status != CTL_RESULT_SUCCESS ? status : ERROR_INVALID_DATA);
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            ctl_temp_properties_t properties{};
            properties.Size = sizeof(properties);
            status = intel_properties(sensors[i], &properties);
            if (status != CTL_RESULT_SUCCESS) {
                return failure(now, L"Read IGCL sensor scope", status);
            }
            if (properties.type == CTL_TEMP_SENSORS_GPU) {
                if (!sensors[i] ||
                    std::ranges::find(intel_gpu_sensors, sensors[i], &IntelSensor::handle) !=
                        intel_gpu_sensors.end()) {
                    return failure(now, L"Duplicate IGCL sensor", ERROR_INVALID_DATA);
                }
                intel_gpu_sensors.push_back({sensors[i]});
            }
        }
        if (intel_gpu_sensors.empty()) {
            return unavailable_capability(now, L"IGCL GPU-domain temperature unavailable",
                                          ERROR_NOT_SUPPORTED);
        }
        bound = true;
        return read_intel(now);
    }
    TemperatureReading read_intel(Clock::time_point now) {
        double hottest = -std::numeric_limits<double>::infinity();
        for (const auto &sensor : intel_gpu_sensors) {
            double value = std::numeric_limits<double>::quiet_NaN();
            const auto status = intel_temperature(sensor.handle, &value);
            if (status != CTL_RESULT_SUCCESS || !valid_temperature(value)) {
                return failure(now, L"Read IGCL GPU-domain temperature",
                               status != CTL_RESULT_SUCCESS ? status : ERROR_INVALID_DATA);
            }
            hottest = std::max(hottest, value);
        }
        return observation(hottest, now, L"Intel IGCL GPU-domain maximum temperature");
    }
    TemperatureReading read(Clock::time_point now) {
        if (!bound) {
            return bind(now);
        }
        if (device.vendor_id == 0x10de) {
            return read_nvidia(now);
        }
        return device.vendor_id == 0x1002 ? read_amd(now) : read_intel(now);
    }
};
VendorGpuTemperature::VendorGpuTemperature(DriverLibraryIo io) : io_(std::move(io)) {
    if (!io_.load) {
        io_.load = [](const wchar_t *name) {
            return LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        };
    }
    if (!io_.symbol) {
        // Keep the import call inside our own CFG-valid callable. A cached GetProcAddress
        // function pointer failed CFG validation after loading the host's NVIDIA shim.
        io_.symbol = [](HMODULE module, const char *name) { return GetProcAddress(module, name); };
    }
    if (!io_.free) {
        io_.free = [](HMODULE module) { FreeLibrary(module); };
    }
    if (!io_.physical_count) {
        io_.physical_count = physical_count;
    }
}
VendorGpuTemperature::~VendorGpuTemperature() = default;
void VendorGpuTemperature::reset() {
    state_.reset();
    missing_.reset();
}
TemperatureReading VendorGpuTemperature::sample(const Device &device, Clock::time_point now) {
    if (missing_) {
        if (missing_->device == device && now >= missing_->reading.metric.timestamp &&
            now < missing_->retry_at) {
            return missing_->reading;
        }
        missing_.reset();
    }
    if (!state_ || state_->device != device) {
        state_ = std::make_unique<State>(io_, device);
    }
    try {
        auto reading = state_->read(now);
        if (reading.metric.status != Status::valid) {
            if (state_->missing_capability) {
                missing_ = MissingCapability{device, reading, now + kCapabilityRetryInterval};
            }
            state_.reset();
        }
        return reading;
    } catch (...) {
        // A failed allocation during enumeration must not leave a half-bound API that
        // initializes again on the next tick, leaking its module or device references.
        state_.reset();
        throw;
    }
}
} // namespace loadbar
