#pragma once

#include <windows.h>
#include <wrl/client.h>

#include <expected>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace loadbar {
struct LocalDeleter {
    void operator()(void *value) const noexcept {
        LocalFree(value);
    }
};
using LocalAllocation = std::unique_ptr<void, LocalDeleter>;
template <class Pointer, class Integer> Pointer message_pointer(Integer value) noexcept {
    static_assert(std::is_pointer_v<Pointer> && std::is_integral_v<Integer>);
    static_assert(sizeof(Pointer) == sizeof(Integer));
    // The Win32 message/child-ID ABI explicitly transports pointers in integer-sized slots.
    return reinterpret_cast<Pointer>(value); // NOLINT(performance-no-int-to-ptr)
}
struct Error {
    std::wstring operation;
    unsigned long code{};
};
template <class T> using Result = std::expected<T, Error>;
class Handle {
  public:
    explicit Handle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~Handle() {
        reset();
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    Handle(Handle &&other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    Handle &operator=(Handle &&other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.value_, nullptr));
        }
        return *this;
    }
    void reset(HANDLE value = nullptr) noexcept {
        if (value_ && value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
        value_ = value;
    }
    [[nodiscard]] HANDLE get() const noexcept {
        return value_;
    }
    explicit operator bool() const noexcept {
        return value_ && value_ != INVALID_HANDLE_VALUE;
    }

  private:
    HANDLE value_{};
};
class RegistryKey {
  public:
    ~RegistryKey() {
        if (value) {
            RegCloseKey(value);
        }
    }
    RegistryKey() = default;
    RegistryKey(const RegistryKey &) = delete;
    RegistryKey &operator=(const RegistryKey &) = delete;
    HKEY value{};
};
[[nodiscard]] std::wstring error_text(const Error &error);
[[nodiscard]] std::wstring utf16(const std::string &text);
} // namespace loadbar
