#include "model/layout.hpp"
#include "model/presentation.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <map>
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
    std::vector<std::size_t> members;
    std::optional<unsigned> efficiency;
};
struct GridCell {
    CoreBox core;
    int column{}, columns{};
};
struct CpuGrid {
    std::vector<GridCell> cells;
    float band{22}, minimum_width{48};
};
CpuGrid cpu_grid(const std::vector<Processor> &processors, float width) {
    CpuGrid result;
    std::map<unsigned, Core> grouped;
    std::set<unsigned> classes;
    for (std::size_t i = 0; i < processors.size(); ++i) {
        auto &c = grouped[processors[i].core];
        c.key = processors[i].core;
        c.members.push_back(i);
        c.efficiency = processors[i].efficiency_class;
        if (c.efficiency) {
            classes.insert(*c.efficiency);
        }
    }
    std::vector<Core> cores;
    for (auto &[key, core] : grouped) {
        (void)key;
        cores.push_back(std::move(core));
    }
    const bool hybrid = classes.size() > 1;
    if (hybrid) {
        std::ranges::stable_sort(
            cores, [](const Core &a, const Core &b) { return a.efficiency > b.efficiency; });
    }
    const unsigned highest = classes.empty() ? 0 : *classes.rbegin();
    const auto p_count = std::ranges::count_if(
        cores, [&](const Core &c) { return c.efficiency && *c.efficiency == highest; });
    const int columns = std::max(
        1, std::min(hybrid ? static_cast<int>(p_count) : static_cast<int>((cores.size() + 2) / 3),
                    static_cast<int>((width + 2) / (hybrid ? 16.0F : 8.0F))));
    result.minimum_width =
        std::max(48.0F, static_cast<float>(columns) * (hybrid ? 16.0F : 8.0F) - 2);
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
                                 processors[core.members.front()].mapped,
                                 processors[core.members.front()].id,
                                 core.members},
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
                                       std::size_t disk_count) {
    if (!valid_inputs(width, height, processors, text_scale, disk_count)) {
        return std::nullopt;
    }
    const float w = width / text_scale, h = height / text_scale;
    const auto count = static_cast<int>(std::max(std::size_t{1}, disk_count)) + 4;
    for (int cols = horizontal ? count : 1; cols >= 1; --cols) {
        const float overhead =
            22 + static_cast<float>(cols - 1) * 12 + (cols == count ? 13.0F : 0.0F);
        const float g = (w - overhead) / static_cast<float>(cols) - 21;
        if (g < 48) {
            continue;
        }
        auto cpu = cpu_grid(processors, g);
        const int rows = (count + cols - 1) / cols;
        const float used_h =
            static_cast<float>(rows) * cpu.band + static_cast<float>(rows - 1) * 12 + 18;
        if (used_h <= h) {
            const float minimum_w = overhead + static_cast<float>(cols) * (21 + cpu.minimum_width);
            return LayoutPlan{std::move(cpu), cols, count, minimum_w, used_h};
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
                                             float text_scale, std::size_t disk_count) {
    if (!std::isfinite(maximum) || maximum < 40 || maximum > 100000) {
        return std::nullopt;
    }
    const auto plan_at = [&](int thickness) {
        const auto t = static_cast<float>(thickness);
        return compact_plan(horizontal ? length : t, horizontal ? t : length, horizontal,
                            processors, text_scale, disk_count);
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
Layout arrange(const LayoutPlan &plan, float width, float height, bool horizontal, float scale,
               Alignment alignment) {
    Layout l;
    l.content_scale = scale;
    const float w = width / scale, h = height / scale;
    const int cols = plan.columns, count = plan.count;
    const bool single_row = cols == count;
    const float g = (w - 22 - static_cast<float>(cols - 1) * 12 - (single_row ? 13.0F : 0.0F)) /
                        static_cast<float>(cols) -
                    21;
    // Allow only floating-point rounding at an analytically calculated fit boundary.
    if (g + 0.0001F < plan.cpu.minimum_width || h + 0.0001F < plan.minimum_height) {
        return l;
    }
    const float band = plan.cpu.band;
    l.blocks.resize(static_cast<std::size_t>(count));
    const float top = horizontal ? (h - plan.minimum_height) / 2 + 9
                                 : 9 + alignment_offset(h - plan.minimum_height, alignment);
    for (int i = 0; i < count; ++i) {
        auto &b = l.blocks[static_cast<std::size_t>(i)];
        b.kind = i < 3 ? static_cast<unsigned>(i) : i == count - 1 ? 4U : 3U;
        b.disk = i >= 3 ? static_cast<std::size_t>(i - 3) : 0;
        const int row = i / cols, col = i % cols;
        const int row_count = std::min(cols, count - row * cols);
        const float row_free = static_cast<float>(cols - row_count) * (g + 33);
        const float x = 11 + static_cast<float>(col) * (g + 33) +
                        (single_row && i > 0 ? 13.0F : 0.0F) +
                        alignment_offset(row_free, alignment);
        place_block(b, x, top + static_cast<float>(row) * (band + 12), g, band);
    }
    l.cores.reserve(plan.cpu.cells.size());
    for (const auto &cell : plan.cpu.cells) {
        auto core = cell.core;
        const float cw =
            (g - 2 * static_cast<float>(cell.columns - 1)) / static_cast<float>(cell.columns);
        core.label.x = static_cast<float>(cell.column) * (cw + 2);
        core.label.width = cw;
        move(core.label, l.blocks[0].graphic.x, l.blocks[0].graphic.y);
        scale_box(core.label, scale);
        l.cores.push_back(std::move(core));
    }
    if (single_row) {
        l.divider = {l.blocks[0].bounds.x + g + 21 + 12, top + (band - 20) / 2, 1, 20};
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
Layout make_layout(float width, float height, bool horizontal,
                   const std::vector<Processor> &processors, float text_scale,
                   const Settings &settings, std::size_t disk_count) {
    if (!valid_inputs(width, height, processors, text_scale, disk_count)) {
        return {};
    }
    const float thickness = horizontal ? height : width;
    const auto compact = compact_minimum(horizontal ? width : height, std::ceil(thickness),
                                         horizontal, processors, text_scale, disk_count);
    if (!compact) {
        return {};
    }
    // Freeze both widget and CPU rows at the compact minimum. Enlarging contents must
    // neither wrap them nor increase the Shell reservation required for readability.
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
    auto result = arrange(plan, width, height, horizontal, scale, settings.alignment);
    if (!result.fits) {
        result = arrange(plan, width, height, horizontal, text_scale, settings.alignment);
    }
    return result;
}
double minimum_thickness(float length, float maximum, bool horizontal,
                         const std::vector<Processor> &processors, float scale,
                         const Settings &settings, std::size_t disk_count) {
    (void)settings; // Alignment does not affect compact feasibility.
    if (!std::isfinite(maximum) || maximum < 1 || maximum > 10000) {
        return maximum + 1;
    }
    const auto compact =
        compact_minimum(length, maximum, horizontal, processors, scale, disk_count);
    return compact ? compact->thickness : static_cast<double>(maximum) + 1;
}
std::wstring core_label(const CoreBox &c) {
    return c.mapped ? std::format(L"Core {}", c.core)
                    : std::format(L"G{}:{}", c.logical.group, c.logical.number);
}
MetricView core_metric(const CoreBox &core, const Snapshot &snapshot, Clock::time_point now,
                       unsigned interval_ms) {
    MetricView result;
    result.timestamp = now;
    result.detail = L"Core data absent";
    for (auto index : core.processors) {
        if (index >= snapshot.processors.size()) {
            return result;
        }
        auto m =
            presented_metric(aged_metric(snapshot.processors[index].utilization, now, interval_ms));
        if (m.status != Status::valid) {
            return m;
        }
        if (!std::isfinite(m.value) || m.value < 0) {
            m.status = Status::error;
            return m;
        }
        if (result.status != Status::valid || m.value > result.value) {
            result = m;
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
        auto result = core_label(core) + L" — color: busiest logical processor (% Processor Time)";
        for (auto index : core.processors) {
            if (index >= s.processors.size()) {
                continue;
            }
            const auto &cpu = s.processors[index];
            const auto m = aged_metric(cpu.utilization, now, settings.interval_ms);
            if (index == core.processors.front()) {
                result += cpu.efficiency_class
                              ? std::format(L"; efficiency class {}", *cpu.efficiency_class)
                              : L"; class unknown";
            }
            result += std::format(L"\nGroup {}, logical processor {}: {} [{}] {}", cpu.id.group,
                                  cpu.id.number, metric_text(m), status_text(m.status), m.detail);
        }
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
        disks += d.label + L"\n";
    }
    return disks + s.gpu_label + L"\n" + s.network_label + L"\nRight-click for Settings and Exit";
}
} // namespace loadbar
