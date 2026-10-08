#pragma once

#include "app/tooltip_cache.hpp"
#include "model/settings_form.hpp"
#include "platform/desktop.hpp"
#include "telemetry/collector.hpp"
#include "ui/renderer.hpp"
#include "ui/text_preferences.hpp"

#include <shellapi.h>

#include <string>
#include <vector>

namespace loadbar {
class Application {
  public:
    explicit Application(HINSTANCE instance);
    ~Application();
    int run();

  private:
    friend struct SettingsWindowTests;
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam,
                                        LPARAM lparam) noexcept;
    LRESULT message(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    void place_bar();
    void tray_menu();
    bool add_tray();
    void remove_tray() noexcept;
    void create_settings();
    void show_settings(bool activate = true);
    void populate_settings();
    void populate_devices(bool committed, bool monitors_only = false);
    void populate_drives(bool committed);
    void drive_check_changed(int index);
    void update_monitors(std::vector<Monitor> monitors);
    void layout_settings();
    HWND settings_control(int id) const noexcept;
    void apply_settings();
    SettingsDraft settings_draft() const;
    void update_settings_actions();
    void cancel_settings();
    bool commit_settings(Settings settings);
    void update_details();
    void update_settings_status();
    void update_tooltip();
    void schedule_refresh();
    void reconfigure_worker(bool reset = true);
    void sampling_failed(std::optional<Delivery> update);
    void ensure_visible(HWND control);
    void shutdown() noexcept;
    // Observe resize invalidation in hidden-window tests (hidden HWNDs have no update region).
    decltype(&InvalidateRect) resize_invalidate_{&InvalidateRect};
    // Replace only in hidden-window tests to inspect requests without changing the tray.
    decltype(&Shell_NotifyIconW) notify_icon_{&Shell_NotifyIconW};
    decltype(&ShellExecuteExW) launch_task_manager_{&ShellExecuteExW};
    HINSTANCE instance_{};
    HWND window_{};
    HWND settings_window_{};
    HWND settings_content_{};
    bool populating_settings_{};
    bool settings_dirty_{};
    bool settings_minimized_{};
    bool retry_visible_{};
    SettingsRecovery recovery_;
    std::wstring rendering_error_;
    Handle instance_lock_;
    Settings settings_;
    std::vector<Monitor> monitors_;
    WindowsShell shell_;
    AppBar appbar_;
    UINT taskbar_created_{};
    bool tray_added_{};
    bool placing_{};
    bool fullscreen_{};
    bool closing_{};
    bool fallback_{};
    std::wstring placement_error_;
    bool settings_save_failed_{};
    std::wstring settings_notice_;
    std::wstring active_monitor_;
    Latest<Delivery> delivery_;
    SessionPeaks session_peaks_; // Outlives every worker, including Retry replacements.
    DisplayContinuity continuity_;
    CpuSamples known_processors_; // Metadata, including PDH fallback identities, survives Retry.
    DiskInventory known_disks_;   // Last successful discovery survives joined-worker Retry.
    std::unique_ptr<SamplingWorker> worker_;
    std::unique_ptr<TextPreferences> text_preferences_;
    Renderer renderer_;
    Snapshot snapshot_;
    std::shared_ptr<const Catalog> catalog_{std::make_shared<const Catalog>()};
    std::array<std::vector<std::wstring>, 3> choice_ids_;
    std::vector<std::pair<std::wstring, std::wstring>> drive_choices_;
    HiddenDiskIds draft_hidden_disks_;
    std::vector<std::wstring> detail_keys_, detail_values_;
    HWND tooltip_{};
    std::wstring tooltip_text_;
    TooltipCache tooltip_cache_;
    std::optional<Clock::time_point> refresh_deadline_;
    std::uint64_t observation_revision_{};
    bool detail_structure_changed_{true};
    HFONT settings_font_{};
    int settings_font_height_{};
    float text_scale_{1};
    Edge effective_edge_{Edge::right};
    bool paused_{};
    bool hovered_{};
    bool selection_prompted_{};
    ActivityState activity_;
    int scroll_{};
    unsigned recovery_attempts_{};
    std::uint64_t expected_generation_{1};
    std::uint64_t worker_id_{1};
};
} // namespace loadbar
