#include "app/application.hpp"
#include "ui/control_ids.hpp"

#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char *reason) {
    if (!condition) {
        throw std::runtime_error(reason);
    }
}
void drive_settings_tests() {
    using namespace loadbar;
    Settings settings;
    check(settings.hidden_disks.empty() && disk_visible(L"new", settings), "New disks are visible");
    settings.hidden_disks = {L"device\\quoted\"id", L"disconnected"};
    check(decode_settings(encode_settings(settings)) == settings, "Disk exclusions roundtrip");
    Settings ordered;
    check(hide_disk(ordered.hidden_disks, L"b") && hide_disk(ordered.hidden_disks, L"a") &&
              !hide_disk(ordered.hidden_disks, L"b") &&
              ordered.hidden_disks == HiddenDiskIds{L"a", L"b"},
          "Checkbox order cannot create duplicate or noncanonical exclusions");
    show_disk(ordered.hidden_disks, L"a");
    show_disk(ordered.hidden_disks, L"absent");
    check(ordered.hidden_disks == HiddenDiskIds{L"b"}, "Show removes only the matching identity");
    ordered.hidden_disks = {L"b", L"a"};
    check(!valid_settings(ordered), "Noncanonical internal exclusions are rejected");
    ordered.hidden_disks = {L"a", L"a"};
    check(!valid_settings(ordered), "Duplicate internal exclusions are rejected");
    const auto old = decode_settings(L"Loadbar 8 0 40 1000 \"\" \"gpu\" \"nic\" 0 0 1 0");
    check(old && old->hidden_disks.empty() && !old->gpu_visible && old->network_visible,
          "Schema eight preserves GPU visibility and shows all disks");
    const std::wstring prefix = L"Loadbar 9 0 40 1000 \"\" \"\" \"\" 0 1 1 0 ";
    for (const auto *tail :
         {L"1", L"1 \"\"", L"2 \"a\" \"a\"", L"1025", L"-1", L"1 \"a\n\"", L"1 \"a\" extra"}) {
        check(!decode_settings(prefix + tail), "Malformed disk exclusions are rejected");
    }
    check(!decode_settings(prefix + L"1 \"" + std::wstring(1025, L'x') + L"\""),
          "Oversized disk identity rejected");
    settings.hidden_disks.clear();
    for (std::size_t i = 0; i < kMaximumHiddenDisks; ++i) {
        auto id = std::to_wstring(i);
        id.append(1024 - id.size(), L'\\');
        hide_disk(settings.hidden_disks, std::move(id));
    }
    const auto encoded = encode_settings(settings);
    check(valid_settings(settings) && encoded.size() > 8192 &&
              encoded.size() <= kMaximumSettingsCharacters && decode_settings(encoded) == settings,
          "Maximum escaped hidden identities fit the coordinated persistence limit");
    hide_disk(settings.hidden_disks, L"overflow");
    check(!valid_settings(settings), "Hidden identity count bounded");
    check(!decode_settings(std::wstring(kMaximumSettingsCharacters + 1, L' ')),
          "Oversized settings rejected before parsing");
    for (const float scale : {1.0F, 1.25F, 2.0F, 4.0F}) {
        for (const int width : {320, 800}) {
            const auto row = static_cast<int>(20 * scale);
            for (const int count : {1, 2, 4, 12}) {
                const auto layout = settings_layout(width, 700, scale, false, 0, 17, 0, count, row);
                check(layout.drives.y >= layout.fields[2].y + layout.fields[2].height &&
                          layout.drives.y + layout.drives.height <= layout.labels[3].y &&
                          layout.drives.height ==
                              std::min(count, 4) * row + static_cast<int>(std::round(4 * scale)),
                      "Drive section fits between Network and Edge with four-row cap");
            }
        }
    }
}

void geometry_tests() {
    using namespace loadbar;
    for (const float scale : {1.0F, 1.25F, 1.5F, 2.0F, 4.0F}) {
        const auto px = [scale](int value) {
            return static_cast<int>(std::round(static_cast<float>(value) * scale));
        };
        for (const bool retry : {false, true}) {
            const auto compact = settings_layout(px(800), px(700), scale, retry, 0, px(17));
            const auto large = settings_layout(px(1000), px(900), scale, retry, 0, px(17));
            check(large.readings.height - compact.readings.height == px(200),
                  "Table height follows resize");
            check(large.readings.width - compact.readings.width == px(200),
                  "Table width follows resize");
            check(large.buttons[3].x + large.buttons[3].width == px(1000) - px(12),
                  "Exit right anchored");
            check(large.buttons[3].y + large.buttons[3].height == px(900) - px(12),
                  "Exit bottom anchored");
            const auto narrow = settings_layout(px(320), px(240), scale, retry, px(10000), px(17));
            check(narrow.readings.height == px(120), "Table minimum retained");
            check(narrow.scroll > 0, "Short windows scroll");
            check(narrow.fields[0].y > narrow.labels[0].y, "Narrow labels stacked");
            check(narrow.readings.y + narrow.readings.height + px(12) == narrow.viewport.height,
                  "Scroll reaches table bottom");
            for (std::size_t i = 0; i < narrow.buttons.size(); ++i) {
                if (i == 2 && !retry) {
                    continue;
                }
                const auto &button = narrow.buttons[i];
                check(button.y >= narrow.viewport.height && button.x >= 0 &&
                          button.x + button.width <= px(320) && button.y + button.height <= px(240),
                      "Footer stays inside window, outside clipped content");
            }
            check(compact.readings_label.y == px(12) + 7 * px(30) + px(8),
                  "Readings immediately follow form without a status block");
            check(compact.footer_status.height == 0, "Healthy footer has no message space");
            const auto notice = settings_layout(px(800), px(700), scale, retry, 0, px(17), px(36));
            check(notice.readings.height == compact.readings.height - px(36) - px(8),
                  "Visible footer message consumes exactly its height and gap");
            check(notice.footer_status.y >= notice.viewport.height &&
                      notice.footer_status.y + notice.footer_status.height <= notice.buttons[0].y,
                  "Footer message cannot overlap content or buttons");
            const auto short_notice =
                settings_layout(px(320), px(240), scale, retry, px(10000), px(17), px(60));
            check(short_notice.footer_status.y + short_notice.footer_status.height <=
                          short_notice.buttons[0].y &&
                      short_notice.buttons[3].y + short_notice.buttons[3].height <= px(240),
                  "Wrapped message and buttons fit short windows");
            const auto restored =
                settings_layout(px(800), px(900), scale, retry, narrow.scroll, px(17));
            check(restored.scroll == 0, "Enlarging removes old scroll offset");
        }
    }
}
void constrained_geometry_tests() {
    using namespace loadbar;
    // Keep physical size fixed while Windows text scaling or available work area changes.
    for (const float scale : {1.0F, 2.0F, 4.0F}) {
        for (const bool retry : {false, true}) {
            for (const int notice : {0, 240, 600}) {
                const auto start = settings_layout(320, 240, scale, retry, 0, 34, notice);
                const auto end = settings_layout(320, 240, scale, retry, 100000, 34, notice);
                check(start.viewport.height > 0, "Constrained window retains a usable viewport");
                if (start.scroll_footer) {
                    const auto first = settings_layout(
                        320, 240, scale, retry,
                        std::max(0, start.fields[0].y + start.fields[0].height - 240), 34, notice);
                    check(start.viewport.height == 240 && start.labels[0].y >= 0 &&
                              first.fields[0].y >= 0 &&
                              first.fields[0].y + first.fields[0].height <= 240,
                          "Scrolling footer leaves the first form field reachable");
                    check(start.footer_status.y >= start.readings.y + start.readings.height &&
                              start.buttons[0].y >=
                                  start.footer_status.y + start.footer_status.height,
                          "Overflow notice and buttons follow readings without overlap");
                    check(end.buttons[3].y >= 0 && end.buttons[3].y + end.buttons[3].height <= 240,
                          "Scrolling reaches Exit under fixed-size text scaling");
                    for (std::size_t i = 0; i < start.buttons.size(); ++i) {
                        if (i == 2 && !retry) {
                            continue;
                        }
                        check(start.buttons[i].x >= 0 &&
                                  start.buttons[i].x + start.buttons[i].width <= 286,
                              "Overflow buttons fit content width excluding scrollbar");
                    }
                }
            }
        }
    }
}
void migration_tests() {
    using namespace loadbar;
    for (const double value : {40.0, 41.0, 480.0, 640.0}) {
        Settings settings;
        settings.thickness = value;
        check(valid_settings(settings) && decode_settings(encode_settings(settings)) == settings &&
                  encode_settings(settings).starts_with(L"Loadbar 9 "),
              "Schema 7 boundaries round trip");
    }
    for (const int version : {1, 2, 3, 4, 5, 6, 7}) {
        for (const double size : {0.0, 1.0, 6.0, 39.5, 40.0, 40.25, 40.5, 479.5, 480.0, 481.0,
                                  639.5, 640.0, 640.1, 641.0}) {
            auto record =
                std::format(L"Loadbar {} 2 {}{} 1000", version, version < 3 ? L"1 " : L"", size);
            if (version < 4) {
                record += L" 10000 10000 10000 10000";
            }
            record += L" \"monitor\"";
            if (version < 3) {
                record += L" \"old-disk\"";
            }
            record += L" \"gpu\" \"nic\"";
            if (version >= 2) {
                record += L" 1";
            }
            if (version >= 2 && version < 4) {
                record += L" 0";
            }
            if (version >= 2 && version < 5) {
                record += L" 0 0 0";
            }
            const auto decoded = decode_settings(record);
            const bool expected = version < 6 ? size >= 1 && size <= 480
                                              : size >= 40 && size <= 640 &&
                                                    (version < 7 || std::floor(size) == size);
            check(decoded.has_value() == expected, "Validate each schema before migrating");
            if (decoded) {
                check(decoded->thickness == std::round(std::max(40.0, size)) &&
                          decoded->monitor_id == L"monitor" && decoded->gpu_id == L"gpu" &&
                          decoded->network_id == L"nic" && decoded->edge == Edge::top &&
                          decoded->alignment ==
                              (version == 1 ? Alignment::start : Alignment::center),
                      "Migration preserves choices and normalizes old small/fractional thickness");
            }
        }
    }
    for (const auto *bad : {L"nan", L"inf", L"-1", L"1e100"}) {
        check(!decode_settings(std::format(L"Loadbar 7 0 {} 1000 \"\" \"\" \"\" 0", bad)),
              "Malformed thickness rejected");
    }
    auto legacy =
        decode_settings(L"Loadbar 1 2 0 25 1000 10000 10000 10000 10000 \"m\" \"d\" \"g\" \"n\"");
    if (!legacy) {
        throw std::runtime_error("Expected percentage record");
    }
    check(migrate_thickness(*legacy, {0, 0, 3840, 4320}, 96) && legacy->thickness == 640,
          "Percentage migration observes new upper bound");
    legacy->legacy_percent = 5;
    check(migrate_thickness(*legacy, {0, 0, 1920, 1230}, 96) && legacy->thickness == 62 &&
              !legacy->legacy_percent && valid_settings(*legacy),
          "Percentage migration rounds to whole DIPs");
    Settings fractional;
    fractional.thickness = 40.25;
    check(!valid_settings(fractional), "New settings reject fractional DIPs");
    check(!decode_settings(L"Loadbar 10 0 40 1000 \"\" \"\" \"\" 0"), "Reject future schema");
}
void draft_tests() {
    using namespace loadbar;
    const Settings applied;
    SettingsDraft draft{{}, 0, 0, L"40.0", L"1000.00"};
    check(!settings_dirty(draft, applied), "Equivalent numbers are clean");
    for (const auto *bad : {L"", L"abc", L"nan", L"inf", L"0", L"39.999", L"40.25", L"639.5",
                            L"640.001", L"1e100", L"40x"}) {
        draft.thickness = bad;
        check(settings_dirty(draft, applied) && !settings_from_draft(draft),
              "Invalid thickness is dirty and cannot apply");
    }
    for (const auto *valid : {L"40", L"41", L"480", L"640"}) {
        draft.thickness = valid;
        check(settings_from_draft(draft).has_value(),
              "Whole-number DIPs and inclusive bounds accepted");
    }
    draft.thickness = L"40";
    for (const auto *bad : {L"", L"249", L"5001", L"250.5", L"1e100"}) {
        draft.interval = bad;
        check(settings_dirty(draft, applied) && !settings_from_draft(draft),
              "Invalid interval cannot apply");
    }
    draft.interval = L"250";
    auto desired = settings_from_draft(draft);
    check(desired && settings_dirty(draft, applied), "Valid edit enables actions");
    if (!desired) {
        throw std::runtime_error("Valid draft missing");
    }
    check(!settings_dirty(draft, *desired),
          "Applied settings become clean even if persistence fails");
    check(settings_dirty(draft, applied), "Rollback preserves dirty draft");
    draft.edge = 256;
    check(!settings_from_draft(draft), "Enum cannot wrap");
    draft.edge = 0;
    draft.selections_valid = false;
    check(!settings_from_draft(draft), "Missing selection is invalid");
    SettingsRecovery recovery;
    check(!recovery.needed() && !recovery.can_retry(), "Healthy needs no Retry");
    recovery.sampling = true;
    check(recovery.can_retry(), "Stopped worker can retry");
    recovery.restarting = true;
    check(recovery.needed() && !recovery.can_retry(), "Restart stays visible but cannot repeat");
    recovery.sampling = recovery.restarting = false;
    recovery.rendering = true;
    check(recovery.can_retry(), "Rendering recovery independent of worker");
    recovery.rendering = false;
    recovery.tray = true;
    check(recovery.can_retry(), "Tray recovery independent of worker");
    recovery.tray = false;
    recovery.desktop = true;
    check(recovery.can_retry(), "Placement recovery independent of worker");
}
} // namespace

namespace loadbar {
// Hidden native controls only: never run(), register an AppBar, start collectors, or save settings.
struct SettingsWindowTests {
    static void run() {
        const auto instance = GetModuleHandleW(nullptr);
        INITCOMMONCONTROLSEX controls{sizeof(controls),
                                      ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
        check(InitCommonControlsEx(&controls) != FALSE, "Common controls initialized");
        for (const auto *name : {L"Loadbar.Bar", L"Loadbar.Settings", L"Loadbar.SettingsContent"}) {
            WNDCLASSEXW cls{};
            cls.cbSize = sizeof(cls);
            cls.hInstance = instance;
            cls.lpfnWndProc = Application::window_proc;
            cls.lpszClassName = name;
            check(RegisterClassExW(&cls) != 0, "Register hidden test class");
        }
        {
            struct TrayCalls {
                std::array<std::pair<DWORD, NOTIFYICONDATAW>, 7> requests{};
                std::size_t count{};
            } tray;
            Application app(instance);
            app.settings_ = {};
            app.window_ = CreateWindowExW(0, L"Loadbar.Bar", L"", WS_POPUP, 0, 0, 1, 1, nullptr,
                                          nullptr, instance, &app);
            if (!app.window_) {
                throw std::runtime_error("Create hidden owner");
            }
            check(SetPropW(app.window_, L"Loadbar.TestTray", &tray) != FALSE,
                  "Install tray request recorder");
            app.notify_icon_ = [](DWORD operation, NOTIFYICONDATAW *data) -> BOOL {
                auto *calls = static_cast<TrayCalls *>(GetPropW(data->hWnd, L"Loadbar.TestTray"));
                if (!calls || calls->count == calls->requests.size()) {
                    return FALSE;
                }
                calls->requests[calls->count++] = {operation, *data};
                return TRUE;
            };
            check(app.add_tray() && app.add_tray(), "Record initial tray add and update");
            app.tray_added_ = false; // Explorer restart forgets the previous registration.
            check(app.add_tray(), "Record tray restoration after Shell restart");
            app.remove_tray();
            check(tray.count == 7, "Each tray registration selects version 4, followed by removal");
            for (std::size_t i = 0; i < 6; i += 2) {
                const auto &[operation, data] = tray.requests[i];
                constexpr DWORD tooltip_flags = NIF_TIP | NIF_SHOWTIP;
                check(operation == static_cast<DWORD>(i == 2 ? NIM_MODIFY : NIM_ADD),
                      "Tray add/update sequence");
                check(data.cbSize == sizeof(data) && data.hWnd == app.window_ && data.uID == 1 &&
                          (data.uFlags & tooltip_flags) == tooltip_flags &&
                          std::wstring_view(data.szTip) == L"Loadbar — Settings and Exit",
                      "Standard tray tooltip is enabled with a nonempty Loadbar label");
                check(tray.requests[i + 1].first == NIM_SETVERSION &&
                          tray.requests[i + 1].second.uVersion == NOTIFYICON_VERSION_4,
                      "Tray tooltip remains enabled with version 4 events");
            }
            check(tray.requests.back().first == NIM_DELETE && !app.tray_added_,
                  "Recorded tray lifecycle cleans up");
            RemovePropW(app.window_, L"Loadbar.TestTray");
            app.notify_icon_ = &Shell_NotifyIconW;
            unsigned invalidations{};
            check(SetPropW(app.window_, L"Loadbar.TestResize", &invalidations) != FALSE,
                  "Install hidden resize observation");
            app.resize_invalidate_ = [](HWND window, const RECT *area, BOOL erase) -> BOOL {
                auto *count = static_cast<unsigned *>(GetPropW(window, L"Loadbar.TestResize"));
                if (count && !area && !erase) {
                    ++*count;
                }
                return InvalidateRect(window, area, erase);
            };
            // Native SetWindowPos sends WM_SIZE through the production dispatcher. The window
            // stays hidden: inspect the invalidation request, not an invisible HWND update region.
            for (const auto size : {SIZE{600, 74}, SIZE{560, 74}, SIZE{74, 600}, SIZE{74, 560}}) {
                const auto previous = invalidations;
                check(SetWindowPos(app.window_, nullptr, 0, 0, size.cx, size.cy,
                                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
                      "Resize hidden bar without registering it");
                check(invalidations == previous + 1,
                      "Bar grow/shrink invalidates independently of telemetry and hover");
            }
            const auto previous = invalidations;
            SendMessageW(app.window_, WM_SIZE, SIZE_MINIMIZED, 0);
            check(invalidations == previous, "Minimized bar does not request a paint");
            RemovePropW(app.window_, L"Loadbar.TestResize");
            app.resize_invalidate_ = &InvalidateRect;
            app.create_settings();
            app.populate_settings();
            app.layout_settings();
            check(!IsWindowVisible(app.window_) && !IsWindowVisible(app.settings_window_),
                  "Tests never show windows");
            const auto field = [&](int index) {
                return GetDlgItem(app.settings_content_, kFirstField + index);
            };
            const auto enabled = [&](int id) {
                return IsWindowEnabled(app.settings_control(id)) != FALSE;
            };
            const auto retry_visible = [&] {
                return (static_cast<ULONG_PTR>(
                            GetWindowLongPtrW(app.settings_control(102), GWL_STYLE)) &
                        WS_VISIBLE) != 0;
            };
            check(!enabled(kApplySettings) && !enabled(kCancelSettings) && !retry_visible(),
                  "Initial disabled actions and hidden Retry");
            const auto status = GetDlgItem(app.settings_window_, kFooterStatus);
            if (!status) {
                throw std::runtime_error("Footer status control missing");
            }
            check(!GetDlgItem(app.settings_content_, kFooterStatus),
                  "Summary block removed; notice belongs only to footer");
            check((static_cast<ULONG_PTR>(GetWindowLongPtrW(status, GWL_STYLE)) & WS_VISIBLE) == 0,
                  "Healthy footer starts hidden");
            // Query the real color handler with an offscreen DC, without painting the desktop.
            const auto dc = CreateCompatibleDC(nullptr);
            if (!dc) {
                throw std::runtime_error("Offscreen color test DC creation failed");
            }
            bool label_colors = true;
            for (int i = 0; i < 9; ++i) {
                const auto parent = i == 8 ? app.settings_window_ : app.settings_content_;
                const auto label = i == 8 ? status : GetDlgItem(parent, kFirstLabel + i);
                SetBkMode(dc, OPAQUE);
                SetTextColor(dc, RGB(1, 2, 3));
                const auto brush =
                    SendMessageW(parent, WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(dc),
                                 reinterpret_cast<LPARAM>(label));
                label_colors = label_colors &&
                               brush == reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW)) &&
                               GetBkMode(dc) == TRANSPARENT &&
                               GetTextColor(dc) == GetSysColor(COLOR_WINDOWTEXT);
            }
            DeleteDC(dc);
            check(label_colors, "Every label uses transparent text on system window background");
            SendMessageW(app.settings_window_, WM_SYSCOLORCHANGE, 0, 0);
            SendMessageW(app.settings_window_, WM_THEMECHANGED, 0, 0);
            // Real EN_CHANGE travels through the content host to the Settings callback.
            SetWindowTextW(field(4), L"60");
            check(enabled(kApplySettings) && enabled(kCancelSettings),
                  "Native edit enables actions");
            SetWindowTextW(field(4), L"40.000");
            check(!enabled(kApplySettings) && !enabled(kCancelSettings),
                  "Equivalent value disables actions");
            for (const int index : {4, 5}) {
                SetWindowTextW(field(index), L"");
                check(enabled(kApplySettings) && enabled(kCancelSettings),
                      "Invalid edit is cancellable");
                SendMessageW(app.settings_window_, WM_COMMAND, kCancelSettings, 0);
                check(!app.settings_dirty_ && !enabled(kCancelSettings),
                      "Cancel restores committed values");
            }
            for (const int index : {0, 1, 2, 3, 6}) {
                if (index < 3) {
                    app.choice_ids_[static_cast<std::size_t>(index)].push_back(L"test-device");
                    SendMessageW(field(index), CB_ADDSTRING, 0,
                                 reinterpret_cast<LPARAM>(L"Test device"));
                }
                SendMessageW(field(index), CB_SETCURSEL, 1, 0);
                SendMessageW(app.settings_content_, WM_COMMAND,
                             MAKEWPARAM(kFirstField + index, CBN_SELCHANGE),
                             reinterpret_cast<LPARAM>(field(index)));
                check(app.settings_dirty_ && enabled(kApplySettings),
                      "Every combo field is tracked");
                const auto before = app.settings_draft();
                app.populate_devices(false);
                check(settings_from_draft(before) == settings_from_draft(app.settings_draft()),
                      "Device discovery preserves identity draft");
                app.cancel_settings();
                check(!enabled(kApplySettings), "Cancel disables Apply");
            }

            for (const int index : {1, 2}) {
                wchar_t text[16]{};
                SendMessageW(field(index), CB_GETLBTEXT, 1, reinterpret_cast<LPARAM>(text));
                check(std::wstring_view(text) == L"Hide", "GPU and Network offer Hide");
                const auto select = [&](int row) {
                    SendMessageW(field(index), CB_SETCURSEL, static_cast<WPARAM>(row), 0);
                    SendMessageW(app.settings_content_, WM_COMMAND,
                                 MAKEWPARAM(kFirstField + index, CBN_SELCHANGE),
                                 reinterpret_cast<LPARAM>(field(index)));
                };
                app.choice_ids_[static_cast<std::size_t>(index)].push_back(L"remembered-device");
                const auto row = SendMessageW(field(index), CB_ADDSTRING, 0,
                                              reinterpret_cast<LPARAM>(L"Remembered device"));
                select(static_cast<int>(row));
                select(1);
                auto hidden = settings_from_draft(app.settings_draft());
                check(hidden && (index == 1 ? !hidden->gpu_visible : !hidden->network_visible) &&
                          (index == 1 ? hidden->gpu_id : hidden->network_id) ==
                              L"remembered-device",
                      "Selecting Hide retains the most recent draft device identity");
                app.populate_devices(false);
                check(settings_from_draft(app.settings_draft()) == hidden,
                      "Rediscovery preserves hidden disconnected draft selection");
                // Simulate the successful commit/reopen path without touching the registry or
                // Shell.
                const auto applied_before_hide = app.settings_;
                app.settings_ = *hidden;
                app.populate_settings();
                check(!app.settings_dirty_ && SendMessageW(field(index), CB_GETCURSEL, 0, 0) == 1 &&
                          settings_from_draft(app.settings_draft()) == hidden,
                      "Committed Hide reopens cleanly with identity retained");
                app.settings_ = applied_before_hide;
                app.cancel_settings();
                check(!app.settings_dirty_, "Cancel restores committed visible selection");
            }

            {
                const auto original_catalog = app.catalog_;
                const auto original_settings = app.settings_;
                auto catalog = std::make_shared<Catalog>(*original_catalog);
                catalog->disks = {{DeviceKind::disk, L"drive-a", L"Physical drive A", 0},
                                  {DeviceKind::disk, L"drive-b", L"Physical drive B", 1}};
                app.catalog_ = catalog;
                app.populate_devices(false);
                const auto list = GetDlgItem(app.settings_content_, kDrivesControl);
                check(ListView_GetItemCount(list) == 2 && ListView_GetCheckState(list, 0) &&
                          ListView_GetCheckState(list, 1) && !app.settings_dirty_,
                      "New disks appear checked without making settings dirty");
                check(GetNextDlgTabItem(app.settings_window_, field(2), FALSE) == list &&
                          GetNextDlgTabItem(app.settings_window_, list, FALSE) == field(3),
                      "Native drive checklist tab order is between Network and Edge");
                RECT network{}, drives{}, edge{};
                GetWindowRect(field(2), &network);
                GetWindowRect(list, &drives);
                GetWindowRect(field(3), &edge);
                check(network.bottom <= drives.top && drives.bottom <= edge.top,
                      "Native drive checklist occupies requested section");
                ListView_SetCheckState(list, 0, FALSE);
                check(app.settings_dirty_ && enabled(kApplySettings) && enabled(kCancelSettings) &&
                          disk_hidden(app.settings_draft().hidden_disks, L"drive-a") &&
                          app.settings_.hidden_disks.empty(),
                      "Checkbox notifications update draft, not committed settings");
                app.cancel_settings();
                check(ListView_GetCheckState(list, 0) && !app.settings_dirty_,
                      "Cancel restores checkboxes");
                ListView_SetCheckState(list, 0, FALSE);
                std::swap(catalog->disks[0], catalog->disks[1]);
                catalog->disks.push_back({DeviceKind::disk, L"drive-c", L"New physical drive", 2});
                app.populate_devices(false);
                check(ListView_GetCheckState(list, 0) && !ListView_GetCheckState(list, 1) &&
                          ListView_GetCheckState(list, 2) &&
                          app.settings_draft().hidden_disks == HiddenDiskIds{L"drive-a"},
                      "Reorder and arrival preserve draft exclusions by identity");
                app.settings_ = *settings_from_draft(app.settings_draft());
                app.populate_settings();
                check(!app.settings_dirty_ && !ListView_GetCheckState(list, 1),
                      "Committed disk visibility reopens cleanly");
                catalog->disks.erase(catalog->disks.begin() + 1);
                app.populate_devices(false);
                check(!app.settings_dirty_ &&
                          disk_hidden(app.settings_draft().hidden_disks, L"drive-a"),
                      "Disappearance retains saved exclusion without a dirty draft");
                catalog->disks.push_back({DeviceKind::disk, L"drive-a", L"Reconnected drive", 9});
                app.populate_devices(false);
                check(!ListView_GetCheckState(list, 2),
                      "Reconnected excluded disk stays unchecked");
                for (unsigned i = 3; i < 12; ++i) {
                    catalog->disks.push_back({DeviceKind::disk, L"extra" + std::to_wstring(i),
                                              L"Additional drive " + std::to_wstring(i), i});
                }
                app.populate_devices(false);
                check(ListView_GetCountPerPage(list) == 4,
                      "Many disks use four visible native rows with scrolling");
                ListView_SetItemState(list, -1, 0, LVIS_FOCUSED | LVIS_SELECTED);
                ListView_SetItemState(list, 7, LVIS_FOCUSED | LVIS_SELECTED,
                                      LVIS_FOCUSED | LVIS_SELECTED);
                ListView_EnsureVisible(list, 7, FALSE);
                const auto focused_id = app.drive_choices_[7].first;
                const auto top_id =
                    app.drive_choices_[static_cast<std::size_t>(ListView_GetTopIndex(list))].first;
                SendMessageW(list, WM_KEYDOWN, VK_SPACE, 0);
                SendMessageW(list, WM_KEYUP, VK_SPACE, 0);
                check(disk_hidden(app.settings_draft().hidden_disks, focused_id),
                      "Native Space toggles the focused disk checkbox");
                std::swap(catalog->disks[0], catalog->disks[1]);
                app.populate_devices(false);
                check(app.drive_choices_[static_cast<std::size_t>(
                                             ListView_GetNextItem(list, -1, LVNI_FOCUSED))]
                                  .first == focused_id &&
                          app.drive_choices_[static_cast<std::size_t>(ListView_GetTopIndex(list))]
                                  .first == top_id,
                      "Catalog refresh preserves focus and scroll identity");
                catalog->disks.clear();
                app.populate_devices(false);
                check(ListView_GetItemCount(list) == 1 &&
                          ListView_GetItemState(list, 0, LVIS_STATEIMAGEMASK) == 0,
                      "Empty inventory has an informational row without a checkbox");
                check(!IsWindowEnabled(list) &&
                          GetNextDlgTabItem(app.settings_window_, field(2), FALSE) == field(3),
                      "Empty informational row cannot receive keyboard or mouse interaction");
                catalog->disks.push_back({DeviceKind::disk, L"another", L"New drive", 10});
                app.populate_devices(false);
                check(IsWindowEnabled(list) && ListView_GetCheckState(list, 0) &&
                          GetNextDlgTabItem(app.settings_window_, field(2), FALSE) == list,
                      "Discovery re-enables the checklist with new drive checked");
                app.settings_ = original_settings;
                app.catalog_ = original_catalog;
                app.populate_settings();
            }

            const auto catalog_before = app.catalog_;
            const Monitor display_a{L"display-a", L"Display A", {0, 0, 1920, 1080}, 96, true};
            Monitor display_b{L"display-b", L"Display B", {-1080, 0, 0, 1920}, 144, false};
            app.update_monitors({display_a});
            SetWindowTextW(field(4), L"60");
            SetWindowTextW(field(5), L"1250");
            SendMessageW(field(0), CB_SETCURSEL, 1, 0);
            app.update_settings_actions();
            const auto draft_before = settings_from_draft(app.settings_draft());
            app.update_monitors({display_b, display_a});
            check(app.catalog_ == catalog_before && app.choice_ids_[0].size() == 3 &&
                      settings_from_draft(app.settings_draft()) == draft_before &&
                      app.settings_dirty_,
                  "Monitor-only arrival/reorder updates choices and preserves draft");
            display_b.label = L"Display B rotated (1920 x 1080)";
            display_b.bounds = {-1920, 0, 0, 1080};
            display_b.primary = true;
            app.update_monitors({display_b});
            check(app.choice_ids_[0].back() == display_a.id &&
                      settings_from_draft(app.settings_draft()) == draft_before,
                  "Disconnected draft selection stays resolvable as a disconnected choice");
            wchar_t monitor_text[128]{};
            SendMessageW(field(0), CB_GETLBTEXT, 1, reinterpret_cast<LPARAM>(monitor_text));
            check(std::wstring_view(monitor_text) == display_b.label,
                  "Rotation/primary label refreshes without a telemetry catalog change");
            app.update_monitors({display_a, display_b});
            check(app.choice_ids_[0].size() == 3 &&
                      settings_from_draft(app.settings_draft()) == draft_before,
                  "Reconnected selection uses its real entry without duplicates");
            app.update_monitors({});
            check(settings_from_draft(app.settings_draft()) == draft_before &&
                      app.choice_ids_[0].size() == 2,
                  "Empty display enumeration preserves the current draft");
            app.cancel_settings();
            check(!app.settings_dirty_,
                  "Cancel restores committed choices after monitor-only changes");
            SetWindowTextW(field(4), L"60");
            app.recovery_.desktop = true;
            app.update_settings_status();
            check(retry_visible() && enabled(102) && app.settings_dirty_,
                  "Recovery shown without losing draft");
            app.recovery_.desktop = false;
            app.recovery_.sampling = app.recovery_.restarting = true;
            app.update_settings_status();
            check(retry_visible() && !enabled(102), "Pending recovery disabled");
            app.recovery_ = {};
            app.update_settings_status();
            check(!retry_visible() && app.settings_dirty_, "Recovery cleared, edits preserved");
            check(app.settings_notice_.empty(), "Healthy recovery clears footer text");
            app.recovery_ = {true, false, true, true, true};
            app.settings_save_failed_ = app.fallback_ = true;
            for (int stage = 0; stage < 6; ++stage) {
                app.update_settings_status();
                const wchar_t *expected[]{L"Desktop placement",  L"Monitoring stopped",
                                          L"Notification-area",  L"Rendering failed",
                                          L"Settings could not", L"Preferred monitor"};
                check(app.settings_notice_.starts_with(expected[stage]),
                      "Footer prioritizes actionable failures");
                RECT message_bounds{}, first_button{};
                GetWindowRect(status, &message_bounds);
                GetWindowRect(app.settings_control(kApplySettings), &first_button);
                check(message_bounds.bottom <= first_button.top,
                      "Actual footer message stays above buttons");
                if (stage == 0) {
                    app.recovery_.desktop = false;
                }
                if (stage == 1) {
                    app.recovery_.sampling = false;
                }
                if (stage == 2) {
                    app.recovery_.tray = false;
                }
                if (stage == 3) {
                    app.recovery_.rendering = false;
                }
                if (stage == 4) {
                    app.settings_save_failed_ = false;
                }
                if (stage == 5) {
                    app.fallback_ = false;
                }
            }
            app.update_settings_status();
            check(app.settings_notice_.empty() &&
                      (static_cast<ULONG_PTR>(GetWindowLongPtrW(status, GWL_STYLE)) & WS_VISIBLE) ==
                          0,
                  "Resolved footer hides and releases layout space");
            app.cancel_settings();
            // Changes applied elsewhere become the Cancel baseline, without losing pending edits.
            SetWindowTextW(field(4), L"60");
            app.settings_.thickness = 80;
            app.update_settings_actions();
            check(app.settings_dirty_ && app.settings_draft().thickness == L"60",
                  "External committed change preserves draft");
            app.cancel_settings();
            check(!app.settings_dirty_ && app.settings_draft().thickness == L"80",
                  "Cancel restores latest committed setting");
            SendMessageW(app.settings_window_, WM_COMMAND, kApplySettings, 0);
            check(!app.appbar_.registered(), "Disabled Apply performs no placement");
            // Synthetic size notifications keep these native windows hidden.
            SendMessageW(app.settings_window_, WM_SIZE, SIZE_MINIMIZED, 0);
            check(app.settings_minimized_, "Minimization arms a readings refresh on restore");
            SendMessageW(app.settings_window_, WM_SIZE, SIZE_RESTORED, MAKELPARAM(800, 700));
            check(!app.settings_minimized_, "Restore consumes the pending refresh");
            SendMessageW(app.settings_window_, WM_SIZE, SIZE_RESTORED, MAKELPARAM(900, 700));
            check(!app.settings_minimized_, "Ordinary resize does not rearm restore refresh");
            const auto font = app.settings_font_;
            SetWindowPos(app.settings_window_, nullptr, 0, 0, 1000, 900,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            RECT large{};
            GetWindowRect(GetDlgItem(app.settings_content_, kDetailsControl), &large);
            SetWindowPos(app.settings_window_, nullptr, 0, 0, 800, 700,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            RECT compact{};
            GetWindowRect(GetDlgItem(app.settings_content_, kDetailsControl), &compact);
            check(large.bottom - large.top > compact.bottom - compact.top,
                  "Actual ListView grows vertically");
            check(large.right - large.left > compact.right - compact.left,
                  "Actual ListView grows horizontally");
            check(font == app.settings_font_, "Resize reuses font");
            SetWindowPos(app.settings_window_, nullptr, 0, 0, 340, 280,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            RECT footer_before{}, footer_after{};
            GetWindowRect(app.settings_control(100), &footer_before);
            SendMessageW(app.settings_content_, WM_VSCROLL, SB_BOTTOM, 0);
            GetWindowRect(app.settings_control(100), &footer_after);
            check(app.scroll_ > 0 && EqualRect(&footer_before, &footer_after),
                  "Scrolling content leaves native footer fixed");
            app.ensure_visible(field(0));
            check(app.scroll_ < 30, "Keyboard focus can reveal first field");
            SendMessageW(app.settings_content_, WM_UPDATEUISTATE,
                         MAKEWPARAM(UIS_SET, UISF_HIDEACCEL | UISF_HIDEFOCUS), 0);
            app.text_scale_ = 2;
            app.layout_settings();
            check(app.settings_font_height_ ==
                      -static_cast<int>(std::round(
                          24.0F * static_cast<float>(GetDpiForWindow(app.settings_window_)) / 96)),
                  "Font follows text scale");
            const auto exit_button = app.settings_control(100);
            check(GetParent(exit_button) == app.settings_content_,
                  "Text scaling in a small window moves footer into scrollable content");
            check(SendMessageW(exit_button, WM_QUERYUISTATE, 0, 0) ==
                      SendMessageW(app.settings_content_, WM_QUERYUISTATE, 0, 0),
                  "Reparented footer inherits its parent's keyboard cue state");
            const auto inside_viewport = [&](HWND control) {
                RECT bounds{}, viewport{};
                GetWindowRect(control, &bounds);
                GetWindowRect(app.settings_content_, &viewport);
                return bounds.top >= viewport.top && bounds.bottom <= viewport.bottom;
            };
            SendMessageW(app.settings_content_, WM_COMMAND, MAKEWPARAM(100, BN_SETFOCUS),
                         reinterpret_cast<LPARAM>(exit_button));
            check(inside_viewport(exit_button), "Keyboard focus scrolls Exit into view");
            app.ensure_visible(field(0));
            check(inside_viewport(field(0)), "First field remains keyboard reachable");
            app.recovery_.rendering = true;
            app.rendering_error_ = std::wstring(600, L'x');
            app.update_settings_status();
            check(GetParent(status) == app.settings_content_ && app.settings_control(102),
                  "Recovery growth retains scrollable status and Retry controls");
            app.ensure_visible(app.settings_control(102));
            check(inside_viewport(app.settings_control(102)), "Retry remains keyboard reachable");
            app.ensure_visible(exit_button);
            check(inside_viewport(exit_button),
                  "Long recovery notice cannot hide Exit permanently");
            app.ensure_visible(field(4));
            SetWindowTextW(field(4), L"90");
            check(enabled(kApplySettings) && enabled(kCancelSettings),
                  "Reparented actions still track edits");
            const auto check_tab_order = [&] {
                auto previous = GetDlgItem(app.settings_content_, kDetailsControl);
                for (const auto id : {kApplySettings, kCancelSettings, 102, 100}) {
                    const auto action = app.settings_control(id);
                    const auto next = GetNextDlgTabItem(app.settings_window_, previous, FALSE);
                    if (next != action) {
                        throw std::runtime_error(std::format(
                            "Native tab order: after {} expected {}, got {} (parent {})",
                            GetDlgCtrlID(previous), id, GetDlgCtrlID(next),
                            GetParent(action) == app.settings_content_ ? "content" : "root"));
                    }
                    check(GetNextDlgTabItem(app.settings_window_, action, TRUE) == previous,
                          "Native reverse tab order preserves footer action order");
                    previous = action;
                }
                check(GetNextDlgTabItem(app.settings_window_, previous, FALSE) == field(0),
                      "Tab wraps from Exit to the first field");
            };
            check_tab_order();
            const auto cancel = app.settings_control(kCancelSettings);
            SendMessageW(app.settings_content_, WM_COMMAND, MAKEWPARAM(kCancelSettings, BN_CLICKED),
                         reinterpret_cast<LPARAM>(cancel));
            check(!app.settings_dirty_, "Reparented Cancel still routes through content host");
            app.recovery_ = {};
            app.rendering_error_.clear();
            app.update_settings_status();
            app.text_scale_ = 1;
            SetWindowPos(app.settings_window_, nullptr, 0, 0, 800, 700,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            app.layout_settings();
            check(GetParent(exit_button) == app.settings_window_ &&
                      GetParent(status) == app.settings_window_,
                  "Larger window restores the pinned footer");
            SetWindowTextW(field(4), L"90");
            app.recovery_.sampling = true;
            app.update_settings_status();
            check_tab_order();
            check(!IsWindowVisible(app.settings_window_), "Window stayed hidden throughout tests");
        }
        MSG message{};
        PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
        for (const auto *name : {L"Loadbar.Bar", L"Loadbar.Settings", L"Loadbar.SettingsContent"}) {
            check(UnregisterClassW(name, instance) != FALSE, "All hidden windows destroyed");
        }
    }
};
} // namespace loadbar
int main() {
    try {
        drive_settings_tests();
        geometry_tests();
        constrained_geometry_tests();
        draft_tests();
        migration_tests();
        loadbar::SettingsWindowTests::run();
        std::cout << "Settings geometry, drafts, recovery and hidden native controls passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    } catch (...) {
        std::fputs("Unexpected Settings test failure\n", stderr);
        return 1;
    }
}
