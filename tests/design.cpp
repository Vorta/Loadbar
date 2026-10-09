#include "model/geometry.hpp"
#include "model/layout.hpp"
#include "model/presentation.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
bool near(float a, float b) {
    return std::abs(a - b) < 0.002F;
}

void visibility_layout_tests() {
    using namespace loadbar;
    for (unsigned mask = 0; mask < 4; ++mask) {
        Settings settings;
        settings.gpu_id = L"remembered-gpu";
        settings.network_id = L"remembered-nic";
        settings.gpu_visible = (mask & 1U) != 0;
        settings.network_visible = (mask & 2U) != 0;
        require(decode_settings(encode_settings(settings)) == settings,
                "Visibility persists independently of remembered identities");
        settings.legacy_percent = 5.5;
        require(decode_settings(encode_settings(settings)) == settings,
                "Pending legacy percentage migration survives visibility persistence");
        for (unsigned count : {1U, 4U, 8U, 24U, 64U, 128U}) {
            std::vector<Processor> cpus;
            cpus.reserve(static_cast<std::size_t>(count) * 2);
            for (unsigned i = 0; i < count * 2; ++i) {
                cpus.push_back({{i / 64, i % 64}, i / 2, true, {}, 0U});
            }
            for (bool horizontal : {false, true}) {
                for (float text_scale : {1.0F, 1.5F, 2.25F}) {
                    const auto minimum = static_cast<float>(minimum_thickness(
                        1400, 4096, (horizontal ? loadbar::Edge::top : loadbar::Edge::right), cpus,
                        text_scale, settings, 2));
                    const float thickness = std::max(60.0F, minimum);
                    auto layout =
                        make_layout(horizontal ? 1400 : thickness, horizontal ? thickness : 1400,
                                    (horizontal ? loadbar::Edge::top : loadbar::Edge::right), cpus,
                                    text_scale, settings, 2);
                    require(layout.fits && layout.cores.size() == cpus.size() &&
                                layout.blocks.size() ==
                                    4U + settings.gpu_visible + settings.network_visible,
                            "Every physical core and visible device fits both orientations");
                    for (const auto &block : layout.blocks) {
                        require((block.kind == 0 ||
                                 near(block.graphic.width, layout.blocks[1].graphic.width)),
                                "Visible non-CPU widgets retain equal graphic widths");
                        require((block.kind != 2 || settings.gpu_visible) &&
                                    (block.kind != 4 || settings.network_visible),
                                "Hidden widgets have no block or tooltip hit target");
                    }
                    for (float dpi : {96.0F, 120.0F, 144.0F, 192.0F}) {
                        auto snapped = layout;
                        snap_layout(snapped, dpi);
                        for (std::size_t i = 0; i < snapped.cores.size(); ++i) {
                            const auto &box = snapped.cores[i].label;
                            require(near(box.width, box.height) &&
                                        near(box.width, snapped.cores[0].label.width) &&
                                        box.width >= 6 && snapped.cores[i].processor < cpus.size(),
                                    "Uniform SMT cores remain equal readable squares after pixel "
                                    "snapping");
                            for (std::size_t j = 0; j < i; ++j) {
                                const auto &other = snapped.cores[j].label;
                                require(box.x >= other.x + other.width ||
                                            other.x >= box.x + box.width ||
                                            box.y >= other.y + other.height ||
                                            other.y >= box.y + box.height,
                                        "Square core rows never overlap");
                            }
                        }
                    }
                    if (count == 4 && horizontal && text_scale == 1) {
                        require(
                            minimum == 40 &&
                                near(layout.cores.front().label.y, layout.cores.back().label.y),
                            "Four uniform cores use one row without increasing the minimum height");
                        const auto original =
                            make_layout(1400, thickness, loadbar::Edge::top, cpus, 1, {}, 2);
                        require(layout.blocks[1].graphic.width >= original.blocks[1].graphic.width,
                                "Removing widgets redistributes freed width");
                    }
                }
            }
            for (auto &cpu : cpus) {
                cpu.efficiency_class.reset();
            }
            const auto unknown = make_layout(1400, 640, loadbar::Edge::top, cpus, 1, settings, 2);
            require(unknown.fits &&
                        near(unknown.cores[0].label.width, unknown.cores[0].label.height),
                    "Unknown efficiency classes use honest uniform squares");
        }
    }
    auto old = decode_settings(L"Loadbar 7 2 60 1000 \"monitor\" \"gpu\" \"nic\" 1");
    require(old && old->gpu_visible && old->network_visible && old->gpu_id == L"gpu",
            "Version 7 migrates with both widgets visible and identities intact");
    for (auto suffix : {L"2 1 0", L"1 2 0", L"1 1 -1", L"1 1 26", L"1", L"1 1 0 extra"}) {
        require(!decode_settings(std::wstring(L"Loadbar 8 2 60 1000 \"\" \"\" \"\" 1 ") + suffix),
                "Malformed visibility and migration records rejected");
    }
    Snapshot snapshot;
    snapshot.gauges[1] = {70, Status::valid, Unit::percent, {}, {}, {}};
    snapshot.gauges[4] = {1024, Status::valid, Unit::bytes_per_second, {}, {}, {}};
    Settings hidden;
    hidden.gpu_visible = hidden.network_visible = false;
    require(
        next_stale_deadline(snapshot, 1000).has_value() &&
            !next_stale_deadline(snapshot, 1000, hidden) &&
            !expire_snapshot(snapshot, Clock::time_point{} + std::chrono::seconds(9), 1000, hidden),
        "Hidden metrics neither arm freshness deadlines nor trigger expiry repaint");
}

void content_width_tests() {
    using namespace loadbar;
    std::vector<Processor> processors;
    processors.reserve(8);
    for (unsigned i = 0; i < 8; ++i) {
        processors.push_back({{0, i}, i / 2, true, {}, 0});
    }
    auto eight = make_layout(1400, 60, loadbar::Edge::top, processors, 1, {}, 2);
    processors.resize(4);
    auto four = make_layout(1400, 60, loadbar::Edge::top, processors, 1, {}, 2);
    require(four.fits && eight.fits &&
                near(four.cores[0].label.width, eight.cores[0].label.width) &&
                near(eight.blocks[0].graphic.width,
                     2 * four.blocks[0].graphic.width + 2 * four.content_scale) &&
                four.blocks[1].graphic.width > eight.blocks[1].graphic.width,
            "CPU width follows thread count; device meters receive the freed space");
    for (unsigned mask = 0; mask < 4; ++mask) {
        Settings settings;
        settings.gpu_visible = (mask & 1U) != 0;
        settings.network_visible = (mask & 2U) != 0;
        for (auto alignment : {Alignment::start, Alignment::center, Alignment::end}) {
            settings.alignment = alignment;
            const auto layout =
                make_layout(1400, 60, loadbar::Edge::top, processors, 1, settings, 2);
            const auto &last_core = layout.cores.back().label;
            const auto &cpu = layout.blocks[0];
            const auto &ram = layout.blocks[1];
            const auto &last = layout.blocks.back();
            require(layout.fits &&
                        near(last_core.x + last_core.width, cpu.graphic.x + cpu.graphic.width),
                    "CPU allocation ends at its last tile without a blank slot");
            require(near(ram.bounds.x - cpu.bounds.x - cpu.bounds.width, 25 * layout.content_scale),
                    "Only normal spacing and divider separate CPU and RAM");
            require(near(last.bounds.x + last.bounds.width, 1400 - 11 * layout.content_scale),
                    "Remaining devices fill the edge through its normal outer margin");
        }
    }
    float previous{};
    for (float height : {40.0F, 60.0F, 120.0F, 320.0F, 640.0F}) {
        const auto layout = make_layout(1400, height, loadbar::Edge::top, processors, 1, {}, 2);
        require(layout.fits && layout.cores.front().label.width + 0.002F >= previous,
                "Increasing thickness cannot shrink content-sized CPU tiles");
        previous = layout.cores.front().label.width;
    }
}

void scaling_tests() {
    using namespace loadbar;
    std::vector<Processor> hybrid;
    hybrid.reserve(24);
    for (unsigned i = 0; i < 24; ++i) {
        hybrid.push_back({{0, i}, i, true, {}, i < 8 ? 1U : 0U});
    }
    const auto base = make_layout(3840, 40, loadbar::Edge::top, hybrid, 1, {}, 2);
    require(base.fits && base.content_scale == 1, "Compact appearance stays at its baseline");
    for (float height : {40.0F, 80.0F, 120.0F, 80.0F, 40.0F}) {
        const auto layout = make_layout(3840, height, loadbar::Edge::top, hybrid, 1, {}, 2);
        const float scale = height / 40;
        require(layout.fits && near(layout.content_scale, scale) &&
                    near(layout.blocks[0].icon.height, 14 * scale) &&
                    near(layout.blocks[1].graphic.height, 22 * scale) &&
                    near(layout.blocks[2].meters[0].height, 10 * scale) &&
                    near(layout.blocks[2].meters[1].height, 4 * scale) &&
                    near(layout.blocks.back().meters[0].height, 14 * scale) &&
                    near(layout.blocks.back().meters[1].height, 6 * scale),
                "Unconstrained thickness proportionally scales icons and every meter family");
    }
    const auto capped = make_layout(1000, 200, loadbar::Edge::top, hybrid, 1, {}, 2);
    const auto taller = make_layout(1000, 400, loadbar::Edge::top, hybrid, 1, {}, 2);
    require(capped.fits && taller.fits && capped.content_scale > 1 &&
                near(capped.content_scale, taller.content_scale),
            "Width-constrained contents stop growing instead of adding rows");
    const auto large_text = make_layout(3840, 180, loadbar::Edge::top, hybrid, 2.25F, {}, 2);
    require(large_text.fits && large_text.content_scale > 2.25F,
            "Combined content scale may exceed the Windows text-scale input limit");
    require(!make_layout(3840, 180, loadbar::Edge::top, hybrid, 2.26F, {}, 2).fits &&
                !make_layout(10, 40, loadbar::Edge::top, hybrid, 1, {}, 2).fits,
            "Invalid text scale and compact layout failures remain explicit");
    std::vector<std::vector<Processor>> topologies{{}, hybrid};
    for (bool mixed : {false, true}) {
        std::vector<Processor> topology;
        topology.reserve(128);
        for (unsigned i = 0; i < 128; ++i) {
            topology.push_back({{i / 64, i % 64},
                                i / 2,
                                true,
                                {},
                                mixed ? std::optional<unsigned>(i < 32 ? 1U : 0U) : std::nullopt});
        }
        topologies.push_back(std::move(topology));
    }
    for (const auto &processors : topologies) {
        for (bool horizontal : {false, true}) {
            const float length = horizontal ? 618.0F : 1080.0F;
            for (float text_scale : {1.0F, 1.25F, 1.5F, 2.0F, 2.25F}) {
                for (std::size_t disks : {0U, 2U, 4U}) {
                    const auto minimum = static_cast<float>(minimum_thickness(
                        length, 4096, (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                        processors, text_scale, {}, disks));
                    require(minimum <= 4096, "Test topology has a fitting compact layout");
                    const auto at = [&](float thickness) {
                        return make_layout(horizontal ? length : thickness,
                                           horizontal ? thickness : length,
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           processors, text_scale, {}, disks);
                    };
                    const auto compact = at(minimum);
                    require(compact.fits && near(compact.content_scale, text_scale),
                            "Readable minimum does not magnify contents");
                    float previous_scale = text_scale;
                    for (float multiple : {1.0F, 1.25F, 2.0F, 3.0F}) {
                        const float thickness = minimum * multiple;
                        const float width = horizontal ? length : thickness;
                        const float height = horizontal ? thickness : length;
                        const auto layout = at(thickness);
                        require(layout.fits && layout.content_scale + 0.002F >= previous_scale,
                                "Magnification is nondecreasing with thickness");
                        previous_scale = layout.content_scale;
                        require(layout.cores.size() == compact.cores.size() &&
                                    layout.blocks.size() == compact.blocks.size(),
                                "Scaling retains every physical core and disk");
                        std::set<std::size_t> membership;
                        for (std::size_t i = 0; i < layout.cores.size(); ++i) {
                            const auto &a = layout.cores[i];
                            const auto &b = compact.cores[i];
                            require(a.processor == b.processor &&
                                        (horizontal
                                             ? near((a.label.y - layout.blocks[0].graphic.y) /
                                                        layout.content_scale,
                                                    (b.label.y - compact.blocks[0].graphic.y) /
                                                        compact.content_scale)
                                             : near((a.label.x - layout.blocks[0].graphic.x) /
                                                        a.label.width,
                                                    (b.label.x - compact.blocks[0].graphic.x) /
                                                        b.label.width)),
                                    "CPU row and logical-processor membership remain fixed");
                            require(horizontal ? a.label.width + 0.002F >= a.label.height
                                               : a.label.height + 0.002F >= a.label.width,
                                    "Fixed CPU columns retain their minimum cell widths");
                            membership.insert(a.processor);
                        }
                        require(membership.size() == processors.size(),
                                "No logical processor is omitted by scaling");
                        for (std::size_t i = 0; i < layout.blocks.size(); ++i) {
                            const auto &a = layout.blocks[i];
                            const auto &b = compact.blocks[i];
                            require(
                                (a.kind == 0 ||
                                 near(a.graphic.width, layout.blocks[1].graphic.width)) &&
                                    (!horizontal || near((a.bounds.y - layout.blocks[0].bounds.y) /
                                                             layout.content_scale,
                                                         (b.bounds.y - compact.blocks[0].bounds.y) /
                                                             compact.content_scale)),
                                "Equal graphic widths and widget rows remain fixed");
                        }
                        for (float dpi : {96.0F, 120.0F, 144.0F, 192.0F}) {
                            auto snapped = layout;
                            snap_layout(snapped, dpi);
                            const float pixel =
                                96 / dpi +
                                0.002F; // Include float roundoff at a one-pixel boundary.
                            for (const auto &block : snapped.blocks) {
                                require((block.kind == 0 ||
                                         block.graphic.width == snapped.blocks[1].graphic.width) &&
                                            block.bounds.x >= 0 && block.bounds.y >= 0 &&
                                            block.bounds.x + block.bounds.width <= width + pixel &&
                                            block.bounds.y + block.bounds.height <= height + pixel,
                                        "Snapped widgets fit with equal physical widths");
                            }
                            const auto graphic = snapped.blocks[0].graphic;
                            for (const auto &core : snapped.cores) {
                                require(core.label.x >= graphic.x - pixel &&
                                            core.label.y >= graphic.y - pixel &&
                                            core.label.x + core.label.width <=
                                                graphic.x + graphic.width + pixel &&
                                            core.label.y + core.label.height <=
                                                graphic.y + graphic.height + pixel,
                                        "Every snapped CPU cell stays within its graphic");
                            }
                        }
                    }
                }
            }
        }
    }
    Snapshot snapshot;
    snapshot.processors = hybrid;
    const auto enlarged = make_layout(3840, 120, loadbar::Edge::top, hybrid, 1);
    const auto cell = enlarged.cores.front().label;
    require(metric_tooltip(enlarged, cell.x + cell.width / 2, cell.y + cell.height / 2, snapshot,
                           {}, {})
                    .find(L"Core 0") != std::wstring::npos,
            "Tooltip hit testing follows enlarged CPU bounds");
    for (float dpi : {96.0F, 120.0F, 144.0F, 192.0F}) {
        const Box box{13.3F, 30.7F, 107.4F, 22.3F};
        const float scale = 2.37F, pixels_per_dip = dpi / 96;
        const auto paint = expanded_paint_bounds(box, scale, dpi);
        const float pad = glow_padding_dips(scale, dpi);
        require(pad >= 12 * scale && near(pad * pixels_per_dip, std::round(pad * pixels_per_dip)) &&
                    paint.x <= box.x - pad && paint.y <= box.y - pad &&
                    paint.x + paint.width >=
                        box.x - pad + (std::ceil(box.width * pixels_per_dip) / pixels_per_dip) +
                            2 * pad,
                "Dirty bounds include scaled glow and bitmap rounding at each DPI");
    }
}
void edge_cpu_tests() {
    using namespace loadbar;
    std::vector<Processor> cpus;
    cpus.reserve(128);
    for (unsigned i = 0; i < 128; ++i) {
        cpus.push_back({{i / 64, i % 64}, i / 2, true, {}, i < 32 ? 1U : 0U});
    }
    for (bool squares : {false, true}) {
        Settings settings;
        settings.cpu_squares = squares;
        for (Edge edge : {Edge::left, Edge::right, Edge::top, Edge::bottom}) {
            const bool along_x = horizontal(edge);
            for (float text_scale : {1.0F, 1.5F, 2.25F}) {
                for (float requested : {40.0F, 60.0F, 120.0F, 320.0F, 640.0F}) {
                    const auto minimum = static_cast<float>(
                        minimum_thickness(1400, 4096, edge, cpus, text_scale, settings, 2));
                    require(minimum <= 4096,
                            "Grouped topology has a readable minimum on every edge");
                    const float thickness = std::max(requested, minimum);
                    const float width = along_x ? 1400 : thickness,
                                height = along_x ? thickness : 1400;
                    const auto layout =
                        make_layout(width, height, edge, cpus, text_scale, settings, 2);
                    require(layout.fits && layout.cores.size() == cpus.size(),
                            "Every thread survives fitting and rotation");
                    float p_left = width, p_right = 0, e_left = width, e_right = 0;
                    for (const auto &core : layout.cores) {
                        const auto b = core.label;
                        const auto area = layout.blocks[0].graphic;
                        require(b.x >= area.x - .002F && b.y >= area.y - .002F &&
                                    b.x + b.width <= area.x + area.width + .002F &&
                                    b.y + b.height <= area.y + area.height + .002F &&
                                    std::min(b.width, b.height) + .002F >= 6 * text_scale,
                                "Fitted CPU tiles remain readable and inside their widget");
                        require(!squares || near(b.width, b.height),
                                "Square mode never distorts a logical tile");
                        if (cpus[core.processor].efficiency_class == 1U) {
                            p_left = std::min(p_left, b.x);
                            p_right = std::max(p_right, b.x + b.width);
                        } else {
                            e_left = std::min(e_left, b.x);
                            e_right = std::max(e_right, b.x + b.width);
                        }
                    }
                    if (edge == Edge::right) {
                        require(e_right < p_left, "Right edge places all E lanes left of P lanes");
                    }
                    if (edge == Edge::left) {
                        require(p_right < e_left, "Left edge places all E lanes right of P lanes");
                    }
                    for (const auto &block : layout.blocks) {
                        require(block.bounds.x >= -.002F && block.bounds.y >= -.002F &&
                                    block.bounds.x + block.bounds.width <= width + .002F &&
                                    block.bounds.y + block.bounds.height <= height + .002F,
                                "Rotated and fitted widgets stay inside the reserved region");
                        if (!along_x && block.kind != 0) {
                            require(near(block.graphic.height, layout.blocks[1].graphic.height) &&
                                        block.graphic.height >= 22 * text_scale - .002F,
                                    "Remaining vertical length is equal and readable");
                        }
                    }
                }
            }
        }
    }
    for (const auto efficiency : {std::optional<unsigned>{0}, std::optional<unsigned>{}}) {
        for (auto &cpu : cpus) {
            cpu.efficiency_class = efficiency;
        }
        Settings uniform_settings;
        uniform_settings.cpu_squares = true;
        for (Edge edge : {Edge::left, Edge::right, Edge::top, Edge::bottom}) {
            const bool along_x = horizontal(edge);
            const auto layout = make_layout(along_x ? 1400.0F : 60.0F, along_x ? 60.0F : 1400.0F,
                                            edge, cpus, 1, uniform_settings, 2);
            require(layout.fits && layout.cores.size() == cpus.size(),
                    "Uniform square fitting keeps all threads");
            float occupied_min = 1400, occupied_max = 0;
            for (const auto &core : layout.cores) {
                const auto b = core.label;
                occupied_min = std::min(occupied_min, along_x ? b.y : b.x);
                occupied_max = std::max(occupied_max, along_x ? b.y + b.height : b.x + b.width);
                require(near(b.width, b.height) && b.width >= 6,
                        "Uniform fitting retains readable squares");
            }
            const auto area = layout.blocks[0].graphic;
            require(near(occupied_max - occupied_min, along_x ? area.height : area.width),
                    "Uniform and unknown square grids fill occupied strip thickness");
        }
    }
    cpus.resize(24);
    for (unsigned i = 0; i < cpus.size(); ++i) {
        cpus[i].efficiency_class = i < 8 ? 1U : 0U;
    }
    Settings settings;
    settings.cpu_squares = true;
    for (float thickness : {60.0F, 120.0F}) {
        for (Edge edge : {Edge::left, Edge::right}) {
            const auto layout = make_layout(thickness, 1400, edge, cpus, 1, settings, 2);
            require(layout.fits &&
                        near(layout.blocks[0].graphic.width, layout.blocks[1].graphic.width),
                    "Squares fill the usable vertical strip thickness");
            const auto rectangular = make_layout(thickness, 1400, edge, cpus, 1, {}, 2);
            require(layout.cores[0].label.width + 0.02F >= rectangular.cores[0].label.width &&
                        layout.blocks[0].graphic.height < rectangular.blocks[0].graphic.height &&
                        layout.blocks[1].graphic.height > rectangular.blocks[1].graphic.height,
                    "Square mode never shrinks cross-strip tiles and reallocates freed length");
        }
    }
    require(!make_layout(1400, 60, Edge::automatic, cpus, 1).fits &&
                !make_layout(1400, 60, static_cast<Edge>(99), cpus, 1).fits,
            "Layout rejects unresolved or malformed edges");
}
} // namespace
void run_design_tests() {
    edge_cpu_tests();
    {
        using namespace loadbar;
        std::vector<Processor> cpus;
        cpus.reserve(24);
        for (unsigned i = 0; i < 24; ++i) {
            cpus.push_back({{0, i}, i / 2, true, {}, i < 16 ? 1U : 0U});
        }
        Settings settings;
        settings.cpu_squares = true;
        for (bool horizontal : {false, true}) {
            for (float thickness : {60.0F, 120.0F, 240.0F}) {
                const auto layout = make_layout(
                    horizontal ? 1400 : thickness, horizontal ? thickness : 1400,
                    (horizontal ? loadbar::Edge::top : loadbar::Edge::right), cpus, 1, settings, 2);
                require(layout.fits && layout.cores.size() == cpus.size(),
                        "Square mode retains every thread");
                for (const auto &core : layout.cores) {
                    require(std::abs(core.label.width - core.label.height) < 0.001F,
                            "Every hybrid logical processor is square when requested");
                }
                require(std::abs(layout.cores[0].label.width / layout.cores.back().label.width -
                                 14.0F / 6) < 0.001F,
                        "Square mode retains different performance/efficiency sizes");
                if (!horizontal) {
                    for (const auto &block : layout.blocks) {
                        require(block.icon.y > block.graphic.y + block.graphic.height &&
                                    std::abs(block.icon.x + block.icon.width / 2 - block.graphic.x -
                                             block.graphic.width / 2) < 0.001F,
                                "Vertical icons sit centered below their graphics");
                        if (block.kind > 1) {
                            require(block.meters[0].y == block.meters[1].y &&
                                        block.meters[0].height == block.meters[1].height &&
                                        block.meters[1].x > block.meters[0].x,
                                    "Vertical meters share height and run side by side");
                        }
                        if (block.kind != 0) {
                            require(std::abs(block.graphic.height -
                                             layout.blocks[1].graphic.height) < 0.001F,
                                    "Non-CPU vertical widgets divide remaining height equally");
                        }
                    }
                }
            }
        }
    }

    scaling_tests();
    content_width_tests();
    visibility_layout_tests();
    using namespace loadbar;
    Metric old_value{50, Status::valid, Unit::percent, {}, {}, {}};
    auto new_value = old_value;
    new_value.timestamp = Clock::time_point{} + std::chrono::seconds(4);
    require(metric_changed(old_value, new_value, new_value.timestamp, 1000),
            "Fresh equal value repaints a previously aged status");
    require(!metric_changed(new_value, new_value, new_value.timestamp, 1000),
            "Unchanged fresh value does not repaint");
    Snapshot expired;
    expired.gauges[3] = old_value;
    expired.gpu_memory_bytes = {50, Status::valid, Unit::bytes, {}, {}, {}, L"selected-gpu"};
    require(expire_snapshot(expired, new_value.timestamp, 1000) &&
                expired.gauges[3].status == Status::stale &&
                expired.gpu_memory_bytes.status == Status::stale,
            "Both GPU memory representations expire");
    Snapshot s;
    for (auto status : {Status::warming_up, Status::error}) {
        set_snapshot_status(expired, status, L"Transition");
        require(expired.gpu_memory_bytes.status == status && expired.gauges[3].status == status &&
                    expired.gpu_memory_bytes.unit == Unit::bytes &&
                    expired.gpu_memory_bytes.identity == L"selected-gpu",
                "Priming and failure preserve GPU memory units and identities");
    }
    for (unsigned i = 0; i < 24; ++i) {
        s.processors.push_back(
            {{0, i}, i, true, {50, Status::valid, Unit::percent, {}, {}, {}}, i < 8 ? 1U : 0U});
    }
    for (std::size_t disks : {0U, 1U, 2U, 4U}) {
        auto inventory = s;
        inventory.disks.resize(disks);
        for (float w : {506.0F, 618.0F, 1274.0F, 2560.0F}) {
            const auto thickness = minimum_thickness(w, 480, loadbar::Edge::top, s.processors, 1,
                                                     {}, disk_widget_count(inventory.disks, {}));
            const auto l = make_snapshot_layout(w, static_cast<float>(thickness),
                                                loadbar::Edge::top, inventory, 1, {});
            require(l.fits && l.cores.size() == 24,
                    "Every logical processor fits after content-width reflow");
            require(l.blocks.size() == std::max(std::size_t{1}, disks) + 4,
                    "Every disk gets its own block");
            for (const auto &b : l.blocks) {
                require(
                    (b.kind == 0 || std::abs(b.graphic.width - l.blocks[1].graphic.width) < 0.001F),
                    "Non-CPU devices share the remaining graphic width");
                require(b.bounds.x >= 0 && b.bounds.x + b.bounds.width <= w + 0.001F &&
                            b.bounds.y >= 0 && b.bounds.y + b.bounds.height <= thickness,
                        "All blocks remain inside layout");
            }
            for (float dpi : {96.0F, 120.0F, 144.0F, 192.0F}) {
                auto snapped = l;
                snap_layout(snapped, dpi);
                for (const auto &b : snapped.blocks) {
                    require(std::abs(b.graphic.x * dpi / 96 - std::round(b.graphic.x * dpi / 96)) <
                                    0.001F &&
                                (b.kind == 0 || b.graphic.width == snapped.blocks[1].graphic.width),
                            "Pixel-snapped CPU and equal non-CPU device widths");
                }
            }
        }
    }
    for (bool horizontal : {true, false}) {
        for (float scale : {1.0F, 1.25F, 1.5F, 2.0F, 2.25F}) {
            const float length = horizontal ? 540.0F : 1080.0F;
            const auto thickness = minimum_thickness(
                length, 480, (horizontal ? loadbar::Edge::top : loadbar::Edge::right), s.processors,
                scale, {}, 2);
            require(thickness <= 480, "Two-disk layout reflows at text scale");
            const auto l = make_layout(horizontal ? length : static_cast<float>(thickness),
                                       horizontal ? static_cast<float>(thickness) : length,
                                       (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                       s.processors, scale, {}, 2);
            require(l.fits && l.cores.size() == 24, "Reflow retains all core cells");
            for (const auto &b : l.blocks) {
                require(b.graphic.width >= (b.kind == 0  ? 6.0F
                                            : horizontal ? 48.0F
                                                         : 22.0F) *
                                               scale &&
                            b.graphic.height >= 22 * scale,
                        "Readable metrics");
            }
        }
    }
    Settings settings;
    auto wide = make_layout(1920, 40, loadbar::Edge::top, s.processors, 1, settings, 2);
    require(wide.fits && near(wide.blocks[0].graphic.width, 190) &&
                wide.blocks[1].graphic.width > 240,
            "CPU keeps its content width while devices fill a wide edge");
    require(wide.cores[0].label.width > wide.cores[0].label.height, "CPU cells are rectangles");
    require(std::abs(wide.cores[0].label.width - (wide.cores[8].label.width * 2 + 2)) < 0.001F,
            "P cell spans two E cells proportionally");
    require(wide.blocks[2].meters[0].height == 10 && wide.blocks[2].meters[1].height == 4 &&
                wide.blocks[2].meters[2].height == 4,
            "GPU bands");
    require(wide.blocks[3].meters[0].height == 10 && wide.blocks[3].meters[1].height == 4 &&
                wide.blocks[3].meters[2].height == 4,
            "Disk active/read/write bands match GPU geometry");
    auto l = make_layout(1920, 40, loadbar::Edge::top, s.processors, 1, settings, 2);
    require(l.blocks[5].meters[0].height == 14 && l.blocks[5].meters[1].height == 6 &&
                l.blocks[3].meters[0].height == 10 && l.blocks[4].meters[0].height == 10,
            "Network permanently uses the 14/6 reference split");
    require(heat_color(0) == rgb(0x3A4856) && heat_color(30) == rgb(0x34B6A6) &&
                heat_color(100) == rgb(0xFF3456),
            "Exact heat stops");
    const auto mid = heat_color(15);
    require(std::abs(mid.r - (rgb(0x3A4856).r + rgb(0x34B6A6).r) / 2) < 0.00001F,
            "sRGB interpolation");
    require(tone(rgb(0), rgb(0xFFFFFF), 0).r == 0.7F, "Tone at zero");
    Metric m{0.5, Status::valid, Unit::percent, {}, {}, {}};
    require(graphic_fraction(Gauge::gpu_decode, m) == 0, "Decode visual floor");
    m.value = 0.6;
    require(graphic_fraction(Gauge::gpu_decode, m) > 0, "Decode above floor");
    m.value = 91;
    require(glows(Gauge::ram, m) && !glows(Gauge::disk_read, m), "Only percentage families glow");
    m.status = Status::stale;
    require(!glows(Gauge::ram, m) && compact_value(m) == L"—", "Invalid states never healthy zero");
    require(cpu_summary(s, settings, {}) == L"50%  P 50%  E 50%",
            "Logical processor weighted summaries");
    s.processors[0].utilization.status = Status::error;
    require(cpu_summary(s, settings, {}) == L"CPU — incomplete", "Partial CPU data is explicit");
    auto legacy = decode_settings(
        L"Loadbar 1 4 0 5 1000 500 1073741824 131072000 131072000 \"m\" \"d\" \"g\" \"n\"");
    require(legacy && legacy->legacy_percent == 5 && legacy->monitor_id == L"m",
            "Version one migration preserves choices and retires small ceilings");
    require(migrate_thickness(*legacy, {0, 0, 1920, 1080}, 144) && legacy->thickness == 40 &&
                !legacy->legacy_percent,
            "Legacy percent converts once using monitor/DPI then applies the 40-DIP minimum");
    require(migrate_thickness(*legacy, {0, 0, 1080, 1920}, 192) && legacy->thickness == 40,
            "DIP thickness survives rotation and DPI change");
    require(encode_settings(*legacy).starts_with(L"Loadbar 11 ") &&
                decode_settings(encode_settings(*legacy)) == legacy,
            "DIP-only schema persists migration");
    require(!decode_settings(
                L"Loadbar 2 4 0 26 1000 10000 10000 10000 10000 \"m\" \"d\" \"g\" \"n\" 0 1 0 0 0"),
            "Reject malformed legacy percentage");
    Snapshot smt;
    smt.processors = {{{0, 0}, 0, true, {20, Status::valid, Unit::percent, {}, {}, {}}},
                      {{0, 1}, 0, true, {80, Status::valid, Unit::percent, {}, {}, {}}}};
    const auto smt_layout = make_layout(1920, 40, loadbar::Edge::top, smt.processors, 1);
    require(smt_layout.cores.size() == 2 && smt_layout.cores[0].processor == 0 &&
                smt_layout.cores[1].processor == 1 &&
                core_metric(smt_layout.cores[0], smt, {}, 1000).value == 20 &&
                core_metric(smt_layout.cores[1], smt, {}, 1000).value == 80,
            "SMT siblings have independent logical-processor tiles");
    for (std::size_t i = 0; i < 2; ++i) {
        const auto cell = smt_layout.cores[i].label;
        const auto tip = metric_tooltip(smt_layout, cell.x + 2, cell.y + 2, smt, {}, {});
        require(tip.find(L"Core 0") != std::wstring::npos &&
                    tip.find(i ? L"logical processor 1" : L"logical processor 0") !=
                        std::wstring::npos &&
                    tip.find(i ? L"logical processor 0" : L"logical processor 1") ==
                        std::wstring::npos,
                "Each tooltip identifies its own thread and shared physical core");
    }
    smt.processors[1].utilization.status = Status::error;
    require(core_metric(smt_layout.cores[1], smt, {}, 1000).status == Status::error &&
                core_metric(smt_layout.cores[0], smt, {}, 1000).value == 20,
            "A failed sibling cannot mask another thread");
    smt.processors[1].utilization.status = Status::valid;
    smt.processors[1].utilization.value = std::numeric_limits<double>::quiet_NaN();
    require(core_metric(smt_layout.cores[1], smt, {}, 1000).status == Status::error,
            "Nonfinite logical-processor value is not healthy");
    Device d0{DeviceKind::disk, L"id0", L"Disk 0", 0}, d1{DeviceKind::disk, L"id1", L"Disk 1", 1};
    const Metric rate{123, Status::valid, Unit::bytes_per_second, {}, {}, {}};
    std::vector<CounterItem> counters{
        {L"1 D:", rate}, {L"_Total", rate}, {L"10 E:", rate}, {L"0 C:", rate}};
    require(disk_counter(counters, d0, {}).identity == L"id0" &&
                disk_counter(counters, d1, {}).identity == L"id1",
            "Reordered disks match exact index with stable identity");
    counters.pop_back();
    require(disk_counter(counters, d0, {}).status == Status::unavailable &&
                disk_counter(counters, d1, {}).status == Status::valid,
            "Missing disk does not affect other disk");
    counters.push_back(counters.front());
    require(disk_counter(counters, d1, {}).status == Status::error,
            "Duplicate disk counter is rejected");
    require(block_metric(wide.blocks[3], Gauge::disk_read, s).status == Status::unavailable &&
                metric_tooltip(wide, wide.blocks[3].bounds.x + 1, wide.blocks[3].bounds.y + 1, s,
                               {}, {})
                        .find(L"No physical disk discovered") == 0,
            "Empty catalog keeps an explicit unavailable disk widget");
    s.disks = {{L"id0", L"Disk 0", rate, rate}, {L"id1", L"Disk 1", rate, rate}};
    s.disks[1].read.value = 456;
    require(block_metric(wide.blocks[3], Gauge::disk_read, s).value == 123 &&
                block_metric(wide.blocks[4], Gauge::disk_read, s).value == 456,
            "Two disk widgets keep independent values");
    require(
        metric_tooltip(wide, wide.blocks[4].bounds.x + 1, wide.blocks[4].bounds.y + 1, s, {}, {})
            .starts_with(L"Disk 1"),
        "Second disk has its own tooltip");
    require(expire_snapshot(s, Clock::time_point{} + std::chrono::seconds(4), 1000) &&
                s.disks[0].read.status == Status::stale && s.disks[1].write.status == Status::stale,
            "Every disk metric expires");
    set_snapshot_status(s, Status::warming_up, L"Resume");
    require(s.disks[1].read.status == Status::warming_up &&
                s.disks[0].write.unit == Unit::bytes_per_second,
            "Resume primes both disks");
    require(decode_settings(encode_settings(settings)) == settings, "Appearance round trip");
    auto changed = settings;
    changed.edge = Edge::bottom;
    require(!collection_changed(settings, changed),
            "Appearance and placement do not reset collection");
    changed.interval_ms = 500;
    require(collection_changed(settings, changed), "Cadence resets collection");
    std::cout << "Design model, layout, scales and settings tests passed\n";
}
