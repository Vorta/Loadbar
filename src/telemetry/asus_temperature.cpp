#include "telemetry/asus_temperature.hpp"
#include <algorithm>
#include <array>
#include <cstring>

namespace loadbar {
namespace {
TemperatureReading cpu_failure(Clock::time_point now, std::wstring_view operation, DWORD code) {
    const bool absent = code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND ||
                        code == ERROR_NOT_SUPPORTED || code == ERROR_INVALID_FUNCTION;
    TemperatureReading result;
    result.metric = {0,  absent ? Status::unavailable : Status::error, Unit::celsius, now,
                     {}, error_text({std::wstring(operation), code})};
    return result;
}
} // namespace
TemperatureReading parse_asus_temperature(std::span<const std::byte> bytes, Clock::time_point now) {
    if (bytes.size() < sizeof(std::uint32_t) || bytes.size() > 16) {
        return cpu_failure(now, L"ASUS CPU temperature response size", ERROR_INVALID_DATA);
    }
    std::uint32_t raw{};
    std::memcpy(&raw, bytes.data(), sizeof(raw));
    if ((raw & 0xffff0000U) != 0x00010000U || !(raw & 0xffffU)) {
        return cpu_failure(now, L"ASUS firmware did not report CPU temperature",
                           ERROR_NOT_SUPPORTED);
    }
    const auto value = raw & 0xffffU;
    if (value > 150) {
        return cpu_failure(now, L"ASUS CPU temperature response value", ERROR_INVALID_DATA);
    }
    TemperatureReading result;
    result.metric = {static_cast<double>(value), Status::valid, Unit::celsius, now};
    result.sensor = L"ASUS firmware-reported CPU temperature";
    return result;
}
struct AsusTemperature::State {
    AsusTemperatureIo io;
    Handle handle, event;
    OVERLAPPED overlap{};
    // Reviewed read-only DSTS packet. No initialization, DEVS or firmware controls.
    std::array<std::uint32_t, 4> input{0x53545344, 8, 0x00120094, 0};
    std::array<std::byte, 16> output{};
    TemperatureReading reading;
    Clock::time_point next{}, started{};
    unsigned failures{};
    bool pending{}, cancelled{}, restart{};
    bool enabled{true};
    explicit State(AsusTemperatureIo functions) : io(std::move(functions)) {}
    ~State() {
        if (pending) {
            cancel();
            DWORD bytes{};
            // Cancellation is not completion. Keep buffers alive until the driver finishes.
            io.complete(handle.get(), &overlap, &bytes, TRUE);
        }
    }
    void cancel() {
        if (pending && !cancelled) {
            io.cancel(handle.get(), &overlap);
            cancelled = true;
        }
    }
    void finish(Clock::time_point now, DWORD bytes, DWORD code) {
        pending = false;
        reading =
            code == ERROR_SUCCESS
                ? parse_asus_temperature(
                      std::span(output).first(std::min<std::size_t>(bytes, output.size())), started)
                : cpu_failure(now, L"Read ASUS CPU temperature", code);
        if (code == ERROR_SUCCESS && bytes > output.size()) {
            reading = cpu_failure(now, L"ASUS CPU temperature response size", ERROR_INVALID_DATA);
        }
        if (reading.metric.status == Status::valid) {
            failures = 0;
        } else {
            failures = std::min(failures + 1, 5U);
            handle.reset();
            event.reset();
        }
    }
    void schedule(Clock::time_point now, unsigned interval) {
        const auto delay = failures ? std::min(30U, 1U << failures) * 1000U : interval;
        next = now + std::chrono::milliseconds(std::max(interval, delay));
    }
};
AsusTemperature::AsusTemperature(AsusTemperatureIo io) {
    if (!io.open) {
        io.open = [] {
            return CreateFileW(L"\\\\.\\ATKACPI", 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        };
    }
    if (!io.query) {
        io.query = DeviceIoControl;
    }
    if (!io.complete) {
        io.complete = GetOverlappedResult;
    }
    if (!io.cancel) {
        io.cancel = CancelIoEx;
    }
    state_ = std::make_unique<State>(std::move(io));
}
AsusTemperature::~AsusTemperature() = default;
void AsusTemperature::set_enabled(bool enabled) {
    if (state_->enabled != enabled) {
        state_->enabled = enabled;
        reset();
    }
}
void AsusTemperature::reset() {
    auto &s = *state_;
    s.cancel();
    s.restart = true;
    s.next = {};
    s.failures = 0;
    s.reading = {};
    s.reading.metric.status = Status::warming_up;
    if (!s.pending) {
        s.handle.reset();
        s.event.reset();
    }
}
const TemperatureReading &AsusTemperature::sample(Clock::time_point now, unsigned interval) {
    auto &s = *state_;
    interval = std::max(1000U, interval);
    if (s.pending) {
        DWORD bytes{};
        const bool complete = s.io.complete(s.handle.get(), &s.overlap, &bytes, FALSE) != FALSE;
        const auto code = complete ? ERROR_SUCCESS : GetLastError();
        if (complete || code != ERROR_IO_INCOMPLETE) {
            if (s.cancelled) {
                s.pending = false;
                s.handle.reset();
                s.event.reset();
                if (s.restart) {
                    s.next = {};
                }
            } else {
                s.finish(now, bytes, code);
                s.schedule(now, interval);
            }
        } else if (!s.cancelled && now - s.started >= std::chrono::seconds(2)) {
            s.cancel();
            s.reading = cpu_failure(now, L"ASUS CPU temperature timed out", ERROR_TIMEOUT);
            s.next = now + std::chrono::seconds(30);
        }
        return s.reading;
    }
    if (!s.enabled || now < s.next) {
        return s.reading;
    }
    s.next = now + std::chrono::seconds(30);
    s.restart = false;
    if (!s.handle) {
        s.handle.reset(s.io.open());
        if (!s.handle) {
            const auto code = GetLastError();
            s.reading = cpu_failure(now, L"CPU temperature is not available", code);
            s.failures = std::min(s.failures + 1, 5U);
            s.schedule(now, interval);
            return s.reading;
        }
        s.event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!s.event) {
            const auto code = GetLastError();
            s.finish(now, 0, code);
            s.schedule(now, interval);
            return s.reading;
        }
    }
    s.overlap = {};
    s.overlap.hEvent = s.event.get();
    ResetEvent(s.event.get());
    s.output.fill({});
    s.cancelled = false;
    s.started = now;
    DWORD bytes{};
    if (s.io.query(s.handle.get(), 0x0022240c, s.input.data(), sizeof(s.input), s.output.data(),
                   sizeof(s.output), &bytes, &s.overlap)) {
        s.finish(now, bytes, ERROR_SUCCESS);
        s.schedule(now, interval);
    } else {
        const auto code = GetLastError();
        if (code == ERROR_IO_PENDING) {
            s.pending = true;
            s.reading.metric.status = Status::warming_up;
            s.reading.metric.detail = L"ASUS CPU temperature request pending";
        } else {
            s.finish(now, 0, code);
            s.schedule(now, interval);
        }
    }
    return s.reading;
}
} // namespace loadbar
