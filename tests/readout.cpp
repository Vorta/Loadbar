#include "model/geometry.hpp"
#include "model/layout.hpp"
#include "model/presentation.hpp"
#include "model/strip_style.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
bool overlaps(loadbar::Box a, loadbar::Box b) {
    return std::min(a.x + a.width, b.x + b.width) > std::max(a.x, b.x) + 0.01F &&
           std::min(a.y + a.height, b.y + b.height) > std::max(a.y, b.y) + 0.01F;
}
bool same_box(loadbar::Box a, loadbar::Box b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
} // namespace
void readout_tests() {
    using namespace loadbar;
    Settings settings;
    require(settings.always_show_readout, "Readout defaults on");
    auto off = settings;
    off.always_show_readout = false;
    require(!collection_changed(settings, off), "Readout is presentation-only");
    Metric rate{0, Status::valid, Unit::bytes_per_second};
    for (const auto &[value, expected] : {std::pair{0.0, L"0"},
                                          {0.01, L"<0.1B"},
                                          {1024.0, L"1.0K"},
                                          {12.4 * 1048576, L"12.4M"},
                                          {56.0 * 1048576, L"56M"},
                                          {1023.99 * 1024, L"1.0M"},
                                          {11.5 * 1073741824, L"11.5G"}}) {
        rate.value = value;
        require(readout_quantity(rate) == expected,
                "Compact quantities use bounded binary precision");
    }
    rate.status = Status::warming_up;
    require(readout_quantity(rate) == L"0", "Warm-up rates show zero");
    rate.status = Status::error;
    require(readout_quantity(rate) == L"—", "Never-observed failures remain unavailable");
    rate.retained = ObservedValue{1048576, {}};
    require(readout_quantity(rate) == L"1.0M", "Readouts retain last-valid observations");
    rate.retained.reset();
    rate.status = Status::valid;
    rate.value = std::numeric_limits<double>::infinity();
    require(readout_quantity(rate) == L"—", "Nonfinite readouts are unavailable");

    Snapshot s;
    for (unsigned i = 0; i < 8; ++i) {
        s.processors.push_back({{0, i},
                                i / 2,
                                true,
                                {i < 4 ? 80.0 : 20.0, Status::valid, Unit::percent},
                                i < 4 ? 1U : 0U});
    }
    auto cpu = component_readout(0, 0, s, settings, {});
    require(cpu.primary.text == L"50%" && cpu.secondary[0].text == L"P80" &&
                cpu.secondary[1].text == L"E20",
            "CPU summary weights logical processors");
    s.processors[0].utilization.status = Status::error;
    require(component_readout(0, 0, s, settings, {}).primary.text == L"—",
            "CPU summary does not average a partial set");
    s.processors[0].utilization.retained = ObservedValue{80, {}};
    require(component_readout(0, 0, s, settings, {}).primary.text == L"50%",
            "Retained CPU observation participates without changing its raw status");
    for (auto &p : s.processors) {
        p.efficiency_class = 0;
    }
    require(component_readout(0, 0, s, settings, {}).secondary[0].text.empty(),
            "Uniform CPUs do not invent P/E classes");
    set_physical_memory(s, 32ULL << 30U, 12ULL << 30U, {});
    auto ram = component_readout(1, 0, s, settings, {});
    require(ram.secondary[0].text == L"20.0/32G", "RAM readout uses coherent capacity");
    s.gauges[static_cast<std::size_t>(Gauge::gpu_decode)] = {14, Status::valid, Unit::percent};
    auto gpu = component_readout(2, 0, s, settings, {});
    require(gpu.secondary[0].text.empty() && gpu.secondary[1].text == L"▶14%",
            "Missing memory omits only the memory segment");
    s.disks.resize(2);
    s.disks[0].id = L"first";
    s.disks[1].id = L"second";
    s.disks[1].read = {56 * 1048576.0, Status::valid, Unit::bytes_per_second};
    s.disks[1].write = {4 * 1048576.0, Status::valid, Unit::bytes_per_second};
    auto disk = component_readout(3, 1, s, settings, {});
    require(disk.secondary[0].text == L"R56M" && disk.secondary[1].text == L"W4.0M",
            "Drive readouts retain direction and physical-device scope");
    for (unsigned mask = 0; mask < 4; ++mask) {
        auto hidden = settings;
        hidden.gpu_visible = (mask & 1U) != 0;
        hidden.network_visible = (mask & 2U) != 0;
        hide_disk(hidden.hidden_disks, L"first");
        const auto layout = make_snapshot_layout(1400, 60, Edge::top, s, 1, hidden);
        require(layout.fits &&
                    layout.blocks.size() == 3 + static_cast<std::size_t>(hidden.gpu_visible) +
                                                static_cast<std::size_t>(hidden.network_visible),
                "Hidden readout families leave no extra widget");
        for (const auto &block : layout.blocks) {
            if (block.kind == 3) {
                require(block.disk == 1 && component_readout(block.kind, block.disk, s, hidden, {})
                                                   .secondary[0]
                                                   .text == L"R56M",
                        "Hidden drives preserve the readout's source index");
            }
        }
    }
    for (unsigned topology = 0; topology < 3; ++topology) {
        const bool hybrid = topology == 1;
        s.processors.clear();
        for (unsigned i = 0; i < (hybrid ? 24U : 8U); ++i) {
            s.processors.push_back({{0, i}, i / 2, true, {}, hybrid && i < 8 ? 1U : 0U});
        }
        if (topology == 2) {
            s.processors[0].efficiency_class.reset();
        }
        for (auto *temperature : {&s.cpu_temperature, &s.ram_temperature, &s.gpu_temperature,
                                  &s.disks[0].temperature, &s.disks[1].temperature}) {
            temperature->metric = {95, Status::valid, Unit::celsius};
        }
        for (const auto edge : {Edge::top, Edge::bottom, Edge::left, Edge::right}) {
            for (const auto length : {560.0F, 800.0F, 1400.0F, 1600.0F, 3840.0F}) {
                for (const auto text_scale : {1.0F, 1.5F, 2.25F}) {
                    for (const bool squares : {false, true}) {
                        settings.cpu_squares = squares;
                        const auto minimum = minimum_thickness(length, 640, edge, s.processors,
                                                               text_scale, settings, 2);
                        require(minimum <= 640,
                                std::format("Readout fit: hybrid {}, edge {}, length {}, text {}, "
                                            "squares {}, minimum {}",
                                            hybrid, static_cast<unsigned>(edge), length, text_scale,
                                            squares, minimum)
                                    .c_str());
                        const auto thickness = static_cast<float>(std::max(60.0, minimum));
                        const auto layout = make_snapshot_layout(
                            horizontal(edge) ? length : thickness,
                            horizontal(edge) ? thickness : length, edge, s, text_scale, settings);
                        require(layout.fits && layout.cores.size() == s.processors.size() &&
                                    layout.blocks.size() == 6,
                                "Reflow preserves all processors and devices");
                        const auto contained = [](Box inner, Box outer) {
                            return inner.x >= outer.x - 0.01F && inner.y >= outer.y - 0.01F &&
                                   inner.x + inner.width <= outer.x + outer.width + 0.01F &&
                                   inner.y + inner.height <= outer.y + outer.height + 0.01F;
                        };
                        const Box client{0, 0, horizontal(edge) ? length : thickness,
                                         horizontal(edge) ? thickness : length};
                        for (const auto &core : layout.cores) {
                            require(
                                contained(core.label, layout.blocks[0].graphic),
                                "Every logical processor remains inside the fitted CPU graphic");
                        }
                        for (std::size_t i = 0; i < layout.blocks.size(); ++i) {
                            const auto &block = layout.blocks[i];
                            const float expected_readout =
                                i != 0   ? strip_style::device_readout_width(block.kind)
                                : hybrid ? strip_style::kHybridReadoutWidth
                                         : strip_style::kCpuReadoutWidth;
                            const float readout_length =
                                horizontal(edge) ? block.readout.width : block.readout.height;
                            require(std::abs(readout_length / layout.content_scale -
                                             expected_readout) < 0.01F,
                                    "Each component reserves its own maximum readout width");
                            if (i > 0) {
                                const auto &ram_graphic = layout.blocks[1].graphic;
                                require(std::abs(horizontal(edge)
                                                     ? block.graphic.width - ram_graphic.width
                                                     : block.graphic.height - ram_graphic.height) <
                                            0.01F,
                                        "Non-CPU graphics share the remaining space equally");
                            }
                            require(contained(block.bounds, client) &&
                                        contained(block.readout, block.bounds),
                                    "Readouts and thermal widget bounds stay inside the bar");
                            require(block.readout.width > 0 && block.readout.height > 0 &&
                                        !overlaps(block.readout, block.graphic) &&
                                        !overlaps(block.readout, block.icon),
                                    "Readout has reserved, disjoint space");
                            for (std::size_t j = i + 1; j < layout.blocks.size(); ++j) {
                                require(
                                    !overlaps(block.bounds, layout.blocks[j].bounds),
                                    std::format("Wrapped readouts overlap: topology {}, edge {}, "
                                                "length {}, text {}, squares {}, blocks {}/{}",
                                                topology, static_cast<unsigned>(edge), length,
                                                text_scale, squares, i, j)
                                        .c_str());
                            }
                        }
                    }
                }
            }
        }
        for (const auto edge : {Edge::top, Edge::bottom, Edge::left, Edge::right}) {
            const float width = horizontal(edge) ? 1400.0F : 60.0F;
            const float height = horizontal(edge) ? 60.0F : 1400.0F;
            for (auto &p : s.processors) {
                p.utilization = {0, Status::valid, Unit::percent};
            }
            const auto idle = make_snapshot_layout(width, height, edge, s, 1, settings);
            for (auto &p : s.processors) {
                p.utilization.value = 100;
            }
            const auto busy = make_snapshot_layout(width, height, edge, s, 1, settings);
            require(idle.fits && busy.fits && idle.blocks.size() == busy.blocks.size(),
                    "Readout spacing fits idle and full utilization");
            for (std::size_t i = 0; i < idle.blocks.size(); ++i) {
                require(same_box(idle.blocks[i].bounds, busy.blocks[i].bounds) &&
                            same_box(idle.blocks[i].readout, busy.blocks[i].readout) &&
                            same_box(idle.blocks[i].graphic, busy.blocks[i].graphic),
                        "Percentage digit changes never reflow the widgets");
            }
            if (horizontal(edge)) {
                const auto &cpu_box = busy.blocks[0].readout;
                require(std::abs(busy.divider.x - cpu_box.x - cpu_box.width -
                                 strip_style::kReadoutWidgetGap * busy.content_scale) < 0.01F,
                        "Divider follows the compact CPU column with normal padding");
            }
            const auto disabled = make_snapshot_layout(width, height, edge, s, 1, off);
            require(disabled.fits && std::ranges::all_of(disabled.blocks,
                                                         [](const auto &block) {
                                                             return block.readout.width == 0 &&
                                                                    block.readout.height == 0;
                                                         }),
                    "Readout disabled reserves no numeric columns");
        }
    }
    for (unsigned mask = 0; mask < 4; ++mask) {
        settings.gpu_visible = (mask & 1U) != 0;
        settings.network_visible = (mask & 2U) != 0;
        for (const std::size_t disks : {0U, 1U, 4U}) {
            s.disks.resize(std::max<std::size_t>(1, disks));
            settings.hidden_disks.clear();
            for (std::size_t i = 0; i < s.disks.size(); ++i) {
                s.disks[i].id = std::format(L"drive-{}", i);
            }
            if (disks == 0) {
                hide_disk(settings.hidden_disks, s.disks[0].id);
            }
            for (const auto edge : {Edge::top, Edge::bottom, Edge::left, Edge::right}) {
                for (const auto alignment : {Alignment::start, Alignment::center, Alignment::end}) {
                    settings.alignment = alignment;
                    const auto minimum =
                        minimum_thickness(560, 640, edge, s.processors, 1.5F, settings, disks);
                    require(minimum <= 640, "Mixed-width hidden-device layout has a readable fit");
                    const auto thickness = static_cast<float>(std::max(60.0, minimum));
                    const auto layout = make_snapshot_layout(horizontal(edge) ? 560 : thickness,
                                                             horizontal(edge) ? thickness : 560,
                                                             edge, s, 1.5F, settings);
                    require(layout.fits &&
                                layout.blocks.size() ==
                                    2 + disks + static_cast<std::size_t>(settings.gpu_visible) +
                                        static_cast<std::size_t>(settings.network_visible),
                            std::format("Mixed fit: mask {}, disks {}, edge {}, alignment {}, "
                                        "thickness {}, fits {}, blocks {}",
                                        mask, disks, static_cast<unsigned>(edge),
                                        static_cast<unsigned>(alignment), thickness, layout.fits,
                                        layout.blocks.size())
                                .c_str());
                    for (std::size_t i = 0; i < layout.blocks.size(); ++i) {
                        const auto &b = layout.blocks[i];
                        require(b.bounds.x >= -0.01F && b.bounds.y >= -0.01F &&
                                    b.bounds.x + b.bounds.width <=
                                        (horizontal(edge) ? 560 : thickness) + 0.01F &&
                                    b.bounds.y + b.bounds.height <=
                                        (horizontal(edge) ? thickness : 560) + 0.01F,
                                "Aligned groups remain inside the negotiated strip");
                        for (std::size_t j = i + 1; j < layout.blocks.size(); ++j) {
                            require(!overlaps(b.bounds, layout.blocks[j].bounds),
                                    "Mixed-width aligned rows/columns remain disjoint");
                        }
                    }
                }
            }
        }
    }
}
