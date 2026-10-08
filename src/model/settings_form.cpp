#include "model/settings_form.hpp"

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace loadbar {
namespace {
std::optional<double> number(const std::wstring &text) {
    wchar_t *end{};
    const auto value = wcstod(text.c_str(), &end);
    if (end == text.c_str() || end != text.c_str() + text.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}
} // namespace
std::optional<Settings> settings_from_draft(const SettingsDraft &draft) {
    const auto thickness = number(draft.thickness);
    const auto interval = number(draft.interval);
    if (!draft.selections_valid || !thickness || !interval || *interval < 250 || *interval > 5000 ||
        std::floor(*interval) != *interval || draft.edge < 0 || draft.edge > 4 ||
        draft.alignment < 0 || draft.alignment > 2) {
        return std::nullopt;
    }
    Settings result;
    result.monitor_id = draft.device_ids[0];
    result.gpu_id = draft.device_ids[1];
    result.network_id = draft.device_ids[2];
    result.gpu_visible = draft.gpu_visible;
    result.network_visible = draft.network_visible;
    result.hidden_disks = draft.hidden_disks;
    result.cpu_squares = draft.cpu_squares;
    result.show_hover_info = draft.show_hover_info;
    result.open_task_manager_on_click = draft.open_task_manager_on_click;
    result.edge = static_cast<Edge>(draft.edge);
    result.alignment = static_cast<Alignment>(draft.alignment);
    result.thickness = *thickness;
    result.interval_ms = static_cast<std::uint32_t>(*interval);
    return valid_settings(result) ? std::optional{std::move(result)} : std::nullopt;
}
bool settings_dirty(const SettingsDraft &draft, const Settings &applied) {
    const auto desired = settings_from_draft(draft);
    return !desired || *desired != applied;
}
SettingsLayout settings_layout(int width, int height, float scale, bool retry, int scroll,
                               int scrollbar_width, int status_height, int drive_rows,
                               int drive_row_height) {
    const auto px = [scale](int value) {
        return static_cast<int>(std::round(static_cast<float>(value) * scale));
    };
    SettingsLayout result;
    const int margin = px(12), gap = px(8), button_height = px(28);
    const std::array widths{px(85), px(85), px(150), px(110)};
    const int content_width = std::max(0, width - scrollbar_width);
    const auto footer = [&](int available_width, bool compact) {
        // Preserve keyboard order and right alignment. A constrained footer becomes part
        // of the scrolling content, including any wrapped buttons and recovery notice.
        const int max_width = std::max(1, available_width - 2 * margin);
        const int status_space = status_height > 0 ? status_height + gap : 0;
        int row_start = 0, row_width = 0, row_y = margin + status_space, row_height = 0;
        const auto finish_row = [&](int end) {
            int x = std::max(margin, available_width - margin - row_width);
            for (int i = row_start; i < end; ++i) {
                if (i == 2 && !retry) {
                    continue;
                }
                auto &button = result.buttons[static_cast<std::size_t>(i)];
                button.x = x;
                button.y = row_y;
                x += button.width + gap;
            }
        };
        for (int i = 0; i < 4; ++i) {
            if (i == 2 && !retry) {
                continue;
            }
            const int natural_width = compact ? widths[0] : widths[static_cast<std::size_t>(i)];
            const int next_width = std::min(natural_width, max_width);
            const int next_height = button_height * ((natural_width + next_width - 1) / next_width);
            if (row_width && row_width + gap + next_width > max_width) {
                finish_row(i);
                row_start = i;
                row_y += row_height + gap;
                row_width = 0;
                row_height = 0;
            }
            result.buttons[static_cast<std::size_t>(i)] = {0, 0, next_width, next_height};
            row_width += (row_width ? gap : 0) + next_width;
            row_height = std::max(row_height, next_height);
        }
        finish_row(4);
        result.footer_status = {margin, margin, max_width, std::max(0, status_height)};
        return row_y + row_height + margin;
    };
    int footer_height = footer(width, false);
    // Keep at least one usable form row; DPI/text changes can outgrow an existing window
    // even when the normal minimum track size would have prevented a manual resize.
    result.scroll_footer = height < footer_height + px(80);
    if (result.scroll_footer) {
        footer_height = footer(content_width, true);
    }
    const int viewport_height = std::max(0, result.scroll_footer ? height : height - footer_height);
    result.viewport = {0, 0, std::max(0, width), viewport_height};
    const bool stacked = content_width < px(500);
    const int row_height = px(stacked ? 54 : 30);
    const int drive_height =
        drive_rows > 0 ? std::clamp(drive_rows, 1, 4) * std::max(1, drive_row_height) + px(4) : 0;
    const int drive_space = drive_rows > 0 ? drive_height + gap + (stacked ? px(24) : 0) : 0;
    const int drive_y = margin + 3 * row_height;
    result.drives_label = {margin, drive_y, stacked ? content_width - 2 * margin : px(166), px(24)};
    const int drive_x = stacked ? margin : px(186);
    result.drives = {drive_x, drive_y + (stacked ? px(24) : 0),
                     std::max(1, content_width - drive_x - margin), drive_height};
    for (std::size_t i = 0; i < result.fields.size(); ++i) {
        const int y = margin + static_cast<int>(i) * row_height + (i >= 3 ? drive_space : 0);
        result.labels[i] = {margin, y + (stacked ? 0 : px(2)),
                            stacked ? content_width - 2 * margin : px(166), px(24)};
        const int x = stacked ? margin : px(186);
        result.fields[i] = {x, y + (stacked ? px(24) : 0), std::max(1, content_width - x - margin),
                            px(25)};
    }
    const int preference_y = margin + 7 * row_height + gap + drive_space;
    const int preference_height = px(stacked ? 48 : 30);
    for (std::size_t i = 0; i < result.preferences.size(); ++i) {
        result.preferences[i] = {margin, preference_y + static_cast<int>(i) * preference_height,
                                 std::max(1, content_width - 2 * margin), preference_height};
    }
    const int form_bottom = preference_y + 3 * preference_height + gap;
    const int table_width = std::max(1, content_width - 2 * margin);
    result.readings_label = {margin, form_bottom, table_width, px(stacked ? 42 : 26)};
    const int table_y = result.readings_label.y + result.readings_label.height;
    const int table_height =
        result.scroll_footer ? px(120) : std::max(px(120), viewport_height - table_y - margin);
    result.readings = {margin, table_y, table_width, table_height};
    result.content_height = table_y + table_height + margin;
    const int footer_y = result.scroll_footer ? result.content_height : viewport_height;
    if (result.scroll_footer) {
        const int bottom_padding =
            std::clamp(viewport_height - result.buttons.back().height, 0, margin);
        result.content_height += footer_height - margin + bottom_padding;
    }
    result.scroll = std::clamp(scroll, 0, std::max(0, result.content_height - viewport_height));
    const int footer_offset = footer_y - (result.scroll_footer ? result.scroll : 0);
    result.footer_status.y += footer_offset;
    for (auto &button : result.buttons) {
        button.y += footer_offset;
    }
    for (auto &label : result.labels) {
        label.y -= result.scroll;
    }
    for (auto &control : result.fields) {
        control.y -= result.scroll;
    }
    for (auto &preference : result.preferences) {
        preference.y -= result.scroll;
    }
    result.drives_label.y -= result.scroll;
    result.drives.y -= result.scroll;
    result.readings_label.y -= result.scroll;
    result.readings.y -= result.scroll;
    result.metric_width = std::min(px(180), result.readings.width / 2);
    return result;
}
} // namespace loadbar
