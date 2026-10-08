#include "model/placement.hpp"

#include <algorithm>

namespace loadbar {
std::optional<Rect> place_readable_bar(AppBar &bar, const Monitor &monitor,
                                       const Settings &settings,
                                       const std::vector<Processor> &processors, float text_scale,
                                       std::size_t disk_count) {
    if (!valid_rect(monitor.bounds) || monitor.dpi < 48 || monitor.dpi > 960) {
        return std::nullopt;
    }
    const auto edge = resolve_edge(settings.edge, monitor.bounds);
    const bool along_x = horizontal(edge);
    const float dpi_scale = static_cast<float>(monitor.dpi) / 96;
    const auto length = [along_x, dpi_scale](Rect bounds) {
        return static_cast<float>(along_x ? static_cast<std::int64_t>(bounds.right) - bounds.left
                                          : static_cast<std::int64_t>(bounds.bottom) - bounds.top) /
               dpi_scale;
    };
    const auto maximum = static_cast<float>(along_x ? monitor.bounds.bottom - monitor.bounds.top
                                                    : monitor.bounds.right - monitor.bounds.left) /
                         dpi_scale * 0.25F;
    auto minimum = minimum_thickness(length(monitor.bounds), maximum, edge, processors, text_scale,
                                     settings, disk_count);
    int previous_pixels{};
    // The Shell can shorten the edge at either QUERYPOS or SETPOS, including during a
    // retry. Raise only the temporary readable minimum; never rewrite the user's request.
    for (int attempt = 0; attempt < 4; ++attempt) {
        const auto thickness = resolve_thickness(settings, monitor.bounds, monitor.dpi, minimum);
        if (!thickness || thickness->pixels <= previous_pixels) {
            return std::nullopt;
        }
        int proposed_pixels = thickness->pixels;
        const auto rectangle = bar.position(
            monitor.bounds, edge, proposed_pixels, [&](Rect queried) -> std::optional<int> {
                // Recompute before SETPOS as well, so an unchanged shortened edge does not
                // repeatedly shrink and grow its reservation on ABN_POSCHANGED.
                minimum =
                    std::max(minimum, minimum_thickness(length(queried), maximum, edge, processors,
                                                        text_scale, settings, disk_count));
                const auto adjusted =
                    resolve_thickness(settings, monitor.bounds, monitor.dpi, minimum);
                if (!adjusted) {
                    return std::nullopt;
                }
                proposed_pixels = adjusted->pixels;
                return proposed_pixels;
            });
        if (!rectangle) {
            return std::nullopt;
        }
        if (make_layout(static_cast<float>(rectangle->right - rectangle->left) / dpi_scale,
                        static_cast<float>(rectangle->bottom - rectangle->top) / dpi_scale, edge,
                        processors, text_scale, settings, disk_count)
                .fits) {
            return rectangle;
        }
        previous_pixels = proposed_pixels;
        minimum = std::max(minimum, minimum_thickness(length(*rectangle), maximum, edge, processors,
                                                      text_scale, settings, disk_count));
    }
    return std::nullopt;
}
} // namespace loadbar
