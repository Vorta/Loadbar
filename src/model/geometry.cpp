#include "model/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace loadbar {
std::optional<MonitorSelection> select_monitor(std::span<const Monitor> monitors,
                                               std::wstring_view preference) {
    if (monitors.empty()) {
        return std::nullopt;
    }
    if (!preference.empty()) {
        for (std::size_t index = 0; index < monitors.size(); ++index) {
            if (monitors[index].id == preference) {
                return MonitorSelection{index, false};
            }
        }
    }
    const auto primary = std::ranges::find(monitors, true, &Monitor::primary);
    const auto index =
        primary == monitors.end() ? 0U : static_cast<std::size_t>(primary - monitors.begin());
    return MonitorSelection{index, !preference.empty()};
}
bool valid_rect(Rect rect) noexcept {
    return rect.right > rect.left && rect.bottom > rect.top &&
           static_cast<std::int64_t>(rect.right) - rect.left <= std::numeric_limits<int>::max() &&
           static_cast<std::int64_t>(rect.bottom) - rect.top <= std::numeric_limits<int>::max();
}
std::optional<Rect> fit_window(Rect suggested, Rect work_area) noexcept {
    if (!valid_rect(suggested) || !valid_rect(work_area)) {
        return std::nullopt;
    }
    const int width = std::min(suggested.right - suggested.left, work_area.right - work_area.left);
    const int height = std::min(suggested.bottom - suggested.top, work_area.bottom - work_area.top);
    const int left = std::clamp(suggested.left, work_area.left, work_area.right - width);
    const int top = std::clamp(suggested.top, work_area.top, work_area.bottom - height);
    return Rect{left, top, left + width, top + height};
}
bool horizontal(Edge edge) noexcept {
    return edge == Edge::top || edge == Edge::bottom;
}
Edge resolve_edge(Edge preference, Rect monitor) noexcept {
    if (preference != Edge::automatic) {
        return preference;
    }
    return static_cast<std::int64_t>(monitor.bottom) - monitor.top >
                   static_cast<std::int64_t>(monitor.right) - monitor.left
               ? Edge::bottom
               : Edge::right;
}
bool migrate_thickness(Settings &settings, Rect monitor, unsigned dpi) {
    if (!valid_rect(monitor) || !valid_settings(settings) || dpi < 48 || dpi > 960) {
        return false;
    }
    if (settings.legacy_percent) {
        const auto edge = resolve_edge(settings.edge, monitor);
        const auto dimension =
            horizontal(edge) ? monitor.bottom - monitor.top : monitor.right - monitor.left;
        settings.thickness =
            std::clamp(std::round(dimension * *settings.legacy_percent / 100 * 96 / dpi),
                       kMinimumThicknessDips, kMaximumThicknessDips);
        settings.legacy_percent.reset();
    }
    return true;
}
std::optional<ThicknessResolution> resolve_thickness(const Settings &settings, Rect monitor,
                                                     unsigned dpi, double minimum_dips) {
    if (!valid_rect(monitor) || !valid_settings(settings) || settings.legacy_percent || dpi < 48 ||
        dpi > 960 || !std::isfinite(minimum_dips) || minimum_dips <= 0) {
        return std::nullopt;
    }
    const auto edge = resolve_edge(settings.edge, monitor);
    const auto dimension =
        horizontal(edge) ? monitor.bottom - monitor.top : monitor.right - monitor.left;
    const double scale = static_cast<double>(dpi) / 96;
    const double minimum = std::ceil(minimum_dips * scale);
    const double maximum = std::floor(dimension * 0.25);
    if (minimum > maximum) {
        return std::nullopt;
    }
    const double desired = settings.thickness * scale;
    const auto pixels = static_cast<int>(std::clamp(std::round(desired), minimum, maximum));
    const auto adjustment = desired < minimum                       ? ThicknessAdjustment::minimum
                            : desired > maximum                     ? ThicknessAdjustment::maximum
                            : std::abs(desired - pixels) > 0.000001 ? ThicknessAdjustment::rounding
                                                                    : ThicknessAdjustment::none;
    return ThicknessResolution{settings.thickness, minimum / scale, maximum / scale,
                               pixels / scale,     pixels,          adjustment};
}
std::optional<int> thickness_pixels(const Settings &settings, Rect monitor, unsigned dpi,
                                    double minimum_dips) {
    const auto resolved = resolve_thickness(settings, monitor, dpi, minimum_dips);
    return resolved ? std::optional(resolved->pixels) : std::nullopt;
}
std::optional<Rect> edge_rectangle(Rect available, Edge edge, int thickness) {
    if (!valid_rect(available) || thickness <= 0 || edge == Edge::automatic ||
        edge > Edge::bottom ||
        thickness > (horizontal(edge) ? available.bottom - available.top
                                      : available.right - available.left)) {
        return std::nullopt;
    }
    switch (edge) {
    case Edge::left:
        available.right = available.left + thickness;
        break;
    case Edge::top:
        available.bottom = available.top + thickness;
        break;
    case Edge::right:
        available.left = available.right - thickness;
        break;
    case Edge::bottom:
        available.top = available.bottom - thickness;
        break;
    default:
        return std::nullopt;
    }
    return available;
}
AppBar::~AppBar() {
    remove();
}
std::optional<Rect> AppBar::position(Rect monitor, Edge edge, int thickness,
                                     const ThicknessForEdge &thickness_for_edge) {
    if (negotiating_) {
        return final_;
    }
    const auto proposed = edge_rectangle(monitor, edge, thickness);
    if (!proposed) {
        return std::nullopt;
    }
    if (!registered_) {
        registered_ = shell_.add();
        if (!registered_) {
            return std::nullopt;
        }
    }
    negotiating_ = true;
    // Calls may synchronously notify the owner. No recursive Shell negotiation is permitted.
    try {
        const auto queried = shell_.query(*proposed, edge);
        // Query geometry excludes its provisional thickness: the Shell may consume that
        // dimension, but its edge anchor and longitudinal span describe available space.
        const auto same_query_geometry = [edge](Rect a, Rect b) {
            switch (edge) {
            case Edge::left:
                return a.left == b.left && a.top == b.top && a.bottom == b.bottom;
            case Edge::right:
                return a.right == b.right && a.top == b.top && a.bottom == b.bottom;
            case Edge::top:
                return a.top == b.top && a.left == b.left && a.right == b.right;
            case Edge::bottom:
                return a.bottom == b.bottom && a.left == b.left && a.right == b.right;
            default:
                return false;
            }
        };
        const bool unchanged_query = last_monitor_ == monitor && final_edge_ == edge &&
                                     last_query_ && same_query_geometry(*last_query_, queried);
        if (thickness_for_edge) {
            // A SETPOS-only adjustment is a settled constraint until QUERY changes. This
            // also handles our own deferred position notifications without shrink/grow loops.
            const auto readable = thickness_for_edge(unchanged_query && final_ ? *final_ : queried);
            if (!readable || *readable <= 0) {
                negotiating_ = false;
                return std::nullopt;
            }
            thickness = *readable;
        }
        // QUERYPOS may consume the original thickness; restore it against the returned edge.
        Rect restored = queried;
        const auto safe = [](std::int64_t value) {
            return value >= std::numeric_limits<int>::min() &&
                   value <= std::numeric_limits<int>::max();
        };
        std::int64_t boundary{};
        switch (edge) {
        case Edge::left:
            boundary = static_cast<std::int64_t>(queried.left) + thickness;
            break;
        case Edge::top:
            boundary = static_cast<std::int64_t>(queried.top) + thickness;
            break;
        case Edge::right:
            boundary = static_cast<std::int64_t>(queried.right) - thickness;
            break;
        case Edge::bottom:
            boundary = static_cast<std::int64_t>(queried.bottom) - thickness;
            break;
        default:
            negotiating_ = false;
            return std::nullopt;
        }
        if (!safe(boundary)) {
            negotiating_ = false;
            return std::nullopt;
        }
        switch (edge) {
        case Edge::left:
            restored.right = static_cast<int>(boundary);
            break;
        case Edge::top:
            restored.bottom = static_cast<int>(boundary);
            break;
        case Edge::right:
            restored.left = static_cast<int>(boundary);
            break;
        case Edge::bottom:
            restored.top = static_cast<int>(boundary);
            break;
        default:
            break;
        }
        if (!valid_rect(restored) || restored.left < monitor.left || restored.top < monitor.top ||
            restored.right > monitor.right || restored.bottom > monitor.bottom) {
            negotiating_ = false;
            return std::nullopt;
        }
        if (unchanged_query && final_ && (final_ == restored || last_submission_ == restored)) {
            negotiating_ = false;
            return final_;
        }
        const auto result = shell_.set(restored, edge);
        negotiating_ = false;
        if (!valid_rect(result) || result.left < monitor.left || result.top < monitor.top ||
            result.right > monitor.right || result.bottom > monitor.bottom) {
            return std::nullopt;
        }
        final_ = result;
        last_monitor_ = monitor;
        last_query_ = queried;
        last_submission_ = restored;
        final_edge_ = edge;
        return result;
    } catch (...) {
        negotiating_ = false;
        throw;
    }
}
void AppBar::remove() noexcept {
    if (registered_) {
        registered_ = false;
        shell_.remove();
    }
    final_.reset();
    invalidate_position();
    final_edge_ = Edge::automatic;
}
void AppBar::invalidate_position() noexcept {
    last_query_.reset();
    last_submission_.reset();
    last_monitor_.reset();
}
void AppBar::shell_restarted() noexcept {
    registered_ = false;
    final_.reset();
    invalidate_position();
    final_edge_ = Edge::automatic;
}
} // namespace loadbar
