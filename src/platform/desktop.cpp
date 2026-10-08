#include "platform/desktop.hpp"

#include <sddl.h>
#include <shellapi.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <array>
#include <format>
#include <limits>

namespace loadbar {
std::wstring error_text(const Error &error) {
    return std::format(L"{} (0x{:08X})", error.operation, error.code);
}
std::wstring utf16(const std::string &text) {
    if (text.empty() || text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {};
    }
    const auto count = static_cast<int>(text.size());
    const int size =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), count, nullptr, 0);
    if (size <= 0) {
        return L"Invalid UTF-8";
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), count, result.data(),
                             size)) {
        return L"Invalid UTF-8";
    }
    return result;
}
namespace {
struct MonitorContext {
    std::vector<Monitor> monitors;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    bool failed{};
};
BOOL CALLBACK monitor_callback(HMONITOR handle, HDC, LPRECT, LPARAM parameter) noexcept {
    auto &context = *message_pointer<MonitorContext *>(parameter);
    try {
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(handle, &info)) {
            return TRUE;
        }
        Monitor monitor;
        monitor.bounds = {info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right,
                          info.rcMonitor.bottom};
        monitor.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        monitor.label = info.szDevice;
        unsigned dpi_y{};
        if (FAILED(GetDpiForMonitor(handle, MDT_EFFECTIVE_DPI, &monitor.dpi, &dpi_y))) {
            monitor.dpi = 96;
        }
        for (const auto &path : context.paths) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{{DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME}};
            source.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source),
                             path.sourceInfo.adapterId, path.sourceInfo.id};
            if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
                std::wstring_view(source.viewGdiDeviceName) != info.szDevice) {
                continue;
            }
            DISPLAYCONFIG_TARGET_DEVICE_NAME target{{DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME}};
            target.header = {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(target),
                             path.targetInfo.adapterId, path.targetInfo.id};
            if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS) {
                monitor.id = target.monitorDevicePath;
                if (target.monitorFriendlyDeviceName[0]) {
                    monitor.label = target.monitorFriendlyDeviceName;
                }
                break;
            }
        }
        // An unresolved identity is intentionally not persisted as DISPLAYn or HMONITOR.
        monitor.label += std::format(L" ({} x {}{})", monitor.bounds.right - monitor.bounds.left,
                                     monitor.bounds.bottom - monitor.bounds.top,
                                     monitor.primary ? L", primary" : L"");
        context.monitors.push_back(std::move(monitor));
        return TRUE;
    } catch (...) {
        context.failed = true;
        return FALSE;
    }
}
unsigned shell_edge(Edge edge) {
    switch (edge) {
    case Edge::left:
        return ABE_LEFT;
    case Edge::top:
        return ABE_TOP;
    case Edge::right:
        return ABE_RIGHT;
    default:
        return ABE_BOTTOM;
    }
}
} // namespace
std::vector<Monitor> enumerate_monitors() {
    MonitorContext context;
    for (int attempt = 0; attempt < 3; ++attempt) {
        UINT32 path_count{}, mode_count{};
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) !=
                ERROR_SUCCESS ||
            path_count > 1024 || mode_count > 4096) {
            break;
        }
        context.paths.resize(path_count);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
        const auto code =
            QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, context.paths.data(),
                               &mode_count, modes.data(), nullptr);
        if (code == ERROR_SUCCESS) {
            context.paths.resize(path_count);
            break;
        }
        context.paths.clear();
        if (code != ERROR_INSUFFICIENT_BUFFER) {
            break;
        }
    }
    if (!EnumDisplayMonitors(nullptr, nullptr, monitor_callback,
                             reinterpret_cast<LPARAM>(&context)) ||
        context.failed) {
        return {};
    }
    std::ranges::sort(context.monitors, [](const Monitor &a, const Monitor &b) {
        if (a.primary != b.primary) {
            return a.primary;
        }
        return a.id < b.id;
    });
    return context.monitors;
}
Settings load_settings() {
    DWORD bytes{}, type{};
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Loadbar", L"Settings", RRF_RT_REG_SZ, &type,
                     nullptr, &bytes) != ERROR_SUCCESS ||
        bytes < sizeof(wchar_t) || bytes > 16386 || bytes % sizeof(wchar_t) != 0) {
        return {};
    }
    std::wstring buffer(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Loadbar", L"Settings", RRF_RT_REG_SZ, &type,
                     buffer.data(), &bytes) != ERROR_SUCCESS) {
        return {};
    }
    buffer.resize(bytes / sizeof(wchar_t));
    if (buffer.empty() || buffer.back() != L'\0') {
        return {};
    }
    buffer.pop_back();
    return decode_settings(buffer).value_or(Settings{});
}
Result<void> save_settings(const Settings &settings) {
    if (!valid_settings(settings)) {
        return std::unexpected(Error{L"Validate settings", ERROR_INVALID_DATA});
    }
    RegistryKey key;
    auto code = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Loadbar", 0, nullptr, 0,
                                KEY_SET_VALUE, nullptr, &key.value, nullptr);
    if (code != ERROR_SUCCESS) {
        return std::unexpected(Error{L"Open settings", static_cast<DWORD>(code)});
    }
    const auto text = encode_settings(settings);
    code = RegSetValueExW(key.value, L"Settings", 0, REG_SZ,
                          reinterpret_cast<const BYTE *>(text.c_str()),
                          static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)));
    if (code != ERROR_SUCCESS) {
        return std::unexpected(Error{L"Save settings", static_cast<DWORD>(code)});
    }
    return {};
}
Result<Handle> acquire_instance() {
    HANDLE raw_token{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token)) {
        return std::unexpected(Error{L"Open user token", GetLastError()});
    }
    Handle token(raw_token);
    DWORD size{};
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    if (size == 0 || size > 65536) {
        return std::unexpected(Error{L"Size user token", ERROR_INVALID_DATA});
    }
    std::vector<std::byte> storage(size);
    if (!GetTokenInformation(token.get(), TokenUser, storage.data(), size, &size)) {
        return std::unexpected(Error{L"Read user token", GetLastError()});
    }
    auto *user = reinterpret_cast<TOKEN_USER *>(storage.data());
    LPWSTR sid{};
    if (!ConvertSidToStringSidW(user->User.Sid, &sid)) {
        return std::unexpected(Error{L"User SID", GetLastError()});
    }
    const LocalAllocation sid_owner(sid);
    const std::wstring sid_text(sid);
    const std::wstring name = L"Local\\Loadbar.Instance." + sid_text;
    const auto descriptor_text = L"D:P(A;;GA;;;" + sid_text + L")";
    PSECURITY_DESCRIPTOR descriptor{};
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            descriptor_text.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
        return std::unexpected(Error{L"Instance security", GetLastError()});
    }
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    const LocalAllocation descriptor_owner(descriptor);
    Handle mutex(CreateMutexW(&attributes, FALSE, name.c_str()));
    const DWORD code = GetLastError();
    if (!mutex || code == ERROR_ALREADY_EXISTS) {
        return std::unexpected(Error{L"Loadbar instance", code});
    }
    return mutex;
}
bool WindowsShell::add() {
    APPBARDATA data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uCallbackMessage = callback;
    return SHAppBarMessage(ABM_NEW, &data) != 0;
}
void WindowsShell::remove() noexcept {
    APPBARDATA data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    SHAppBarMessage(ABM_REMOVE, &data);
}
Rect WindowsShell::position(DWORD message, Rect rectangle, Edge edge) const {
    APPBARDATA data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uEdge = shell_edge(edge);
    data.rc = {rectangle.left, rectangle.top, rectangle.right, rectangle.bottom};
    SHAppBarMessage(message, &data);
    return {data.rc.left, data.rc.top, data.rc.right, data.rc.bottom};
}
Rect WindowsShell::query(Rect rectangle, Edge edge) {
    return position(ABM_QUERYPOS, rectangle, edge);
}
Rect WindowsShell::set(Rect rectangle, Edge edge) {
    return position(ABM_SETPOS, rectangle, edge);
}
void WindowsShell::notify(DWORD message) const {
    APPBARDATA data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    SHAppBarMessage(message, &data);
}
} // namespace loadbar
