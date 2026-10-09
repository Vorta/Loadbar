#pragma once

#include "model/snapshot.hpp"
#include "platform/windows.hpp"
#include <functional>

namespace loadbar {
// This seam substitutes the loader only in explicit tests. Production loads System32 only.
struct DriverLibraryIo {
    std::function<HMODULE(const wchar_t *)> load;
    std::function<FARPROC(HMODULE, const char *)> symbol;
    std::function<void(HMODULE)> free;
    std::function<Result<unsigned>(const Device &)> physical_count;
};
class VendorGpuTemperature {
  public:
    explicit VendorGpuTemperature(DriverLibraryIo io = {});
    ~VendorGpuTemperature();
    VendorGpuTemperature(const VendorGpuTemperature &) = delete;
    VendorGpuTemperature &operator=(const VendorGpuTemperature &) = delete;
    void reset();
    [[nodiscard]] TemperatureReading sample(const Device &device, Clock::time_point now);

  private:
    struct State;
    struct MissingCapability {
        Device device;
        TemperatureReading reading;
        Clock::time_point retry_at;
    };
    DriverLibraryIo io_;
    std::unique_ptr<State> state_;
    std::optional<MissingCapability> missing_;
};
} // namespace loadbar
