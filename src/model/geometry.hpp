#pragma once

#include "model/settings.hpp"

#include <functional>
#include <optional>
#include <span>

namespace loadbar {
struct Rect {
    int left{}, top{}, right{}, bottom{};
    bool operator==(const Rect &) const = default;
};
struct Monitor {
    std::wstring id;
    std::wstring label;
    Rect bounds;
    unsigned dpi{96};
    bool primary{};
    bool operator==(const Monitor &) const = default;
};
struct MonitorSelection {
    std::size_t index{};
    bool fallback{};
};
[[nodiscard]] std::optional<MonitorSelection> select_monitor(std::span<const Monitor> monitors,
                                                             std::wstring_view preference);
[[nodiscard]] bool valid_rect(Rect rect) noexcept;
[[nodiscard]] std::optional<Rect> fit_window(Rect suggested, Rect work_area) noexcept;
[[nodiscard]] bool horizontal(Edge edge) noexcept;
[[nodiscard]] Edge resolve_edge(Edge preference, Rect monitor) noexcept;
// Convert the previous percentage choice once; future rotation/DPI changes retain DIPs.
bool migrate_thickness(Settings &settings, Rect monitor, unsigned dpi);
enum class ThicknessAdjustment : std::uint8_t { none, minimum, maximum, rounding };
struct ThicknessResolution {
    double requested{}, minimum{}, maximum{}, effective{};
    int pixels{};
    ThicknessAdjustment adjustment{};
};
[[nodiscard]] std::optional<ThicknessResolution>
resolve_thickness(const Settings &settings, Rect monitor, unsigned dpi, double minimum_dips);
[[nodiscard]] std::optional<int> thickness_pixels(const Settings &settings, Rect monitor,
                                                  unsigned dpi, double minimum_dips);
[[nodiscard]] std::optional<Rect> edge_rectangle(Rect available, Edge edge, int thickness);

class ShellPort {
  public:
    virtual ~ShellPort() = default;
    virtual bool add() = 0;
    virtual void remove() noexcept = 0;
    virtual Rect query(Rect rectangle, Edge edge) = 0;
    virtual Rect set(Rect rectangle, Edge edge) = 0;
};
class AppBar {
  public:
    explicit AppBar(ShellPort &shell) : shell_(shell) {}
    ~AppBar();
    AppBar(const AppBar &) = delete;
    AppBar &operator=(const AppBar &) = delete;
    // Optional readable-thickness policy runs on the Shell-approved edge before SETPOS.
    using ThicknessForEdge = std::function<std::optional<int>(Rect)>;
    [[nodiscard]] std::optional<Rect> position(Rect monitor, Edge edge, int thickness,
                                               const ThicknessForEdge &thickness_for_edge = {});
    void remove() noexcept;
    void shell_restarted() noexcept;
    // Explicit display/DPI changes invalidate hints, not the owned registration.
    void invalidate_position() noexcept;
    [[nodiscard]] bool registered() const noexcept {
        return registered_;
    }

  private:
    ShellPort &shell_;
    bool registered_{};
    bool negotiating_{};
    std::optional<Rect> final_, last_query_, last_submission_, last_monitor_;
    Edge final_edge_{Edge::automatic};
};
} // namespace loadbar
