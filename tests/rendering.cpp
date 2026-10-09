#include "model/continuity.hpp"
#include "model/geometry.hpp"
#include "model/strip_style.hpp"
#include "ui/renderer.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <iostream>
#include <stdexcept>
#include <wincodec.h>

namespace {
loadbar::Settings graphics_only() {
    return {.always_show_readout = false};
}
void checked(HRESULT result) {
    if (FAILED(result)) {
        throw std::runtime_error(
            std::format("Render test HRESULT 0x{:08X}", static_cast<unsigned long>(result)));
    }
}
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
struct ComApartment {
    ComApartment() {
        checked(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    }
    ~ComApartment() {
        CoUninitialize();
    }
};
std::vector<BYTE> pixels(IWICBitmap *bitmap, UINT width, UINT height) {
    std::vector<BYTE> bytes(static_cast<std::size_t>(width) * height * 4);
    checked(bitmap->CopyPixels(nullptr, width * 4, static_cast<UINT>(bytes.size()), bytes.data()));
    return bytes;
}
void save(IWICImagingFactory *wic, IWICBitmap *bitmap, const std::filesystem::path &path) {
    Microsoft::WRL::ComPtr<IWICStream> stream;
    checked(wic->CreateStream(&stream));
    checked(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    checked(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    checked(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    checked(encoder->CreateNewFrame(&frame, nullptr));
    checked(frame->Initialize(nullptr));
    checked(frame->WriteSource(bitmap, nullptr));
    checked(frame->Commit());
    checked(encoder->Commit());
}
loadbar::Snapshot fixture() {
    using namespace loadbar;
    Snapshot s;
    for (unsigned i = 0; i < 24; ++i) {
        s.processors.push_back(
            {{0, i},
             i,
             true,
             {static_cast<double>((i * 17) % 101), Status::valid, Unit::percent, {}, {}, {}},
             i < 8 ? 1U : 0U});
    }
    for (std::size_t i = 0; i < kGaugeCount; ++i) {
        s.gauges[i] = {i < 4 ? static_cast<double>(96 - i * 15)
                             : static_cast<double>((i + 1) * 1024 * 1024),
                       Status::valid,
                       i < 4 ? Unit::percent : Unit::bytes_per_second,
                       {},
                       {},
                       {}};
    }
    const auto ram_total = static_cast<std::uint64_t>(63.4 * 1073741824.0);
    set_physical_memory(s, ram_total, ram_total - 47ULL * 1073741824, {});
    s.gauges[4].session_peak = s.gauges[4].value * 2;
    s.gauges[5].session_peak = s.gauges[5].value * 4;
    s.gpu_memory_bytes = {12.0 * 1073741824, Status::valid, Unit::bytes, {}, {}, {}};
    s.gpu_memory_capacity = 24ULL * 1073741824;
    s.gauges[3].value = 50;
    s.gpu_label = L"Test GPU (fixture only)";
    s.disks = {{L"disk0",
                L"Test disk 0 (fixture only)",
                {1e6, Status::valid, Unit::bytes_per_second, {}, {}, {}},
                {1e5, Status::valid, Unit::bytes_per_second, {}, {}, {}}},
               {L"disk1",
                L"Test disk 1 (fixture only)",
                {1e8, Status::valid, Unit::bytes_per_second, {}, {}, {}},
                {1e7, Status::valid, Unit::bytes_per_second, {}, {}, {}}}};
    s.disks[0].active = {50, Status::valid, Unit::percent, {}, {}, {}};
    s.disks[1].active = {80, Status::valid, Unit::percent, {}, {}, {}};
    for (auto &disk : s.disks) {
        disk.read.session_peak = disk.read.value * 2;
        disk.write.session_peak = disk.write.value * 4;
    }
    s.network_label = L"Test NIC (fixture only)";
    return s;
}

void uniform_visibility_render_tests(IWICImagingFactory *wic, ID2D1Factory *factory,
                                     const std::filesystem::path &directory) {
    using namespace loadbar;
    auto snapshot = fixture();
    snapshot.processors.resize(4);
    for (unsigned i = 0; i < 4; ++i) {
        snapshot.processors[i].efficiency_class = 0;
        snapshot.processors[i].utilization.value = 15 + i * 25;
    }
    for (bool horizontal : {true, false}) {
        const UINT width = horizontal ? 1400U : 140U, height = horizontal ? 60U : 1000U;
        Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
        checked(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA,
                                  WICBitmapCacheOnLoad, &bitmap));
        Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
        checked(factory->CreateWicBitmapRenderTarget(
            bitmap.Get(), D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE), &target));
        Renderer reused;
        for (unsigned mask : {3U, 2U, 0U, 1U, 3U}) {
            Settings settings{.always_show_readout = false};
            settings.gpu_visible = (mask & 1U) != 0;
            settings.network_visible = (mask & 2U) != 0;
            checked(reused.render_to(target.Get(), snapshot, settings,
                                     (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                     false, 1, false, false, {}));
            const auto actual = pixels(bitmap.Get(), width, height);
            Renderer fresh;
            checked(fresh.render_to(target.Get(), snapshot, settings,
                                    (horizontal ? loadbar::Edge::top : loadbar::Edge::right), false,
                                    1, false, false, {}));
            require(actual == pixels(bitmap.Get(), width, height),
                    "Visibility toggles rebuild cached production rendering exactly");
            const auto layout =
                make_layout(static_cast<float>(width), static_cast<float>(height),
                            (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                            snapshot.processors, 1, settings, 2);
            require(layout.fits && layout.cores.size() == 4, "Preview has four physical cores");
            if (horizontal && (mask == 3 || mask == 0)) {
                save(wic, bitmap.Get(),
                     directory /
                         (mask == 3 ? L"uniform4-60-normal.png" : L"uniform4-60-hidden.png"));
            }
        }
    }
}

void vertical_edge_render_tests(IWICImagingFactory *wic, ID2D1Factory *factory,
                                const std::filesystem::path &directory) {
    using namespace loadbar;
    constexpr UINT width = 60, height = 1400;
    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
    checked(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                              &bitmap));
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
    checked(factory->CreateWicBitmapRenderTarget(
        bitmap.Get(), D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE), &target));
    auto snapshot = fixture();
    Renderer reused;
    Settings settings{.always_show_readout = false};
    for (bool squares : {false, true}) {
        settings.cpu_squares = squares;
        for (Edge edge : {Edge::right, Edge::left, Edge::right}) {
            for (bool hover : {false, true}) {
                checked(reused.render_to(target.Get(), snapshot, settings, edge, false, 1, hover,
                                         false, {}));
                const auto actual = pixels(bitmap.Get(), width, height);
                const auto revision = reused.layout_revision();
                checked(reused.render_to(target.Get(), snapshot, settings, edge, false, 1, hover,
                                         false, {}));
                require(reused.layout_revision() == revision,
                        "Unchanged edge reuses layout and hover caches");
                Renderer fresh;
                checked(fresh.render_to(target.Get(), snapshot, settings, edge, false, 1, hover,
                                        false, {}));
                require(actual == pixels(bitmap.Get(), width, height),
                        "Same-size edge switches match a fresh renderer");
                save(wic, bitmap.Get(),
                     directory / std::format(L"{}-60-{}-{}.png",
                                             edge == Edge::right ? L"right" : L"left",
                                             squares ? L"squares" : L"rectangles",
                                             hover ? L"hover" : L"normal"));
                D2D1_MATRIX_3X2_F transform{};
                target->GetTransform(&transform);
                require(transform._11 == 1 && transform._22 == 1 && transform._12 == 0 &&
                            transform._21 == 0 && transform._31 == 0 && transform._32 == 0,
                        "Every hover path restores the render transform");
                settings.show_hover_info = false;
                checked(reused.render_to(target.Get(), snapshot, settings, edge, false, 1, true,
                                         false, {}));
                const auto disabled = pixels(bitmap.Get(), width, height);
                checked(reused.render_to(target.Get(), snapshot, settings, edge, false, 1, false,
                                         false, {}));
                require(disabled == pixels(bitmap.Get(), width, height),
                        "Disabled hover stays graphics-only on both vertical edges");
                settings.show_hover_info = true;
            }
        }
        // An opaque high-contrast CPU overlay isolates text from the tile colors underneath.
        // Opposite quarter-turns must produce a 180-degree image rotation of the same text.
        checked(reused.render_to(target.Get(), snapshot, settings, Edge::right, false, 1, true,
                                 true, {}));
        const auto right = pixels(bitmap.Get(), width, height);
        checked(reused.render_to(target.Get(), snapshot, settings, Edge::left, false, 1, true, true,
                                 {}));
        const auto left = pixels(bitmap.Get(), width, height);
        auto layout = make_snapshot_layout(width, height, Edge::right, snapshot, 1, settings);
        snap_layout(layout, 96);
        const auto b = layout.blocks[0].graphic;
        const int x0 = static_cast<int>(b.x), y0 = static_cast<int>(b.y);
        const int w = static_cast<int>(b.width), h = static_cast<int>(b.height);
        std::size_t different{}, samples{};
        for (int y = 5; y < h - 5; ++y) {
            for (int x = 5; x < w - 5; ++x) {
                const auto a =
                    (static_cast<std::size_t>(y0 + y) * width + static_cast<std::size_t>(x0 + x)) *
                    4;
                const auto z = (static_cast<std::size_t>(y0 + h - 1 - y) * width +
                                static_cast<std::size_t>(x0 + w - 1 - x)) *
                               4;
                for (std::size_t c = 0; c < 3; ++c) {
                    different +=
                        std::abs(static_cast<int>(right[a + c]) - static_cast<int>(left[z + c])) > 2
                            ? 1U
                            : 0U;
                    ++samples;
                }
            }
        }
        require(samples > 100 && different * 100 < samples,
                "Right top-down and left bottom-up hover text are opposite quarter-turns");
    }
    for (float dpi : {96.0F, 120.0F, 144.0F, 192.0F}) {
        for (float text_scale : {1.0F, 1.5F, 2.25F}) {
            target->SetDpi(dpi, dpi);
            // Fixed pixels exercise narrow-target fallback as DPI/text scale rises.
            // Model tests separately cover readable full-size geometry at each text scale.
            for (Edge edge : {Edge::left, Edge::right}) {
                checked(reused.render_to(target.Get(), snapshot, settings, edge, false, text_scale,
                                         true, false, {}));
                const auto actual = pixels(bitmap.Get(), width, height);
                Renderer fresh_scale;
                checked(fresh_scale.render_to(target.Get(), snapshot, settings, edge, false,
                                              text_scale, true, false, {}));
                require(actual == pixels(bitmap.Get(), width, height),
                        "Vertical DPI and text-scale transitions match a fresh renderer");
            }
        }
    }
    target->SetDpi(96, 96);
    bool reject = true;
    Renderer retry([&](Renderer::ResourceKind kind, unsigned) {
        return reject && kind == Renderer::ResourceKind::text ? E_FAIL : S_OK;
    });
    require(retry.render_to(target.Get(), snapshot, settings, Edge::left, false, 1, true, false,
                            {}) == E_FAIL,
            "Injected rotated text failure reaches recovery");
    reject = false;
    checked(
        retry.render_to(target.Get(), snapshot, settings, Edge::left, false, 1, true, false, {}));
    const auto recovered = pixels(bitmap.Get(), width, height);
    Renderer fresh;
    checked(
        fresh.render_to(target.Get(), snapshot, settings, Edge::left, false, 1, true, false, {}));
    require(recovered == pixels(bitmap.Get(), width, height),
            "Rotated overlay failure restores clipping and transform before retry");
}

void preference_render_tests(IWICImagingFactory *wic, ID2D1Factory *factory,
                             const std::filesystem::path &directory) {
    using namespace loadbar;
    for (bool horizontal : {true, false}) {
        constexpr UINT length = 1400, thickness = 60;
        const UINT width = horizontal ? length : thickness,
                   height = horizontal ? thickness : length;
        Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
        checked(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA,
                                  WICBitmapCacheOnLoad, &bitmap));
        Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
        checked(factory->CreateWicBitmapRenderTarget(
            bitmap.Get(), D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE), &target));
        Renderer renderer;
        auto snapshot = fixture();
        Settings settings{.always_show_readout = false};
        for (bool squares : {false, true}) {
            settings.cpu_squares = squares;
            for (int memory = 0; memory < 3; ++memory) {
                snapshot.gpu_memory_capacity = memory == 0 ? 0 : 24ULL * 1073741824;
                snapshot.gpu_memory_label = memory == 2 ? L"GPU shared" : L"GPU VRAM";
                checked(renderer.render_to(target.Get(), snapshot, settings,
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           false, 1, false, false, {}));
                const auto resting = pixels(bitmap.Get(), width, height);
                Renderer fresh;
                checked(fresh.render_to(target.Get(), snapshot, settings,
                                        (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                        false, 1, false, false, {}));
                require(resting == pixels(bitmap.Get(), width, height),
                        "Square/memory transitions match a fresh layout cache");
                save(wic, bitmap.Get(),
                     directory / std::format(L"{}-60-{}-{}.png",
                                             horizontal ? L"horizontal" : L"vertical",
                                             squares ? L"squares" : L"rectangles",
                                             memory == 0   ? L"engines-only"
                                             : memory == 1 ? L"vram"
                                                           : L"shared"));
                settings.show_hover_info = false;
                checked(renderer.render_to(target.Get(), snapshot, settings,
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           false, 1, true, false, {}));
                require(resting == pixels(bitmap.Get(), width, height) &&
                            !renderer.tooltip(20, 20, snapshot, settings).empty(),
                        "Tooltip remains available with inline hover numbers disabled");
                settings.show_tooltips = false;
                require(renderer.tooltip(20, 20, snapshot, settings).empty(),
                        "Both hover options can be disabled independently");
                settings.show_hover_info = true;
                checked(renderer.render_to(target.Get(), snapshot, settings,
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           false, 1, true, false, {}));
                const auto inline_only = pixels(bitmap.Get(), width, height);
                require(resting != inline_only &&
                            renderer.tooltip(20, 20, snapshot, settings).empty(),
                        "Inline numbers work with tooltips disabled");
                settings.show_tooltips = true;
                checked(renderer.render_to(target.Get(), snapshot, settings,
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           false, 1, true, false, {}));
                require(inline_only == pixels(bitmap.Get(), width, height) &&
                            !renderer.tooltip(20, 20, snapshot, settings).empty(),
                        "Enabling tooltips does not alter the rendered bar");
            }
        }
        if (!horizontal) {
            const auto layout =
                make_snapshot_layout(static_cast<float>(width), static_cast<float>(height),
                                     loadbar::Edge::right, snapshot, 1, settings);
            std::array<std::vector<BYTE>, 3> levels;
            for (int level = 0; level < 3; ++level) {
                const double value = static_cast<double>(level) * 50;
                for (auto &metric : snapshot.gauges) {
                    metric.value = value;
                    metric.session_peak = 100;
                }
                for (auto &disk : snapshot.disks) {
                    for (auto *metric : {&disk.active, &disk.read, &disk.write}) {
                        metric->value = value;
                        metric->session_peak = 100;
                    }
                }
                checked(renderer.render_to(target.Get(), snapshot, settings, loadbar::Edge::right,
                                           false, 1, false, true, {}));
                levels[static_cast<std::size_t>(level)] = pixels(bitmap.Get(), width, height);
            }
            for (const auto &block : layout.blocks) {
                if (block.kind == 0) {
                    continue;
                }
                for (std::size_t i = 0; i < block.types.size() && block.types[i] != Gauge::count;
                     ++i) {
                    const auto b = block.meters[i];
                    const auto pixel = [&](std::size_t level, float fraction) {
                        const auto x = static_cast<UINT>(b.x + b.width / 2);
                        const auto y = static_cast<UINT>(b.y + b.height * fraction);
                        const auto offset = (static_cast<std::size_t>(y) * width + x) * 4;
                        return std::array{levels[level][offset], levels[level][offset + 1],
                                          levels[level][offset + 2]};
                    };
                    require(pixel(0, .25F) == pixel(1, .25F) && pixel(1, .25F) != pixel(2, .25F) &&
                                pixel(0, .75F) != pixel(1, .75F) &&
                                pixel(1, .75F) == pixel(2, .75F),
                            "Each vertical percentage/rate meter fills bottom to top at 0/50/100 "
                            "percent");
                }
            }
        }
    }
}

void logical_processor_render_tests(IWICImagingFactory *wic, ID2D1Factory *factory,
                                    const std::filesystem::path &directory) {
    using namespace loadbar;
    auto snapshot = fixture();
    snapshot.processors.resize(8);
    for (unsigned i = 0; i < 8; ++i) {
        snapshot.processors[i].core = i / 2;
        snapshot.processors[i].efficiency_class = 0;
        snapshot.processors[i].utilization.value = static_cast<double>(i) * 100 / 7;
    }
    for (bool horizontal : {true, false}) {
        const UINT width = horizontal ? 1400U : 140U, height = horizontal ? 60U : 1000U;
        Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
        checked(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA,
                                  WICBitmapCacheOnLoad, &bitmap));
        Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
        checked(factory->CreateWicBitmapRenderTarget(
            bitmap.Get(), D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE), &target));
        Renderer renderer;
        const auto layout = make_snapshot_layout(
            static_cast<float>(width), static_cast<float>(height),
            (horizontal ? loadbar::Edge::top : loadbar::Edge::right), snapshot, 1, graphics_only());
        require(layout.fits && layout.cores.size() == 8,
                "Four-core/eight-thread layout has eight tiles");
        const auto center = [&](const std::vector<BYTE> &image, std::size_t tile) {
            const auto b = layout.cores[tile].label;
            const auto offset = (static_cast<std::size_t>(b.y + b.height / 2) * width +
                                 static_cast<std::size_t>(b.x + b.width / 2)) *
                                4;
            return std::array{image[offset], image[offset + 1], image[offset + 2]};
        };
        for (bool contrast : {false, true}) {
            checked(renderer.render_to(target.Get(), snapshot, graphics_only(),
                                       (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                       false, 1, false, contrast, {}));
            if (horizontal && !contrast) {
                save(wic, bitmap.Get(), directory / L"uniform4-8threads-60-normal.png");
            }
            const auto baseline = pixels(bitmap.Get(), width, height);
            for (std::size_t changed = 0; changed < 8; ++changed) {
                auto update = snapshot;
                update.processors[changed].utilization.value = changed < 4 ? 100 : 0;
                checked(renderer.render_to(target.Get(), update, graphics_only(),
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           false, 1, false, contrast, {}));
                const auto actual = pixels(bitmap.Get(), width, height);
                for (std::size_t tile = 0; tile < 8; ++tile) {
                    require((center(actual, tile) != center(baseline, tile)) == (tile == changed),
                            "Only the changed logical processor changes its own interior color");
                }
            }
            DisplayContinuity continuity;
            auto retained = snapshot;
            continuity.observe(retained);
            retained.processors[1].utilization.status = Status::error;
            continuity.observe(retained);
            checked(renderer.render_to(target.Get(), retained, graphics_only(),
                                       (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                       false, 1, false, contrast, {}));
            require(pixels(bitmap.Get(), width, height) == baseline,
                    "SMT error retention preserves both independent thread tiles");
            auto warming = snapshot;
            warming.processors[0].utilization.status = Status::warming_up;
            checked(renderer.render_to(target.Get(), warming, graphics_only(),
                                       (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                       false, 1, false, contrast, {}));
            require(pixels(bitmap.Get(), width, height) == baseline,
                    "Never-observed thread warmup renders as zero without marks");
        }
        std::ranges::reverse(snapshot.processors);
        const auto reordered = make_snapshot_layout(
            static_cast<float>(width), static_cast<float>(height),
            (horizontal ? loadbar::Edge::top : loadbar::Edge::right), snapshot, 1, graphics_only());
        for (std::size_t tile = 0; tile < 8; ++tile) {
            require(reordered.cores[tile].logical == layout.cores[tile].logical,
                    "CPU tile identity order is stable across reordered samples");
        }
        std::ranges::reverse(snapshot.processors);
    }
}
void bounded_glow_tests(IWICImagingFactory *wic, ID2D1Factory *factory) {
    using namespace loadbar;
    auto snapshot = fixture();
    for (auto &cpu : snapshot.processors) {
        cpu.utilization.value = 95;
    }
    snapshot.gauges[0].value = snapshot.gauges[1].value = snapshot.gauges[3].value = 95;
    for (bool horizontal : {true, false}) {
        const UINT width = horizontal ? 3840U : 640U, height = horizontal ? 640U : 2160U;
        Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
        checked(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA,
                                  WICBitmapCacheOnLoad, &bitmap));
        Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
        checked(factory->CreateWicBitmapRenderTarget(
            bitmap.Get(), D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE), &target));
        unsigned creations{}, largest{};
        Renderer renderer([&](Renderer::ResourceKind kind, unsigned count) {
            if (kind == Renderer::ResourceKind::glow) {
                ++creations;
                largest = std::max(largest, count);
                require(count > 0 && count <= 65536, "Glow temporary images stay within 768 KiB");
            }
            return S_OK;
        });
        checked(renderer.render_to(target.Get(), snapshot, graphics_only(),
                                   (horizontal ? loadbar::Edge::top : loadbar::Edge::right), false,
                                   1, false, false, {}));
        const auto cold_count = creations;
        const auto cold = pixels(bitmap.Get(), width, height);
        require(cold_count > 0 && largest > 32000, "Large fixture exercises bounded glow masks");
        checked(renderer.render_to(target.Get(), snapshot, graphics_only(),
                                   (horizontal ? loadbar::Edge::top : loadbar::Edge::right), false,
                                   1, false, false, {}));
        require(creations == cold_count && pixels(bitmap.Get(), width, height) == cold,
                "Bounded glows cache without repeated construction or pixel changes");
    }
}

void drive_visibility_render_tests(IWICImagingFactory *wic, ID2D1Factory *factory) {
    using namespace loadbar;
    const auto snapshot = fixture();
    for (bool horizontal : {true, false}) {
        const UINT width = horizontal ? 1400U : 160U, height = horizontal ? 80U : 1000U;
        Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
        checked(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA,
                                  WICBitmapCacheOnLoad, &bitmap));
        Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
        checked(factory->CreateWicBitmapRenderTarget(
            bitmap.Get(), D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE), &target));
        Renderer reused;
        for (bool extras : {true, false}) {
            for (unsigned mask : {0U, 1U, 2U, 3U, 0U}) {
                Settings settings{.always_show_readout = false};
                settings.gpu_visible = settings.network_visible = extras;
                for (std::size_t i = 0; i < 2; ++i) {
                    if (mask & (1U << i)) {
                        hide_disk(settings.hidden_disks, snapshot.disks[i].id);
                    }
                }
                const auto layout =
                    make_snapshot_layout(static_cast<float>(width), static_cast<float>(height),
                                         (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                         snapshot, 1, settings);
                require(layout.fits && layout.cores.size() == snapshot.processors.size(),
                        "Disk visibility retains every CPU core in both orientations");
                std::size_t disk_blocks{};
                for (const auto &block : layout.blocks) {
                    require(block.kind == 0 || std::abs(block.graphic.width -
                                                        layout.blocks[1].graphic.width) < 0.001F,
                            "Non-CPU widgets share remaining graphic widths");
                    if (block.kind == 3) {
                        ++disk_blocks;
                        require(block.disk < snapshot.disks.size() &&
                                    disk_visible(snapshot.disks[block.disk].id, settings),
                                "Disk block refers to original visible snapshot index");
                        const auto tip = metric_tooltip(layout, block.bounds.x + 1,
                                                        block.bounds.y + 1, snapshot, settings, {});
                        require(tip.find(snapshot.disks[block.disk].label) != std::wstring::npos,
                                "Visible disk tooltip retains correct device identity");
                    }
                }
                require(disk_blocks == disk_widget_count(snapshot.disks, settings) &&
                            layout.blocks.size() == 2 + disk_blocks + (extras ? 2 : 0),
                        "Hidden disks have no placeholder and remaining widgets fill space");
                checked(reused.render_to(target.Get(), snapshot, settings,
                                         (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                         false, 1, false, false, {}));
                const auto actual = pixels(bitmap.Get(), width, height);
                Renderer fresh;
                checked(fresh.render_to(target.Get(), snapshot, settings,
                                        (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                        false, 1, false, false, {}));
                require(actual == pixels(bitmap.Get(), width, height),
                        "Same-count disk swaps invalidate cached layout identities");
                auto changed = snapshot;
                for (auto &disk : changed.disks) {
                    if (!disk_visible(disk.id, settings)) {
                        disk.active.value = 0;
                        disk.read.value = disk.write.value = 0;
                    }
                }
                checked(reused.render_to(target.Get(), changed, settings,
                                         (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                         false, 1, false, false, {}));
                require(actual == pixels(bitmap.Get(), width, height),
                        "Hidden disk readings cannot affect visible rendering");
            }
        }
    }
    Settings settings{.always_show_readout = false};
    settings.hidden_disks = {L"disk0", L"disk1"};
    auto changed = snapshot;
    changed.disks.push_back({L"new", L"New drive"});
    require(disk_widget_count(changed.disks, settings) == 1,
            "New drive is visible even when prior drives were all hidden");
    changed.disks.clear();
    const auto absent = make_snapshot_layout(1400, 80, loadbar::Edge::top, changed, 1, settings);
    require(disk_widget_count(changed.disks, settings) == 1 &&
                std::ranges::count(absent.blocks, 3U, &Layout::Block::kind) == 1,
            "No discovered drives still produces the explicit unavailable placeholder");
}

void contrast_load_regression(IWICImagingFactory *wic, ID2D1Factory *factory) {
    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
    checked(
        wic->CreateBitmap(1274, 40, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap));
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
    checked(factory->CreateWicBitmapRenderTarget(bitmap.Get(), D2D1::RenderTargetProperties(),
                                                 &target));
    loadbar::Renderer renderer;
    auto snapshot = fixture();
    std::vector<BYTE> previous;
    for (double value : {0.0, 50.0, 100.0}) {
        for (auto &cpu : snapshot.processors) {
            cpu.utilization.value = value;
        }
        checked(renderer.render_to(target.Get(), snapshot, graphics_only(), loadbar::Edge::top,
                                   false, 1, false, true, {}));
        auto actual = pixels(bitmap.Get(), 1274, 40);
        require(actual != previous, "Resting high-contrast CPU images distinguish 0/50/100% load");
        previous = std::move(actual);
    }
}
void contrast_palette_tests(IWICImagingFactory *wic, ID2D1Factory *factory,
                            const std::filesystem::path &directory) {
    using namespace loadbar;
    constexpr std::array palettes{std::array{RGB(0, 0, 0), RGB(255, 255, 255)},
                                  std::array{RGB(255, 255, 255), RGB(0, 0, 0)},
                                  std::array{RGB(32, 32, 32), RGB(255, 231, 158)},
                                  std::array{RGB(255, 250, 239), RGB(61, 61, 61)}};
    for (unsigned dpi : {96U, 120U, 144U, 192U}) {
        for (bool horizontal : {true, false}) {
            const unsigned width = (horizontal ? 1274U : 140U) * dpi / 96;
            const unsigned height = (horizontal ? 40U : 1000U) * dpi / 96;
            Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
            checked(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA,
                                      WICBitmapCacheOnLoad, &bitmap));
            Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
            checked(factory->CreateWicBitmapRenderTarget(
                bitmap.Get(),
                D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(),
                                             static_cast<float>(dpi), static_cast<float>(dpi)),
                &target));
            unsigned palette_index{}, glow_creations{};
            Renderer renderer(
                [&](Renderer::ResourceKind kind, unsigned) {
                    glow_creations += kind == Renderer::ResourceKind::glow;
                    return S_OK;
                },
                [&](int index) {
                    // Deliberately make highlight equal background: CPU magnitude must use
                    // the matched window/text pair, independent of selection colors.
                    return palettes[palette_index][index == COLOR_WINDOWTEXT ? 1 : 0];
                });
            auto snapshot = fixture();
            auto layout =
                make_layout(target->GetSize().width, target->GetSize().height,
                            (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                            snapshot.processors, 1, graphics_only(), snapshot.disks.size());
            snap_layout(layout, static_cast<float>(dpi));
            require(layout.fits, "High-contrast test layout fits every core");
            const auto pixel = [&](const std::vector<BYTE> &bytes, float x, float y) {
                const auto offset =
                    (static_cast<std::size_t>(y * static_cast<float>(dpi) / 96) * width +
                     static_cast<std::size_t>(x * static_cast<float>(dpi) / 96)) *
                    4;
                return std::array{bytes[offset + 2], bytes[offset + 1], bytes[offset]};
            };
            const auto render = [&](bool contrast, bool hover = false) {
                checked(renderer.render_to(target.Get(), snapshot, graphics_only(),
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           false, 1, hover, contrast, {}));
                return pixels(bitmap.Get(), width, height);
            };
            for (; palette_index < palettes.size(); ++palette_index) {
                renderer.preferences_changed();
                const auto bg = palettes[palette_index][0], fg = palettes[palette_index][1];
                const std::array background{GetRValue(bg), GetGValue(bg), GetBValue(bg)};
                const std::array foreground{GetRValue(fg), GetGValue(fg), GetBValue(fg)};
                std::array<std::vector<BYTE>, 3> observations;
                for (unsigned step = 0; step < observations.size(); ++step) {
                    for (auto &cpu : snapshot.processors) {
                        cpu.utilization.value = static_cast<double>(step) * 50;
                    }
                    observations[step] = render(true);
                    for (const auto &core : layout.cores) {
                        const auto cell = core.label;
                        for (float x : {0.35F, 0.5F, 0.65F}) {
                            const auto actual = pixel(observations[step], cell.x + cell.width * x,
                                                      cell.y + cell.height / 2);
                            for (std::size_t channel = 0; channel < actual.size(); ++channel) {
                                const double expected =
                                    background[channel] +
                                    (static_cast<int>(foreground[channel]) - background[channel]) *
                                        static_cast<double>(step) / 2;
                                require(std::abs(actual[channel] - expected) <= 1,
                                        "Every CPU interior is a uniform system-pair load shade");
                            }
                        }
                        if (step == 0) {
                            require(pixel(observations[step], cell.x + cell.width / 2,
                                          cell.y + layout.content_scale / 2) != background,
                                    "Idle CPU retains a visible foreground outline");
                        }
                    }
                }
                require(observations[0] != observations[1] && observations[1] != observations[2] &&
                            observations[0] != observations[2],
                        "All three resting load levels remain distinct in every palette/DPI");
                require(glow_creations == 0, "High contrast creates no glow resources");
                for (auto &cpu : snapshot.processors) {
                    cpu.utilization = {0, Status::warming_up, Unit::percent, {}, {}, {}};
                }
                require(render(true) == observations[0], "Initial warm-up looks like valid zero");
                DisplayContinuity continuity;
                for (auto &cpu : snapshot.processors) {
                    cpu.utilization = {50, Status::valid, Unit::percent, {}, {}, {}};
                }
                continuity.observe(snapshot);
                for (auto &cpu : snapshot.processors) {
                    cpu.utilization.status = Status::error;
                }
                continuity.observe(snapshot);
                require(render(true) == observations[1], "Failure preserves retained CPU shade");
                for (auto &cpu : snapshot.processors) {
                    cpu.utilization.retained.reset();
                    cpu.utilization.status = Status::unavailable;
                }
                require(render(true) != observations[0], "Never-valid unavailable stays marked");
                for (auto &cpu : snapshot.processors) {
                    cpu.utilization = {static_cast<double>(cpu.core % 3) * 50,
                                       Status::valid,
                                       Unit::percent,
                                       {},
                                       {},
                                       {}};
                }
                const auto resting = render(true);
                if (dpi == 96) {
                    save(wic, bitmap.Get(),
                         directory / std::format(L"contrast-cpu-{}-{}.png", palette_index,
                                                 horizontal ? L"h" : L"v"));
                }
                require(render(true, true) != resting, "High-contrast hover still exposes numbers");
                renderer.discard();
                require(render(true) == resting, "Resource recovery preserves high-contrast cells");
                const auto normal = render(false);
                require(render(true) == resting && render(false) == normal,
                        "Reused renderer preserves normal/high-contrast/normal transitions");
                glow_creations = 0;
            }
        }
    }
}
void continuity_render_tests(IWICImagingFactory *wic, ID2D1Factory *factory,
                             const std::filesystem::path &directory) {
    using namespace loadbar;
    Renderer renderer;
    unsigned artifact{};
    const auto render = [&](Renderer &drawing, const Snapshot &snapshot, unsigned height,
                            bool hover, bool contrast) {
        Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
        checked(wic->CreateBitmap(1274, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                  &bitmap));
        Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
        const auto properties = D2D1::RenderTargetProperties();
        checked(factory->CreateWicBitmapRenderTarget(bitmap.Get(), properties, &target));
        checked(drawing.render_to(target.Get(), snapshot, graphics_only(), loadbar::Edge::top,
                                  false, 1, hover, contrast, {}));
        save(wic, bitmap.Get(), directory / std::format(L"continuity-{}.png", artifact++));
        return pixels(bitmap.Get(), 1274, height);
    };
    auto original = fixture();
    original.gpu_id = L"fixture-gpu";
    original.network_id = L"fixture-nic";
    original.gauges[3].value = 50; // Same observation as the fixture's 12/24 GiB.
    DisplayContinuity state;
    state.observe(original);
    auto failed = original;
    set_snapshot_status(failed, Status::error, L"Injected provider failure");
    failed.ram_total_bytes = failed.gpu_memory_capacity = 0;
    state.observe(failed);
    for (bool hover : {false, true}) {
        for (bool contrast : {false, true}) {
            require(
                render(renderer, original, 40, hover, contrast) ==
                    render(renderer, failed, 40, hover, contrast),
                "Retained readings preserve rest/hover/high-contrast pixels without failure marks");
        }
    }
    auto zero = original;
    for (auto &cpu : zero.processors) {
        cpu.utilization.value = 0;
        cpu.utilization.retained.reset();
    }
    for (auto &metric : zero.gauges) {
        metric.value = 0;
        metric.retained.reset();
    }
    for (auto &disk : zero.disks) {
        for (auto *metric : {&disk.read, &disk.write, &disk.active}) {
            metric->value = 0;
            metric->retained.reset();
        }
    }
    zero.ram_used_bytes.value = zero.gpu_memory_bytes.value = 0;
    zero.ram_used_bytes.retained.reset();
    zero.gpu_memory_bytes.retained.reset();
    // Never-observed memory is intentionally omitted, including during initial warm-up.
    zero.gpu_memory_capacity = 0;
    auto warming = original;
    set_snapshot_status(warming, Status::warming_up, L"Initial warm-up");
    DisplayContinuity cold;
    cold.observe(warming);
    for (bool hover : {false, true}) {
        require(render(renderer, warming, 40, hover, false) ==
                    render(renderer, zero, 40, hover, false),
                "Initial warm-up renders exactly like zero with no dots, including hover");
    }
    auto absent = warming;
    set_snapshot_status(absent, Status::unavailable, L"Never supported");
    cold.observe(absent);
    require(render(renderer, absent, 40, false, false) != render(renderer, zero, 40, false, false),
            "Never-valid unavailable metrics remain visibly different from idle zero");
    for (unsigned height : {40U, 80U, 120U, 80U, 40U}) {
        Renderer fresh;
        require(render(renderer, original, height, false, false) ==
                    render(fresh, original, height, false, false),
                "Repeated resize produces the same pixels as a fresh renderer at that size");
    }
    std::cout << artifact << " continuity/resize fixtures passed\n";
}
void scaling_render_tests(IWICImagingFactory *wic, ID2D1Factory *factory,
                          const std::filesystem::path &directory) {
    using namespace loadbar;
    auto snapshot = fixture();
    snapshot.gauges[1].value = snapshot.gauges[3].value = 95; // Exercise scaled device glows.
    Renderer reused;
    unsigned artifact{};
    const auto render = [&](Renderer &renderer, const Snapshot &sample, float width, float height,
                            bool horizontal, float text_scale, float dpi, bool hover, bool contrast,
                            bool keep) {
        const auto pixel_width = static_cast<UINT>(std::ceil(width * dpi / 96));
        const auto pixel_height = static_cast<UINT>(std::ceil(height * dpi / 96));
        Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
        checked(wic->CreateBitmap(pixel_width, pixel_height, GUID_WICPixelFormat32bppPBGRA,
                                  WICBitmapCacheOnLoad, &bitmap));
        Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
        auto properties = D2D1::RenderTargetProperties();
        properties.dpiX = properties.dpiY = dpi;
        checked(factory->CreateWicBitmapRenderTarget(bitmap.Get(), properties, &target));
        checked(renderer.render_to(target.Get(), sample, graphics_only(),
                                   (horizontal ? loadbar::Edge::top : loadbar::Edge::right), false,
                                   text_scale, hover, contrast, {}));
        if (keep) {
            save(wic, bitmap.Get(), directory / std::format(L"scaling-{}.png", artifact++));
        }
        return pixels(bitmap.Get(), pixel_width, pixel_height);
    };
    for (float dpi : {96.0F, 120.0F, 144.0F, 192.0F}) {
        for (float height : {40.0F, 80.0F, 120.0F, 80.0F, 40.0F}) {
            Renderer fresh;
            require(render(reused, snapshot, 3840, height, true, 1, dpi, false, false, true) ==
                        render(fresh, snapshot, 3840, height, true, 1, dpi, false, false, false),
                    "Growing/shrinking reused renderer matches direct construction at every DPI");
        }
        for (bool unavailable : {false, true}) {
            auto state = snapshot;
            if (unavailable) {
                set_snapshot_status(state, Status::unavailable, L"Scaling fixture only");
            }
            for (bool contrast : {false, true}) {
                Renderer fresh;
                const auto first =
                    render(reused, state, 3840, 120, true, 2.25F, dpi, true, contrast, true);
                require(first == render(fresh, state, 3840, 120, true, 2.25F, dpi, true, contrast,
                                        false),
                        "Scaled hover/status/high-contrast rendering is independent of history");
                reused.discard();
                require(first == render(reused, state, 3840, 120, true, 2.25F, dpi, true, contrast,
                                        false),
                        "Scaled resources reconstruct identically after discard");
            }
        }
        for (float text_scale : {1.5F, 2.25F}) {
            const auto minimum = static_cast<float>(
                minimum_thickness(1080, 480, loadbar::Edge::right, snapshot.processors, text_scale,
                                  graphics_only(), snapshot.disks.size()));
            require(minimum <= 480, "Vertical scaling fixture fits");
            Renderer fresh;
            require(render(reused, snapshot, minimum * 2, 1080, false, text_scale, dpi, true, false,
                           true) == render(fresh, snapshot, minimum * 2, 1080, false, text_scale,
                                           dpi, true, false, false),
                    "Vertical scaling, text preference and DPI compose without double scaling");
        }
    }
    // Verify visible hover glyphs grow, in addition to the pure geometry assertions.
    const auto ink_height = [](const std::vector<BYTE> &image, const Box &box) {
        unsigned top = 10000, bottom{};
        for (unsigned y = static_cast<unsigned>(std::ceil(box.y));
             y < static_cast<unsigned>(box.y + box.height); ++y) {
            for (unsigned x = static_cast<unsigned>(std::ceil(box.x));
                 x < static_cast<unsigned>(box.x + box.width); ++x) {
                const auto index = (static_cast<std::size_t>(y) * 3840 + x) * 4;
                if (image[index] == 0xF4 && image[index + 1] == 0xED && image[index + 2] == 0xE8) {
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
            }
        }
        return top <= bottom ? bottom - top + 1 : 0U;
    };
    const auto compact_pixels = render(reused, snapshot, 3840, 40, true, 1, 96, true, false, false);
    const auto enlarged_pixels =
        render(reused, snapshot, 3840, 80, true, 1, 96, true, false, false);
    const auto small_layout =
        make_layout(3840, 40, loadbar::Edge::top, snapshot.processors, 1, graphics_only(), 2);
    const auto large_layout =
        make_layout(3840, 80, loadbar::Edge::top, snapshot.processors, 1, graphics_only(), 2);
    const auto small_ink = ink_height(compact_pixels, small_layout.blocks[0].graphic);
    const auto large_ink = ink_height(enlarged_pixels, large_layout.blocks[0].graphic);
    require(small_ink > 0 && large_ink >= small_ink * 3 / 2,
            "Hover font ink grows along with the larger widget");
    // Reuse one target and existing fonts, then fail each replacement font in turn.
    for (unsigned stage = 0; stage < 5; ++stage) {
        bool armed = false;
        Renderer retry([&](Renderer::ResourceKind kind, unsigned index) {
            return armed && kind == Renderer::ResourceKind::font && index == stage ? E_OUTOFMEMORY
                                                                                   : S_OK;
        });
        Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
        checked(wic->CreateBitmap(1000, 400, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                  &bitmap));
        Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
        auto properties = D2D1::RenderTargetProperties();
        properties.dpiX = properties.dpiY = 96;
        checked(factory->CreateWicBitmapRenderTarget(bitmap.Get(), properties, &target));
        checked(retry.render_to(target.Get(), snapshot, graphics_only(), loadbar::Edge::top, false,
                                1, true, false, {}));
        const auto before =
            make_layout(1000, 400, loadbar::Edge::top, snapshot.processors, 1, graphics_only(), 2);
        const auto after = make_layout(1000, 400, loadbar::Edge::top, snapshot.processors, 2.25F,
                                       graphics_only(), 2);
        require(before.fits && after.fits && before.content_scale != after.content_scale,
                "Font retry fixture changes effective scale on the same target");
        armed = true;
        require(retry.render_to(target.Get(), snapshot, graphics_only(), loadbar::Edge::top, false,
                                2.25F, true, false, {}) == E_OUTOFMEMORY,
                "Font failure during scale change preserves native error");
        armed = false;
        checked(retry.render_to(target.Get(), snapshot, graphics_only(), loadbar::Edge::top, false,
                                2.25F, true, false, {}));
        Renderer fresh;
        require(pixels(bitmap.Get(), 1000, 400) ==
                    render(fresh, snapshot, 1000, 400, true, 2.25F, 96, true, false, false),
                "Font replacement retries atomically after an effective-scale change");
        save(wic, bitmap.Get(), directory / std::format(L"scaling-{}.png", artifact++));
    }
    std::cout << artifact << " scaling fixtures passed\n";
}
void temperature_reference_previews(IWICImagingFactory *wic, ID2D1Factory *factory,
                                    const std::filesystem::path &directory) {
    using namespace loadbar;
    constexpr std::array normal_load{44., 80., 35., 66., 58., 22., 68., 29., 10., 34., 5., 22.,
                                     46., 11., 5.,  29., 12., 7.,  39., 13., 4.,  26., 8., 29.};
    constexpr std::array<std::array<double, 5>, 3> temperatures{
        {{64, 46, 58, 49, 38}, {95, 72, 87, 74, 58}, {99, 77, 92, 79, 61}}};
    constexpr std::array<const wchar_t *, 3> modes{L"normal", L"hot", L"critical"};
    Settings settings{.always_show_readout = false};
    settings.cpu_squares = true;
    settings.show_hover_info = false;
    for (unsigned mode = 0; mode < modes.size(); ++mode) {
        auto snapshot = fixture();
        for (std::size_t i = 0; i < snapshot.processors.size(); ++i) {
            snapshot.processors[i].utilization.value = mode == 0 ? normal_load[i]
                                                       : mode == 1
                                                           ? 86 + static_cast<double>(i % 5) * 3
                                                           : 100;
        }
        const double ram_percent = mode == 0 ? 63 : mode == 1 ? 88 : 93;
        set_physical_memory(snapshot, 64ULL * 1073741824,
                            static_cast<std::uint64_t>(64 * 1073741824.0 * (1 - ram_percent / 100)),
                            {});
        snapshot.gauges[1].value = mode == 0 ? 71 : 98;
        snapshot.gauges[2].value = mode == 0 ? 14 : 0;
        snapshot.gauges[3].value = mode == 0 ? 48 : mode == 1 ? 86 : 94;
        snapshot.gpu_memory_bytes.value =
            snapshot.gauges[3].value * static_cast<double>(snapshot.gpu_memory_capacity) / 100;
        snapshot.disks[0].active.value = mode == 0 ? 38 : mode == 1 ? 92 : 96;
        snapshot.disks[1].active.value = mode == 0 ? 7 : mode == 1 ? 64 : 40;
        const auto rate = [](Metric &metric, double fraction) {
            metric.session_peak = 100e6;
            metric.value = fraction * *metric.session_peak;
        };
        rate(snapshot.disks[0].read, mode == 0 ? .56 : mode == 1 ? .80 : .50);
        rate(snapshot.disks[0].write, mode == 0 ? .12 : mode == 1 ? .62 : .88);
        rate(snapshot.disks[1].read, mode == 0 ? .06 : mode == 1 ? .51 : .55);
        rate(snapshot.disks[1].write, mode == 0 ? .12 : mode == 1 ? .22 : .12);
        rate(snapshot.gauges[4], mode == 0 ? .46 : mode == 1 ? .20 : .12);
        rate(snapshot.gauges[5], mode == 0 ? .18 : mode == 1 ? .09 : .06);
        std::array readings{&snapshot.cpu_temperature, &snapshot.ram_temperature,
                            &snapshot.gpu_temperature, &snapshot.disks[0].temperature,
                            &snapshot.disks[1].temperature};
        for (std::size_t i = 0; i < readings.size(); ++i) {
            readings[i]->metric = {temperatures[mode][i], Status::valid, Unit::celsius};
            readings[i]->sensor = L"Synthetic design comparison only";
        }
        for (auto edge : {Edge::top, Edge::left, Edge::right}) {
            for (unsigned length : {1200U, 1400U}) {
                if (!horizontal(edge) && length == 1200) {
                    continue;
                }
                for (unsigned density : {1U, 2U}) {
                    const UINT width = (horizontal(edge) ? length : 60) * density;
                    const UINT height = (horizontal(edge) ? 60 : length) * density;
                    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
                    checked(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA,
                                              WICBitmapCacheOnLoad, &bitmap));
                    Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
                    const float dpi = 96 * static_cast<float>(density);
                    checked(factory->CreateWicBitmapRenderTarget(
                        bitmap.Get(),
                        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                                     D2D1::PixelFormat(), dpi, dpi),
                        &target));
                    Renderer renderer;
                    checked(renderer.render_to(target.Get(), snapshot, settings, edge, false, 1,
                                               false, false, {}));
                    const auto actual = pixels(bitmap.Get(), width, height);
                    const auto revision = renderer.layout_revision();
                    checked(renderer.render_to(target.Get(), snapshot, settings, edge, false, 1,
                                               false, false, {}));
                    require(renderer.layout_revision() == revision &&
                                pixels(bitmap.Get(), width, height) == actual,
                            "Reference rendering is stable and reuses its layout");
                    const auto *orientation = edge == Edge::top    ? L"top"
                                              : edge == Edge::left ? L"left"
                                                                   : L"right";
                    save(wic, bitmap.Get(),
                         directory / std::format(L"temperature-reference-{}-{}x{}-{}x-{}.png",
                                                 orientation, width / density, height / density,
                                                 density, modes[mode]));
                }
            }
        }
    }
}
void temperature_render_tests(IWICImagingFactory *wic, ID2D1Factory *factory,
                              const std::filesystem::path &directory) {
    using namespace loadbar;
    Microsoft::WRL::ComPtr<IDWriteFactory> write;
    checked(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                reinterpret_cast<IUnknown **>(write.GetAddressOf())));
    EmbeddedFont font;
    checked(font.initialize(write.Get()));
    UINT32 family_index{};
    BOOL exists{};
    checked(font.collection()->FindFamilyName(L"Geist Mono", &family_index, &exists));
    require(exists != FALSE, "Bundled font collection contains Geist Mono");
    Microsoft::WRL::ComPtr<IDWriteFontFamily> family;
    checked(font.collection()->GetFontFamily(family_index, &family));
    for (auto weight : {DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_WEIGHT_MEDIUM}) {
        Microsoft::WRL::ComPtr<IDWriteFont> match;
        checked(family->GetFirstMatchingFont(weight, DWRITE_FONT_STRETCH_NORMAL,
                                             DWRITE_FONT_STYLE_NORMAL, &match));
        require(match->GetWeight() == weight &&
                    match->GetSimulations() == DWRITE_FONT_SIMULATIONS_NONE,
                "Bundled variable font supplies real Regular and Medium weights");
    }
    for (float size : {9.0F, 11.0F, 22.0F}) {
        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
        checked(write->CreateTextFormat(L"Geist Mono", font.collection(), DWRITE_FONT_WEIGHT_MEDIUM,
                                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                                        L"", &format));
        checked(format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
        checked(format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
        for (const std::wstring value : {L"64°C", L"100°C", L"250°C", L"-99°C"}) {
            Microsoft::WRL::ComPtr<IDWriteTextLayout> text;
            checked(write->CreateTextLayout(value.data(), static_cast<UINT32>(value.size()),
                                            format.Get(), 3.1F * size, 14 * size / 11, &text));
            DWRITE_TEXT_METRICS metrics{};
            DWRITE_OVERHANG_METRICS overhang{};
            checked(text->GetMetrics(&metrics));
            checked(text->GetOverhangMetrics(&overhang));
            require(
                metrics.width <= 3.1F * size && metrics.lineCount == 1 && overhang.left <= 0 &&
                    overhang.right <= 0 && overhang.top <= 0 && overhang.bottom <= 0,
                std::format(
                    "Temperature glyph bounds: size {}, width {}, height {}, overhang {}/{}/{}/{}",
                    size, metrics.width, metrics.height, overhang.left, overhang.top,
                    overhang.right, overhang.bottom)
                    .c_str());
        }
    }
    for (auto edge : {Edge::top, Edge::bottom, Edge::left, Edge::right}) {
        for (bool squares : {false, true}) {
            for (float text_scale : {1.0F, 1.5F, 2.25F}) {
                for (float requested : {40.0F, 60.0F, 120.0F}) {
                    auto snapshot = fixture();
                    for (auto *t : {&snapshot.cpu_temperature, &snapshot.ram_temperature,
                                    &snapshot.gpu_temperature, &snapshot.disks[0].temperature,
                                    &snapshot.disks[1].temperature}) {
                        t->metric = {100, Status::valid, Unit::celsius};
                    }
                    Settings settings{.always_show_readout = false};
                    settings.cpu_squares = squares;
                    const float thickness = std::max(
                        requested,
                        static_cast<float>(minimum_thickness(1400, 640, edge, snapshot.processors,
                                                             text_scale, settings, 2)));
                    auto layout = make_snapshot_layout(horizontal(edge) ? 1400 : thickness,
                                                       horizontal(edge) ? thickness : 1400, edge,
                                                       snapshot, text_scale, settings);
                    require(layout.fits, "Thermal glyph test uses a readable layout");
                    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
                    checked(write->CreateTextFormat(
                        L"Geist Mono", font.collection(), DWRITE_FONT_WEIGHT_MEDIUM,
                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                        layout.temperature_font_size, L"", &format));
                    checked(format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
                    checked(format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
                    for (float dpi : {96.0F, 120.0F, 144.0F, 192.0F}) {
                        auto snapped = layout;
                        snap_layout(snapped, dpi);
                        for (const auto &b : snapped.blocks) {
                            if (b.temperature.width == 0) {
                                continue;
                            }
                            for (const std::wstring value :
                                 {L"64°C", L"100°C", L"250°C", L"-99°C"}) {
                                Microsoft::WRL::ComPtr<IDWriteTextLayout> text;
                                checked(write->CreateTextLayout(
                                    value.data(), static_cast<UINT32>(value.size()), format.Get(),
                                    b.temperature.width, b.temperature.height, &text));
                                DWRITE_OVERHANG_METRICS ink{};
                                checked(text->GetOverhangMetrics(&ink));
                                const float y =
                                    b.temperature.y - (horizontal(edge) ? ink.bottom : 0);
                                const float bottom = y + b.temperature.height + ink.bottom;
                                require(y - ink.top >= b.icon.y + b.icon.height &&
                                            y - ink.top >= b.temperature.y - .001F &&
                                            bottom <=
                                                b.temperature.y + b.temperature.height + .001F,
                                        "Actual temperature ink clears the icon and stays in "
                                        "hit/paint bounds");
                                if (horizontal(edge)) {
                                    require(std::abs(bottom - b.graphic.y - b.graphic.height) <
                                                .001F,
                                            "Actual temperature ink ends at the snapped graphic "
                                            "bottom");
                                }
                            }
                        }
                    }
                }
            }
        }
        const bool horizontal = edge == Edge::top || edge == Edge::bottom;
        const UINT width = horizontal ? 1400 : 60, height = horizontal ? 60 : 1400;
        Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
        checked(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA,
                                  WICBitmapCacheOnLoad, &bitmap));
        Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
        checked(factory->CreateWicBitmapRenderTarget(
            bitmap.Get(), D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE), &target));
        Renderer reused;
        auto current = fixture();
        const auto base = current;
        const auto assign = [](TemperatureReading &reading, double value) {
            reading.metric = {value, Status::valid, Unit::celsius};
            reading.sensor = L"Synthetic render fixture";
        };
        std::vector<BYTE> original, normal;
        for (unsigned mode = 0; mode < 6; ++mode) {
            current = base;
            if (mode >= 1 && mode <= 4) {
                assign(current.gpu_temperature, mode == 2 ? 92 : 58);
                assign(current.disks[0].temperature, mode == 2 ? 79 : 49);
                if (mode != 3) {
                    assign(current.cpu_temperature, mode == 2 ? 99 : 64);
                    assign(current.ram_temperature, mode == 2 ? 77 : 46);
                    assign(current.disks[1].temperature, mode == 2 ? 61 : 38);
                }
                if (mode == 4) {
                    for (auto *t : {&current.cpu_temperature, &current.ram_temperature,
                                    &current.gpu_temperature, &current.disks[0].temperature,
                                    &current.disks[1].temperature}) {
                        t->metric.retained = ObservedValue{t->metric.value, {}};
                        t->metric.status = Status::error;
                        t->metric.value = 0;
                    }
                }
            }
            checked(reused.render_to(target.Get(), current, graphics_only(), edge, false, 1, false,
                                     false, {}));
            const auto actual = pixels(bitmap.Get(), width, height);
            if (mode == 0) {
                original = actual;
            }
            if (mode == 1) {
                normal = actual;
                require(actual != original, "Temperature appears without hover");
            }
            if (mode == 4) {
                require(actual == normal, "Retained temperature renders identically");
            }
            if (mode == 5) {
                require(actual == original, "New sensor identity restores original icons");
            }
            const auto revision = reused.layout_revision();
            checked(reused.render_to(target.Get(), current, graphics_only(), edge, false, 1, false,
                                     false, {}));
            require(revision == reused.layout_revision(), "Temperature layout is cached");
            Renderer fresh;
            checked(fresh.render_to(target.Get(), current, graphics_only(), edge, false, 1, false,
                                    false, {}));
            require(actual == pixels(bitmap.Get(), width, height),
                    "Temperature transition equals fresh rendering");
            const wchar_t *names[]{L"unavailable", L"normal",   L"critical",
                                   L"mixed",       L"retained", L"reset"};
            const wchar_t *edges[]{L"automatic", L"left", L"top", L"right", L"bottom"};
            save(wic, bitmap.Get(),
                 directory / std::format(L"temperature-{}-60-{}.png",
                                         edges[static_cast<unsigned>(edge)], names[mode]));
        }
        assign(current.gpu_temperature, 87);
        for (bool hover : {false, true}) {
            checked(reused.render_to(target.Get(), current, graphics_only(), edge, false, 1, hover,
                                     true, {}));
            save(wic, bitmap.Get(),
                 directory / std::format(L"temperature-contrast-{}-{}.png",
                                         static_cast<unsigned>(edge), hover));
        }
        assign(current.gpu_temperature, 58.5);
        checked(reused.render_to(target.Get(), current, graphics_only(), edge, false, 1, false,
                                 false, {}));
        const auto rounded = pixels(bitmap.Get(), width, height);
        assign(current.gpu_temperature, 58.51);
        checked(reused.render_to(target.Get(), current, graphics_only(), edge, false, 1, false,
                                 false, {}));
        require(rounded == pixels(bitmap.Get(), width, height),
                "Outside tint/glow ramps, integer formatting agrees with repaint rounding");
        for (float text_scale : {1.0F, 1.5F, 2.0F}) {
            for (float dpi : {96.0F, 144.0F, 192.0F}) {
                Microsoft::WRL::ComPtr<IWICBitmap> scaled_bitmap;
                const auto scale = dpi / 96;
                const UINT scaled_width =
                    static_cast<UINT>((horizontal ? 1400.0F : 200.0F) * scale);
                const UINT scaled_height =
                    static_cast<UINT>((horizontal ? 200.0F : 1400.0F) * scale);
                checked(wic->CreateBitmap(scaled_width, scaled_height,
                                          GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                          &scaled_bitmap));
                Microsoft::WRL::ComPtr<ID2D1RenderTarget> scaled_target;
                checked(factory->CreateWicBitmapRenderTarget(
                    scaled_bitmap.Get(),
                    D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                                 D2D1::PixelFormat(), dpi, dpi),
                    &scaled_target));
                checked(reused.render_to(scaled_target.Get(), current, graphics_only(), edge, false,
                                         text_scale, false, false, {}));
                Renderer fresh;
                const auto actual = pixels(scaled_bitmap.Get(), scaled_width, scaled_height);
                checked(fresh.render_to(scaled_target.Get(), current, graphics_only(), edge, false,
                                        text_scale, false, false, {}));
                require(actual == pixels(scaled_bitmap.Get(), scaled_width, scaled_height),
                        "Temperature DPI/text-scale transition equals fresh rendering");
            }
        }
    }
}
void temperature_ramp_render_tests(IWICImagingFactory *wic, ID2D1Factory *factory,
                                   const std::filesystem::path &directory) {
    using namespace loadbar;
    constexpr UINT width = 2800, height = 120;
    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
    checked(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                              &bitmap));
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
    checked(factory->CreateWicBitmapRenderTarget(
        bitmap.Get(),
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(), 192,
                                     192),
        &target));
    auto snapshot = fixture();
    Settings settings{.always_show_readout = false};
    settings.cpu_squares = true;
    for (auto &p : snapshot.processors) {
        p.utilization.value = 40;
    }
    for (auto &g : snapshot.gauges) {
        g.value = 40;
    }
    unsigned creations{}, text_creations{};
    Renderer renderer([&](Renderer::ResourceKind kind, unsigned) {
        creations += kind == Renderer::ResourceKind::glow;
        text_creations += kind == Renderer::ResourceKind::text;
        return S_OK;
    });
    const auto render = [&](double value, bool contrast = false) {
        for (auto *t :
             {&snapshot.cpu_temperature, &snapshot.ram_temperature, &snapshot.gpu_temperature,
              &snapshot.disks[0].temperature, &snapshot.disks[1].temperature}) {
            t->metric = {value, Status::valid, Unit::celsius};
        }
        checked(renderer.render_to(target.Get(), snapshot, settings, Edge::top, false, 1, false,
                                   contrast, {}));
        return pixels(bitmap.Get(), width, height);
    };
    const auto icon_pixels = [&](const std::vector<BYTE> &image, Box box) {
        std::vector<BYTE> result;
        for (unsigned y = static_cast<unsigned>(box.y * 2);
             y < static_cast<unsigned>((box.y + box.height) * 2); ++y) {
            const auto start =
                (static_cast<std::size_t>(y) * width + static_cast<unsigned>(box.x * 2)) * 4;
            const auto count = static_cast<std::size_t>(box.width * 2) * 4;
            result.insert(result.end(), image.begin() + static_cast<std::ptrdiff_t>(start),
                          image.begin() + static_cast<std::ptrdiff_t>(start + count));
        }
        return result;
    };
    auto cold = render(59);
    auto edge = render(60);
    auto layout = make_snapshot_layout(1400, 60, Edge::top, snapshot, 1, settings);
    snap_layout(layout, 192);
    for (const auto &b : layout.blocks) {
        require(icon_pixels(cold, b.icon) == icon_pixels(edge, b.icon),
                "All icons retain their original color through 60 C despite number band changes");
    }
    require(creations == 0, "Cool temperatures create no glow resources");
    for (double value : {60.0, 75.0, 90.0, 92.5, 95.0}) {
        const auto actual = render(value);
        save(wic, bitmap.Get(),
             directory / std::format(L"temperature-ramp-top-1400x60-2x-{:.1f}C.png", value));
        Renderer fresh;
        checked(fresh.render_to(target.Get(), snapshot, settings, Edge::top, false, 1, false, false,
                                {}));
        require(actual == pixels(bitmap.Get(), width, height),
                "Thermal ramp transitions equal fresh production rendering");
        if (value == 90) {
            require(creations == 0, "Glow begins above 90 C, not at its zero-strength endpoint");
        }
    }
    const auto built = creations;
    require(built > 0, "Hot thermal icons construct a glow mask");
    const auto tinted = render(80);
    const auto tint_texts = text_creations;
    const auto more_tinted = render(80.4);
    require(tint_texts == text_creations, "Fractional tint updates reuse cached text measurements");
    const auto glowing = render(92);
    const auto glow_texts = text_creations;
    const auto more_glowing = render(92.4);
    require(glow_texts == text_creations, "Fractional glow updates reuse cached text measurements");
    for (const auto &b : layout.blocks) {
        if (b.kind == 4) {
            require(icon_pixels(tinted, b.icon) == icon_pixels(more_glowing, b.icon),
                    "Network does not acquire thermal tint or glow");
            continue;
        }
        require(icon_pixels(tinted, b.icon) != icon_pixels(more_tinted, b.icon),
                "CPU, RAM, GPU and drive icons show fractional tint changes");
        require(icon_pixels(glowing, b.icon) != icon_pixels(more_glowing, b.icon),
                "CPU, RAM, GPU and drive icons show fractional glow changes");
    }
    require(creations == built, "Varying thermal color/intensity reuses cached glow masks");
    const auto contrast = render(90, true);
    require(contrast == render(90.4, true),
            "High contrast ignores thermal effects when the number is unchanged");
}
void temperature_invalidation_tests() {
    using namespace loadbar;
    struct HiddenWindow {
        HWND value = CreateWindowExW(0, L"STATIC", L"Loadbar.TemperatureTest", WS_POPUP, 0, 0, 1400,
                                     60, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        ~HiddenWindow() {
            if (value) {
                DestroyWindow(value);
            }
        }
    } window;
    require(window.value != nullptr, "Hidden redraw test window created");
    Renderer renderer;
    auto before = fixture();
    before.gpu_temperature.metric = {58, Status::valid, Unit::celsius, Clock::now()};
    checked(renderer.paint(window.value, before, graphics_only(), Edge::top, false, 1));
    const auto invalidates = [&](double temperature) {
        auto after = before;
        after.gpu_temperature.metric.value = temperature;
        return renderer.invalidate_changes(window.value, before, after, graphics_only());
    };
    require(!invalidates(58.1), "Sub-degree change avoids a resting repaint");
    require(invalidates(58.5), "Temperature-only integer change repaints");
    before.gpu_temperature.metric.value = 64.9;
    require(invalidates(65.0), "Temperature band change repaints at the same displayed integer");
    before.gpu_temperature.metric.value = 89.9;
    require(invalidates(90.0), "Final tint threshold change repaints");
    for (double value : {60.0, 75.0, 89.0, 90.0, 92.0, 94.0}) {
        before.gpu_temperature.metric.value = value;
        require(invalidates(value + .1), "Sub-degree tint/glow changes repaint the same integer");
        require(!invalidates(value), "Identical thermal appearance avoids repaint");
    }
    before.gpu_temperature.metric.value = 95.0;
    require(!invalidates(95.1), "Saturated glow avoids a repaint for an unchanged integer");
    auto absent = before;
    absent.gpu_temperature = {};
    require(renderer.invalidate_changes(window.value, before, absent, graphics_only()),
            "Temperature removal rebuilds layout");
    Settings disabled{.always_show_readout = false};
    disabled.show_temperatures = false;
    require(renderer.invalidate_changes(window.value, before, before, disabled),
            "Disabling temperatures requests a layout repaint");
    checked(renderer.paint(window.value, before, disabled, Edge::top, false, 1));
    const auto revision = renderer.layout_revision();
    for (auto *snapshot : {&before, &absent}) {
        require(!renderer.invalidate_changes(window.value, before, *snapshot, disabled),
                "Disabled temperature presence or absence never requests a repaint");
        snapshot->cpu_temperature.metric = {97, Status::valid, Unit::celsius, Clock::now()};
        snapshot->gpu_temperature.metric = {97, Status::valid, Unit::celsius, Clock::now()};
        require(!renderer.invalidate_changes(window.value, before, *snapshot, disabled),
                "Disabled temperature values never request a repaint");
        checked(renderer.paint(window.value, *snapshot, disabled, Edge::top, false, 1));
        require(renderer.layout_revision() == revision,
                "Disabled temperature-only changes preserve cached layout");
    }
    require(renderer.invalidate_changes(window.value, before, before, graphics_only()),
            "Re-enabling retained temperatures requests a layout repaint");
}
void temperature_visibility_cache_tests(IWICImagingFactory *wic, ID2D1Factory *factory) {
    using namespace loadbar;
    const auto absent = fixture();
    auto snapshot = absent;
    for (auto *reading :
         {&snapshot.cpu_temperature, &snapshot.ram_temperature, &snapshot.gpu_temperature,
          &snapshot.disks[0].temperature, &snapshot.disks[1].temperature}) {
        reading->metric = {95, Status::valid, Unit::celsius};
    }
    Settings disabled{.always_show_readout = false};
    disabled.show_temperatures = false;
    for (Edge edge : {Edge::top, Edge::bottom, Edge::left, Edge::right}) {
        const UINT width = horizontal(edge) ? 1400 : 60;
        const UINT height = horizontal(edge) ? 60 : 1400;
        Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
        checked(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA,
                                  WICBitmapCacheOnLoad, &bitmap));
        Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
        checked(factory->CreateWicBitmapRenderTarget(
            bitmap.Get(), D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE), &target));
        unsigned temperature_layouts{}, all_layouts{};
        bool reject_temperature{};
        Renderer renderer([&](Renderer::ResourceKind kind, unsigned index) {
            all_layouts += kind == Renderer::ResourceKind::text;
            temperature_layouts += kind == Renderer::ResourceKind::text && index == 4;
            return reject_temperature && kind == Renderer::ResourceKind::text && index == 4
                       ? E_OUTOFMEMORY
                       : S_OK;
        });
        for (bool contrast : {false, true}) {
            checked(renderer.render_to(target.Get(), snapshot, graphics_only(), edge, false, 1,
                                       false, contrast, {}));
            const auto temperature_count = temperature_layouts;
            require(temperature_count >= 5, "All five temperature slots were exercised");
            bool hover_warmed{};
            for (bool hover : {true, false, true, false}) {
                const auto count = all_layouts;
                checked(renderer.render_to(target.Get(), snapshot, graphics_only(), edge, false, 1,
                                           hover, contrast, {}));
                require(temperature_layouts == temperature_count,
                        "Hover transitions reuse every unchanged temperature text layout");
                if (hover_warmed) {
                    require(all_layouts == count,
                            "Rest and hover reuse both independent text caches after warmup");
                }
                hover_warmed = true;
                const auto enabled = pixels(bitmap.Get(), width, height);
                Renderer fresh;
                checked(fresh.render_to(target.Get(), snapshot, graphics_only(), edge, false, 1,
                                        hover, contrast, {}));
                require(enabled == pixels(bitmap.Get(), width, height),
                        "Dedicated temperature cache preserves enabled pixels");
            }
            for (bool hover : {false, true}) {
                const auto count = temperature_layouts;
                checked(renderer.render_to(target.Get(), snapshot, disabled, edge, false, 1, hover,
                                           contrast, {}));
                const auto hidden = pixels(bitmap.Get(), width, height);
                Renderer original;
                checked(original.render_to(target.Get(), absent, graphics_only(), edge, false, 1,
                                           hover, contrast, {}));
                require(hidden == pixels(bitmap.Get(), width, height) &&
                            temperature_layouts == count,
                        "Disabled temperatures match sensor-free rendering without thermal text, "
                        "tint or glow on every edge and hover/contrast mode");
                const auto revision = renderer.layout_revision();
                checked(renderer.render_to(target.Get(), absent, disabled, edge, false, 1, hover,
                                           contrast, {}));
                require(renderer.layout_revision() == revision &&
                            hidden == pixels(bitmap.Get(), width, height),
                        "Disabled retained values do not affect pixels or layout caching");
            }
        }
        reject_temperature = true;
        require(renderer.render_to(target.Get(), snapshot, graphics_only(), edge, false, 1, false,
                                   false, {}) == E_OUTOFMEMORY,
                "Temperature text creation failure reaches resource recovery");
        reject_temperature = false;
        checked(renderer.render_to(target.Get(), snapshot, graphics_only(), edge, false, 1, true,
                                   false, {}));
        const auto recovered = pixels(bitmap.Get(), width, height);
        Renderer fresh;
        checked(fresh.render_to(target.Get(), snapshot, graphics_only(), edge, false, 1, true,
                                false, {}));
        require(recovered == pixels(bitmap.Get(), width, height),
                "Temperature cache recovers completely after failed layout creation");
        const auto count = temperature_layouts;
        renderer.preferences_changed();
        checked(renderer.render_to(target.Get(), snapshot, graphics_only(), edge, false, 1, true,
                                   false, {}));
        require(temperature_layouts == count + 5 &&
                    recovered == pixels(bitmap.Get(), width, height),
                "Font preference invalidation rebuilds all temperature slots without visual drift");
    }
}
void readout_render_tests(IWICImagingFactory *wic, ID2D1Factory *factory,
                          const std::filesystem::path &directory) {
    using namespace loadbar;
    Microsoft::WRL::ComPtr<IDWriteFactory> write;
    checked(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                reinterpret_cast<IUnknown **>(write.GetAddressOf())));
    EmbeddedFont font;
    checked(font.initialize(write.Get()));
    struct TextFit {
        std::wstring_view text;
        float width;
        bool secondary;
    };
    using namespace strip_style;
    const std::array samples{TextFit{L"100%", kCpuReadoutWidth, false},
                             TextFit{L"P100 E100", kHybridReadoutWidth, true},
                             TextFit{L"1023/1023G", kRamReadoutWidth, true},
                             TextFit{L"<0.1/63.4G", kRamReadoutWidth, true},
                             TextFit{L"<0.1B ▶100%", kGpuReadoutWidth, true},
                             TextFit{L"R<0.1B W<0.1B", kDiskReadoutWidth, true},
                             TextFit{L"R1023K W1023K", kDiskReadoutWidth, true},
                             TextFit{L"↓<0.1B", kNetworkReadoutWidth, false},
                             TextFit{L"↑1023K", kNetworkReadoutWidth, true},
                             TextFit{L"—", kCpuReadoutWidth, false}};
    for (const float scale : {1.0F, 1.5F, 2.25F, 6.0F}) {
        for (const auto &sample : samples) {
            const float width = sample.width * scale;
            Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
            checked(write->CreateTextFormat(
                L"Geist Mono", font.collection(),
                sample.secondary ? DWRITE_FONT_WEIGHT_NORMAL : DWRITE_FONT_WEIGHT_MEDIUM,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                (sample.secondary ? 9.0F : 11.0F) * scale, L"", &format));
            checked(format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
            checked(format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
            Microsoft::WRL::ComPtr<IDWriteTextLayout> text;
            checked(write->CreateTextLayout(sample.text.data(),
                                            static_cast<UINT32>(sample.text.size()), format.Get(),
                                            width, 11 * scale, &text));
            DWRITE_TEXT_METRICS metrics{};
            DWRITE_OVERHANG_METRICS overhang{};
            DWRITE_LINE_METRICS line{};
            UINT32 lines{};
            checked(text->GetMetrics(&metrics));
            checked(text->GetOverhangMetrics(&overhang));
            checked(text->GetLineMetrics(&line, 1, &lines));
            const float baseline =
                (sample.secondary ? kReadoutSecondaryBaseline : kReadoutPrimaryBaseline) * scale;
            const float y = baseline - metrics.top - line.baseline;
            require(metrics.width <= width && metrics.lineCount == 1 && overhang.left <= 0 &&
                        overhang.right <= 0 && y - overhang.top >= -0.01F &&
                        y + 11 * scale + overhang.bottom <= kReadoutBand * scale + 0.01F,
                    "Maximum formatted readout fits its column and explicit baseline band");
        }
    }
    auto snapshot = fixture();
    for (auto *t : {&snapshot.cpu_temperature, &snapshot.gpu_temperature,
                    &snapshot.disks[0].temperature, &snapshot.disks[1].temperature}) {
        t->metric = {58, Status::valid, Unit::celsius};
    }
    for (const auto edge : {Edge::top, Edge::bottom, Edge::left, Edge::right}) {
        for (const float length : {1400.0F, 1600.0F}) {
            const std::wstring suffix = length == 1400 ? L"" : L"-1600";
            for (const auto dpi : {96.0F, 144.0F, 192.0F}) {
                for (const bool contrast : {false, true}) {
                    Settings settings;
                    settings.cpu_squares = true;
                    const float width = horizontal(edge) ? length : 60.0F;
                    const float height = horizontal(edge) ? 60.0F : length;
                    const auto pw = static_cast<UINT>(width * dpi / 96);
                    const auto ph = static_cast<UINT>(height * dpi / 96);
                    Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
                    checked(wic->CreateBitmap(pw, ph, GUID_WICPixelFormat32bppPBGRA,
                                              WICBitmapCacheOnLoad, &bitmap));
                    Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
                    auto properties = D2D1::RenderTargetProperties();
                    properties.dpiX = dpi;
                    properties.dpiY = dpi;
                    checked(
                        factory->CreateWicBitmapRenderTarget(bitmap.Get(), properties, &target));
                    unsigned layouts{};
                    Renderer renderer([&](Renderer::ResourceKind kind, unsigned) {
                        layouts += kind == Renderer::ResourceKind::text;
                        return S_OK;
                    });
                    const auto render = [&](const Snapshot &data, const Settings &preferences,
                                            bool hover) {
                        checked(renderer.render_to(target.Get(), data, preferences, edge, false, 1,
                                                   hover, contrast, {}));
                        return pixels(bitmap.Get(), pw, ph);
                    };
                    const auto resting = render(snapshot, settings, false);
                    if (dpi == 96 && !contrast) {
                        bool fail = true;
                        Renderer retry([&](Renderer::ResourceKind kind, unsigned index) {
                            return fail && kind == Renderer::ResourceKind::text && index == 1
                                       ? E_FAIL
                                       : S_OK;
                        });
                        require(retry.render_to(target.Get(), snapshot, settings, edge, false, 1,
                                                false, false, {}) == E_FAIL,
                                "Readout text failure enters normal resource recovery");
                        fail = false;
                        checked(retry.render_to(target.Get(), snapshot, settings, edge, false, 1,
                                                false, false, {}));
                        require(pixels(bitmap.Get(), pw, ph) == resting,
                                "Readout retry restores clip, transform and complete text");
                    }
                    const auto built = layouts;
                    require(built > 6, "Always-on readouts create text without hover");
                    const auto revision = renderer.layout_revision();
                    require(render(snapshot, settings, true) == resting && layouts == built &&
                                renderer.layout_revision() == revision,
                            "Always-on hover reuses layouts and changes no pixels");
                    settings.show_hover_info = false;
                    require(render(snapshot, settings, false) == resting,
                            "Always-on readouts are independent of saved hover preference");
                    auto changed = snapshot;
                    changed.gpu_memory_bytes.value = 11.5 * 1073741824;
                    require(render(changed, settings, false) != resting &&
                                renderer.layout_revision() == revision,
                            "Capacity-only changes refresh readouts without reflow");
                    require(layouts == built + 1,
                            "One changed readout reuses all unrelated text layouts");
                    render(snapshot, settings, false);
                    if (dpi == 96 && !contrast) {
                        save(wic, bitmap.Get(),
                             directory / std::format(L"readout-{}{}-normal.png",
                                                     static_cast<unsigned>(edge), suffix));
                        for (auto &p : changed.processors) {
                            p.utilization.value = 100;
                        }
                        for (auto *t :
                             {&changed.cpu_temperature, &changed.gpu_temperature,
                              &changed.disks[0].temperature, &changed.disks[1].temperature}) {
                            t->metric.value = 95;
                        }
                        for (auto &disk : changed.disks) {
                            disk.active.value = 92;
                        }
                        render(changed, settings, false);
                        save(wic, bitmap.Get(),
                             directory / std::format(L"readout-{}{}-heavy.png",
                                                     static_cast<unsigned>(edge), suffix));
                        auto uniform = snapshot;
                        uniform.processors.resize(4);
                        for (unsigned i = 0; i < 4; ++i) {
                            uniform.processors[i].core = i;
                            uniform.processors[i].efficiency_class = 0;
                        }
                        render(uniform, settings, false);
                        save(wic, bitmap.Get(),
                             directory / std::format(L"readout-{}{}-4-core.png",
                                                     static_cast<unsigned>(edge), suffix));
                    }
                    settings.always_show_readout = false;
                    const auto disabled = render(snapshot, settings, true);
                    Renderer fresh;
                    checked(fresh.render_to(target.Get(), snapshot, settings, edge, false, 1, true,
                                            contrast, {}));
                    require(disabled == pixels(bitmap.Get(), pw, ph),
                            "Disabling readouts equals fresh graphics-only rendering");
                    settings.always_show_readout = true;
                    require(render(snapshot, settings, false) == resting,
                            "Readout off/on restores layout and cached appearance");
                }
            }
        }
    }
}

} // namespace
int wmain(int argc, wchar_t **argv) {
    try {
        require(argc == 2, "Expected output artifact directory");
        ComApartment apartment;
        Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
        checked(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&wic)));
        Microsoft::WRL::ComPtr<ID2D1Factory> factory;
        checked(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()));
        const std::filesystem::path directory(argv[1]);
        std::filesystem::create_directories(directory);
        readout_render_tests(wic.Get(), factory.Get(), directory);
        temperature_render_tests(wic.Get(), factory.Get(), directory);
        temperature_ramp_render_tests(wic.Get(), factory.Get(), directory);
        temperature_reference_previews(wic.Get(), factory.Get(), directory);
        temperature_invalidation_tests();
        temperature_visibility_cache_tests(wic.Get(), factory.Get());
        vertical_edge_render_tests(wic.Get(), factory.Get(), directory);
        preference_render_tests(wic.Get(), factory.Get(), directory);
        uniform_visibility_render_tests(wic.Get(), factory.Get(), directory);
        drive_visibility_render_tests(wic.Get(), factory.Get());
        logical_processor_render_tests(wic.Get(), factory.Get(), directory);
        bounded_glow_tests(wic.Get(), factory.Get());
        contrast_load_regression(wic.Get(), factory.Get());
        contrast_palette_tests(wic.Get(), factory.Get(), directory);
        continuity_render_tests(wic.Get(), factory.Get(), directory);
        scaling_render_tests(wic.Get(), factory.Get(), directory);
        unsigned rendered{};
        for (unsigned dpi : {96U, 120U, 144U, 192U}) {
            for (unsigned variant = 0; variant < 4; ++variant) {
                const bool horizontal = variant != 3;
                const unsigned dip_width = variant == 0   ? 506U
                                           : variant == 1 ? 800U
                                           : variant == 2 ? 1274U
                                                          : 140U;
                const unsigned dip_height =
                    horizontal ? static_cast<unsigned>(loadbar::minimum_thickness(
                                     static_cast<float>(dip_width), 480, loadbar::Edge::top,
                                     fixture().processors, 1, graphics_only(), 2))
                               : 1000U;
                require(dip_height <= 1000, "Fixture layout must fit");
                const unsigned width = (dip_width * dpi + 95) / 96,
                               height = (dip_height * dpi + 95) / 96;
                Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
                checked(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA,
                                          WICBitmapCacheOnLoad, &bitmap));
                Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
                checked(factory->CreateWicBitmapRenderTarget(
                    bitmap.Get(),
                    D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                                 D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                   D2D1_ALPHA_MODE_PREMULTIPLIED),
                                                 static_cast<float>(dpi), static_cast<float>(dpi)),
                    &target));
                unsigned glow_creations{}, text_creations{};
                loadbar::Renderer renderer([&](loadbar::Renderer::ResourceKind kind, unsigned) {
                    glow_creations += kind == loadbar::Renderer::ResourceKind::glow;
                    text_creations += kind == loadbar::Renderer::ResourceKind::text;
                    return S_OK;
                });
                auto snapshot = fixture();
                loadbar::Settings settings{.always_show_readout = false};
                checked(renderer.render_to(target.Get(), snapshot, settings,
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           false, 1, false, false, {}));
                const auto resting = pixels(bitmap.Get(), width, height);
                auto layout =
                    loadbar::make_layout(target->GetSize().width, target->GetSize().height,
                                         (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                         snapshot.processors, 1, settings, snapshot.disks.size());
                loadbar::snap_layout(layout, static_cast<float>(dpi));
                require(layout.fits && layout.blocks.size() == 6,
                        "Actual renderer size includes both disks");
                const auto initial_glows = glow_creations;
                const auto revision = renderer.layout_revision();
                checked(renderer.render_to(target.Get(), snapshot, settings,
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           false, 1, true, false, {}));
                const auto initial_text = text_creations;
                require(initial_glows > 0 && initial_text > 0,
                        "Optimization fixture exercises glow and text resources");
                checked(renderer.render_to(target.Get(), snapshot, settings,
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           false, 1, false, false, {}));
                checked(renderer.render_to(target.Get(), snapshot, settings,
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           false, 1, true, false, {}));
                require(text_creations == initial_text && glow_creations == initial_glows,
                        "Rest/hover transitions reuse text layouts and glow masks");
                require(renderer.layout_revision() == revision,
                        "Unchanged topology and dimensions reuse layout");
                for (std::size_t i = 0; i < layout.cores.size(); ++i) {
                    const auto b = layout.cores[i].label;
                    require(renderer.tooltip_target(b.x + b.width / 2, b.y + b.height / 2) == i + 1,
                            "Tooltip target identifies each core independently");
                }
                for (std::size_t i = 1; i < layout.blocks.size(); ++i) {
                    const auto b = layout.blocks[i].bounds;
                    require(renderer.tooltip_target(b.x + 1, b.y + 1) ==
                                layout.cores.size() + i + 1,
                            "Tooltip target identifies every device block");
                }
                require(renderer.tooltip_target(-1, -1) == 0,
                        "Tooltip background has a separate target");
                checked(renderer.render_to(target.Get(), snapshot, settings,
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           false, 1, false, false, {}));
                require(pixels(bitmap.Get(), width, height) == resting,
                        "Cache reuse preserves resting pixels");
                if (dpi == 96 && variant == 2) {
                    for (double value : {0.0, 50.0, 100.0}) {
                        auto solid = snapshot;
                        for (auto &cpu : solid.processors) {
                            cpu.utilization.value = value;
                        }
                        checked(renderer.render_to(
                            target.Get(), solid, settings,
                            (horizontal ? loadbar::Edge::top : loadbar::Edge::right), false, 1,
                            false, false, {}));
                        const auto actual = pixels(bitmap.Get(), width, height);
                        const auto cell = layout.cores[0].label;
                        const auto color = loadbar::heat_color(value);
                        for (float position : {0.25F, 0.5F, 0.75F}) {
                            const auto x = static_cast<unsigned>(cell.x + cell.width * position);
                            const auto y = static_cast<unsigned>(cell.y + cell.height / 2);
                            const auto offset = (static_cast<std::size_t>(y) * width + x) * 4;
                            require(std::abs(static_cast<int>(actual[offset]) -
                                             static_cast<int>(std::round(color.b * 255))) <= 1 &&
                                        std::abs(static_cast<int>(actual[offset + 1]) -
                                                 static_cast<int>(std::round(color.g * 255))) <=
                                            1 &&
                                        std::abs(static_cast<int>(actual[offset + 2]) -
                                                 static_cast<int>(std::round(color.r * 255))) <= 1,
                                    "Whole CPU rectangle has load color at 0/50/100%, without an "
                                    "internal fill bar");
                        }
                    }
                    // Test production percentage and throughput pixels independently of model
                    // assertions.
                    for (double percent : {0.0, 50.0, 100.0}) {
                        auto bars = snapshot;
                        for (auto &disk : bars.disks) {
                            disk.active.value = percent;
                            disk.read.value = *disk.read.session_peak * percent / 100;
                            disk.write.value = *disk.write.session_peak * percent / 100;
                        }
                        checked(renderer.render_to(
                            target.Get(), bars, settings,
                            (horizontal ? loadbar::Edge::top : loadbar::Edge::right), false, 1,
                            false, false, {}));
                        const auto actual = pixels(bitmap.Get(), width, height);
                        for (std::size_t block : {3U, 4U}) {
                            require(layout.blocks[block].types ==
                                        std::array{loadbar::Gauge::disk_active,
                                                   loadbar::Gauge::disk_read,
                                                   loadbar::Gauge::disk_write},
                                    "Each disk draws active/read/write in that order");
                            for (std::size_t metric = 0; metric < 3; ++metric) {
                                const auto meter = layout.blocks[block].meters[metric];
                                const auto palette =
                                    loadbar::gauge_palette(layout.blocks[block].types[metric]);
                                for (float position : {0.25F, 0.75F}) {
                                    const auto color =
                                        position < percent / 100
                                            ? loadbar::tone(palette.track, palette.hue,
                                                            percent / 100)
                                            : palette.track;
                                    const auto x =
                                        static_cast<unsigned>(meter.x + meter.width * position);
                                    const auto y =
                                        static_cast<unsigned>(meter.y + meter.height / 2);
                                    const auto offset =
                                        (static_cast<std::size_t>(y) * width + x) * 4;
                                    require(
                                        std::abs(static_cast<int>(actual[offset]) -
                                                 static_cast<int>(std::round(color.b * 255))) <=
                                                1 &&
                                            std::abs(static_cast<int>(actual[offset + 1]) -
                                                     static_cast<int>(std::round(color.g * 255))) <=
                                                1 &&
                                            std::abs(static_cast<int>(actual[offset + 2]) -
                                                     static_cast<int>(std::round(color.r * 255))) <=
                                                1,
                                        "Active percentage and independent rate peaks render "
                                        "0/50/100% fills");
                                }
                            }
                        }
                    }
                    checked(
                        renderer.render_to(target.Get(), snapshot, settings,
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           false, 1, false, false, {}));
                }
                if (dpi == 96 && variant == 0) {
                    for (auto kind : {loadbar::Renderer::ResourceKind::icon,
                                      loadbar::Renderer::ResourceKind::font,
                                      loadbar::Renderer::ResourceKind::glow}) {
                        for (unsigned failure_index = 0;
                             failure_index < (kind == loadbar::Renderer::ResourceKind::icon   ? 5U
                                              : kind == loadbar::Renderer::ResourceKind::font ? 5U
                                                                                              : 1U);
                             ++failure_index) {
                            bool failed = false;
                            loadbar::Renderer retry(
                                [&](loadbar::Renderer::ResourceKind stage, unsigned index) {
                                    if (!failed && stage == kind &&
                                        (kind == loadbar::Renderer::ResourceKind::glow ||
                                         index == failure_index)) {
                                        failed = true;
                                        return E_OUTOFMEMORY;
                                    }
                                    return S_OK;
                                });
                            require(retry.render_to(
                                        target.Get(), snapshot, settings,
                                        (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                        false, 1, false, false, {}) == E_OUTOFMEMORY,
                                    "Injected creation failure retains native HRESULT");
                            checked(retry.render_to(
                                target.Get(), snapshot, settings,
                                (horizontal ? loadbar::Edge::top : loadbar::Edge::right), false, 1,
                                false, false, {}));
                            require(pixels(bitmap.Get(), width, height) == resting,
                                    "Partial resource creation retry fully reconstructs rendering");
                        }
                    }
                }
                save(wic.Get(), bitmap.Get(),
                     directory / std::format(L"{}-{}-rest.png", dpi, variant));
                ++rendered;
                renderer.discard();
                checked(renderer.render_to(target.Get(), snapshot, settings,
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           false, 1, false, false, {}));
                require(pixels(bitmap.Get(), width, height) == resting,
                        "Discard/recreate changes stable rendering");
                checked(renderer.render_to(target.Get(), snapshot, settings,
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           false, 1, true, false, {}));
                require(pixels(bitmap.Get(), width, height) != resting,
                        "Hover values must change pixels");
                if (dpi == 96 && variant == 2) {
                    const auto original_hover = pixels(bitmap.Get(), width, height);
                    auto capacity_changed = snapshot;
                    capacity_changed.ram_used_bytes.value *= 2;
                    capacity_changed.ram_total_bytes *= 2;
                    checked(
                        renderer.render_to(target.Get(), capacity_changed, settings,
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           false, 1, true, false, {}));
                    require(
                        pixels(bitmap.Get(), width, height) != original_hover,
                        "RAM capacity changes hover pixels while the percentage stays constant");
                    checked(
                        renderer.render_to(target.Get(), capacity_changed, settings,
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           false, 1, false, false, {}));
                    require(pixels(bitmap.Get(), width, height) == resting,
                            "RAM capacity changes no resting graphics at the same percentage");
                    checked(
                        renderer.render_to(target.Get(), snapshot, settings,
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           false, 1, true, false, {}));
                }
                if (dpi == 96 && variant == 1) {
                    const auto narrow_hover = pixels(bitmap.Get(), width, height);
                    auto oversized_capacity = snapshot;
                    oversized_capacity.ram_used_bytes.value *= 1000;
                    oversized_capacity.ram_total_bytes *= 1000;
                    checked(
                        renderer.render_to(target.Get(), oversized_capacity, settings,
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           false, 1, true, false, {}));
                    const auto ellipsis = pixels(bitmap.Get(), width, height);
                    require(ellipsis != narrow_hover,
                            "Oversized RAM capacity selects explicit ellipsis fallback");
                    save(wic.Get(), bitmap.Get(), directory / L"96-ram-ellipsis-hover.png");
                    ++rendered;
                    oversized_capacity.ram_used_bytes.value *= 2;
                    oversized_capacity.ram_total_bytes *= 2;
                    checked(
                        renderer.render_to(target.Get(), oversized_capacity, settings,
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           false, 1, true, false, {}));
                    require(pixels(bitmap.Get(), width, height) == ellipsis,
                            "Too-wide capacities keep the same ellipsis plus unchanged percentage");
                    const auto ram_box = layout.blocks[1].bounds;
                    const auto tip = loadbar::metric_tooltip(layout, ram_box.x + 1, ram_box.y + 1,
                                                             oversized_capacity, settings, {});
                    require(tip.find(L"94000.0 GB / 126800.0 GB") != std::wstring::npos &&
                                tip.find(L"74.1%") != std::wstring::npos,
                            "Ellipsized RAM retains full truthful values in the tooltip");
                    checked(
                        renderer.render_to(target.Get(), snapshot, settings,
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           false, 1, true, false, {}));
                }
                save(wic.Get(), bitmap.Get(),
                     directory / std::format(L"{}-{}-hover.png", dpi, variant));
                ++rendered;
                checked(renderer.render_to(target.Get(), snapshot, settings,
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           false, 1, true, true, {}));
                save(wic.Get(), bitmap.Get(),
                     directory / std::format(L"{}-{}-contrast.png", dpi, variant));
                ++rendered;
                for (std::size_t i = 0; i < snapshot.processors.size(); ++i) {
                    snapshot.processors[i].utilization.status =
                        static_cast<loadbar::Status>(1 + i % 4);
                }
                for (std::size_t i = 0; i < loadbar::kGaugeCount; ++i) {
                    snapshot.gauges[i].status = static_cast<loadbar::Status>(1 + i % 4);
                }
                for (auto &disk : snapshot.disks) {
                    disk.read.status = loadbar::Status::unavailable;
                    disk.write.status = loadbar::Status::error;
                }
                checked(renderer.render_to(target.Get(), snapshot, settings,
                                           (horizontal ? loadbar::Edge::top : loadbar::Edge::right),
                                           true, 1, false, false, {}));
                save(wic.Get(), bitmap.Get(),
                     directory / std::format(L"{}-{}-missing.png", dpi, variant));
                ++rendered;
            }
        }
        std::cout << rendered
                  << " offscreen fixtures rendered through production code; hover and resource "
                     "reconstruction passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
