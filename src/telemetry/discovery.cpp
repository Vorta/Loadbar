#include "telemetry/discovery.hpp"
#include "platform/windows.hpp"

// Networking requires Winsock/IP definitions first; storage GUIDs are defined once here.
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <dxcore.h>
#include <dxgi1_6.h>
#include <setupapi.h>
#include <initguid.h>
#include <winioctl.h>
// clang-format on

#include <algorithm>
#include <array>
#include <bit>
#include <format>
#include <map>
#include <memory>
#include <set>

namespace loadbar {
namespace {
struct DeviceInfoDeleter {
    using pointer = HDEVINFO;
    const DiskDiscoveryApi *api{};
    void operator()(HDEVINFO value) const noexcept {
        if (value != INVALID_HANDLE_VALUE) {
            api->close(value);
        }
    }
};
struct MibDeleter {
    void operator()(void *value) const noexcept {
        FreeMibTable(value);
    }
};
Result<std::vector<Processor>> cpu_topology(const CpuTopologyRead &read) {
    DWORD size{};
    const auto sized = read(RelationProcessorCore, nullptr, &size);
    const auto size_error = sized ? ERROR_SUCCESS : GetLastError();
    if (size_error != ERROR_INSUFFICIENT_BUFFER && size_error != ERROR_SUCCESS) {
        return std::unexpected(Error{L"Size CPU topology", size_error});
    }
    if (!size || size > 4 * 1024 * 1024) {
        return std::unexpected(Error{L"CPU topology size", ERROR_INVALID_DATA});
    }
    std::vector<std::byte> buffer(size);
    if (!read(RelationProcessorCore,
              reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>(buffer.data()), &size)) {
        const auto error = GetLastError();
        return std::unexpected(Error{L"GetLogicalProcessorInformationEx", error});
    }
    if (!size || size > buffer.size()) {
        return std::unexpected(Error{L"CPU topology returned size", ERROR_INVALID_DATA});
    }
    std::vector<Processor> processors;
    std::size_t offset{};
    unsigned core{};
    while (offset < size) {
        constexpr auto prefix = offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Processor) +
                                offsetof(PROCESSOR_RELATIONSHIP, GroupMask);
        if (size - offset < prefix) {
            return std::unexpected(Error{L"CPU topology structure", ERROR_INVALID_DATA});
        }
        const auto *entry = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>(
            buffer.data() + offset);
        if (entry->Size < prefix || entry->Size > size - offset ||
            entry->Relationship != RelationProcessorCore || !entry->Processor.GroupCount ||
            entry->Processor.GroupCount > (entry->Size - prefix) / sizeof(GROUP_AFFINITY)) {
            return std::unexpected(Error{L"CPU topology structure", ERROR_INVALID_DATA});
        }
        for (WORD group = 0; group < entry->Processor.GroupCount; ++group) {
            const auto affinity = entry->Processor.GroupMask[group];
            for (unsigned bit = 0; bit < 64; ++bit) {
                if ((affinity.Mask & (std::uint64_t{1} << bit)) != 0) {
                    processors.push_back(
                        {{affinity.Group, bit}, core, true, {}, entry->Processor.EfficiencyClass});
                }
            }
        }
        ++core;
        offset += entry->Size;
    }
    if (processors.empty()) {
        return std::unexpected(
            Error{L"CPU topology has no logical processors", ERROR_INVALID_DATA});
    }
    std::ranges::sort(processors, {}, &Processor::id);
    if (std::adjacent_find(processors.begin(), processors.end(),
                           [](const Processor &a, const Processor &b) { return a.id == b.id; }) !=
        processors.end()) {
        return std::unexpected(Error{L"Duplicate CPU topology identity", ERROR_INVALID_DATA});
    }
    std::map<unsigned, unsigned> ordered;
    for (auto &processor : processors) {
        const auto [it, inserted] =
            ordered.try_emplace(processor.core, static_cast<unsigned>(ordered.size()));
        (void)inserted;
        processor.core = it->second;
    }
    return processors;
}
std::optional<DWORD> system_disk() {
    std::vector<wchar_t> windows(32768), root(32768), volume(32768);
    const auto length = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
    if (!length || length >= windows.size() ||
        !GetVolumePathNameW(windows.data(), root.data(), static_cast<DWORD>(root.size())) ||
        !GetVolumeNameForVolumeMountPointW(root.data(), volume.data(),
                                           static_cast<DWORD>(volume.size()))) {
        return std::nullopt;
    }
    std::wstring path = volume.data();
    if (path.ends_with(L'\\')) {
        path.pop_back();
    }
    Handle handle(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, 0, nullptr));
    if (!handle) {
        return std::nullopt;
    }
    std::vector<std::byte> storage(65536);
    DWORD bytes{};
    if (!DeviceIoControl(handle.get(), IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0,
                         storage.data(), static_cast<DWORD>(storage.size()), &bytes, nullptr) ||
        bytes < sizeof(VOLUME_DISK_EXTENTS)) {
        return std::nullopt;
    }
    const auto *extents = reinterpret_cast<const VOLUME_DISK_EXTENTS *>(storage.data());
    if (!extents->NumberOfDiskExtents ||
        extents->NumberOfDiskExtents >
            (bytes - offsetof(VOLUME_DISK_EXTENTS, Extents)) / sizeof(DISK_EXTENT)) {
        return std::nullopt;
    }
    const DWORD disk = extents->Extents[0].DiskNumber;
    for (DWORD i = 1; i < extents->NumberOfDiskExtents; ++i) {
        if (extents->Extents[i].DiskNumber != disk) {
            return std::nullopt;
        }
    }
    return disk;
}
Result<Device> disk_device(HDEVINFO devices, SP_DEVICE_INTERFACE_DATA &interface_data) {
    DWORD bytes{};
    const auto sized =
        SetupDiGetDeviceInterfaceDetailW(devices, &interface_data, nullptr, 0, &bytes, nullptr);
    const auto size_error = sized ? ERROR_SUCCESS : GetLastError();
    if (size_error != ERROR_INSUFFICIENT_BUFFER && size_error != ERROR_SUCCESS) {
        return std::unexpected(Error{L"Size disk interface detail", size_error});
    }
    if (bytes < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || bytes > 65536) {
        return std::unexpected(Error{L"Disk interface detail size", ERROR_INVALID_DATA});
    }
    std::vector<std::byte> buffer(bytes);
    auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buffer.data());
    detail->cbSize = sizeof(*detail);
    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof(info);
    if (!SetupDiGetDeviceInterfaceDetailW(devices, &interface_data, detail, bytes, nullptr,
                                          &info)) {
        const auto error = GetLastError();
        return std::unexpected(Error{L"Read disk interface detail", error});
    }
    Handle disk(CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, 0, nullptr));
    if (!disk) {
        const auto error = GetLastError();
        return std::unexpected(Error{L"Open disk for identity", error});
    }
    STORAGE_DEVICE_NUMBER number{};
    if (!DeviceIoControl(disk.get(), IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &number,
                         sizeof(number), &bytes, nullptr)) {
        const auto error = GetLastError();
        return std::unexpected(Error{L"Read disk device number", error});
    }
    if (bytes < sizeof(number) || number.DeviceType != FILE_DEVICE_DISK) {
        return std::unexpected(Error{L"Disk device number data", ERROR_INVALID_DATA});
    }
    std::array<wchar_t, 1024> identity{}, label{};
    if (!SetupDiGetDeviceInstanceIdW(devices, &info, identity.data(),
                                     static_cast<DWORD>(identity.size()), nullptr)) {
        const auto error = GetLastError();
        return std::unexpected(Error{L"Read disk instance identity", error});
    }
    if (!SetupDiGetDeviceRegistryPropertyW(devices, &info, SPDRP_FRIENDLYNAME, nullptr,
                                           reinterpret_cast<BYTE *>(label.data()),
                                           static_cast<DWORD>(sizeof(label)), nullptr)) {
        SetupDiGetDeviceRegistryPropertyW(devices, &info, SPDRP_DEVICEDESC, nullptr,
                                          reinterpret_cast<BYTE *>(label.data()),
                                          static_cast<DWORD>(sizeof(label)), nullptr);
    }
    // Device instance IDs are mandatory; friendly labels are optional bounded metadata.
    if (!identity[0] || std::ranges::find(identity, L'\0') == identity.end()) {
        return std::unexpected(Error{L"Disk instance identity data", ERROR_INVALID_DATA});
    }
    label.back() = L'\0';
    Device device;
    device.kind = DeviceKind::disk;
    device.id = identity.data();
    device.runtime_id = number.DeviceNumber;
    device.device_path = detail->DevicePath;
    device.label = std::format(L"Disk {} — {}", number.DeviceNumber,
                               label[0] ? label.data() : L"Storage device");
    return device;
}
Result<std::vector<Device>> disks() {
    const DiskDiscoveryApi api{
        [] {
            return SetupDiGetClassDevsW(&GUID_DEVINTERFACE_DISK, nullptr, nullptr,
                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        },
        [](HDEVINFO devices, DWORD index, SP_DEVICE_INTERFACE_DATA *data) {
            return SetupDiEnumDeviceInterfaces(devices, nullptr, &GUID_DEVINTERFACE_DISK, index,
                                               data);
        },
        disk_device, [](HDEVINFO devices) { SetupDiDestroyDeviceInfoList(devices); }};
    auto result = discover_disks(api);
    if (result) {
        const auto preferred = system_disk();
        for (auto &device : *result) {
            device.preferred = preferred && *preferred == device.runtime_id;
        }
    }
    return result;
}
std::vector<Device> networks() {
    MIB_IF_TABLE2 *raw{};
    if (GetIfTable2(&raw) != NO_ERROR) {
        return {};
    }
    const std::unique_ptr<MIB_IF_TABLE2, MibDeleter> table(raw);
    std::set<std::uint64_t> default_routes;
    MIB_IPFORWARD_TABLE2 *routes_raw{};
    if (GetIpForwardTable2(AF_UNSPEC, &routes_raw) == NO_ERROR) {
        const std::unique_ptr<MIB_IPFORWARD_TABLE2, MibDeleter> routes(routes_raw);
        for (ULONG i = 0; i < routes->NumEntries; ++i) {
            const auto &route = routes->Table[i];
            if (route.DestinationPrefix.PrefixLength == 0) {
                default_routes.insert(route.InterfaceLuid.Value);
            }
        }
    }
    std::vector<Device> result;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const auto &row = table->Table[i];
        if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }
        std::array<wchar_t, 40> guid{};
        if (!StringFromGUID2(row.InterfaceGuid, guid.data(), static_cast<int>(guid.size()))) {
            continue;
        }
        Device device;
        device.kind = DeviceKind::network;
        device.id = guid.data();
        device.label = row.Alias;
        device.runtime_id = row.InterfaceLuid.Value;
        device.capacity = row.ReceiveLinkSpeed / 8;
        device.shared_capacity = row.TransmitLinkSpeed / 8;
        device.preferred = row.InterfaceAndOperStatusFlags.HardwareInterface &&
                           row.OperStatus == IfOperStatusUp &&
                           default_routes.contains(row.InterfaceLuid.Value);
        if (!row.InterfaceAndOperStatusFlags.HardwareInterface) {
            device.label += L" (virtual/tunnel)";
        }
        result.push_back(std::move(device));
    }
    return result;
}
std::uint64_t luid_value(LUID luid) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(luid.HighPart)) << 32U) |
           luid.LowPart;
}
std::vector<Device> gpus() {
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return {};
    }
    Microsoft::WRL::ComPtr<IDXCoreAdapterFactory> core_factory;
    DXCoreCreateAdapterFactory(IID_PPV_ARGS(&core_factory));
    std::vector<Device> result;
    for (UINT index = 0; index < 256; ++index) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (!adapter) {
            continue;
        }
        DXGI_ADAPTER_DESC1 info{};
        if (FAILED(adapter->GetDesc1(&info)) ||
            (info.Flags & static_cast<UINT>(DXGI_ADAPTER_FLAG_SOFTWARE))) {
            continue;
        }
        Device device;
        device.kind = DeviceKind::gpu;
        device.label = info.Description;
        device.runtime_id = luid_value(info.AdapterLuid);
        device.vendor_id = info.VendorId;
        device.capacity = info.DedicatedVideoMemory;
        device.shared_capacity = info.SharedSystemMemory;
        DISPLAYCONFIG_ADAPTER_NAME name{{DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME}};
        name.header = {DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME, sizeof(name), info.AdapterLuid,
                       0};
        if (DisplayConfigGetDeviceInfo(&name.header) == ERROR_SUCCESS) {
            device.id = name.adapterDevicePath;
        } else {
            device.id = std::format(L"hardware:{:x}:{:x}:{:x}:{:x}", info.VendorId, info.DeviceId,
                                    info.SubSysId, info.Revision);
        }
        Microsoft::WRL::ComPtr<IDXCoreAdapter> core_adapter;
        if (core_factory && SUCCEEDED(core_factory->GetAdapterByLuid(
                                info.AdapterLuid, IID_PPV_ARGS(&core_adapter)))) {
            bool integrated{};
            std::uint32_t physical_count{};
            const bool kind_known =
                core_adapter->IsPropertySupported(DXCoreAdapterProperty::IsIntegrated) &&
                SUCCEEDED(
                    core_adapter->GetProperty(DXCoreAdapterProperty::IsIntegrated, &integrated));
            const bool count_known =
                core_adapter->IsPropertySupported(DXCoreAdapterProperty::PhysicalAdapterCount) &&
                SUCCEEDED(core_adapter->GetProperty(DXCoreAdapterProperty::PhysicalAdapterCount,
                                                    &physical_count));
            device.integrated = integrated;
            device.memory_scope_known = kind_known && count_known && physical_count == 1;
        }
        result.push_back(std::move(device));
    }
    std::uint64_t largest{};
    for (const auto &device : result) {
        largest = std::max(largest, device.capacity);
    }
    for (auto &device : result) {
        device.preferred = device.capacity == largest;
    }
    return result;
}
} // namespace
Result<std::vector<Processor>> discover_processors(const CpuTopologyRead &read) {
    return cpu_topology(read);
}
Result<std::vector<Device>> discover_disks(const DiskDiscoveryApi &api) {
    const auto raw = api.open();
    if (raw == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        return std::unexpected(Error{L"SetupDiGetClassDevsW (disk)", error});
    }
    const std::unique_ptr<void, DeviceInfoDeleter> devices(raw, DeviceInfoDeleter{&api});
    std::vector<Device> result;
    for (DWORD index = 0; index < 4096; ++index) {
        SP_DEVICE_INTERFACE_DATA data{};
        data.cbSize = sizeof(data);
        if (!api.next(devices.get(), index, &data)) {
            const auto error = GetLastError();
            if (error != ERROR_NO_MORE_ITEMS) {
                return std::unexpected(Error{L"SetupDiEnumDeviceInterfaces (disk)", error});
            }
            std::ranges::sort(result, {}, &Device::runtime_id);
            return result;
        }
        auto device = api.read(devices.get(), data);
        if (!device) {
            return std::unexpected(device.error());
        }
        if (device->kind != DeviceKind::disk || device->id.empty() || device->id.size() >= 1024 ||
            device->id.find(L'\0') != std::wstring::npos) {
            return std::unexpected(Error{L"Disk discovery identity data", ERROR_INVALID_DATA});
        }
        const auto existing = std::ranges::find_if(result, [&](const Device &other) {
            return other.id == device->id || other.runtime_id == device->runtime_id;
        });
        if (existing == result.end()) {
            result.push_back(std::move(*device));
        } else if (existing->id != device->id || existing->runtime_id != device->runtime_id) {
            return std::unexpected(Error{L"Conflicting disk identity mapping", ERROR_INVALID_DATA});
        }
    }
    return std::unexpected(Error{L"Disk discovery enumeration limit", ERROR_MORE_DATA});
}
Discovery discover_devices() {
    Discovery discovery;
    auto &catalog = discovery.catalog;
    // Each family is independent, including allocation/provider failures at discovery boundaries.
    try {
        auto found = discover_processors(GetLogicalProcessorInformationEx);
        if (found) {
            catalog.processors = std::move(*found);
        } else {
            discovery.cpu_error = std::move(found.error());
        }
    } catch (...) {
        discovery.cpu_error = Error{L"CPU discovery exception", ERROR_UNHANDLED_EXCEPTION};
    }
    try {
        auto found = disks();
        if (found) {
            catalog.disks = std::move(*found);
        } else {
            discovery.disk_error = std::move(found.error());
        }
    } catch (...) {
        discovery.disk_error = Error{L"Disk discovery exception", ERROR_UNHANDLED_EXCEPTION};
    }
    try {
        catalog.networks = networks();
    } catch (...) {
        catalog.detail += L"Network discovery error. ";
    }
    try {
        catalog.gpus = gpus();
    } catch (...) {
        catalog.detail += L"GPU discovery error. ";
    }
    return discovery;
}
std::optional<Device> select_device(const std::vector<Device> &devices,
                                    const std::wstring &preference) {
    std::optional<Device> result;
    for (const auto &device : devices) {
        if (preference.empty() ? device.preferred : device.id == preference) {
            if (result) {
                return std::nullopt;
            }
            result = device;
        }
    }
    return result;
}
} // namespace loadbar
