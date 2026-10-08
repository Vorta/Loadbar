#include "model/layout.hpp"
#include "model/geometry.hpp"
#include "model/presentation.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <set>
namespace loadbar {
namespace {
bool contains(Box b, float x, float y) {
    return x >= b.x && y >= b.y && x < b.x + b.width && y < b.y + b.height;
}
float alignment_offset(float free, Alignment a) {
    return std::max(0.0F, free) * (a == Alignment::center ? 0.5F
                                   : a == Alignment::end  ? 1.0F
                                                          : 0.0F);
}
struct Core {
    unsigned key{};
    std::size_t processor{};
    std::optional<unsigned> efficiency;
};
struct GridCell {
    CoreBox core;
    int column{}, columns{};
};
struct CpuGrid {
    std::vector<GridCell> cells;
    float band{22}, minimum_width{48}, preferred_width{48};
    bool uniform{};
};
CpuGrid cpu_grid(const std::vector<Processor> &processors, float width, bool squares) {
    CpuGrid result;
    std::vector<Core> cores;
    cores.reserve(processors.size());
    std::set<unsigned> classes;
    for (std::size_t i = 0; i < processors.size(); ++i) {
        const auto &c = cores.emplace_back(processors[i].core, i, processors[i].efficiency_class);
        if (c.efficiency) {
            classes.insert(*c.efficiency);
        }
    }
    const bool hybrid = classes.size() > 1;
    std::ranges::sort(cores, [&](const Core &a, const Core &b) {
        if (hybrid && a.efficiency != b.efficiency) {
            return a.efficiency > b.efficiency;
        }
        return a.key != b.key ? a.key < b.key
                              : processors[a.processor].id < processors[b.processor].id;
    });
    if (!hybrid) {
        result.uniform = true;
        const int columns = std::max(
            1, std::min(static_cast<int>(cores.size()), static_cast<int>((width + 2) / 8)));
        const int rows = (static_cast<int>(cores.size()) + columns - 1) / columns;
        result.band = std::max(22.0F, static_cast<float>(rows) * 8 - 2);
        result.minimum_width = cores.empty() ? 48.0F : static_cast<float>(columns) * 8 - 2;
        const float side = std::min((width + 2) / static_cast<float>(columns) - 2,
                                    (result.band + 2) / static_cast<float>(std::max(1, rows)) - 2);
        result.preferred_width =
            cores.empty() ? 48.0F : std::min(width, static_cast<float>(columns) * (side + 2) - 2);
        for (std::size_t i = 0; i < cores.size(); ++i) {
            const auto &core = cores[i];
            const auto row = static_cast<int>(i) / columns;
            result.cells.push_back({{{0, static_cast<float>(row) * (side + 2), side, side},
                                     core.key,
                                     processors[core.processor].mapped,
                                     processors[core.processor].id,
                                     core.processor},
                                    static_cast<int>(i) % columns,
                                    columns});
        }
        return result;
    }
    const unsigned highest = classes.empty() ? 0 : *classes.rbegin();
    const auto p_count = std::ranges::count_if(
        cores, [&](const Core &c) { return c.efficiency && *c.efficiency == highest; });
    const int columns =
        std::max(1, std::min(static_cast<int>(p_count), static_cast<int>((width + 2) / 16.0F)));
    result.minimum_width = static_cast<float>(columns) * 16.0F - 2;
    // Preserve the 2:1 class width relationship without stretching a small CPU over
    // a device-sized allocation. At the reference scale P/E tiles are 22/10 DIPs wide.
    result.preferred_width =
        squares ? result.minimum_width : std::min(width, static_cast<float>(columns) * 24 - 2);
    int column{};
    float y{}, row_height{};
    std::optional<unsigned> previous;
    for (const auto &core : cores) {
        const bool performance = hybrid && core.efficiency && *core.efficiency == highest;
        const int cols = hybrid && !performance ? columns * 2 : columns;
        const float ch = performance ? 14.0F : 6.0F;
        if (column > 0 && (column >= cols || (hybrid && core.efficiency != previous))) {
            y += row_height + 2;
            column = 0;
            row_height = 0;
        }
        result.cells.push_back({{{0, y, 0, ch},
                                 core.key,
                                 processors[core.processor].mapped,
                                 processors[core.processor].id,
                                 core.processor},
                                column,
                                cols});
        row_height = std::max(row_height, ch);
        ++column;
        previous = core.efficiency;
    }
    result.band = std::max(22.0F, y + row_height);
    return result;
}
void move(Box &b, float x, float y) {
    b.x += x;
    b.y += y;
}
void scale_box(Box &b, float s) {
    b.x *= s;
    b.y *= s;
    b.width *= s;
    b.height *= s;
}
void place_block(Layout::Block &b, float x, float y, float width, float band) {
    b.bounds = {x, y, width + 21, band};
    b.icon = {x, y + (band - 14) / 2, 14, 14};
    b.graphic = {x + 21, y, width, band};
    if (b.kind == 1) {
        b.types[0] = Gauge::ram;
        b.meters[0] = b.graphic;
    } else if (b.kind == 2 || b.kind == 3) {
        b.types = b.kind == 2 ? std::array{Gauge::gpu_3d, Gauge::gpu_memory, Gauge::gpu_decode}
                              : std::array{Gauge::disk_active, Gauge::disk_read, Gauge::disk_write};
        const float u = (band - 6) / 4;
        b.meters = {{{x + 21, y, width, u * 2 + 2},
                     {x + 21, y + u * 2 + 4, width, u},
                     {x + 21, y + u * 3 + 6, width, u}}};
    } else if (b.kind == 4) {
        b.types = {Gauge::download, Gauge::upload, Gauge::count};
        const float u = (band - 4.0F) / 3.0F;
        b.meters[0] = {x + 21, y, width, band - u - 2};
        b.meters[1] = {x + 21, y + band - u, width, u};
    }
}
struct LayoutPlan {
    CpuGrid cpu;
    int columns{}, count{};
    std::vector<unsigned> kinds;
    float minimum_width{}, minimum_height{};
};
bool valid_inputs(float width, float height, const std::vector<Processor> &processors,
                  float text_scale, std::size_t disk_count) {
    return std::isfinite(width) && std::isfinite(height) && width >= 1 && height >= 1 &&
           width <= 100000 && height <= 100000 && std::isfinite(text_scale) && text_scale >= 1 &&
           text_scale <= 2.25F && processors.size() <= 65536 && disk_count <= 1024;
}
std::optional<LayoutPlan> compact_plan(float width, float height, bool horizontal,
                                       const std::vector<Processor> &processors, float text_scale,
                                       std::size_t disk_count, const Settings &settings) {
    if (!valid_inputs(width, height, processors, text_scale, disk_count)) {
        return std::nullopt;
    }
    const float w = width / text_scale, h = height / text_scale;
    std::vector<unsigned> kinds{0, 1};
    if (settings.gpu_visible) {
        kinds.push_back(2);
    }
    kinds.insert(kinds.end(), disk_count, 3);
    if (settings.network_visible) {
        kinds.push_back(4);
    }
    const auto count = static_cast<int>(kinds.size());
    if (!horizontal) {
        const float available = w - 18;
        if (available < 22) {
            return std::nullopt;
        }
        const float overhead =
            22 + static_cast<float>(count) * 21 + static_cast<float>(count - 1) * (22 + 12);
        const float budget = h - overhead;
        if (budget < 48) {
            return std::nullopt;
        }
        auto cpu = cpu_grid(processors, budget, settings.cpu_squares);
        if (cpu.band > available || cpu.preferred_width > budget) {
            return std::nullopt;
        }
        const float minimum_w = 18 + cpu.band;
        const float used_h = overhead + cpu.preferred_width;
        return LayoutPlan{std::move(cpu), 1, count, std::move(kinds), minimum_w, used_h};
    }
    for (int cols = horizontal ? count : 1; cols >= 1; --cols) {
        const float overhead =
            22 + static_cast<float>(cols - 1) * 12 + (cols == count ? 13.0F : 0.0F);
        const float graphics = w - overhead - static_cast<float>(cols) * 21;
        const float cpu_budget = graphics - static_cast<float>(cols - 1) * 48;
        if (cpu_budget < (processors.empty() ? 48.0F : 6.0F) ||
            (cols < count && graphics < static_cast<float>(cols) * 48)) {
            continue;
        }
        auto cpu = cpu_grid(processors, cpu_budget, settings.cpu_squares);
        if (cpu.minimum_width > cpu_budget) {
            continue;
        }
        const int rows = (count + cols - 1) / cols;
        const float used_h =
            static_cast<float>(rows) * cpu.band + static_cast<float>(rows - 1) * 12 + 18;
        if (used_h <= h) {
            const float minimum_graphics =
                std::max(cpu.preferred_width + static_cast<float>(cols - 1) * 48,
                         cols < count ? static_cast<float>(cols) * 48 : 0.0F);
            const float minimum_w = overhead + static_cast<float>(cols) * 21 + minimum_graphics;
            return LayoutPlan{std::move(cpu), cols, count, std::move(kinds), minimum_w, used_h};
        }
    }
    return std::nullopt;
}
struct CompactLayout {
    float thickness{};
    LayoutPlan plan;
};
std::optional<CompactLayout> compact_minimum(float length, float maximum, bool horizontal,
                                             const std::vector<Processor> &processors,
                                             float text_scale, std::size_t disk_count,
                                             const Settings &settings) {
    if (!std::isfinite(maximum) || maximum < 40 || maximum > 100000) {
        return std::nullopt;
    }
    const auto plan_at = [&](int thickness) {
        const auto t = static_cast<float>(thickness);
        return compact_plan(horizontal ? length : t, horizontal ? t : length, horizontal,
                            processors, text_scale, disk_count, settings);
    };
    int low = 40, high = static_cast<int>(std::floor(maximum));
    if (!plan_at(high)) {
        return std::nullopt;
    }
    // Compact feasibility is monotonic: more thickness only adds usable space.
    while (low < high) {
        const int middle = low + (high - low) / 2;
        if (plan_at(middle)) {
            high = middle;
        } else {
            low = middle + 1;
        }
    }
    auto plan = plan_at(low);
    if (!plan) {
        return std::nullopt;
    }
    return CompactLayout{static_cast<float>(low), std::move(*plan)};
}
struct FittedCpu {
    CpuGrid grid;
    float factor{1};
};
FittedCpu fill_uniform_grid(FittedCpu fitted, float length, float thickness) {
    auto &grid = fitted.grid;
    if (!grid.uniform || grid.cells.empty()) {
        return fitted;
    }
    const auto count = static_cast<int>(grid.cells.size());
    const float gap = 2 * fitted.factor;
    const int maximum_rows =
        std::min(count, static_cast<int>((thickness + gap) / (6 * fitted.factor + gap)));
    // The uniform baseline reserves at least 22 DIPs even when its tiles occupy less.
    // Try complete row arrangements so explicit square mode fills the actual band.
    for (int rows = 1; rows <= maximum_rows; ++rows) {
        const int columns = (count + rows - 1) / rows;
        if ((count + columns - 1) / columns != rows) {
            continue;
        }
        const float side =
            (thickness - static_cast<float>(rows - 1) * gap) / static_cast<float>(rows);
        const float extent = static_cast<float>(columns) * (side + gap) - gap;
        if (extent > length) {
            continue;
        }
        const float reference_side = side / fitted.factor;
        grid.band = thickness / fitted.factor;
        grid.preferred_width = extent / fitted.factor;
        grid.minimum_width = static_cast<float>(columns) * 8 - 2;
        for (std::size_t i = 0; i < grid.cells.size(); ++i) {
            auto &cell = grid.cells[i];
            cell.column = static_cast<int>(i) % columns;
            cell.columns = columns;
            const int row = static_cast<int>(i) / columns;
            cell.core.label = {0, static_cast<float>(row) * (reference_side + 2), reference_side,
                               reference_side};
        }
        return fitted;
    }
    return fitted;
}
FittedCpu fit_square_cpu(const std::vector<Processor> &processors, float length, float thickness) {
    FittedCpu result{cpu_grid(processors, length, true)};
    if (processors.empty()) {
        return result;
    }
    const auto candidate = [&](float factor) {
        return cpu_grid(processors, length / factor, true);
    };
    const auto fits = [&](const CpuGrid &grid, float factor) {
        return grid.band * factor <= thickness && grid.preferred_width * factor <= length &&
               grid.minimum_width * factor <= length;
    };
    float low = 1, high = std::max(1.0F, thickness / 22);
    auto grid = candidate(high);
    if (fits(grid, high)) {
        return fill_uniform_grid({std::move(grid), high}, length, thickness);
    }
    // At larger scales the longitudinal budget shrinks, so grid bands can only increase.
    // Bounded bisection fits complete rows; it never shrinks tiles below their readable size.
    for (int attempt = 0; attempt < 20; ++attempt) {
        const float middle = (low + high) / 2;
        grid = candidate(middle);
        if (fits(grid, middle)) {
            low = middle;
            result = {std::move(grid), middle};
        } else {
            high = middle;
        }
    }
    return fill_uniform_grid(std::move(result), length, thickness);
}
void place_cpu(Layout &layout, const CpuGrid &grid, float width, float factor, Edge edge,
               float scale) {
    layout.cores.reserve(grid.cells.size());
    const auto origin = layout.blocks[0].graphic;
    for (const auto &cell : grid.cells) {
        auto core = cell.core;
        float cw =
            (width - 2 * static_cast<float>(cell.columns - 1)) / static_cast<float>(cell.columns);
        if (grid.uniform) {
            cw = std::min(cw, core.label.height);
            const float row = core.label.y / (core.label.height + 2);
            core.label.y = row * (cw + 2);
            core.label.height = cw;
        }
        core.label.x = static_cast<float>(cell.column) * (cw + 2);
        core.label.width = cw;
        const auto b = core.label;
        if (edge == Edge::right) {
            core.label = {grid.band - b.y - b.height, b.x, b.height, b.width};
        } else if (edge == Edge::left) {
            core.label = {b.y, width - b.x - b.width, b.height, b.width};
        }
        scale_box(core.label, factor);
        move(core.label, origin.x, origin.y);
        scale_box(core.label, scale);
        layout.cores.push_back(core);
    }
}
Layout arrange_vertical(const LayoutPlan &plan, float width, float height, float scale,
                        Alignment alignment, Edge edge, const std::vector<Processor> &processors,
                        bool squares) {
    Layout l;
    l.content_scale = scale;
    const float w = width / scale, h = height / scale;
    if (w + 0.0001F < plan.minimum_width || h + 0.0001F < plan.minimum_height) {
        return l;
    }
    const float cpu_budget = h - 22 - static_cast<float>(plan.count) * 21 -
                             static_cast<float>(plan.count - 1) * (22 + 12);
    auto fitted = squares ? fit_square_cpu(processors, cpu_budget, w - 18)
                          : FittedCpu{plan.cpu, std::min((w - 18) / plan.cpu.band,
                                                         cpu_budget / plan.cpu.preferred_width)};
    const float cpu_width = fitted.grid.band * fitted.factor;
    const float cpu_height = fitted.grid.preferred_width * fitted.factor;
    const float device_height = (h - 22 - cpu_height - static_cast<float>(plan.count) * 21 -
                                 static_cast<float>(plan.count - 1) * 12) /
                                static_cast<float>(plan.count - 1);
    float y = 11;
    std::size_t disk_index{};
    for (auto kind : plan.kinds) {
        auto &b = l.blocks.emplace_back();
        b.kind = kind;
        b.disk = kind == 3 ? disk_index++ : 0;
        const float gw = kind == 0 ? cpu_width : w - 18;
        const float gh = kind == 0 ? cpu_height : device_height;
        const float x = 9 + alignment_offset(w - 18 - gw, alignment);
        // Transpose only meter geometry, keeping icons upright and below their group.
        place_block(b, 0, 0, gh, gw);
        for (auto &m : b.meters) {
            if (m.width > 0 && m.height > 0) {
                m = {x + m.y, y, m.height, gh};
            }
        }
        b.graphic = {x, y, gw, gh};
        b.icon = {x + (gw - 14) / 2, y + gh + 7, 14, 14};
        b.bounds = {x, y, gw, gh + 21};
        y += gh + 33;
    }
    place_cpu(l, fitted.grid, fitted.grid.preferred_width, fitted.factor, edge, scale);
    for (auto &b : l.blocks) {
        scale_box(b.bounds, scale);
        scale_box(b.icon, scale);
        scale_box(b.graphic, scale);
        for (auto &m : b.meters) {
            scale_box(m, scale);
        }
    }
    l.fits = true;
    return l;
}
Layout arrange(const LayoutPlan &plan, float width, float height, bool horizontal, float scale,
               Alignment alignment, Edge edge, const std::vector<Processor> &processors,
               bool squares) {
    if (!horizontal) {
        return arrange_vertical(plan, width, height, scale, alignment, edge, processors, squares);
    }
    Layout l;
    l.content_scale = scale;
    const float w = width / scale, h = height / scale;
    const int cols = plan.columns, count = plan.count;
    const bool single_row = cols == count;
    const float graphics = w - 22 - static_cast<float>(cols - 1) * 12 -
                           (single_row ? 13.0F : 0.0F) - static_cast<float>(cols) * 21;
    const int rows = (count + cols - 1) / cols;
    const float available_band =
        (h - 18 - static_cast<float>(rows - 1) * 12) / static_cast<float>(rows);
    const float cpu_budget = graphics - static_cast<float>(cols - 1) * 48;
    auto fitted =
        squares ? fit_square_cpu(processors, cpu_budget, available_band) : FittedCpu{plan.cpu};
    const float cpu_width = std::min(fitted.grid.preferred_width * fitted.factor, cpu_budget);
    // All device meters share the remainder. With wrapped rows, also respect the
    // full device-only row; its free space follows the selected content alignment.
    const float shared_width =
        cols == 1 ? graphics : (graphics - cpu_width) / static_cast<float>(cols - 1);
    const float g =
        single_row ? shared_width : std::min(shared_width, graphics / static_cast<float>(cols));
    // Allow only floating-point rounding at an analytically calculated fit boundary.
    if (cpu_width + 0.0001F < fitted.grid.minimum_width * fitted.factor || g + 0.0001F < 48 ||
        h + 0.0001F < plan.minimum_height) {
        return l;
    }
    const float band = fitted.grid.band * fitted.factor;
    const float used_height =
        static_cast<float>(rows) * band + static_cast<float>(rows - 1) * 12 + 18;
    l.blocks.resize(static_cast<std::size_t>(count));
    const float top = (h - used_height) / 2 + 9;
    std::size_t disk_index{};
    for (int i = 0; i < count; ++i) {
        auto &b = l.blocks[static_cast<std::size_t>(i)];
        b.kind = plan.kinds[static_cast<std::size_t>(i)];
        b.disk = b.kind == 3 ? disk_index++ : 0;
        const int row = i / cols, col = i % cols;
        const int row_count = std::min(cols, count - row * cols);
        const float row_width = static_cast<float>(row_count) * (g + 21) +
                                static_cast<float>(row_count - 1) * 12 +
                                (row == 0 ? cpu_width - g : 0) + (single_row ? 13.0F : 0.0F);
        const float x =
            11 + static_cast<float>(col) * (g + 33) + (row == 0 && col > 0 ? cpu_width - g : 0) +
            (single_row && i > 0 ? 13.0F : 0.0F) + alignment_offset(w - 22 - row_width, alignment);
        place_block(b, x, top + static_cast<float>(row) * (band + 12), i == 0 ? cpu_width : g,
                    band);
    }
    place_cpu(l, fitted.grid, cpu_width / fitted.factor, fitted.factor, edge, scale);
    if (single_row) {
        l.divider = {l.blocks[0].bounds.x + cpu_width + 21 + 12, top + (band - 20) / 2, 1, 20};
    }
    for (auto &b : l.blocks) {
        scale_box(b.bounds, scale);
        scale_box(b.icon, scale);
        scale_box(b.graphic, scale);
        for (auto &m : b.meters) {
            scale_box(m, scale);
        }
    }
    scale_box(l.divider, scale);
    l.fits = true;
    return l;
}
} // namespace
Layout make_layout(float width, float height, Edge edge, const std::vector<Processor> &processors,
                   float text_scale, const Settings &settings, std::size_t disk_count) {
    const bool horizontal = loadbar::horizontal(edge);
    if (edge < Edge::left || edge > Edge::bottom) {
        return {};
    }
    if (!valid_inputs(width, height, processors, text_scale, disk_count)) {
        return {};
    }
    const float thickness = horizontal ? height : width;
    const auto compact = compact_minimum(horizontal ? width : height, std::ceil(thickness),
                                         horizontal, processors, text_scale, disk_count, settings);
    if (!compact) {
        return {};
    }
    // Freeze widget rows and the rectangle-mode CPU grid at the compact minimum.
    // Explicit square mode may refit its grid within the existing readable reservation.
    const auto &plan = compact->plan;
    const double requested = static_cast<double>(text_scale) *
                             std::max(1.0, static_cast<double>(thickness) / compact->thickness);
    const double limit = std::min(static_cast<double>(width) / plan.minimum_width,
                                  static_cast<double>(height) / plan.minimum_height);
    if (limit + 0.000001 < text_scale) {
        return {};
    }
    const double chosen = std::min(requested, limit);
    auto scale = static_cast<float>(chosen);
    if (scale > chosen) {
        scale = std::nextafter(scale, 0.0F);
    }
    auto result = arrange(plan, width, height, horizontal, scale, settings.alignment, edge,
                          processors, settings.cpu_squares);
    if (!result.fits) {
        result = arrange(plan, width, height, horizontal, text_scale, settings.alignment, edge,
                         processors, settings.cpu_squares);
    }
    return result;
}
Layout make_snapshot_layout(float width, float height, Edge edge, const Snapshot &snapshot,
                            float text_scale, const Settings &settings) {
    const bool horizontal = loadbar::horizontal(edge);
    if (edge < Edge::left || edge > Edge::bottom) {
        return {};
    }
    if (snapshot.disks.size() > 1024) {
        return {};
    }
    auto layout = make_layout(width, height, edge, snapshot.processors, text_scale, settings,
                              disk_widget_count(snapshot.disks, settings));
    std::size_t source{};
    for (auto &block : layout.blocks) {
        if (block.kind == 2 && !gpu_memory_visible(snapshot)) {
            block.types = {Gauge::gpu_3d, Gauge::gpu_decode, Gauge::count};
            const auto b = block.graphic;
            const float band = horizontal ? b.height : b.width;
            const float gap = 2 * layout.content_scale;
            const float small = (band - 2 * gap) / 3;
            block.meters = horizontal
                               ? std::array<Box, 3>{{{b.x, b.y, b.width, band - small - gap},
                                                     {b.x, b.y + band - small, b.width, small},
                                                     {}}}
                               : std::array<Box, 3>{{{b.x, b.y, band - small - gap, b.height},
                                                     {b.x + band - small, b.y, small, b.height},
                                                     {}}};
        }
        if (block.kind != 3) {
            continue;
        }
        while (source < snapshot.disks.size() &&
               !disk_visible(snapshot.disks[source].id, settings)) {
            ++source;
        }
        block.disk = source++;
    }
    return layout;
}
double minimum_thickness(float length, float maximum, Edge edge,
                         const std::vector<Processor> &processors, float scale,
                         const Settings &settings, std::size_t disk_count) {
    const bool horizontal = loadbar::horizontal(edge);
    if (edge < Edge::left || edge > Edge::bottom) {
        return maximum + 1;
    }
    if (!std::isfinite(maximum) || maximum < 1 || maximum > 10000) {
        return maximum + 1;
    }
    const auto compact =
        compact_minimum(length, maximum, horizontal, processors, scale, disk_count, settings);
    return compact ? compact->thickness : static_cast<double>(maximum) + 1;
}
std::wstring core_label(const CoreBox &c) {
    return c.mapped ? std::format(L"Core {} · G{} LP{}", c.core, c.logical.group, c.logical.number)
                    : std::format(L"G{}:{}", c.logical.group, c.logical.number);
}
MetricView core_metric(const CoreBox &core, const Snapshot &snapshot, Clock::time_point now,
                       unsigned interval_ms) {
    MetricView result;
    result.timestamp = now;
    result.detail = L"Core data absent";
    if (core.processor < snapshot.processors.size()) {
        result = presented_metric(
            aged_metric(snapshot.processors[core.processor].utilization, now, interval_ms));
        if (result.status == Status::valid && (!std::isfinite(result.value) || result.value < 0)) {
            result.status = Status::error;
        }
    }
    return result;
}
MetricView block_metric(const Layout::Block &block, Gauge gauge, const Snapshot &snapshot) {
    if (block.kind == 3) {
        if (block.disk < snapshot.disks.size()) {
            const auto &disk = snapshot.disks[block.disk];
            switch (gauge) {
            case Gauge::disk_active:
                return disk.active;
            case Gauge::disk_read:
                return disk.read;
            case Gauge::disk_write:
                return disk.write;
            default:
                return {};
            }
        }
        MetricView missing = snapshot.disk_discovery;
        missing.unit = gauge_unit(gauge);
        return missing;
    }
    const auto index = static_cast<std::size_t>(gauge);
    return index < snapshot.gauges.size() ? MetricView(snapshot.gauges[index]) : MetricView{};
}
float glow_padding_dips(float content_scale, float dpi) {
    return std::ceil(12 * content_scale * dpi / 96) * 96 / dpi;
}
Box expanded_paint_bounds(Box bounds, float content_scale, float dpi) {
    // Glow bitmaps round their width/height up independently of their origin.
    const float padding = glow_padding_dips(content_scale, dpi) + 96 / dpi;
    return {bounds.x - padding, bounds.y - padding, bounds.width + 2 * padding,
            bounds.height + 2 * padding};
}
void snap_layout(Layout &layout, float dpi) {
    if (!std::isfinite(dpi) || dpi < 48 || dpi > 960) {
        return;
    }
    const float factor = dpi / 96;
    const auto snap = [&](Box &box) {
        if (box.width <= 0 || box.height <= 0) {
            return;
        }
        box.x = std::round(box.x * factor) / factor;
        box.y = std::round(box.y * factor) / factor;
        box.width = std::max(1.0F, std::round(box.width * factor)) / factor;
        box.height = std::max(1.0F, std::round(box.height * factor)) / factor;
    };
    for (auto &c : layout.cores) {
        snap(c.label);
    }
    for (auto &b : layout.blocks) {
        snap(b.bounds);
        snap(b.icon);
        snap(b.graphic);
        for (auto &m : b.meters) {
            snap(m);
        }
    }
    snap(layout.divider);
}
std::wstring metric_tooltip(const Layout &l, float x, float y, const Snapshot &s,
                            const Settings &settings, Clock::time_point now) {
    for (const auto &core : l.cores) {
        if (!contains(core.label, x, y)) {
            continue;
        }
        auto result = core_label(core) + L" — % Processor Time";
        const auto index = core.processor;
        if (index >= s.processors.size()) {
            continue;
        }
        const auto &cpu = s.processors[index];
        const auto m = aged_metric(cpu.utilization, now, settings.interval_ms);
        result += cpu.efficiency_class
                      ? std::format(L"; efficiency class {}", *cpu.efficiency_class)
                      : L"; class unknown";
        result += std::format(L"\nGroup {}, logical processor {}: {} [{}] {}", cpu.id.group,
                              cpu.id.number, metric_text(m), status_text(m.status), m.detail);
        return result;
    }
    for (const auto &b : l.blocks) {
        if (b.kind == 0 || !contains(b.bounds, x, y)) {
            continue;
        }
        auto result = b.kind == 1   ? std::wstring(L"Physical memory")
                      : b.kind == 2 ? s.gpu_label
                      : b.kind == 3
                          ? (b.disk < s.disks.size() ? s.disks[b.disk].label
                                                     : (s.disk_discovery.status == Status::error
                                                            ? L"Physical disk discovery"
                                                            : L"No physical disk discovered"))
                          : s.network_label;
        for (auto g : b.types) {
            if (g == Gauge::count) {
                break;
            }
            const auto m = aged_metric(block_metric(b, g, s), now, settings.interval_ms);
            result += std::format(L"\n{}: {} [{}] {}",
                                  g == Gauge::gpu_memory ? s.gpu_memory_label : gauge_name(g),
                                  metric_text(m), status_text(m.status), m.detail);
            if (is_rate_gauge(g)) {
                result += L"\n" + peak_text(m);
            }
            if (g == Gauge::ram) {
                result += L"\n" + ram_summary(s, now, settings.interval_ms) +
                          L"; RAM GB = 1,073,741,824 bytes";
                const auto bytes =
                    presented_metric(aged_metric(s.ram_used_bytes, now, settings.interval_ms));
                if (presented_metric(m).status == Status::valid && bytes.status == Status::valid &&
                    s.ram_total_bytes > 0) {
                    result += std::format(L"\n{:.0f} / {} bytes", bytes.value, s.ram_total_bytes);
                }
            }
            if (g == Gauge::gpu_memory) {
                const auto bytes =
                    presented_metric(aged_metric(s.gpu_memory_bytes, now, settings.interval_ms));
                if (presented_metric(m).status == Status::valid && bytes.status == Status::valid &&
                    s.gpu_memory_capacity > 0) {
                    result +=
                        std::format(L"\n{:.0f} / {} bytes", bytes.value, s.gpu_memory_capacity);
                }
            }
        }
        return result;
    }
    std::wstring disks;
    for (const auto &d : s.disks) {
        if (disk_visible(d.id, settings)) {
            disks += d.label + L"\n";
        }
    }
    if (settings.gpu_visible) {
        disks += s.gpu_label + L"\n";
    }
    if (settings.network_visible) {
        disks += s.network_label + L"\n";
    }
    return disks + L"Right-click for Settings and Exit";
}
} // namespace loadbar
