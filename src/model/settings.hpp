#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace loadbar {
inline constexpr double kMinimumThicknessDips = 40;
inline constexpr double kMaximumThicknessDips = 640;

// Sorted unique IDs give allocation-free empty state and nonthrowing moves. Mutate via
// hide_disk/show_disk so equality and binary lookup remain independent of checkbox order.
using HiddenDiskIds = std::vector<std::wstring>;
[[nodiscard]] bool disk_hidden(const HiddenDiskIds &ids, std::wstring_view id) noexcept;
bool hide_disk(HiddenDiskIds &ids, std::wstring id);
void show_disk(HiddenDiskIds &ids, std::wstring_view id);
inline constexpr std::size_t kMaximumHiddenDisks = 1024;
// Quoted IDs may escape every character; include space/quotes and the existing settings fields.
inline constexpr std::size_t kMaximumSettingsCharacters =
    8192 + kMaximumHiddenDisks * (2 * 1024 + 3);

enum class Edge : std::uint8_t { automatic, left, top, right, bottom };
enum class Alignment : std::uint8_t { start, center, end };
struct Settings {
    Edge edge{Edge::automatic};
    double thickness{kMinimumThicknessDips};
    std::uint32_t interval_ms{1000};
    std::wstring monitor_id;
    // Migrated from v1/v2 and retained until resolved against the chosen monitor.
    std::optional<double> legacy_percent;
    std::wstring gpu_id;
    std::wstring network_id;
    Alignment alignment{Alignment::start};
    bool gpu_visible{true}, network_visible{true};
    bool cpu_squares{};
    bool show_hover_info{true}, open_task_manager_on_click{true};
    HiddenDiskIds hidden_disks;
    bool operator==(const Settings &) const = default;
};
[[nodiscard]] inline bool disk_visible(std::wstring_view id, const Settings &settings) {
    return !disk_hidden(settings.hidden_disks, id);
}
[[nodiscard]] bool valid_settings(const Settings &settings);
[[nodiscard]] bool collection_changed(const Settings &before, const Settings &after);
[[nodiscard]] std::wstring encode_settings(const Settings &settings);
[[nodiscard]] std::optional<Settings> decode_settings(std::wstring_view text);
} // namespace loadbar
