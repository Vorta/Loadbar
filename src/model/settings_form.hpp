#pragma once

#include "model/settings.hpp"

#include <array>

namespace loadbar {
struct SettingsDraft {
    std::array<std::wstring, 3> device_ids;
    int edge{}, alignment{};
    std::wstring thickness, interval;
    bool selections_valid{true};
};
[[nodiscard]] std::optional<Settings> settings_from_draft(const SettingsDraft &draft);
[[nodiscard]] bool settings_dirty(const SettingsDraft &draft, const Settings &applied);

struct SettingsRecovery {
    bool sampling{}, restarting{}, desktop{}, tray{}, rendering{};
    [[nodiscard]] bool needed() const noexcept {
        return sampling || desktop || tray || rendering;
    }
    [[nodiscard]] bool can_retry() const noexcept {
        return needed() && !restarting;
    }
};
struct ControlBounds {
    int x{}, y{}, width{}, height{};
};
struct SettingsLayout {
    ControlBounds viewport;
    std::array<ControlBounds, 7> labels, fields;
    std::array<ControlBounds, 4> buttons;
    ControlBounds footer_status, readings_label, readings;
    int content_height{}, scroll{}, metric_width{};
    bool scroll_footer{};
};
// All outputs are physical client pixels. Content coordinates include the scroll offset.
[[nodiscard]] SettingsLayout settings_layout(int width, int height, float scale, bool retry,
                                             int scroll, int scrollbar_width,
                                             int status_height = 0);
} // namespace loadbar
