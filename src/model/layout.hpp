#pragma once

#include "model/snapshot.hpp"

namespace loadbar {
struct Box {
    float x{}, y{}, width{}, height{};
};
struct CoreBox {
    Box label;
    unsigned core{};
    bool mapped{};
    ProcessorId logical;
    // One independently colored logical processor; core retains its physical relationship.
    std::size_t processor{};
};
struct Layout {
    bool fits{};
    // Windows text scale multiplied by optional, space-limited widget magnification.
    float content_scale{1};
    float temperature_font_size{9};
    std::vector<CoreBox> cores;
    struct Block {
        Box bounds, icon, graphic, temperature, readout;
        unsigned kind{}; // Icon/family: CPU, RAM, GPU, physical disk, network.
        std::size_t disk{};
        std::array<Gauge, 3> types{Gauge::count, Gauge::count, Gauge::count};
        std::array<Box, 3> meters{};
    };
    std::vector<Block> blocks;
    Box divider;
};
[[nodiscard]] MetricView core_metric(const CoreBox &core, const Snapshot &snapshot,
                                     Clock::time_point now, unsigned interval_ms);
[[nodiscard]] MetricView block_metric(const Layout::Block &block, Gauge gauge,
                                      const Snapshot &snapshot);
[[nodiscard]] const TemperatureReading *block_temperature(const Layout::Block &block,
                                                          const Snapshot &snapshot) noexcept;
[[nodiscard]] std::wstring core_label(const CoreBox &core);
void snap_layout(Layout &layout, float dpi);
[[nodiscard]] float glow_padding_dips(float content_scale, float dpi);
[[nodiscard]] Box expanded_paint_bounds(Box bounds, float content_scale, float dpi);
[[nodiscard]] std::wstring metric_tooltip(const Layout &layout, float x, float y,
                                          const Snapshot &snapshot, const Settings &settings,
                                          Clock::time_point now);
// Geometry requires an actual edge, already resolved against the active monitor.
// Filters explicit exclusions and maps disk blocks back to their snapshot indices.
[[nodiscard]] Layout make_snapshot_layout(float width, float height, Edge edge,
                                          const Snapshot &snapshot, float text_scale,
                                          const Settings &settings);
// Count-based geometry uses the number of displayed disk widgets; zero means none.
[[nodiscard]] Layout make_layout(float width, float height, Edge edge,
                                 const std::vector<Processor> &processors, float text_scale,
                                 const Settings &settings = {}, std::size_t disk_count = 1);
[[nodiscard]] double minimum_thickness(float length, float maximum, Edge edge,
                                       const std::vector<Processor> &processors, float text_scale,
                                       const Settings &settings = {}, std::size_t disk_count = 1);
} // namespace loadbar
