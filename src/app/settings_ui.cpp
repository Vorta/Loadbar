#include "app/application.hpp"
#include "ui/control_ids.hpp"

#include <commctrl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <stdexcept>
#include <type_traits>

namespace loadbar {
namespace {
constexpr std::array<const wchar_t *, 7> kLabels{
    L"&Monitor",          L"&GPU",           L"&Network",         L"&Edge",
    L"Thickness (&DIPs)", L"Sampling (&ms)", L"Content alignment"};
HWND field(HWND parent, int index) {
    return GetDlgItem(parent, kFirstField + index);
}
HWND child(HWND parent, HINSTANCE instance, const wchar_t *type, const wchar_t *text, DWORD style,
           int id) {
    const auto control =
        CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, parent,
                        message_pointer<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    if (!control) {
        throw std::runtime_error("Create settings control failed");
    }
    return control;
}
int selection(HWND control) {
    return static_cast<int>(SendMessageW(control, CB_GETCURSEL, 0, 0));
}
std::wstring edit_text(HWND control) {
    const auto length = GetWindowTextLengthW(control);
    if (length < 0 || length > 1024) {
        return {};
    }
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(std::max(0, copied)));
    return text;
}
} // namespace
void Application::create_settings() {
    if (!settings_window_) {
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTOPRIMARY), &info)) {
            throw std::runtime_error("Settings monitor unavailable");
        }
        const double scale = static_cast<double>(GetDpiForWindow(window_)) / 96 * text_scale_;
        const auto width =
            std::min(static_cast<LONG>(800 * scale), info.rcWork.right - info.rcWork.left - 16);
        const auto height =
            std::min(static_cast<LONG>(760 * scale), info.rcWork.bottom - info.rcWork.top - 16);
        settings_window_ = CreateWindowExW(
            WS_EX_CONTROLPARENT, L"Loadbar.Settings", L"Loadbar Settings and current readings",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, info.rcWork.left + 8, info.rcWork.top + 8, width,
            height, nullptr, nullptr, instance_, this);
        if (!settings_window_) {
            throw std::runtime_error("Create settings window failed");
        }
        settings_content_ = CreateWindowExW(WS_EX_CONTROLPARENT, L"Loadbar.SettingsContent", L"",
                                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN, 0,
                                            0, 1, 1, settings_window_, nullptr, instance_, this);
        if (!settings_content_) {
            throw std::runtime_error("Create settings content failed");
        }
        for (int i = 0; i < 7; ++i) {
            if (i == 3) {
                child(settings_content_, instance_, L"STATIC", L"Dri&ves", 0, kDrivesLabel);
                const auto drives =
                    child(settings_content_, instance_, WC_LISTVIEWW, L"Drive visibility",
                          WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL |
                              LVS_SHOWSELALWAYS,
                          kDrivesControl);
                ListView_SetExtendedListViewStyle(drives, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT |
                                                              LVS_EX_DOUBLEBUFFER |
                                                              LVS_EX_LABELTIP);
                LVCOLUMNW column{};
                column.mask = LVCF_WIDTH;
                column.cx = 400;
                ListView_InsertColumn(drives, 0, &column);
            }
            const auto label = i == 4 ? std::format(L"Thickness (&DIPs, {:g}–{:g})",
                                                    kMinimumThicknessDips, kMaximumThicknessDips)
                                      : std::wstring(kLabels[static_cast<std::size_t>(i)]);
            child(settings_content_, instance_, L"STATIC", label.c_str(), 0, kFirstLabel + i);
            if (i < 4 || i >= 6) {
                child(settings_content_, instance_, L"COMBOBOX", L"",
                      WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, kFirstField + i);
            } else {
                const auto edit = child(settings_content_, instance_, L"EDIT", L"",
                                        WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, kFirstField + i);
                SendMessageW(edit, EM_SETLIMITTEXT, 64, 0);
            }
        }
        child(settings_content_, instance_, L"BUTTON", L"Display CPU usage always as s&quares",
              WS_TABSTOP | BS_AUTOCHECKBOX | BS_NOTIFY | BS_MULTILINE, kCpuSquares);
        child(settings_content_, instance_, L"BUTTON", L"Show info on &hover",
              WS_TABSTOP | BS_AUTOCHECKBOX | BS_NOTIFY | BS_MULTILINE, kHoverInfo);
        child(settings_content_, instance_, L"BUTTON", L"Show toolti&ps",
              WS_TABSTOP | BS_AUTOCHECKBOX | BS_NOTIFY | BS_MULTILINE, kTooltips);
        child(settings_content_, instance_, L"BUTTON", L"Open &Task Manager on click",
              WS_TABSTOP | BS_AUTOCHECKBOX | BS_NOTIFY | BS_MULTILINE, kTaskManagerClick);
        const auto status =
            child(settings_window_, instance_, L"STATIC", L"", SS_NOPREFIX, kFooterStatus);
        ShowWindow(status, SW_HIDE);
        child(settings_window_, instance_, L"BUTTON", L"Exit &Loadbar",
              WS_TABSTOP | BS_NOTIFY | BS_MULTILINE, 100);
        child(settings_window_, instance_, L"BUTTON", L"&Apply",
              WS_TABSTOP | BS_DEFPUSHBUTTON | BS_NOTIFY | BS_MULTILINE, kApplySettings);
        child(settings_window_, instance_, L"BUTTON", L"&Cancel",
              WS_TABSTOP | BS_NOTIFY | BS_MULTILINE, kCancelSettings);
        child(settings_window_, instance_, L"BUTTON", L"&Retry monitoring",
              WS_TABSTOP | BS_NOTIFY | BS_MULTILINE, 102);
        child(settings_window_, instance_, L"BUTTON", L"Cl&ose",
              WS_TABSTOP | BS_NOTIFY | BS_MULTILINE, kCloseSettings);
        child(settings_content_, instance_, L"STATIC",
              L"Current readings — keyboard and screen-reader accessible", 0, kFirstLabel + 7);
        const auto list =
            child(settings_content_, instance_, WC_LISTVIEWW, L"Current metric readings",
                  WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                  kDetailsControl);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.cx = 180;
        wchar_t metric_title[] = L"Metric / logical processor";
        column.pszText = metric_title;
        ListView_InsertColumn(list, 0, &column);
        wchar_t reading_title[] = L"Reading, status and source";
        column.pszText = reading_title;
        column.cx = 480;
        ListView_InsertColumn(list, 1, &column);
    }
}
void Application::show_settings(bool activate) {
    const bool populate = !settings_window_ || !IsWindowVisible(settings_window_);
    create_settings();
    if (!settings_window_) {
        throw std::runtime_error("Settings window creation did not complete");
    }
    if (populate) {
        populate_settings();
    }
    layout_settings();
    update_settings_status();
    ShowWindow(settings_window_, activate ? SW_SHOWNORMAL : SW_SHOWNOACTIVATE);
    update_details();
    schedule_refresh();
    if (activate) {
        SetForegroundWindow(settings_window_);
        SetFocus(field(settings_content_, 0));
    }
}
void Application::populate_settings() {
    populating_settings_ = true;
    populate_devices(true);
    SendMessageW(field(settings_content_, 3), CB_RESETCONTENT, 0, 0);
    for (const auto *edge : {L"Automatic (orientation)", L"Left", L"Top", L"Right", L"Bottom"}) {
        SendMessageW(field(settings_content_, 3), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(edge));
    }
    SendMessageW(field(settings_content_, 3), CB_SETCURSEL, static_cast<WPARAM>(settings_.edge), 0);
    SetWindowTextW(field(settings_content_, 4), std::format(L"{}", settings_.thickness).c_str());
    SetWindowTextW(field(settings_content_, 5), std::to_wstring(settings_.interval_ms).c_str());
    const auto combo = field(settings_content_, 6);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const auto *text : {L"Start (left/top)", L"Center", L"End (right/bottom)"}) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    }
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(settings_.alignment), 0);
    CheckDlgButton(settings_content_, kCpuSquares,
                   settings_.cpu_squares ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(settings_content_, kTooltips,
                   settings_.show_tooltips ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(settings_content_, kHoverInfo,
                   settings_.show_hover_info ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(settings_content_, kTaskManagerClick,
                   settings_.open_task_manager_on_click ? BST_CHECKED : BST_UNCHECKED);
    populating_settings_ = false;
    update_settings_actions();
}
void Application::update_monitors(std::vector<Monitor> monitors) {
    if (monitors_ == monitors) {
        return;
    }
    monitors_ = std::move(monitors);
    if (settings_content_ && field(settings_content_, 0)) {
        populate_devices(false, true);
    }
}
void Application::populate_devices(bool committed, bool monitors_only) {
    const int count = monitors_only ? 1 : 3;
    const bool was_populating = populating_settings_;
    populating_settings_ = true;
    std::array<std::wstring, 3> selected{settings_.monitor_id, settings_.gpu_id,
                                         settings_.network_id};
    std::array visible{true, settings_.gpu_visible, settings_.network_visible};
    if (!committed) {
        for (int i = 0; i < count; ++i) {
            const int index = selection(field(settings_content_, i));
            const auto &ids = choice_ids_[static_cast<std::size_t>(i)];
            if (index >= 0 && static_cast<std::size_t>(index) < ids.size()) {
                selected[static_cast<std::size_t>(i)] = ids[static_cast<std::size_t>(index)];
                visible[static_cast<std::size_t>(i)] = i == 0 || index != 1;
            }
        }
    }
    for (int i = 0; i < count; ++i) {
        const auto combo = field(settings_content_, i);
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        auto &identities = choice_ids_[static_cast<std::size_t>(i)];
        identities.clear();
        identities.emplace_back();
        SendMessageW(combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(i == 0 ? L"Automatic — primary monitor"
                                                     : L"Automatic — only if unambiguous"));
        if (i != 0) {
            identities.push_back(selected[static_cast<std::size_t>(i)]);
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Hide"));
        }
        const auto add = [&](const std::wstring &id, const std::wstring &label) {
            if (id.empty()) {
                return;
            }
            identities.push_back(id);
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        };
        if (i == 0) {
            for (const auto &monitor : monitors_) {
                add(monitor.id, monitor.label);
            }
        } else {
            const auto &devices = i == 1 ? catalog_->gpus : catalog_->networks;
            for (const auto &device : devices) {
                add(device.id, device.label);
            }
        }
        // Skip Hide when resolving a visible device; its identity is remembered, not a sentinel.
        auto found = selected[static_cast<std::size_t>(i)].empty()
                         ? identities.begin()
                         : std::find(identities.begin() + (i == 0 ? 1 : 2), identities.end(),
                                     selected[static_cast<std::size_t>(i)]);
        if (found == identities.end()) {
            add(selected[static_cast<std::size_t>(i)], L"Saved selection — currently disconnected");
            found = std::prev(identities.end());
        }
        SendMessageW(combo, CB_SETCURSEL,
                     visible[static_cast<std::size_t>(i)]
                         ? static_cast<WPARAM>(found - identities.begin())
                         : 1,
                     0);
    }
    if (!monitors_only) {
        populate_drives(committed);
    }
    populating_settings_ = was_populating;
    update_settings_actions();
}
void Application::populate_drives(bool committed) {
    const auto list = GetDlgItem(settings_content_, kDrivesControl);
    if (!list) {
        return;
    }
    const bool was_populating = populating_settings_;
    populating_settings_ = true;
    if (committed) {
        draft_hidden_disks_ = settings_.hidden_disks;
    }
    const bool rebuild =
        !std::ranges::equal(catalog_->disks, drive_choices_,
                            [](const auto &disk, const auto &choice) {
                                return disk.id == choice.first && disk.label == choice.second;
                            }) ||
        ListView_GetItemCount(list) == 0;
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    if (rebuild) {
        const int focused = ListView_GetNextItem(list, -1, LVNI_FOCUSED);
        const int top = ListView_GetTopIndex(list);
        const auto identity_at = [&](int index) -> std::wstring {
            return index >= 0 && static_cast<std::size_t>(index) < drive_choices_.size()
                       ? drive_choices_[static_cast<std::size_t>(index)].first
                       : L"";
        };
        const auto focus_id = identity_at(focused), top_id = identity_at(top);
        ListView_DeleteAllItems(list);
        drive_choices_.clear();
        drive_choices_.reserve(catalog_->disks.size());
        int focus_row =
            std::clamp(focused, 0, std::max(0, static_cast<int>(catalog_->disks.size()) - 1));
        int top_row{};
        for (const auto &disk : catalog_->disks) {
            const int row = static_cast<int>(drive_choices_.size());
            drive_choices_.emplace_back(disk.id, disk.label);
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = row;
            item.pszText = drive_choices_.back().second.data();
            if (ListView_InsertItem(list, &item) == -1) {
                throw std::runtime_error("Insert drive choice failed");
            }
            if (disk.id == focus_id) {
                focus_row = row;
            }
            if (disk.id == top_id) {
                top_row = row;
            }
        }
        if (!drive_choices_.empty()) {
            ListView_SetItemState(list, focus_row, LVIS_FOCUSED | LVIS_SELECTED,
                                  LVIS_FOCUSED | LVIS_SELECTED);
            RECT row{};
            if (ListView_GetItemRect(list, 0, &row, LVIR_BOUNDS)) {
                ListView_Scroll(list, 0,
                                (top_row - ListView_GetTopIndex(list)) * (row.bottom - row.top));
            }
        }
    }
    if (drive_choices_.empty()) {
        std::wstring text = snapshot_.disk_discovery.status == Status::error
                                ? L"Drive discovery unavailable"
                                : L"No physical drives detected";
        if (ListView_GetItemCount(list) == 0) {
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.pszText = text.data();
            if (ListView_InsertItem(list, &item) == -1) {
                throw std::runtime_error("Insert empty drive status failed");
            }
        } else {
            ListView_SetItemText(list, 0, 0, text.data());
        }
        ListView_SetItemState(list, 0, 0, LVIS_STATEIMAGEMASK);
    } else {
        for (std::size_t i = 0; i < drive_choices_.size(); ++i) {
            const bool checked = !disk_hidden(draft_hidden_disks_, drive_choices_[i].first);
            if ((ListView_GetCheckState(list, static_cast<int>(i)) != FALSE) != checked) {
                ListView_SetCheckState(list, static_cast<int>(i), checked);
            }
        }
    }
    if (drive_choices_.empty() && GetFocus() == list) {
        SetFocus(field(settings_content_, 3));
    }
    EnableWindow(list, !drive_choices_.empty());
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, FALSE);
    populating_settings_ = was_populating;
    if (rebuild) {
        layout_settings();
    }
}
void Application::drive_check_changed(int index) {
    if (populating_settings_ || index < 0 ||
        static_cast<std::size_t>(index) >= drive_choices_.size()) {
        return;
    }
    const auto &id = drive_choices_[static_cast<std::size_t>(index)].first;
    if (ListView_GetCheckState(GetDlgItem(settings_content_, kDrivesControl), index)) {
        show_disk(draft_hidden_disks_, id);
    } else {
        if (draft_hidden_disks_.size() >= kMaximumHiddenDisks &&
            !disk_hidden(draft_hidden_disks_, id)) {
            populate_drives(false);
            MessageBoxW(settings_window_, L"The saved hidden-drive limit has been reached.",
                        L"Loadbar — drive visibility", MB_OK | MB_ICONWARNING);
            return;
        }
        hide_disk(draft_hidden_disks_, id);
    }
    update_settings_actions();
}
HWND Application::settings_control(int id) const noexcept {
    const auto control = GetDlgItem(settings_window_, id);
    return control ? control : GetDlgItem(settings_content_, id);
}
void Application::layout_settings() {
    if (!settings_content_ || !GetDlgItem(settings_content_, kDetailsControl)) {
        return;
    }
    const auto dpi = GetDpiForWindow(settings_window_);
    const float scale = static_cast<float>(dpi) / 96 * text_scale_;
    const int font_height = -static_cast<int>(std::round(12 * scale));
    if (!settings_font_ || settings_font_height_ != font_height) {
        LOGFONTW font{};
        font.lfHeight = font_height;
        wcscpy_s(font.lfFaceName, L"Segoe UI");
        const auto new_font = CreateFontIndirectW(&font);
        if (new_font) {
            EnumChildWindows(
                settings_window_,
                [](HWND control, LPARAM parameter) -> BOOL {
                    SendMessageW(control, WM_SETFONT, static_cast<WPARAM>(parameter), FALSE);
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(new_font));
            if (settings_font_) {
                DeleteObject(settings_font_);
            }
            settings_font_ = new_font;
            settings_font_height_ = font_height;
        }
    }
    RECT client{};
    GetClientRect(settings_window_, &client);
    int status_height{};
    if (!settings_notice_.empty()) {
        struct DcDeleter {
            HWND window{};
            void operator()(HDC dc) const noexcept {
                ReleaseDC(window, dc);
            }
        };
        const std::unique_ptr<std::remove_pointer_t<HDC>, DcDeleter> dc(
            GetDC(settings_window_), DcDeleter{settings_window_});
        // Use the same font, width and wrapping as the native footer label.
        const int margin = static_cast<int>(std::round(12 * scale));
        RECT text_bounds{
            0, 0,
            std::max(1L, client.right - 2 * margin - GetSystemMetricsForDpi(SM_CXVSCROLL, dpi)), 0};
        if (dc) {
            const auto previous_font = SelectObject(dc.get(), settings_font_);
            status_height = DrawTextW(dc.get(), settings_notice_.c_str(), -1, &text_bounds,
                                      DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            if (previous_font && previous_font != HGDI_ERROR) {
                SelectObject(dc.get(), previous_font);
            }
        }
        if (status_height <= 0) {
            // Retain an accessible recovery message if font measurement fails.
            status_height = static_cast<int>(std::round(60 * scale));
        }
    }
    const auto drives = GetDlgItem(settings_content_, kDrivesControl);
    RECT drive_row{};
    const int drive_row_height = ListView_GetItemRect(drives, 0, &drive_row, LVIR_BOUNDS)
                                     ? drive_row.bottom - drive_row.top
                                     : static_cast<int>(std::ceil(20 * scale));
    const auto layout =
        settings_layout(client.right, client.bottom, scale, retry_visible_, scroll_,
                        GetSystemMetricsForDpi(SM_CXVSCROLL, dpi), status_height,
                        std::max(1, ListView_GetItemCount(drives)), drive_row_height);
    scroll_ = layout.scroll;
    const auto footer_parent = layout.scroll_footer ? settings_content_ : settings_window_;
    for (const auto id :
         {kFooterStatus, 100, kApplySettings, kCancelSettings, 102, kCloseSettings}) {
        const auto control = settings_control(id);
        if (GetParent(control) != footer_parent) {
            if (!SetParent(control, footer_parent)) {
                const auto error = GetLastError();
                throw std::runtime_error(std::format("Settings footer parent failed: {}", error));
            }
            // SetParent inserts at the top of the new sibling order. Append in form order
            // so native Tab traversal remains readings, Exit Loadbar, Apply, Cancel, Retry, Close.
            if (!SetWindowPos(control, HWND_BOTTOM, 0, 0, 0, 0,
                              SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)) {
                const auto error = GetLastError();
                throw std::runtime_error(std::format("Settings footer order failed: {}", error));
            }
            // Keep keyboard cues consistent when switching between parents in the same
            // DPI context. Only transitioned controls need UI-state synchronization.
            constexpr DWORD state_mask = UISF_HIDEACCEL | UISF_HIDEFOCUS | UISF_ACTIVE;
            const auto state =
                static_cast<DWORD>(SendMessageW(footer_parent, WM_QUERYUISTATE, 0, 0)) & state_mask;
            SendMessageW(control, WM_UPDATEUISTATE, MAKEWPARAM(UIS_SET, state), 0);
            SendMessageW(control, WM_UPDATEUISTATE, MAKEWPARAM(UIS_CLEAR, state_mask & ~state), 0);
            if (id == 102) {
                SetWindowTextW(control, layout.scroll_footer ? L"&Retry" : L"&Retry monitoring");
            }
        }
    }
    const auto position = [](HWND control, ControlBounds bounds) {
        SetWindowPos(control, nullptr, bounds.x, bounds.y, bounds.width, bounds.height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    };
    position(settings_content_, layout.viewport);
    SCROLLINFO info{sizeof(info),
                    SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL,
                    0,
                    layout.content_height - 1,
                    static_cast<UINT>(layout.viewport.height),
                    scroll_,
                    0};
    SetScrollInfo(settings_content_, SB_VERT, &info, TRUE);
    for (std::size_t i = 0; i < layout.fields.size(); ++i) {
        position(GetDlgItem(settings_content_, kFirstLabel + static_cast<int>(i)),
                 layout.labels[i]);
        auto bounds = layout.fields[i];
        if (i < 4 || i == 6) {
            bounds.height = static_cast<int>(std::round(240 * scale));
        }
        position(field(settings_content_, static_cast<int>(i)), bounds);
    }
    constexpr std::array preference_ids{kCpuSquares, kHoverInfo, kTooltips, kTaskManagerClick};
    for (std::size_t i = 0; i < preference_ids.size(); ++i) {
        position(GetDlgItem(settings_content_, preference_ids[i]), layout.preferences[i]);
    }
    position(GetDlgItem(settings_content_, kDrivesLabel), layout.drives_label);
    position(drives, layout.drives);
    RECT drive_client{};
    GetClientRect(drives, &drive_client);
    ListView_SetColumnWidth(drives, 0, std::max(1L, drive_client.right));
    position(settings_control(kFooterStatus), layout.footer_status);
    position(GetDlgItem(settings_content_, kFirstLabel + 7), layout.readings_label);
    const auto list = GetDlgItem(settings_content_, kDetailsControl);
    position(list, layout.readings);
    constexpr std::array ids{kApplySettings, kCancelSettings, 102, 100, kCloseSettings};
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i != 2 || retry_visible_) {
            position(settings_control(ids[i]), layout.buttons[i]);
        }
    }
    ListView_SetColumnWidth(list, 0, layout.metric_width);
    RECT list_client{};
    GetClientRect(list, &list_client);
    ListView_SetColumnWidth(list, 1,
                            std::max(1, static_cast<int>(list_client.right) - layout.metric_width));
    InvalidateRect(settings_content_, nullptr, TRUE);
    InvalidateRect(settings_window_, nullptr, TRUE);
}
void Application::ensure_visible(HWND control) {
    if (!control || !settings_content_ || GetParent(control) != settings_content_) {
        return;
    }
    RECT bounds{}, client{};
    GetWindowRect(control, &bounds);
    const int id = GetDlgCtrlID(control);
    if (id >= kFirstField && id < kFirstField + 7) {
        RECT label{};
        if (GetWindowRect(GetDlgItem(settings_content_, kFirstLabel + id - kFirstField), &label)) {
            bounds.top = std::min(bounds.top, label.top);
        }
    }
    GetClientRect(settings_content_, &client);
    POINT origin{bounds.left, bounds.top};
    ScreenToClient(settings_content_, &origin);
    const int bottom = origin.y + std::min(bounds.bottom - bounds.top, client.bottom);
    if (origin.y < 0) {
        scroll_ += origin.y;
        layout_settings();
    } else if (bottom > client.bottom) {
        scroll_ += bottom - client.bottom;
        layout_settings();
    }
}
SettingsDraft Application::settings_draft() const {
    SettingsDraft draft;
    for (std::size_t i = 0; i < draft.device_ids.size(); ++i) {
        const int index = selection(field(settings_content_, static_cast<int>(i)));
        if (index < 0 || static_cast<std::size_t>(index) >= choice_ids_[i].size()) {
            draft.selections_valid = false;
        } else {
            draft.device_ids[i] = choice_ids_[i][static_cast<std::size_t>(index)];
            if (i == 1) {
                draft.gpu_visible = index != 1;
            }
            if (i == 2) {
                draft.network_visible = index != 1;
            }
        }
    }
    draft.hidden_disks = draft_hidden_disks_;
    draft.cpu_squares = IsDlgButtonChecked(settings_content_, kCpuSquares) == BST_CHECKED;
    draft.show_hover_info = IsDlgButtonChecked(settings_content_, kHoverInfo) == BST_CHECKED;
    draft.show_tooltips = IsDlgButtonChecked(settings_content_, kTooltips) == BST_CHECKED;
    draft.open_task_manager_on_click =
        IsDlgButtonChecked(settings_content_, kTaskManagerClick) == BST_CHECKED;
    draft.edge = selection(field(settings_content_, 3));
    draft.alignment = selection(field(settings_content_, 6));
    draft.thickness = edit_text(field(settings_content_, 4));
    draft.interval = edit_text(field(settings_content_, 5));
    return draft;
}
void Application::update_settings_actions() {
    if (!settings_content_ || populating_settings_) {
        return;
    }
    for (std::size_t i = 1; i < choice_ids_.size(); ++i) {
        const int index = selection(field(settings_content_, static_cast<int>(i)));
        if (index >= 0 && index != 1 && static_cast<std::size_t>(index) < choice_ids_[i].size() &&
            choice_ids_[i].size() > 1) {
            choice_ids_[i][1] = choice_ids_[i][static_cast<std::size_t>(index)];
        }
    }
    settings_dirty_ = settings_dirty(settings_draft(), settings_);
    for (const auto id : {kApplySettings, kCancelSettings}) {
        const auto control = settings_control(id);
        if (!settings_dirty_ && GetFocus() == control) {
            SetFocus(field(settings_content_, 0));
        }
        EnableWindow(control, settings_dirty_);
    }
    const auto retry = settings_control(102);
    const bool visible = recovery_.needed();
    if ((!visible || !recovery_.can_retry()) && GetFocus() == retry) {
        SetFocus(field(settings_content_, 0));
    }
    EnableWindow(retry, recovery_.can_retry());
    if (retry_visible_ != visible) {
        retry_visible_ = visible;
        ShowWindow(retry, visible ? SW_SHOWNA : SW_HIDE);
        layout_settings();
    } else if (!visible) {
        ShowWindow(retry, SW_HIDE);
    }
}
void Application::cancel_settings() {
    populate_settings();
    update_settings_status();
}
void Application::apply_settings() {
    if (!settings_dirty_) {
        return;
    }
    auto desired = settings_from_draft(settings_draft());
    if (!desired) {
        const auto text =
            std::format(L"Choose a monitor, GPU, network, edge and alignment. Enter a whole-number "
                        L"thickness from {:g}–{:g} DIPs and a whole-number sampling interval "
                        L"from 250–5000 ms.",
                        kMinimumThicknessDips, kMaximumThicknessDips);
        MessageBoxW(settings_window_, text.c_str(), L"Loadbar — invalid settings",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    if (commit_settings(std::move(*desired))) {
        populate_settings();
    }
}
bool Application::commit_settings(Settings settings) {
    const bool preserve_draft = settings_dirty_;
    const auto previous = settings_;
    settings_ = std::move(settings);
    ++observation_revision_;
    place_bar();
    if (recovery_.desktop) {
        const auto failure = placement_error_;
        settings_ = previous;
        ++observation_revision_;
        place_bar();
        MessageBoxW(settings_window_ ? settings_window_ : window_, failure.c_str(),
                    L"Loadbar — placement not applied", MB_OK | MB_ICONWARNING);
        update_settings_actions();
        return false;
    }
    const auto saved = save_settings(settings_);
    settings_save_failed_ = !saved;
    if (!saved) {
        MessageBoxW(settings_window_ ? settings_window_ : window_,
                    error_text(saved.error()).c_str(),
                    L"Loadbar — active settings could not be saved", MB_OK | MB_ICONWARNING);
    }
    if (previous.show_tooltips != settings_.show_tooltips) {
        update_tooltip_activation();
    }
    if (collection_changed(previous, settings_)) {
        reconfigure_worker(previous.interval_ms != settings_.interval_ms);
    } else {
        InvalidateRect(window_, nullptr, FALSE);
        update_tooltip();
    }
    if (settings_content_) {
        if (!preserve_draft) {
            populate_settings();
        } else {
            update_settings_actions();
        }
    }
    detail_structure_changed_ = true;
    update_details();
    update_settings_status();
    schedule_refresh();
    return true;
}
void Application::update_settings_status() {
    if (!settings_window_) {
        return;
    }
    std::wstring text;
    if (recovery_.desktop) {
        text = L"Desktop placement failed. Change monitor, edge or size and Apply, or Retry "
               L"monitoring.";
    } else if (recovery_.sampling) {
        text = recovery_.restarting ? L"Monitoring is restarting."
                                    : L"Monitoring stopped. Choose Retry monitoring.";
    } else if (recovery_.tray) {
        text = L"Notification-area icon unavailable. Choose Retry monitoring.";
    } else if (recovery_.rendering) {
        text = rendering_error_.empty() ? L"Rendering failed. Choose Retry monitoring."
                                        : rendering_error_;
    } else if (settings_save_failed_) {
        text = L"Settings could not be saved. Current values are active for this session.";
    } else if (fallback_) {
        text = L"Preferred monitor unavailable. Using the primary monitor.";
    }
    const bool changed = text != settings_notice_;
    settings_notice_ = std::move(text);
    update_settings_actions();
    if (changed) {
        SetWindowTextW(settings_control(kFooterStatus), settings_notice_.c_str());
    }
    ShowWindow(settings_control(kFooterStatus), settings_notice_.empty() ? SW_HIDE : SW_SHOWNA);
    if (changed) {
        layout_settings();
    }
}
void Application::reconfigure_worker(bool reset) {
    if (worker_) {
        ++expected_generation_;
        worker_->configure(settings_, paused_, reset);
        if (reset) {
            set_snapshot_status(snapshot_, Status::warming_up,
                                L"Priming after configuration or resume");
        }
        ++observation_revision_;
        if (!paused_ && IsWindowVisible(window_)) {
            InvalidateRect(window_, nullptr, FALSE);
        }
        update_tooltip();
        schedule_refresh();
    }
}
void Application::update_details() {
    if (!settings_window_ || !IsWindowVisible(settings_window_) || IsIconic(settings_window_)) {
        return;
    }
    const auto list = GetDlgItem(settings_content_, kDetailsControl);
    if (!list) {
        return; // Initial WM_SIZE can precede creation of the Settings child controls.
    }
    std::vector<std::wstring> keys, values;
    const auto rows = snapshot_.processors.size() + kGaugeCount +
                      3 * std::max(std::size_t{1}, snapshot_.disks.size());
    values.reserve(rows);
    if (detail_structure_changed_) {
        keys.reserve(rows);
    }
    const auto now = Clock::now();
    for (const auto &cpu : snapshot_.processors) {
        const auto metric = aged_metric(cpu.utilization, now, settings_.interval_ms);
        if (detail_structure_changed_) {
            keys.push_back(std::format(L"{} · G{} LP{}",
                                       cpu.mapped ? std::format(L"Core {}", cpu.core) : L"Unmapped",
                                       cpu.id.group, cpu.id.number));
        }
        values.push_back(std::format(L"{} — {} {}", metric_text(metric), status_text(metric.status),
                                     metric.detail));
    }
    for (std::size_t index = 0; index < kGaugeCount; ++index) {
        if (!gauge_visible(static_cast<Gauge>(index), settings_)) {
            if (index == 1 || index == 4) {
                if (detail_structure_changed_) {
                    keys.emplace_back(index == 1 ? L"GPU" : L"Network");
                }
                values.emplace_back(L"Hidden — sampling paused");
            }
            continue;
        }
        const auto metric = aged_metric(snapshot_.gauges[index], now, settings_.interval_ms);
        if (detail_structure_changed_) {
            keys.emplace_back(index == 3 ? snapshot_.gpu_memory_label
                                         : gauge_name(static_cast<Gauge>(index)));
        }
        const auto label = index == 0  ? L"Physical memory"
                           : index < 4 ? snapshot_.gpu_label
                                       : snapshot_.network_label;
        auto reading =
            index == 0 ? ram_summary(snapshot_, now, settings_.interval_ms) : metric_text(metric);
        if (index == 0 && metric.status != Status::valid) {
            reading += L"; " + retention_text(metric, now);
        }
        if (is_rate_gauge(static_cast<Gauge>(index))) {
            reading += L"; " + peak_text(metric);
        }
        values.push_back(std::format(L"{} — {}; {}; {}", reading, status_text(metric.status), label,
                                     metric.detail));
    }
    DiskReading missing_disk{L"", L"Physical disk discovery"};
    missing_disk.active = snapshot_.disk_discovery;
    missing_disk.read = missing_disk.write = snapshot_.disk_discovery;
    missing_disk.read.unit = missing_disk.write.unit = Unit::bytes_per_second;
    for (std::size_t i = 0; i < std::max(std::size_t{1}, snapshot_.disks.size()); ++i) {
        const auto &disk = snapshot_.disks.empty() ? missing_disk : snapshot_.disks[i];
        if (!disk_visible(disk.id, settings_)) {
            if (detail_structure_changed_) {
                keys.push_back(disk.label);
            }
            values.emplace_back(L"Hidden — sampling paused");
            continue;
        }
        for (auto direction : {Gauge::disk_active, Gauge::disk_read, Gauge::disk_write}) {
            const auto &source = direction == Gauge::disk_active ? disk.active
                                 : direction == Gauge::disk_read ? disk.read
                                                                 : disk.write;
            const auto metric = aged_metric(source, now, settings_.interval_ms);
            if (detail_structure_changed_) {
                keys.push_back(disk.label + L" · " + gauge_name(direction));
            }
            values.push_back(std::format(
                L"{} — {}; {}{}", metric_text(metric), status_text(metric.status), metric.detail,
                is_rate_gauge(direction) ? L"; " + peak_text(metric) : L""));
        }
    }

    if (catalog_ && !catalog_->detail.empty()) {
        if (detail_structure_changed_) {
            keys.push_back(L"Device discovery");
        }
        values.push_back(catalog_->detail);
    }
    const bool rebuild = detail_structure_changed_ && keys != detail_keys_;
    detail_structure_changed_ = false;
    if (!rebuild && values == detail_values_) {
        return;
    }
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    if (rebuild) {
        ListView_DeleteAllItems(list);
        for (std::size_t index = 0; index < keys.size(); ++index) {
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = static_cast<int>(index);
            item.pszText = keys[index].data();
            ListView_InsertItem(list, &item);
        }
        detail_keys_ = std::move(keys);
        detail_values_.clear();
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index >= detail_values_.size() || values[index] != detail_values_[index]) {
            ListView_SetItemText(list, static_cast<int>(index), 1, values[index].data());
        }
    }
    detail_values_ = std::move(values);
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    if (rebuild) {
        RECT client{};
        GetClientRect(list, &client);
        ListView_SetColumnWidth(
            list, 1,
            std::max(1, static_cast<int>(client.right) - ListView_GetColumnWidth(list, 0)));
    }
    InvalidateRect(list, nullptr, FALSE);
}
} // namespace loadbar
