#pragma once
#include "model/snapshot.hpp"
#include "platform/windows.hpp"
#include "telemetry/asus_temperature.hpp"
#include <functional>
#include <memory>

namespace loadbar {
// Pure parsing of a completed Windows storage descriptor; also exercised with hostile buffers.
[[nodiscard]] TemperatureReading parse_storage_temperature(std::span<const std::byte> bytes,
                                                           Clock::time_point now);
// Optional, owned test seams. Empty callbacks use the real Windows implementation.
struct TemperatureIo {
    AsusTemperatureIo cpu;
    std::function<HANDLE(const Device &)> open_disk;
    std::function<BOOL(HANDLE, BOOL *)> power_state;
    std::function<BOOL(HANDLE, void *, DWORD, DWORD *, OVERLAPPED *)> query_disk;
    std::function<BOOL(HANDLE, OVERLAPPED *, DWORD *, BOOL)> complete_disk;
    std::function<BOOL(HANDLE, OVERLAPPED *)> cancel_disk;
    std::function<TemperatureReading(const Device &, Clock::time_point)> read_gpu;
    std::function<TemperatureReading(const Device &, Clock::time_point)> read_vendor_gpu;
};
class TemperatureProviders {
  public:
    explicit TemperatureProviders(TemperatureIo io = {});
    ~TemperatureProviders();
    TemperatureProviders(const TemperatureProviders &) = delete;
    TemperatureProviders &operator=(const TemperatureProviders &) = delete;
    void configure(const Catalog &catalog, const std::optional<Device> &gpu,
                   const Settings &settings, bool reset, bool disk_discovery_failed,
                   bool reset_gpu = false);
    void sample(Snapshot &snapshot, Clock::time_point now);
    [[nodiscard]] std::size_t disk_request_count() const noexcept;

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace loadbar
