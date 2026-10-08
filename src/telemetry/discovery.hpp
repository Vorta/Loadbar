#pragma once

#include "model/snapshot.hpp"
#include "platform/windows.hpp"
#include <functional>
#include <setupapi.h>

namespace loadbar {
// Operations are injected only in tests; close must not throw. No fake discovery in production.
struct DiskDiscoveryApi {
    std::function<HDEVINFO()> open;
    std::function<BOOL(HDEVINFO, DWORD, SP_DEVICE_INTERFACE_DATA *)> next;
    std::function<Result<Device>(HDEVINFO, SP_DEVICE_INTERFACE_DATA &)> read;
    std::function<void(HDEVINFO)> close;
};
[[nodiscard]] Result<std::vector<Device>> discover_disks(const DiskDiscoveryApi &api);
using CpuTopologyRead = std::function<BOOL(LOGICAL_PROCESSOR_RELATIONSHIP,
                                           PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, PDWORD)>;
[[nodiscard]] Result<std::vector<Processor>> discover_processors(const CpuTopologyRead &read);
struct Discovery {
    Catalog catalog;
    std::optional<Error> disk_error;
    std::optional<Error> cpu_error;
};
// Application-owned, accessed only by the current joinable worker; survives worker Retry.
// Successful empty discovery clears it. Failed/incomplete discovery never replaces it.
struct DiskInventory {
    std::vector<Device> devices;
};
[[nodiscard]] Discovery discover_devices();
[[nodiscard]] std::optional<Device> select_device(const std::vector<Device> &devices,
                                                  const std::wstring &preference);
} // namespace loadbar
