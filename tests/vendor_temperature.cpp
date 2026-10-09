#include "telemetry/vendor_temperature.hpp"
#include <array>
#include <cstring>
#include <igcl_api.h>
#include <limits>
#include <nvapi.h>
#include <stdexcept>
#include <string_view>
#include <temperature_abi.hpp>

namespace {
namespace adlx = loadbar::adlx;
void check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
// Context-free DLL exports need a current fixture. The scoped, thread-local pointer is
// test-only; it is restored before the next fixture and never used by production code.
struct FakeDriver {
    inline static thread_local FakeDriver *current{};
    FakeDriver *previous{current};
    std::array<int, 8> tokens{};
    unsigned opens{}, frees{}, initializes{}, closes{}, reads{}, enumerations{}, references{},
        topology_queries{}, symbols{};
    unsigned physical{1};
    DWORD load_error{ERROR_MOD_NOT_FOUND}, selected_luid{42};
    bool missing_library{}, missing_export{}, fail_init{}, fail_read{}, mismatch{}, duplicate{},
        incomplete{}, overflow{}, memory_only{}, bad_value{}, throw_on_symbol{}, fail_enumeration{},
        fail_identity{}, fail_support{}, parameters_ok{true};
    adlx::GpuTable amd_gpu_table{};
    adlx::Gpu amd_gpu{&amd_gpu_table};
    adlx::ListTable amd_list_table{};
    adlx::List amd_list{&amd_list_table};
    adlx::SupportTable amd_support_table{};
    adlx::Support amd_support{&amd_support_table};
    adlx::MetricsTable amd_metrics_table{};
    adlx::Metrics amd_metrics{&amd_metrics_table};
    adlx::PerformanceTable amd_performance_table{};
    adlx::Performance amd_performance{&amd_performance_table};
    adlx::SystemTable amd_system_table{};
    adlx::System amd_system{&amd_system_table};
    FakeDriver() {
        current = this;
        const adlx::Base base{nullptr, amd_release, amd_query};
        amd_gpu_table.base = base;
        amd_gpu_table.unique_id = [](void *, int *id) {
            *id = 7;
            return adlx::kOk;
        };
        amd_gpu_table.luid = [](void *, void *id) {
            *static_cast<LUID *>(id) = {current->mismatch ? 99U : current->selected_luid, 0};
            return current->fail_identity ? 3 : adlx::kOk;
        };
        amd_list_table.base = base;
        amd_list_table.size = [](void *) { return current->duplicate ? 2U : 1U; };
        amd_list_table.begin = [](void *) { return 0U; };
        amd_list_table.end = amd_list_table.size;
        amd_list_table.at = [](void *, unsigned, adlx::Gpu **gpu) {
            *gpu = &current->amd_gpu;
            ++current->references;
            return adlx::kOk;
        };
        amd_support_table.base = base;
        amd_support_table.temperature = [](void *, unsigned char *supported) {
            *supported = current->memory_only ? 0 : 1;
            return current->fail_support ? 3 : adlx::kOk;
        };
        amd_metrics_table.base = base;
        amd_metrics_table.temperature = [](void *, double *value) {
            *value = current->bad_value ? std::numeric_limits<double>::quiet_NaN() : 61.25;
            return adlx::kOk;
        };
        amd_performance_table.base = base;
        amd_performance_table.current = [](void *, adlx::Gpu *, adlx::Metrics **metrics) {
            ++current->reads;
            if (current->fail_read) {
                return 3;
            }
            *metrics = &current->amd_metrics;
            ++current->references;
            return adlx::kOk;
        };
        amd_performance_table.supported = [](void *, adlx::Gpu *, adlx::Support **support) {
            *support = &current->amd_support;
            ++current->references;
            return adlx::kOk;
        };
        amd_system_table.gpus = [](void *, adlx::List **list) {
            ++current->enumerations;
            if (current->fail_enumeration) {
                return 3;
            }
            *list = &current->amd_list;
            ++current->references;
            return adlx::kOk;
        };
        amd_system_table.performance = [](void *, adlx::Performance **performance) {
            *performance = &current->amd_performance;
            ++current->references;
            return adlx::kOk;
        };
    }
    ~FakeDriver() {
        current = previous;
    }
    template <class T> T handle(unsigned i) {
        return reinterpret_cast<T>(&tokens[i]);
    }
    [[nodiscard]] auto calls() const {
        return std::array{opens, frees,        initializes,      closes,
                          reads, enumerations, topology_queries, symbols};
    }
    loadbar::DriverLibraryIo io() {
        loadbar::DriverLibraryIo functions;
        functions.load = [this](const wchar_t *) {
            ++opens;
            SetLastError(load_error);
            return missing_library ? nullptr : GetModuleHandleW(nullptr);
        };
        functions.free = [this](HMODULE) { ++frees; };
        functions.physical_count = [this](const loadbar::Device &) -> loadbar::Result<unsigned> {
            ++topology_queries;
            return physical;
        };
        functions.symbol = [this](HMODULE, const char *name) -> FARPROC {
            ++symbols;
            if (throw_on_symbol) {
                throw std::bad_alloc();
            }
            if (missing_export) {
                return nullptr;
            }
            const std::string_view symbol(name);
            if (symbol == "nvapi_QueryInterface") {
                return reinterpret_cast<FARPROC>(&nv_query);
            }
            if (symbol == "ADLXInitialize") {
                return reinterpret_cast<FARPROC>(&amd_init);
            }
            if (symbol == "ADLXTerminate") {
                return reinterpret_cast<FARPROC>(&amd_close);
            }
            if (symbol == "ctlInit") {
                return reinterpret_cast<FARPROC>(&intel_init);
            }
            if (symbol == "ctlClose") {
                return reinterpret_cast<FARPROC>(&intel_close);
            }
            if (symbol == "ctlEnumerateDevices") {
                return reinterpret_cast<FARPROC>(&intel_devices);
            }
            if (symbol == "ctlGetDeviceProperties") {
                return reinterpret_cast<FARPROC>(&intel_info);
            }
            if (symbol == "ctlEnumTemperatureSensors") {
                return reinterpret_cast<FARPROC>(&intel_sensors);
            }
            if (symbol == "ctlTemperatureGetProperties") {
                return reinterpret_cast<FARPROC>(&intel_properties);
            }
            if (symbol == "ctlTemperatureGetState") {
                return reinterpret_cast<FARPROC>(&intel_temperature);
            }
            return nullptr;
        };
        return functions;
    }
    static NvAPI_Status __cdecl nv_init() {
        ++current->initializes;
        return current->fail_init ? NVAPI_ERROR : NVAPI_OK;
    }
    static NvAPI_Status __cdecl nv_close() {
        ++current->closes;
        return NVAPI_OK;
    }
    static NvAPI_Status __cdecl nv_devices(NvLogicalGpuHandle *devices, NvU32 *count) {
        ++current->enumerations;
        if (current->fail_enumeration) {
            return NVAPI_ERROR;
        }
        *count = current->overflow ? NVAPI_MAX_LOGICAL_GPUS + 1 : 2;
        devices[0] = current->handle<NvLogicalGpuHandle>(0);
        devices[1] = current->handle<NvLogicalGpuHandle>(1);
        return NVAPI_OK;
    }
    static NvAPI_Status __cdecl nv_info(NvLogicalGpuHandle device, NV_LOGICAL_GPU_DATA *data) {
        current->parameters_ok &= data->version == NV_LOGICAL_GPU_DATA_VER && data->pOSAdapterId;
        if (!data->pOSAdapterId) {
            return NVAPI_INVALID_ARGUMENT;
        }
        if (current->fail_identity) {
            return NVAPI_ERROR;
        }
        const bool selected =
            device == current->handle<NvLogicalGpuHandle>(1) || current->duplicate;
        *static_cast<LUID *>(data->pOSAdapterId) = {
            selected && !current->mismatch ? current->selected_luid : 99U, 0};
        data->physicalGpuCount = current->incomplete ? 1 : current->physical;
        for (unsigned i = 0; i < data->physicalGpuCount; ++i) {
            data->physicalGpuHandles[i] = current->handle<NvPhysicalGpuHandle>(i + 2);
        }
        return NVAPI_OK;
    }
    static NvAPI_Status __cdecl nv_temperature(NvPhysicalGpuHandle gpu, NvU32 index,
                                               NV_GPU_THERMAL_SETTINGS *data) {
        ++current->reads;
        current->parameters_ok &=
            index == NVAPI_THERMAL_TARGET_ALL && data->version == NV_GPU_THERMAL_SETTINGS_VER;
        if (current->fail_read) {
            return NVAPI_ERROR;
        }
        data->count = 2;
        data->sensor[0].target = NVAPI_THERMAL_TARGET_MEMORY;
        data->sensor[0].currentTemp = 99;
        data->sensor[1].target =
            current->memory_only ? NVAPI_THERMAL_TARGET_BOARD : NVAPI_THERMAL_TARGET_GPU;
        data->sensor[1].currentTemp = current->bad_value                               ? 1000
                                      : gpu == current->handle<NvPhysicalGpuHandle>(3) ? 73
                                                                                       : 61;
        return NVAPI_OK;
    }
    static void *__cdecl nv_query(unsigned id) {
        switch (id) {
        case 0x0150e828:
            return reinterpret_cast<void *>(&nv_init);
        case 0xd22bdd7e:
            return reinterpret_cast<void *>(&nv_close);
        case 0x48b3ea59:
            return reinterpret_cast<void *>(&nv_devices);
        case 0x842b066e:
            return reinterpret_cast<void *>(&nv_info);
        case 0xe3640a56:
            return reinterpret_cast<void *>(&nv_temperature);
        default:
            return nullptr;
        }
    }
    static long __stdcall amd_release(void *) {
        --current->references;
        return 0;
    }
    static int __stdcall amd_query(void *, const wchar_t *name, void **extended) {
        current->parameters_ok &= std::wstring_view(name) == L"IADLXGPU2";
        *extended = &current->amd_gpu;
        ++current->references;
        return adlx::kOk;
    }
    static int __cdecl amd_init(std::uint64_t version, adlx::System **system) {
        ++current->initializes;
        current->parameters_ok &= version == adlx::kVersion;
        if (current->fail_init) {
            return 3;
        }
        *system = &current->amd_system;
        return adlx::kOk;
    }
    static int __cdecl amd_close() {
        ++current->closes;
        return adlx::kOk;
    }
    static ctl_result_t __cdecl intel_init(ctl_init_args_t *args, ctl_api_handle_t *api) {
        ++current->initializes;
        current->parameters_ok &=
            args->Size == sizeof(*args) && args->flags == CTL_INIT_FLAG_USE_LEVEL_ZERO;
        if (current->fail_init) {
            return CTL_RESULT_ERROR_NOT_INITIALIZED;
        }
        *api = current->handle<ctl_api_handle_t>(7);
        return CTL_RESULT_SUCCESS;
    }
    static ctl_result_t __cdecl intel_close(ctl_api_handle_t) {
        ++current->closes;
        return CTL_RESULT_SUCCESS;
    }
    static ctl_result_t __cdecl intel_devices(ctl_api_handle_t, std::uint32_t *count,
                                              ctl_device_adapter_handle_t *devices) {
        ++current->enumerations;
        if (current->fail_enumeration) {
            return CTL_RESULT_ERROR_DEVICE_LOST;
        }
        if (!devices) {
            *count = 2;
            return CTL_RESULT_SUCCESS;
        }
        devices[0] = current->handle<ctl_device_adapter_handle_t>(0);
        devices[1] = current->handle<ctl_device_adapter_handle_t>(1);
        *count = current->overflow ? 3 : 2;
        return CTL_RESULT_SUCCESS;
    }
    static ctl_result_t __cdecl intel_info(ctl_device_adapter_handle_t device,
                                           ctl_device_adapter_properties_t *properties) {
        current->parameters_ok &=
            properties->Size == sizeof(*properties) && properties->device_id_size == sizeof(LUID);
        const bool selected =
            device == current->handle<ctl_device_adapter_handle_t>(1) || current->duplicate;
        *static_cast<LUID *>(properties->pDeviceID) = {
            selected && !current->mismatch ? current->selected_luid : 99U, 0};
        properties->pci_vendor_id = 0x8086;
        properties->device_type = CTL_DEVICE_TYPE_GRAPHICS;
        return current->fail_identity ? CTL_RESULT_ERROR_DEVICE_LOST : CTL_RESULT_SUCCESS;
    }
    static ctl_result_t __cdecl intel_sensors(ctl_device_adapter_handle_t, std::uint32_t *count,
                                              ctl_temp_handle_t *sensors) {
        if (sensors) {
            sensors[0] = current->handle<ctl_temp_handle_t>(2);
            sensors[1] = current->handle<ctl_temp_handle_t>(3);
        }
        *count = 2;
        return CTL_RESULT_SUCCESS;
    }
    static ctl_result_t __cdecl intel_properties(ctl_temp_handle_t sensor,
                                                 ctl_temp_properties_t *p) {
        current->parameters_ok &= p->Size == sizeof(*p);
        p->type = !current->memory_only && sensor == current->handle<ctl_temp_handle_t>(3)
                      ? CTL_TEMP_SENSORS_GPU
                      : CTL_TEMP_SENSORS_MEMORY;
        return CTL_RESULT_SUCCESS;
    }
    static ctl_result_t __cdecl intel_temperature(ctl_temp_handle_t, double *value) {
        ++current->reads;
        *value = current->bad_value ? std::numeric_limits<double>::infinity() : 61.5;
        return current->fail_read ? CTL_RESULT_ERROR_DEVICE_LOST : CTL_RESULT_SUCCESS;
    }
};
loadbar::Device selected_device(unsigned vendor) {
    loadbar::Device device;
    device.id = L"selected GPU";
    device.runtime_id = 42;
    device.vendor_id = vendor;
    return device;
}
void negative_capability_tests(unsigned vendor, loadbar::Clock::time_point now) {
    using namespace loadbar;
    for (const auto fault :
         {&FakeDriver::memory_only, &FakeDriver::missing_export, &FakeDriver::missing_library}) {
        FakeDriver fake;
        fake.*fault = true;
        VendorGpuTemperature provider(fake.io());
        const auto device = selected_device(vendor);
        const auto missing = provider.sample(device, now);
        check(missing.metric.status == Status::unavailable && missing.metric.timestamp == now,
              "Explicit missing vendor capability is unavailable at its observation time");
        check(fake.opens == 1 && fake.frees == (fault == &FakeDriver::missing_library ? 0U : 1U) &&
                  fake.closes == fake.initializes && !fake.references,
              "Negative capability releases initialized API and library immediately");
        const auto calls = fake.calls();
        fake.*fault = false;
        for (const auto elapsed : {std::chrono::seconds(30), std::chrono::seconds(299)}) {
            const auto cached = provider.sample(device, now + elapsed);
            check(cached.metric.status == Status::unavailable &&
                      cached.metric.timestamp == missing.metric.timestamp &&
                      cached.metric.detail == missing.metric.detail && fake.calls() == calls,
                  "Negative capability retains age without driver calls before five minutes");
        }
        const auto retry = provider.sample(device, now + std::chrono::minutes(5));
        check(retry.metric.status == Status::valid &&
                  retry.metric.timestamp == now + std::chrono::minutes(5) && fake.opens == 2,
              "Negative capability expiry rebinds and recovers exactly at five minutes");
        provider.reset();
        check(fake.closes == fake.initializes && !fake.references &&
                  fake.frees == fake.opens - (fault == &FakeDriver::missing_library ? 1U : 0U),
              "Expired negative capability leaves no vendor resources after reset");
    }
    for (const bool runtime_change : {false, true}) {
        FakeDriver fake;
        fake.memory_only = true;
        VendorGpuTemperature provider(fake.io());
        auto device = selected_device(vendor);
        check(provider.sample(device, now).metric.status == Status::unavailable,
              "Device-invalidation fixture establishes negative capability");
        fake.memory_only = false;
        if (runtime_change) {
            device.runtime_id = ++fake.selected_luid;
        } else {
            device.id = L"another GPU";
        }
        check(provider.sample(device, now + std::chrono::seconds(1)).metric.status ==
                      Status::valid &&
                  fake.opens == 2,
              "Stable device or runtime binding change bypasses negative capability");
        provider.reset();
        check(fake.closes == fake.initializes && fake.frees == fake.opens && !fake.references,
              "Device invalidation balances all vendor resources");
    }
    FakeDriver fake;
    fake.memory_only = true;
    VendorGpuTemperature provider(fake.io());
    const auto device = selected_device(vendor);
    check(provider.sample(device, now).metric.status == Status::unavailable,
          "Reset fixture establishes negative capability");
    fake.memory_only = false;
    provider.reset();
    check(provider.sample(device, now + std::chrono::seconds(1)).metric.status == Status::valid &&
              fake.opens == 2,
          "Explicit provider reset clears negative capability immediately");
    provider.reset();
    check(fake.closes == fake.initializes && fake.frees == fake.opens && !fake.references,
          "Explicit reset balances vendor API and module lifetimes");
}
void transient_failure_tests(unsigned vendor, loadbar::Clock::time_point now) {
    using namespace loadbar;
    for (const auto fault :
         {&FakeDriver::fail_init, &FakeDriver::fail_enumeration, &FakeDriver::fail_identity,
          &FakeDriver::mismatch, &FakeDriver::duplicate, &FakeDriver::fail_read,
          &FakeDriver::bad_value}) {
        FakeDriver fake;
        fake.*fault = true;
        VendorGpuTemperature provider(fake.io());
        const auto device = selected_device(vendor);
        check(provider.sample(device, now).metric.status != Status::valid && fake.opens == 1 &&
                  fake.frees == 1 && !fake.references,
              "Transient vendor failure releases partially initialized resources");
        fake.*fault = false;
        check(provider.sample(device, now + std::chrono::seconds(1)).metric.status ==
                      Status::valid &&
                  fake.opens == 2,
              "Transient vendor failure is not cached as missing capability");
        provider.reset();
        check(fake.closes == fake.initializes - (fault == &FakeDriver::fail_init ? 1U : 0U) &&
                  fake.frees == fake.opens && !fake.references,
              "Transient failure recovery balances initialization and references");
    }
    FakeDriver fake;
    fake.missing_library = true;
    fake.load_error = ERROR_ACCESS_DENIED;
    VendorGpuTemperature provider(fake.io());
    const auto device = selected_device(vendor);
    check(provider.sample(device, now).metric.status == Status::unavailable,
          "Library access error remains an unavailable observation");
    fake.missing_library = false;
    check(provider.sample(device, now + std::chrono::seconds(1)).metric.status == Status::valid &&
              fake.opens == 2,
          "Unavailable library access error does not imply missing capability");
    provider.reset();
    if (vendor == 0x1002) {
        fake.fail_support = true;
        check(provider.sample(device, now).metric.status == Status::unavailable,
              "ADLX failed support query remains unavailable");
        fake.fail_support = false;
        check(provider.sample(device, now + std::chrono::seconds(1)).metric.status == Status::valid,
              "ADLX support query failure must not cache a false capability result");
        provider.reset();
    }
    fake.physical = 2;
    fake.incomplete = true;
    fake.memory_only = true;
    check(provider.sample(device, now).metric.status != Status::valid,
          "Missing sensor cannot establish capability for an incomplete physical GPU scope");
    fake.physical = 1;
    fake.incomplete = false;
    fake.memory_only = false;
    check(provider.sample(device, now + std::chrono::seconds(1)).metric.status == Status::valid,
          "Incomplete physical GPU scope remains transient even when a sensor is absent");
    provider.reset();
    check(fake.closes == fake.initializes && fake.frees == fake.opens - 1 && !fake.references,
          "Unavailable transient failures release and recover vendor resources");
}
} // namespace
void vendor_temperature_tests() {
    using namespace loadbar;
    const auto now = Clock::time_point(std::chrono::seconds(10));
    for (const auto vendor : {0x10deU, 0x1002U, 0x8086U}) {
        negative_capability_tests(vendor, now);
        transient_failure_tests(vendor, now);
        FakeDriver fake;
        const auto device = selected_device(vendor);
        VendorGpuTemperature provider(fake.io());
        auto reading = provider.sample(device, now);
        check(reading.metric.status == Status::valid && reading.metric.value >= 61 &&
                  reading.metric.value < 62,
              "Vendor adapter identity and GPU sensor selection");
        const auto enumerations = fake.enumerations;
        check(provider.sample(device, now).metric.status == Status::valid &&
                  fake.enumerations == enumerations && fake.opens == 1 && fake.reads == 2,
              "Vendor handles and metadata cached between samples");
        fake.fail_read = true;
        check(provider.sample(device, now).metric.status == Status::error && fake.closes == 1 &&
                  fake.frees == 1 && !fake.references,
              "Vendor read failure releases API objects before DLL");
        fake.fail_read = false;
        check(provider.sample(device, now).metric.status == Status::valid,
              "Vendor rebind recovers");
        provider.reset();
        check(!fake.references && fake.closes == fake.initializes && fake.frees == fake.opens,
              "Vendor reset balances initialization and references");
        fake.throw_on_symbol = true;
        const auto frees_before = fake.frees;
        bool threw{};
        try {
            static_cast<void>(provider.sample(device, now));
        } catch (const std::bad_alloc &) {
            threw = true;
        }
        check(threw && fake.frees == frees_before + 1,
              "Binding exception releases the loaded module");
        fake.throw_on_symbol = false;
        for (bool *fault : {&fake.mismatch, &fake.duplicate, &fake.memory_only, &fake.bad_value,
                            &fake.missing_export, &fake.missing_library, &fake.fail_init}) {
            provider.reset();
            *fault = true;
            check(provider.sample(device, now).metric.status != Status::valid,
                  "Vendor failure must not fabricate a valid observation");
            check(!fake.references, "Partial startup releases all vendor interfaces");
            *fault = false;
        }
        provider.reset();
        if (vendor != 0x1002) {
            fake.overflow = true;
            check(provider.sample(device, now).metric.status == Status::error,
                  "Enumeration count growth cannot overrun caller storage");
            fake.overflow = false;
        }
        fake.physical = 2;
        if (vendor == 0x10de) {
            check(provider.sample(device, now).metric.value == 73,
                  "NVIDIA linked GPU hottest main sensor");
            provider.reset();
            fake.incomplete = true;
        }
        check(provider.sample(device, now).metric.status != Status::valid,
              "Incomplete physical GPU coverage is rejected");
        check(fake.parameters_ok, "Correct vendor ABI versions and read-only initialization");
    }
}
