#pragma once

#include "model/temperature.hpp"
#include "platform/windows.hpp"
#include <functional>
#include <span>

namespace loadbar {
[[nodiscard]] TemperatureReading parse_asus_temperature(std::span<const std::byte> bytes,
                                                        Clock::time_point now);
struct AsusTemperatureIo {
    std::function<HANDLE()> open;
    std::function<BOOL(HANDLE, DWORD, void *, DWORD, void *, DWORD, DWORD *, OVERLAPPED *)> query;
    std::function<BOOL(HANDLE, OVERLAPPED *, DWORD *, BOOL)> complete;
    std::function<BOOL(HANDLE, OVERLAPPED *)> cancel;
};
class AsusTemperature {
  public:
    explicit AsusTemperature(AsusTemperatureIo io = {});
    ~AsusTemperature();
    AsusTemperature(const AsusTemperature &) = delete;
    AsusTemperature &operator=(const AsusTemperature &) = delete;
    void reset();
    // Pausing cancels outstanding I/O; sample() drains it without issuing new requests.
    void set_enabled(bool enabled);
    [[nodiscard]] const TemperatureReading &sample(Clock::time_point now, unsigned interval);

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace loadbar
