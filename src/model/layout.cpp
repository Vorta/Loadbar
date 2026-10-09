#include "model/layout.hpp"
#include "model/geometry.hpp"
#include "model/presentation.hpp"
#include "model/strip_style.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <set>
namespace loadbar {
namespace {
using namespace strip_style;
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
    b.bounds = {x, y, width + kHeader, band};
    b.icon = {x + (kColumn - kIcon) / 2, y + (band - kIcon) / 2, kIcon, kIcon};
    b.graphic = {x + kHeader, y, width, band};
    if (b.kind == 1) {
        b.types[0] = Gauge::ram;
        b.meters[0] = b.graphic;
    } else if (b.kind == 2 || b.kind == 3) {
        b.types = b.kind == 2 ? std::array{Gauge::gpu_3d, Gauge::gpu_memory, Gauge::gpu_decode}
                              : std::array{Gauge::disk_active, Gauge::disk_read, Gauge::disk_write};
        const float u = (band - 6) / 4;
        b.meters = {{{x + kHeader, y, width, u * 2 + 2},
                     {x + kHeader, y + u * 2 + 4, width, u},
                     {x + kHeader, y + u * 3 + 6, width, u}}};
    } else if (b.kind == 4) {
        b.types = {Gauge::download, Gauge::upload, Gauge::count};
        const float u = (band - 4.0F) / 3.0F;
        b.meters[0] = {x + kHeader, y, width, band - u - 2};
        b.meters[1] = {x + kHeader, y + band - u, width, u};
    }
}
struct LayoutPlan {
    CpuGrid cpu;
    int columns{}, count{};
    std::vector<unsigned> kinds;
    float minimum_width{}, minimum_height{};
    float gap{kWidgetGap};
    float vertical_tail{kVerticalTail};
    // Prefix sums let every wrapped row/column use its actual component widths.
    std::vector<float> readout_offsets;
    bool readouts() const {
        return readout_offsets.back() > 0;
    }
    float readout_span(int first, int end) const {
        return readout_offsets[static_cast<std::size_t>(end)] -
               readout_offsets[static_cast<std::size_t>(first)];
    }
    float readout_extent(int index) const {
        return readout_span(index, index + 1);
    }
    float group_overhead(int first, int end, bool horizontal, int slots) const {
        return 2 * kMargin + static_cast<float>(slots - 1) * gap +
               static_cast<float>(slots) * (horizontal ? kHeader : vertical_tail) +
               readout_span(first, end) +
               (horizontal && first == 0 && end == count ? gap + kDividerWidth : 0);
    }
};
float cpu_readout_width(const std::vector<Processor> &processors) {
    const auto first = processors.empty() ? std::nullopt : processors.front().efficiency_class;
    bool hybrid = false;
    for (const auto &processor : processors) {
        if (!processor.efficiency_class) {
            return kCpuReadoutWidth;
        }
        hybrid |= processor.efficiency_class != first;
    }
    return hybrid ? kHybridReadoutWidth : kCpuReadoutWidth;
}
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
    LayoutPlan plan;
    plan.count = static_cast<int>(kinds.size());
    plan.kinds = std::move(kinds);
    plan.gap = settings.always_show_readout ? kReadoutWidgetGap : kWidgetGap;
    // The former 16-DIP widget gap also supplied temperature clearance. Keep that
    // clearance inside the vertical group when using the tighter readout gap.
    if (settings.always_show_readout) {
        plan.vertical_tail += kWidgetGap - kReadoutWidgetGap;
    }
    plan.readout_offsets.reserve(plan.kinds.size() + 1);
    plan.readout_offsets.push_back(0);
    for (const auto kind : plan.kinds) {
        const float width_for_text =
            kind == 0 ? cpu_readout_width(processors) : device_readout_width(kind);
        plan.readout_offsets.push_back(
            plan.readout_offsets.back() +
            (settings.always_show_readout ? width_for_text + kReadoutGap : 0));
    }
    const int count = plan.count;
    if (!horizontal) {
        // Wrap vertical widgets into columns when the edge cannot hold their readouts.
        // The same plan feeds minimum-thickness negotiation and final arrangement.
        for (int columns = 1; columns <= (settings.always_show_readout ? count : 1); ++columns) {
            const int rows = (count + columns - 1) / columns;
            const int used_columns = (count + rows - 1) / rows;
            const float available = (w - 18 - static_cast<float>(used_columns - 1) * plan.gap) /
                                    static_cast<float>(used_columns);
            // Upright temperature labels can be 3.1 * 9 compact DIPs wide.
            const float minimum_column = plan.readouts() && used_columns > 1 ? 28.0F : 22.0F;
            if (available < minimum_column) {
                continue;
            }
            const float overhead =
                plan.group_overhead(0, rows, false, rows) + static_cast<float>(rows - 1) * 22;
            const float budget = h - overhead;
            if (budget < 48) {
                continue;
            }
            auto cpu = cpu_grid(processors, budget, settings.cpu_squares);
            if (cpu.band > available || cpu.preferred_width > budget) {
                continue;
            }
            const float minimum_w =
                18 + static_cast<float>(used_columns) * std::max(cpu.band, minimum_column) +
                static_cast<float>(used_columns - 1) * plan.gap;
            float used_h = overhead + std::max(cpu.preferred_width, 22.0F);
            for (int first = rows; first < count; first += rows) {
                const int end = std::min(first + rows, count);
                used_h = std::max(used_h, plan.group_overhead(first, end, false, end - first) +
                                              static_cast<float>(end - first) * 22);
            }
            if (used_h > h + 0.0001F) {
                continue;
            }
            plan.cpu = std::move(cpu);
            plan.columns = used_columns;
            plan.minimum_width = minimum_w;
            plan.minimum_height = used_h;
            return plan;
        }
        return std::nullopt;
    }
    for (int cols = horizontal ? count : 1; cols >= 1; --cols) {
        const float overhead = plan.group_overhead(0, cols, true, cols);
        const float cpu_budget = w - overhead - static_cast<float>(cols - 1) * 48;
        float other_minimum{};
        for (int first = cols; first < count; first += cols) {
            const int end = std::min(first + cols, count);
            const int slots = plan.readouts() ? end - first : cols;
            other_minimum = std::max(other_minimum, plan.group_overhead(first, end, true, slots) +
                                                        static_cast<float>(slots) * 48);
        }
        if (cpu_budget < (processors.empty() ? 48.0F : 6.0F) || other_minimum > w) {
            continue;
        }
        auto cpu = cpu_grid(processors, cpu_budget, settings.cpu_squares);
        if (cpu.minimum_width > cpu_budget) {
            continue;
        }
        const int rows = (count + cols - 1) / cols;
        const float other_band = plan.readouts() ? 22 : cpu.band;
        const float used_h = cpu.band + static_cast<float>(rows - 1) * (other_band + plan.gap) + 18;
        if (used_h <= h) {
            plan.minimum_width = std::max(
                overhead + cpu.preferred_width + static_cast<float>(cols - 1) * 48, other_minimum);
            plan.cpu = std::move(cpu);
            plan.columns = cols;
            plan.minimum_height = used_h;
            return plan;
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
    const int rows = (plan.count + plan.columns - 1) / plan.columns;
    const float column_width = (w - 18 - static_cast<float>(plan.columns - 1) * plan.gap) /
                               static_cast<float>(plan.columns);
    const float usable_height = h - plan.group_overhead(0, rows, false, rows);
    const float cpu_budget = usable_height - static_cast<float>(rows - 1) * 22;
    auto fitted = squares ? fit_square_cpu(processors, cpu_budget, column_width)
                          : FittedCpu{plan.cpu, std::min(column_width / plan.cpu.band,
                                                         cpu_budget / plan.cpu.preferred_width)};
    const float cpu_width = fitted.grid.band * fitted.factor;
    const float cpu_height = fitted.grid.preferred_width * fitted.factor;
    float device_height =
        rows > 1 ? (usable_height - cpu_height) / static_cast<float>(rows - 1) : usable_height;
    for (int first = rows; first < plan.count; first += rows) {
        const int end = std::min(first + rows, plan.count);
        const float other_height = (h - plan.group_overhead(first, end, false, end - first)) /
                                   static_cast<float>(end - first);
        device_height = std::min(device_height, other_height);
    }
    std::size_t disk_index{};
    float y = kMargin;
    for (std::size_t i = 0; i < plan.kinds.size(); ++i) {
        const auto kind = plan.kinds[i];
        const int column = static_cast<int>(i) / rows, row = static_cast<int>(i) % rows;
        auto &b = l.blocks.emplace_back();
        b.kind = kind;
        b.disk = kind == 3 ? disk_index++ : 0;
        const float gw = kind == 0 ? cpu_width : column_width;
        const float gh = kind == 0 ? cpu_height : device_height;
        const float x = 9 + static_cast<float>(column) * (column_width + plan.gap) +
                        alignment_offset(column_width - gw, alignment);
        if (row == 0) {
            y = kMargin;
        }
        const float readout = plan.readout_extent(static_cast<int>(i));
        const float graphic_y = y + (edge == Edge::left ? readout : 0);
        // Transpose only meter geometry, keeping icons upright and below their group.
        place_block(b, 0, 0, gh, gw);
        for (auto &m : b.meters) {
            if (m.width > 0 && m.height > 0) {
                m = {x + m.y, graphic_y, m.height, gh};
            }
        }
        b.graphic = {x, graphic_y, gw, gh};
        if (plan.readouts()) {
            b.readout = {x, edge == Edge::left ? y : y + gh + kReadoutGap, gw,
                         readout - kReadoutGap};
        }
        b.icon = {x + (gw - kIcon) / 2, y + gh + readout + kGraphicGap, kIcon, kIcon};
        b.bounds = {x, y, gw, gh + plan.vertical_tail + readout};
        y += b.bounds.height + plan.gap;
    }
    place_cpu(l, fitted.grid, fitted.grid.preferred_width, fitted.factor, edge, scale);
    for (auto &b : l.blocks) {
        scale_box(b.bounds, scale);
        scale_box(b.icon, scale);
        scale_box(b.graphic, scale);
        scale_box(b.readout, scale);
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
    const float graphics = w - plan.group_overhead(0, cols, true, cols);
    const int rows = (count + cols - 1) / cols;
    const float available_band =
        plan.readouts()
            ? h - 18 - static_cast<float>(rows - 1) * (22 + plan.gap)
            : (h - 18 - static_cast<float>(rows - 1) * plan.gap) / static_cast<float>(rows);
    const float cpu_budget = graphics - static_cast<float>(cols - 1) * 48;
    auto fitted =
        squares ? fit_square_cpu(processors, cpu_budget, available_band) : FittedCpu{plan.cpu};
    const float cpu_width = std::min(fitted.grid.preferred_width * fitted.factor, cpu_budget);
    // All device meters share the remainder. With wrapped rows, also respect the
    // full device-only row; its free space follows the selected content alignment.
    float g = cols == 1 ? graphics : (graphics - cpu_width) / static_cast<float>(cols - 1);
    for (int first = cols; first < count; first += cols) {
        const int end = std::min(first + cols, count);
        const int slots = plan.readouts() ? end - first : cols;
        g = std::min(g, (w - plan.group_overhead(first, end, true, slots)) /
                            static_cast<float>(slots));
    }
    // Allow only floating-point rounding at an analytically calculated fit boundary.
    if (cpu_width + 0.0001F < fitted.grid.minimum_width * fitted.factor || g + 0.0001F < 48 ||
        h + 0.0001F < plan.minimum_height) {
        return l;
    }
    const float band = fitted.grid.band * fitted.factor;
    // Only the CPU-containing row needs the wrapped CPU grid's height. Device-only
    // rows keep their compact band, avoiding unnecessary AppBar thickness at large text scales.
    const float other_band = plan.readouts() ? 22 : band;
    const float used_height = band + static_cast<float>(rows - 1) * (other_band + plan.gap) + 18;
    l.blocks.resize(static_cast<std::size_t>(count));
    const float top = (h - used_height) / 2 + 9;
    std::size_t disk_index{};
    float x{};
    for (int i = 0; i < count; ++i) {
        auto &b = l.blocks[static_cast<std::size_t>(i)];
        b.kind = plan.kinds[static_cast<std::size_t>(i)];
        b.disk = b.kind == 3 ? disk_index++ : 0;
        const int row = i / cols, col = i % cols;
        const int row_count = std::min(cols, count - row * cols);
        if (col == 0) {
            const float row_width = plan.group_overhead(i, i + row_count, true, row_count) -
                                    2 * kMargin + static_cast<float>(row_count) * g +
                                    (row == 0 ? cpu_width - g : 0);
            x = kMargin + alignment_offset(w - 2 * kMargin - row_width, alignment);
        }
        const float y = row == 0 ? top
                                 : top + band + plan.gap +
                                       static_cast<float>(row - 1) * (other_band + plan.gap);
        place_block(b, x, y, i == 0 ? cpu_width : g, row == 0 ? band : other_band);
        if (plan.readouts()) {
            const float readout = plan.readout_extent(i);
            b.readout = {b.graphic.x + b.graphic.width + kReadoutGap, b.graphic.y,
                         readout - kReadoutGap, b.graphic.height};
            b.bounds.width += readout;
        }
        x += b.bounds.width + plan.gap + (single_row && i == 0 ? plan.gap + kDividerWidth : 0);
    }
    place_cpu(l, fitted.grid, cpu_width / fitted.factor, fitted.factor, edge, scale);
    if (single_row) {
        l.divider = {l.blocks[0].bounds.x + l.blocks[0].bounds.width + plan.gap,
                     top + (band - 20) / 2, kDividerWidth, 20};
    }
    for (auto &b : l.blocks) {
        scale_box(b.bounds, scale);
        scale_box(b.icon, scale);
        scale_box(b.graphic, scale);
        scale_box(b.readout, scale);
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
    result.temperature_font_size =
        strip_style::temperature_font_size(result.content_scale, text_scale);
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
    if (!settings.show_temperatures) {
        return layout;
    }
    for (auto &block : layout.blocks) {
        const auto *reading = block_temperature(block, snapshot);
        if (!reading || !temperature_visible(*reading)) {
            continue;
        }
        // Move into the existing icon/graphic separation; neither meters nor
        // adjacent widgets move when a first temperature arrives. Five glyphs
        // cover every accepted value (-99..250) with room for font side bearings.
        const float scale = layout.content_scale;
        block.icon.y -= kTemperatureShift * scale;
        const float text_width = std::max(kColumn * scale, 3.1F * layout.temperature_font_size);
        float text_height = 14 * layout.temperature_font_size / 11;
        float text_top = block.icon.y + block.icon.height + kTemperatureGap * scale;
        if (horizontal) {
            const float bottom = block.graphic.y + block.graphic.height;
            text_top = std::max(text_top, bottom - text_height);
            text_height = bottom - text_top;
        }
        block.temperature = {block.icon.x + (block.icon.width - text_width) / 2, text_top,
                             text_width, text_height};
        const auto old = block.bounds;
        const float left = std::min(old.x, block.temperature.x);
        const float top = std::min(old.y, block.icon.y);
        block.bounds = {
            left, top,
            std::max(old.x + old.width, block.temperature.x + block.temperature.width) - left,
            std::max(old.y + old.height, block.temperature.y + block.temperature.height) - top};
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
const TemperatureReading *block_temperature(const Layout::Block &block,
                                            const Snapshot &snapshot) noexcept {
    switch (block.kind) {
    case 0:
        return &snapshot.cpu_temperature;
    case 1:
        return &snapshot.ram_temperature;
    case 2:
        return &snapshot.gpu_temperature;
    case 3:
        return block.disk < snapshot.disks.size() ? &snapshot.disks[block.disk].temperature
                                                  : nullptr;
    default:
        return nullptr;
    }
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
        const bool temperature_aligned =
            b.temperature.width > 0 && std::abs(b.temperature.y + b.temperature.height -
                                                b.graphic.y - b.graphic.height) < .001F;
        snap(b.temperature);
        snap(b.bounds);
        snap(b.icon);
        snap(b.graphic);
        snap(b.readout);
        if (temperature_aligned) {
            b.temperature.height = b.graphic.y + b.graphic.height - b.temperature.y;
        }
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
                              cpu.id.number, metric_text(m, ByteFormat::tooltip),
                              status_text(m.status), m.detail);
        if (settings.show_temperatures) {
            result += L"\n" + temperature_details(s.cpu_temperature, now, settings.interval_ms);
        }
        return result;
    }
    for (const auto &b : l.blocks) {
        if (!contains(b.bounds, x, y)) {
            continue;
        }
        auto result = b.kind == 0   ? std::wstring(L"CPU")
                      : b.kind == 1 ? std::wstring(L"Physical memory")
                      : b.kind == 2 ? s.gpu_label
                      : b.kind == 3
                          ? (b.disk < s.disks.size() ? s.disks[b.disk].label
                                                     : (s.disk_discovery.status == Status::error
                                                            ? L"Physical disk discovery"
                                                            : L"No physical disk discovered"))
                          : s.network_label;
        bool byte_values{};
        for (auto g : b.types) {
            if (g == Gauge::count) {
                break;
            }
            const auto m = aged_metric(block_metric(b, g, s), now, settings.interval_ms);
            result += std::format(
                L"\n{}: {} [{}] {}", g == Gauge::gpu_memory ? s.gpu_memory_label : gauge_name(g),
                metric_text(m, ByteFormat::tooltip), status_text(m.status), m.detail);
            if (is_rate_gauge(g)) {
                result += L"\n" + peak_text(m, ByteFormat::tooltip);
            }
            if (g == Gauge::ram || g == Gauge::gpu_memory) {
                const auto bytes = presented_metric(
                    aged_metric(g == Gauge::ram ? s.ram_used_bytes : s.gpu_memory_bytes, now,
                                settings.interval_ms));
                const auto capacity = g == Gauge::ram ? s.ram_total_bytes : s.gpu_memory_capacity;
                if (presented_metric(m).status == Status::valid && bytes.status == Status::valid &&
                    capacity > 0 && bytes.value <= static_cast<double>(capacity)) {
                    auto total = bytes;
                    total.value = static_cast<double>(capacity);
                    result += L"\n" + metric_text(bytes, ByteFormat::tooltip) + L" / " +
                              metric_text(total, ByteFormat::tooltip);
                    byte_values = true;
                }
            }
            byte_values = byte_values || is_rate_gauge(g);
        }
        if (byte_values) {
            result += L"\nMB/GB: Windows-style binary units (1024-based)";
            if (settings.always_show_readout) {
                result += L"\nReadout K/M/G/T/P/E: binary byte units; rates are per second";
            }
        }
        if (settings.show_temperatures) {
            if (const auto *temperature = block_temperature(b, s)) {
                result += L"\n" + temperature_details(*temperature, now, settings.interval_ms);
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
