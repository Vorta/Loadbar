#include "app/application.hpp"
#include "model/placement.hpp"
#include "ui/control_ids.hpp"

#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>
#include <wtsapi32.h>

#include <algorithm>
#include <format>
#include <stdexcept>

namespace loadbar {
namespace {
constexpr UINT kAppbarMessage = WM_APP + 1;
constexpr UINT kTrayMessage = WM_APP + 2;
constexpr UINT kLayoutMessage = WM_APP + 3;
constexpr UINT kSampleMessage = WM_APP + 4;
constexpr UINT kPreferencesMessage = WM_APP + 5;
constexpr UINT kExit = 100;
constexpr UINT kSettings = 101;
constexpr UINT kRetry = 102;
constexpr UINT kEdgeBase = 200;
constexpr UINT kMonitorBase = 300;
constexpr wchar_t kWindowClass[] = L"Loadbar.Bar";
constexpr wchar_t kSettingsClass[] = L"Loadbar.Settings";
constexpr wchar_t kContentClass[] = L"Loadbar.SettingsContent";
} // namespace
Application::Application(HINSTANCE instance)
    : instance_(instance), settings_(load_settings()), appbar_(shell_) {}
Application::~Application() {
    shutdown();
}
int Application::run() {
    auto ownership = acquire_instance();
    if (!ownership) {
        const auto text = ownership.error().code == ERROR_ALREADY_EXISTS
                              ? L"Loadbar is already running in this user session. Use its "
                                L"notification-area icon to open Settings or Exit."
                              : L"Loadbar could not establish single-instance ownership.";
        MessageBoxW(nullptr, text, L"Loadbar", MB_OK | MB_ICONINFORMATION);
        return ownership.error().code == ERROR_ALREADY_EXISTS ? 0 : 1;
    }
    instance_lock_ = std::move(*ownership);
    for (const auto *name : {kWindowClass, kSettingsClass, kContentClass}) {
        WNDCLASSEXW cls{};
        cls.cbSize = sizeof(cls);
        cls.hInstance = instance_;
        cls.lpfnWndProc = window_proc;
        cls.lpszClassName = name;
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(1));
        if (!RegisterClassExW(&cls)) {
            throw std::runtime_error("Register Loadbar window class failed");
        }
    }
    taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");
    if (!taskbar_created_) {
        throw std::runtime_error("Register TaskbarCreated failed");
    }
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"Loadbar", WS_POPUP, 0, 0, 1, 1,
                              nullptr, nullptr, instance_, this);
    if (!window_) {
        throw std::runtime_error("Create Loadbar window failed");
    }
    shell_.window = window_;
    shell_.callback = kAppbarMessage;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    if (!InitCommonControlsEx(&controls)) {
        throw std::runtime_error("Initialize common controls failed");
    }
    text_preferences_ = std::make_unique<TextPreferences>(window_, kPreferencesMessage);
    text_scale_ = text_preferences_->scale();
    tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, window_,
                               nullptr, instance_, nullptr);
    if (tooltip_) {
        TOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tool.hwnd = window_;
        tool.uId = reinterpret_cast<UINT_PTR>(window_);
        tooltip_text_ = L"Loadbar — current readings";
        tool.lpszText = tooltip_text_.data();
        SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, 600);
        update_tooltip_activation();
        SendMessageW(tooltip_, TTM_SETDELAYTIME, TTDT_INITIAL, 800);
        SendMessageW(tooltip_, TTM_SETDELAYTIME, TTDT_RESHOW, 800);
    }
    WTSRegisterSessionNotification(window_, NOTIFY_FOR_THIS_SESSION);
    place_bar();
    if (!add_tray()) {
        show_settings();
    }
    worker_ =
        std::make_unique<SamplingWorker>(window_, kSampleMessage, delivery_, session_peaks_,
                                         continuity_, known_disks_, known_processors_, settings_);
    schedule_refresh();
    MSG event{};
    int code{};
    while ((code = static_cast<int>(GetMessageW(&event, nullptr, 0, 0))) > 0) {
        if (!settings_window_ || !IsDialogMessageW(settings_window_, &event)) {
            TranslateMessage(&event);
            DispatchMessageW(&event);
        }
    }
    shutdown();
    return code < 0 ? 1 : 0;
}
LRESULT CALLBACK Application::window_proc(HWND window, UINT message, WPARAM wparam,
                                          LPARAM lparam) noexcept {
    auto *app = message_pointer<Application *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<Application *>(message_pointer<CREATESTRUCTW *>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        const auto *creation = message_pointer<CREATESTRUCTW *>(lparam);
        if (wcscmp(creation->lpszClass, kSettingsClass) == 0) {
            app->settings_window_ = window;
        } else if (wcscmp(creation->lpszClass, kContentClass) == 0) {
            app->settings_content_ = window;
        } else {
            app->window_ = window;
        }
    }
    if (!app) {
        return DefWindowProcW(window, message, wparam, lparam);
    }
    try {
        return app->message(window, message, wparam, lparam);
    } catch (...) {
        app->shutdown();
        MessageBoxW(
            nullptr,
            L"Loadbar encountered an unexpected failure and released its desktop reservation.",
            L"Loadbar", MB_OK | MB_ICONERROR);
        PostQuitMessage(1);
        return 0;
    }
}
void Application::place_bar() {
    if (closing_ || placing_) {
        return;
    }
    placing_ = true;
    try {
        update_monitors(enumerate_monitors());
        const auto selected = select_monitor(monitors_, settings_.monitor_id);
        if (!selected) {
            throw std::runtime_error("No monitor");
        }
        fallback_ = selected->fallback;
        const auto &monitor = monitors_[selected->index];
        if (active_monitor_ != monitor.id) {
            appbar_.remove();
            active_monitor_ = monitor.id;
        }
        if (!migrate_thickness(settings_, monitor.bounds, monitor.dpi)) {
            throw std::runtime_error("Invalid monitor for thickness migration");
        }
        const auto edge = resolve_edge(settings_.edge, monitor.bounds);
        effective_edge_ = edge;
        const auto rectangle =
            place_readable_bar(appbar_, monitor, settings_, snapshot_.processors, text_scale_,
                               disk_widget_count(snapshot_.disks, settings_));
        if (!rectangle) {
            throw std::runtime_error("AppBar could not negotiate a readable region");
        }
        RECT current{};
        GetWindowRect(window_, &current);
        if (current.left != rectangle->left || current.top != rectangle->top ||
            current.right != rectangle->right || current.bottom != rectangle->bottom ||
            !IsWindowVisible(window_)) {
            if (!SetWindowPos(window_, fullscreen_ ? HWND_BOTTOM : HWND_TOPMOST, rectangle->left,
                              rectangle->top, rectangle->right - rectangle->left,
                              rectangle->bottom - rectangle->top,
                              SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
                throw std::runtime_error("Position AppBar failed");
            }
        }
        placement_error_.clear();
        recovery_.desktop = false;
        update_settings_status();
        recovery_attempts_ = 0;
    } catch (...) {
        appbar_.remove();
        ShowWindow(window_, SW_HIDE);
        recovery_.desktop = true;
        placement_error_ = L"The AppBar could not reserve a readable desktop region. Choose "
                           L"another monitor, edge or size and Apply, or choose Retry monitoring.";
        show_settings(false);
        if (recovery_attempts_ < 5) {
            SetTimer(window_, 2, 1000U << recovery_attempts_, nullptr);
            ++recovery_attempts_;
        }
    }
    placing_ = false;
}
bool Application::add_tray() {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window_;
    data.uID = 1;
    // Version 4 suppresses the standard tooltip unless explicitly requested.
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = kTrayMessage;
    data.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(1));
    wcscpy_s(data.szTip, L"Loadbar — Settings and Exit");
    tray_added_ = notify_icon_(tray_added_ ? NIM_MODIFY : NIM_ADD, &data) != FALSE;
    if (tray_added_) {
        data.uVersion = NOTIFYICON_VERSION_4;
        notify_icon_(NIM_SETVERSION, &data);
    }
    recovery_.tray = !tray_added_;
    update_settings_status();
    return tray_added_;
}
void Application::remove_tray() noexcept {
    if (tray_added_) {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = 1;
        notify_icon_(NIM_DELETE, &data);
        tray_added_ = false;
    }
}
void Application::update_tooltip_activation() {
    if (tooltip_) {
        SendMessageW(tooltip_, TTM_POP, 0, 0);
        SendMessageW(tooltip_, TTM_ACTIVATE, settings_.show_tooltips, 0);
    }
    tooltip_cache_ = {};
}
void Application::update_tooltip() {
    if (!tooltip_ || !window_ || !settings_.show_tooltips) {
        return;
    }
    POINT point{};
    RECT client{};
    if (!IsWindowVisible(window_) || !GetCursorPos(&point) || !ScreenToClient(window_, &point) ||
        !GetClientRect(window_, &client) || !PtInRect(&client, point)) {
        if (IsWindowVisible(tooltip_)) {
            SendMessageW(tooltip_, TTM_POP, 0, 0);
        }
        return;
    }
    const float scale = static_cast<float>(GetDpiForWindow(window_)) / 96;
    const auto x = static_cast<float>(point.x) / scale;
    const auto y = static_cast<float>(point.y) / scale;
    const auto now = Clock::now();
    const TooltipKey key{renderer_.tooltip_target(x, y), renderer_.layout_revision(),
                         observation_revision_, fallback_};
    if (tooltip_cache_.fresh(key, now)) {
        return;
    }
    auto text = renderer_.tooltip(x, y, snapshot_, settings_);
    if (fallback_) {
        text += L"\nPreferred monitor unavailable — using primary monitor";
    }
    auto expiry = next_retention_deadline(snapshot_, now, settings_.interval_ms, settings_);
    const auto stale = next_stale_deadline(snapshot_, settings_.interval_ms, settings_);
    if (stale && *stale > now && (!expiry || *stale < *expiry)) {
        expiry = stale;
    }
    tooltip_cache_.remember(key, expiry);
    if (text == tooltip_text_) {
        return;
    }
    tooltip_text_ = std::move(text);
    TOOLINFOW tool{};
    tool.cbSize = sizeof(tool);
    tool.hwnd = window_;
    tool.uId = reinterpret_cast<UINT_PTR>(window_);
    tool.hinst = instance_;
    tool.lpszText = tooltip_text_.data();
    SendMessageW(tooltip_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
    if (IsWindowVisible(tooltip_)) {
        SendMessageW(tooltip_, TTM_UPDATE, 0, 0);
    }
}
void Application::schedule_refresh() {
    if (!window_ || closing_) {
        return;
    }
    const auto now = Clock::now();
    auto deadline = next_stale_deadline(snapshot_, settings_.interval_ms, settings_);
    // Age text needs ticks only while it can be read. Valid sampling needs no independent
    // periodic UI wakeups; each accepted sample moves the stale deadline forward.
    if (!paused_ &&
        ((settings_.show_tooltips && hovered_ && IsWindowVisible(window_)) ||
         (settings_window_ && IsWindowVisible(settings_window_) && !IsIconic(settings_window_)))) {
        const auto age = next_retention_deadline(snapshot_, now, settings_.interval_ms, settings_);
        if (age && (!deadline || *age < *deadline)) {
            deadline = age;
        }
    }
    if (deadline == refresh_deadline_) {
        return;
    }
    if (deadline) {
        const auto delay = std::chrono::ceil<std::chrono::milliseconds>(*deadline - now).count();
        const auto milliseconds = static_cast<UINT>(
            std::clamp<std::int64_t>(delay, USER_TIMER_MINIMUM, USER_TIMER_MAXIMUM));
        if (!SetTimer(window_, 1, milliseconds, nullptr)) {
            const auto error = GetLastError();
            throw std::runtime_error(
                std::format("Schedule metric freshness update failed: {}", error));
        }
    } else {
        KillTimer(window_, 1);
    }
    refresh_deadline_ = deadline;
}
void Application::tray_menu() {
    const HMENU menu = CreatePopupMenu();
    if (!menu) {
        show_settings();
        return;
    }
    AppendMenuW(menu, MF_STRING, kSettings, L"&Settings / metric details…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    constexpr const wchar_t *edges[]{L"&Automatic edge", L"&Left", L"&Top", L"&Right", L"&Bottom"};
    for (unsigned i = 0; i < 5; ++i) {
        AppendMenuW(menu, MF_STRING | (static_cast<unsigned>(settings_.edge) == i ? MF_CHECKED : 0),
                    kEdgeBase + i, edges[i]);
    }
    for (std::size_t i = 0; i < monitors_.size(); ++i) {
        AppendMenuW(menu, MF_STRING | (monitors_[i].id == active_monitor_ ? MF_CHECKED : 0),
                    kMonitorBase + i, monitors_[i].label.c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kExit, L"E&xit");
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(window_);
    const UINT command = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                                          point.x, point.y, 0, window_, nullptr));
    DestroyMenu(menu);
    PostMessageW(window_, WM_NULL, 0, 0);
    if (command) {
        SendMessageW(window_, WM_COMMAND, command, 0);
    }
}
void Application::shutdown() noexcept {
    if (closing_) {
        return;
    }
    closing_ = true;
    delivery_.close();
    appbar_.remove();
    remove_tray();
    if (window_) {
        KillTimer(window_, 1);
        KillTimer(window_, 2);
    }
    if (worker_) {
        worker_->stop();
        worker_.reset();
    }
    text_preferences_.reset();
    renderer_.discard();
    if (tooltip_) {
        DestroyWindow(tooltip_);
        tooltip_ = nullptr;
    }
    if (window_) {
        WTSUnRegisterSessionNotification(window_);
    }
    if (settings_window_) {
        DestroyWindow(settings_window_);
        settings_window_ = nullptr;
        settings_content_ = nullptr;
    }
    if (settings_font_) {
        DeleteObject(settings_font_);
        settings_font_ = nullptr;
    }
    if (window_) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
    PostQuitMessage(0);
}
void Application::sampling_failed(std::optional<Delivery> update) {
    if (update && update->has_snapshot) {
        snapshot_ = std::move(update->snapshot);
        detail_structure_changed_ = true;
        if (update->catalog) {
            catalog_ = std::move(update->catalog);
            if (settings_window_) {
                populate_devices(false);
            }
        }
        place_bar();
    }
    set_snapshot_status(snapshot_, Status::error, L"Sampling worker stopped");
    ++observation_revision_;
    recovery_.sampling = true;
    recovery_.restarting = false;
    show_settings(false);
    InvalidateRect(window_, nullptr, FALSE);
    update_tooltip();
    schedule_refresh();
}
LRESULT Application::message(HWND window, UINT message_id, WPARAM wparam, LPARAM lparam) {
    if (message_id == WM_NCDESTROY) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    }
    if (closing_) {
        return DefWindowProcW(window, message_id, wparam, lparam);
    }
    if (window == settings_content_ || window == settings_window_) {
        if (message_id == WM_CTLCOLORSTATIC || message_id == WM_CTLCOLORBTN) {
            const int id = GetDlgCtrlID(message_pointer<HWND>(lparam));
            if ((id >= kFirstLabel && id <= kFirstLabel + 7) || id == kFooterStatus ||
                id == kDrivesLabel || id == kCpuSquares || id == kHoverInfo ||
                id == kTaskManagerClick || id == kTooltips) {
                const auto dc = message_pointer<HDC>(wparam);
                const auto color = IsWindowEnabled(message_pointer<HWND>(lparam)) ? COLOR_WINDOWTEXT
                                                                                  : COLOR_GRAYTEXT;
                if (SetTextColor(dc, GetSysColor(color)) != CLR_INVALID &&
                    SetBkColor(dc, GetSysColor(COLOR_WINDOW)) != CLR_INVALID &&
                    SetBkMode(dc, TRANSPARENT) != 0) {
                    return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
                }
            }
        }
        if (message_id == WM_SYSCOLORCHANGE || message_id == WM_THEMECHANGED) {
            RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        }
    }
    if (window == settings_content_) {
        if (message_id == WM_COMMAND || message_id == WM_NOTIFY || message_id == WM_VSCROLL ||
            message_id == WM_MOUSEWHEEL) {
            return SendMessageW(settings_window_, message_id, wparam, lparam);
        }
        if (message_id == WM_ERASEBKGND) {
            RECT bounds{};
            GetClientRect(window, &bounds);
            FillRect(message_pointer<HDC>(wparam), &bounds, GetSysColorBrush(COLOR_WINDOW));
            return 1;
        }
        return DefWindowProcW(window, message_id, wparam, lparam);
    }
    if (message_id == WM_COMMAND) {
        const UINT command = LOWORD(wparam);
        const UINT notification = HIWORD(wparam);
        if (window == settings_window_ &&
            (notification == EN_SETFOCUS || notification == CBN_SETFOCUS ||
             notification == BN_SETFOCUS)) {
            ensure_visible(message_pointer<HWND>(lparam));
        }
        if (window == settings_window_ && command >= kFirstField && command < kFirstField + 7 &&
            (notification == EN_CHANGE || notification == CBN_SELCHANGE)) {
            update_settings_actions();
            return 0;
        }
        if (!command_activates(notification, lparam != 0)) {
            return 0;
        }
        if (window == settings_window_ && lparam != 0 && command >= kCpuSquares &&
            command <= kTooltips) {
            update_settings_actions();
            return 0;
        }
        if (command == kApplySettings || (window == settings_window_ && command == IDOK)) {
            if (settings_dirty_) {
                apply_settings();
            }
            return 0;
        }
        if (window == settings_window_ && command == kCloseSettings) {
            cancel_settings();
            ShowWindow(settings_window_, SW_HIDE);
            schedule_refresh();
            return 0;
        }
        if (window == settings_window_ && command == IDCANCEL) {
            SendMessageW(settings_window_, WM_CLOSE, 0, 0);
            return 0;
        }
        if (command == kCancelSettings) {
            if (settings_dirty_) {
                cancel_settings();
            }
            return 0;
        }
        if (command == kExit) {
            shutdown();
            return 0;
        }
        if (command == kSettings) {
            show_settings();
            return 0;
        }
        if (command == kRetry) {
            if (!recovery_.can_retry()) {
                return 0;
            }
            if (recovery_.sampling) {
                if (worker_) {
                    worker_->stop();
                    worker_.reset();
                }
                worker_ = std::make_unique<SamplingWorker>(
                    window_, kSampleMessage, delivery_, session_peaks_, continuity_, known_disks_,
                    known_processors_, settings_, ++expected_generation_, ++worker_id_);
                if (paused_) {
                    ++expected_generation_;
                    worker_->configure(settings_, true);
                }
                recovery_.restarting = true;
            }
            if (recovery_.desktop) {
                place_bar();
            }
            if (recovery_.tray) {
                add_tray();
            }
            if (recovery_.rendering) {
                renderer_.discard();
                InvalidateRect(window_, nullptr, FALSE);
            }
            update_settings_status();
            return 0;
        }
        auto desired = settings_;
        if (command >= kEdgeBase && command < kEdgeBase + 5) {
            desired.edge = static_cast<Edge>(command - kEdgeBase);
        } else if (command >= kMonitorBase && command < kMonitorBase + monitors_.size()) {
            desired.monitor_id = monitors_[command - kMonitorBase].id;
        } else {
            return 0;
        }
        commit_settings(std::move(desired));
        return 0;
    }
    if (message_id == WM_CLOSE) {
        if (window == settings_window_ && tray_added_) {
            cancel_settings();
            ShowWindow(window, SW_HIDE);
            schedule_refresh();
        } else {
            shutdown();
        }
        return 0;
    }
    if (window == settings_window_) {
        if (message_id == DM_GETDEFID) {
            return settings_dirty_ ? MAKELRESULT(kApplySettings, DC_HASDEFID) : 0;
        }
        if (message_id == WM_GETMINMAXINFO) {
            const auto dpi = GetDpiForWindow(window);
            const auto scale = static_cast<float>(dpi) / 96 * text_scale_;
            RECT minimum{0, 0, static_cast<LONG>(320 * scale), static_cast<LONG>(240 * scale)};
            if (!AdjustWindowRectExForDpi(&minimum, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_CONTROLPARENT,
                                          dpi)) {
                return DefWindowProcW(window, message_id, wparam, lparam);
            }
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) {
                auto &info = *message_pointer<MINMAXINFO *>(lparam);
                info.ptMinTrackSize = {std::min(minimum.right - minimum.left,
                                                monitor.rcWork.right - monitor.rcWork.left),
                                       std::min(minimum.bottom - minimum.top,
                                                monitor.rcWork.bottom - monitor.rcWork.top)};
            }
            return 0;
        }
        if (message_id == WM_DPICHANGED) {
            const auto &suggested = *message_pointer<RECT *>(lparam);
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            if (!GetMonitorInfoW(MonitorFromRect(&suggested, MONITOR_DEFAULTTONEAREST), &info)) {
                throw std::runtime_error(
                    std::format("Settings DPI monitor failed: {}", GetLastError()));
            }
            const auto bounds = fit_window(
                {suggested.left, suggested.top, suggested.right, suggested.bottom},
                {info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom});
            if (!bounds) {
                throw std::runtime_error("Invalid Settings DPI bounds");
            }
            if (!SetWindowPos(window, nullptr, bounds->left, bounds->top,
                              bounds->right - bounds->left, bounds->bottom - bounds->top,
                              SWP_NOZORDER | SWP_NOACTIVATE)) {
                throw std::runtime_error(
                    std::format("Settings DPI position failed: {}", GetLastError()));
            }
            layout_settings();
            return 0;
        }
        if (message_id == WM_SIZE) {
            const bool restoring = settings_minimized_ && wparam != SIZE_MINIMIZED;
            settings_minimized_ = wparam == SIZE_MINIMIZED;
            if (!settings_minimized_) {
                layout_settings();
                // Resize changes geometry only. Show/sample/freshness events update text;
                // restore catches observations skipped while the window was minimized.
                if (restoring) {
                    update_details();
                }
            }
            if (settings_minimized_ || restoring) {
                schedule_refresh();
            }
            return 0;
        }
        if (message_id == WM_ERASEBKGND) {
            RECT bounds{};
            GetClientRect(window, &bounds);
            FillRect(message_pointer<HDC>(wparam), &bounds, GetSysColorBrush(COLOR_WINDOW));
            return 1;
        }
        if (message_id == WM_VSCROLL || message_id == WM_MOUSEWHEEL) {
            SCROLLINFO info{sizeof(info), SIF_ALL};
            GetScrollInfo(settings_content_, SB_VERT, &info);
            if (message_id == WM_MOUSEWHEEL) {
                scroll_ -= GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA * 60;
            } else {
                switch (LOWORD(wparam)) {
                case SB_LINEUP:
                    scroll_ -= 30;
                    break;
                case SB_LINEDOWN:
                    scroll_ += 30;
                    break;
                case SB_PAGEUP:
                    scroll_ -= static_cast<int>(info.nPage);
                    break;
                case SB_PAGEDOWN:
                    scroll_ += static_cast<int>(info.nPage);
                    break;
                case SB_TOP:
                    scroll_ = info.nMin;
                    break;
                case SB_BOTTOM:
                    scroll_ = info.nMax;
                    break;
                case SB_THUMBPOSITION:
                case SB_THUMBTRACK:
                    scroll_ = info.nTrackPos;
                    break;
                default:
                    break;
                }
            }
            layout_settings();
            return 0;
        }
        if (message_id == WM_NOTIFY && message_pointer<NMHDR *>(lparam)->idFrom == kDrivesControl &&
            message_pointer<NMHDR *>(lparam)->code == LVN_ITEMCHANGED) {
            const auto *change = message_pointer<NMLISTVIEW *>(lparam);
            if ((change->uChanged & LVIF_STATE) != 0 &&
                ((change->uOldState ^ change->uNewState) & LVIS_STATEIMAGEMASK) != 0) {
                drive_check_changed(change->iItem);
            }
        }
        if (message_id == WM_NOTIFY && message_pointer<NMHDR *>(lparam)->code == NM_SETFOCUS) {
            ensure_visible(message_pointer<NMHDR *>(lparam)->hwndFrom);
        }
        return DefWindowProcW(window, message_id, wparam, lparam);
    }
    if (message_id == taskbar_created_) {
        appbar_.shell_restarted();
        tray_added_ = false;
        place_bar();
        if (!add_tray()) {
            show_settings(false);
        }
        return 0;
    }
    switch (message_id) {
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wparam) {
            shutdown();
        }
        return 0;
    case WM_APP + 20:
        if (static_cast<std::uint64_t>(wparam) == worker_id_) {
            sampling_failed(delivery_.take());
        }
        return 0;
    case kSampleMessage:
        if (auto update = delivery_.take();
            update && accept_delivery(*update, worker_id_, expected_generation_)) {
            if (update->worker_failed) {
                sampling_failed(std::move(update));
                return 0;
            }
            if (recovery_.sampling) {
                recovery_.sampling = false;
                recovery_.restarting = false;
                update_settings_status();
            }
            const bool changed_topology =
                disk_widget_count(snapshot_.disks, settings_) !=
                    disk_widget_count(update->snapshot.disks, settings_) ||
                !std::ranges::equal(snapshot_.processors, update->snapshot.processors,
                                    [](const Processor &a, const Processor &b) {
                                        return a.id == b.id && a.core == b.core &&
                                               a.mapped == b.mapped &&
                                               a.efficiency_class == b.efficiency_class;
                                    });
            detail_structure_changed_ =
                detail_structure_changed_ || changed_topology ||
                snapshot_.gpu_memory_label != update->snapshot.gpu_memory_label ||
                !std::ranges::equal(snapshot_.disks, update->snapshot.disks,
                                    [](const DiskReading &a, const DiskReading &b) {
                                        return a.id == b.id && a.label == b.label;
                                    });
            if (!paused_ && IsWindowVisible(window_)) {
                renderer_.invalidate_changes(window_, snapshot_, update->snapshot, settings_);
            }
            snapshot_ = std::move(update->snapshot);
            ++observation_revision_;
            if (update->catalog && update->catalog != catalog_) {
                detail_structure_changed_ = true;
                catalog_ = std::move(update->catalog);
                if (settings_window_) {
                    populate_devices(false);
                }
                if (!selection_prompted_ &&
                    ((settings_.gpu_visible && !select_device(catalog_->gpus, settings_.gpu_id)) ||
                     (settings_.network_visible &&
                      !select_device(catalog_->networks, settings_.network_id)))) {
                    selection_prompted_ = true;
                    show_settings(false);
                }
            }
            if (changed_topology) {
                place_bar();
            }
            update_details();
            update_tooltip();
            schedule_refresh();
        }
        return 0;
    case kPreferencesMessage:
        text_scale_ = text_preferences_ ? text_preferences_->scale() : 1;
        renderer_.preferences_changed();
        place_bar();
        layout_settings();
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_TIMER:
        if (wparam == 2) {
            KillTimer(window_, 2);
            place_bar();
            if (!tray_added_ && !add_tray()) {
                show_settings(false);
            }
        } else if (wparam == 1) {
            KillTimer(window_, 1);
            refresh_deadline_.reset();
            if (expire_snapshot(snapshot_, Clock::now(), settings_.interval_ms, settings_)) {
                ++observation_revision_;
                if (!paused_ && IsWindowVisible(window_)) {
                    InvalidateRect(window_, nullptr, FALSE);
                }
            }
            update_details();
            update_tooltip();
            schedule_refresh();
        }
        return 0;
    case WM_POWERBROADCAST:
        if (wparam == PBT_APMSUSPEND) {
            activity_.suspended = true;
            paused_ = activity_.paused();
            reconfigure_worker();
        } else if (wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND) {
            activity_.suspended = false;
            paused_ = activity_.paused();
            reconfigure_worker();
            place_bar();
        }
        return TRUE;
    case WM_WTSSESSION_CHANGE:
        if (wparam == WTS_SESSION_LOCK || wparam == WTS_SESSION_UNLOCK) {
            activity_.locked = wparam == WTS_SESSION_LOCK;
            paused_ = activity_.paused();
            reconfigure_worker();
        }
        return 0;
    case WM_DEVICECHANGE:
        reconfigure_worker();
        return 0;
    case WM_MOUSEMOVE:
        if (!hovered_) {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window_, 0};
            if (TrackMouseEvent(&tracking)) {
                hovered_ = true;
                if (settings_.show_hover_info) {
                    InvalidateRect(window_, nullptr, FALSE);
                }
                schedule_refresh();
            }
        }
        update_tooltip();
        return 0;
    case WM_MOUSELEAVE:
        hovered_ = false;
        schedule_refresh();
        if (settings_.show_hover_info) {
            InvalidateRect(window_, nullptr, FALSE);
        }
        if (tooltip_) {
            SendMessageW(tooltip_, TTM_POP, 0, 0);
        }
        return 0;
    case kAppbarMessage:
        if (wparam == ABN_POSCHANGED && !placing_) {
            place_bar();
        } else if (wparam == ABN_FULLSCREENAPP) {
            fullscreen_ = lparam != 0;
            SetWindowPos(window_, fullscreen_ ? HWND_BOTTOM : HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return 0;
    case WM_ACTIVATE:
        if (appbar_.registered()) {
            shell_.notify(ABM_ACTIVATE);
        }
        break;
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) {
            // Shell placement can preserve old pixels while changing the readable layout.
            // Request paint even when telemetry is unchanged; hidden windows paint on exposure.
            resize_invalidate_(window, nullptr, FALSE);
        }
        return 0;
    case WM_WINDOWPOSCHANGED:
        if (appbar_.registered()) {
            shell_.notify(ABM_WINDOWPOSCHANGED);
        }
        break;
    case WM_DISPLAYCHANGE:
        appbar_.invalidate_position();
        reconfigure_worker();
        place_bar();
        return 0;
    case WM_DPICHANGED:
        appbar_.invalidate_position();
        renderer_.discard();
        place_bar();
        return 0;
    case WM_SETTINGCHANGE:
        PostMessageW(window_, kPreferencesMessage, 0, 0);
        return 0;
    case kLayoutMessage:
        place_bar();
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_RETURN || wparam == VK_SPACE) {
            show_settings();
        }
        return 0;
    case WM_LBUTTONUP: {
        if (!settings_.open_task_manager_on_click) {
            return 0;
        }
        std::wstring path(32768, L'\0');
        const auto length = GetSystemDirectoryW(path.data(), static_cast<UINT>(path.size()));
        if (length == 0 || length >= path.size()) {
            MessageBoxW(window_, L"Task Manager path could not be resolved.", L"Loadbar",
                        MB_OK | MB_ICONWARNING);
            return 0;
        }
        path.resize(length);
        path += L"\\Taskmgr.exe";
        SHELLEXECUTEINFOW launch{};
        launch.cbSize = sizeof(launch);
        launch.fMask = SEE_MASK_FLAG_NO_UI;
        launch.hwnd = window_;
        launch.lpFile = path.c_str();
        launch.nShow = SW_SHOWNORMAL;
        if (!launch_task_manager_(&launch)) {
            const auto code = GetLastError();
            MessageBoxW(window_, error_text({L"Open Task Manager", code}).c_str(), L"Loadbar",
                        MB_OK | MB_ICONWARNING);
        }
        return 0;
    }
    case kTrayMessage:
        if (LOWORD(lparam) == WM_CONTEXTMENU || LOWORD(lparam) == NIN_KEYSELECT) {
            tray_menu();
        } else if (LOWORD(lparam) == NIN_SELECT) {
            show_settings();
        }
        return 0;
    case WM_CONTEXTMENU:
        tray_menu();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        const auto rendered = paused_
                                  ? S_OK
                                  : renderer_.paint(window, snapshot_, settings_, effective_edge_,
                                                    fallback_, text_scale_, hovered_);
        EndPaint(window, &paint);
        update_tooltip();
        if (FAILED(rendered) && rendered != D2DERR_RECREATE_TARGET) {
            const bool first_failure = !recovery_.rendering;
            recovery_.rendering = true;
            rendering_error_ = std::format(L"Rendering failed (0x{:08X}). Choose Retry monitoring.",
                                           static_cast<unsigned long>(rendered));
            if (first_failure) {
                show_settings(false);
            }
        } else if (SUCCEEDED(rendered) && !paused_ && recovery_.rendering) {
            recovery_.rendering = false;
            rendering_error_.clear();
            update_settings_status();
        }
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(window, message_id, wparam, lparam);
}
} // namespace loadbar
