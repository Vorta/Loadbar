#include "model/continuity.hpp"
#include "ui/renderer.hpp"
#include <cmath>
#include <filesystem>
#include <format>
#include <iostream>
#include <stdexcept>
#include <wincodec.h>

namespace {
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
        checked(renderer.render_to(target.Get(), snapshot, {}, true, false, 1, false, true, {}));
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
            auto layout = make_layout(target->GetSize().width, target->GetSize().height, horizontal,
                                      snapshot.processors, 1, {}, snapshot.disks.size());
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
                checked(renderer.render_to(target.Get(), snapshot, {}, horizontal, false, 1, hover,
                                           contrast, {}));
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
        checked(drawing.render_to(target.Get(), snapshot, {}, true, false, 1, hover, contrast, {}));
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
        checked(renderer.render_to(target.Get(), sample, {}, horizontal, false, text_scale, hover,
                                   contrast, {}));
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
            const auto minimum = static_cast<float>(minimum_thickness(
                1080, 480, false, snapshot.processors, text_scale, {}, snapshot.disks.size()));
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
    const auto small_layout = make_layout(3840, 40, true, snapshot.processors, 1, {}, 2);
    const auto large_layout = make_layout(3840, 80, true, snapshot.processors, 1, {}, 2);
    const auto small_ink = ink_height(compact_pixels, small_layout.blocks[0].graphic);
    const auto large_ink = ink_height(enlarged_pixels, large_layout.blocks[0].graphic);
    require(small_ink > 0 && large_ink >= small_ink * 3 / 2,
            "Hover font ink grows along with the larger widget");
    // Reuse one target and existing fonts, then fail each replacement font in turn.
    for (unsigned stage = 0; stage < 4; ++stage) {
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
        checked(retry.render_to(target.Get(), snapshot, {}, true, false, 1, true, false, {}));
        const auto before = make_layout(1000, 400, true, snapshot.processors, 1, {}, 2);
        const auto after = make_layout(1000, 400, true, snapshot.processors, 2.25F, {}, 2);
        require(before.fits && after.fits && before.content_scale != after.content_scale,
                "Font retry fixture changes effective scale on the same target");
        armed = true;
        require(retry.render_to(target.Get(), snapshot, {}, true, false, 2.25F, true, false, {}) ==
                    E_OUTOFMEMORY,
                "Font failure during scale change preserves native error");
        armed = false;
        checked(retry.render_to(target.Get(), snapshot, {}, true, false, 2.25F, true, false, {}));
        Renderer fresh;
        require(pixels(bitmap.Get(), 1000, 400) ==
                    render(fresh, snapshot, 1000, 400, true, 2.25F, 96, true, false, false),
                "Font replacement retries atomically after an effective-scale change");
        save(wic, bitmap.Get(), directory / std::format(L"scaling-{}.png", artifact++));
    }
    std::cout << artifact << " scaling fixtures passed\n";
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
        contrast_load_regression(wic.Get(), factory.Get());
        contrast_palette_tests(wic.Get(), factory.Get(), directory);
        continuity_render_tests(wic.Get(), factory.Get(), directory);
        scaling_render_tests(wic.Get(), factory.Get(), directory);
        unsigned rendered{};
        for (unsigned dpi : {96U, 120U, 144U, 192U}) {
            for (unsigned variant = 0; variant < 4; ++variant) {
                const bool horizontal = variant != 3;
                const unsigned dip_width = variant == 0   ? 506U
                                           : variant == 1 ? 618U
                                           : variant == 2 ? 1274U
                                                          : 140U;
                const unsigned dip_height = horizontal
                                                ? static_cast<unsigned>(loadbar::minimum_thickness(
                                                      static_cast<float>(dip_width), 480, true,
                                                      fixture().processors, 1, {}, 2))
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
                loadbar::Settings settings;
                checked(renderer.render_to(target.Get(), snapshot, settings, horizontal, false, 1,
                                           false, false, {}));
                const auto resting = pixels(bitmap.Get(), width, height);
                auto layout = loadbar::make_layout(
                    target->GetSize().width, target->GetSize().height, horizontal,
                    snapshot.processors, 1, settings, snapshot.disks.size());
                loadbar::snap_layout(layout, static_cast<float>(dpi));
                require(layout.fits && layout.blocks.size() == 6,
                        "Actual renderer size includes both disks");
                const auto initial_glows = glow_creations;
                const auto revision = renderer.layout_revision();
                checked(renderer.render_to(target.Get(), snapshot, settings, horizontal, false, 1,
                                           true, false, {}));
                const auto initial_text = text_creations;
                require(initial_glows > 0 && initial_text > 0,
                        "Optimization fixture exercises glow and text resources");
                checked(renderer.render_to(target.Get(), snapshot, settings, horizontal, false, 1,
                                           false, false, {}));
                checked(renderer.render_to(target.Get(), snapshot, settings, horizontal, false, 1,
                                           true, false, {}));
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
                checked(renderer.render_to(target.Get(), snapshot, settings, horizontal, false, 1,
                                           false, false, {}));
                require(pixels(bitmap.Get(), width, height) == resting,
                        "Cache reuse preserves resting pixels");
                if (dpi == 96 && variant == 2) {
                    for (double value : {0.0, 50.0, 100.0}) {
                        auto solid = snapshot;
                        for (auto &cpu : solid.processors) {
                            cpu.utilization.value = value;
                        }
                        checked(renderer.render_to(target.Get(), solid, settings, horizontal, false,
                                                   1, false, false, {}));
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
                        checked(renderer.render_to(target.Get(), bars, settings, horizontal, false,
                                                   1, false, false, {}));
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
                    checked(renderer.render_to(target.Get(), snapshot, settings, horizontal, false,
                                               1, false, false, {}));
                }
                if (dpi == 96 && variant == 0) {
                    for (auto kind : {loadbar::Renderer::ResourceKind::icon,
                                      loadbar::Renderer::ResourceKind::font,
                                      loadbar::Renderer::ResourceKind::glow}) {
                        for (unsigned failure_index = 0;
                             failure_index < (kind == loadbar::Renderer::ResourceKind::icon   ? 5U
                                              : kind == loadbar::Renderer::ResourceKind::font ? 4U
                                                                                              : 1U);
                             ++failure_index) {
                            bool failed = false;
                            loadbar::Renderer retry(
                                [&](loadbar::Renderer::ResourceKind stage, unsigned index) {
                                    if (!failed && stage == kind && index == failure_index) {
                                        failed = true;
                                        return E_OUTOFMEMORY;
                                    }
                                    return S_OK;
                                });
                            require(retry.render_to(target.Get(), snapshot, settings, horizontal,
                                                    false, 1, false, false, {}) == E_OUTOFMEMORY,
                                    "Injected creation failure retains native HRESULT");
                            checked(retry.render_to(target.Get(), snapshot, settings, horizontal,
                                                    false, 1, false, false, {}));
                            require(pixels(bitmap.Get(), width, height) == resting,
                                    "Partial resource creation retry fully reconstructs rendering");
                        }
                    }
                }
                save(wic.Get(), bitmap.Get(),
                     directory / std::format(L"{}-{}-rest.png", dpi, variant));
                ++rendered;
                renderer.discard();
                checked(renderer.render_to(target.Get(), snapshot, settings, horizontal, false, 1,
                                           false, false, {}));
                require(pixels(bitmap.Get(), width, height) == resting,
                        "Discard/recreate changes stable rendering");
                checked(renderer.render_to(target.Get(), snapshot, settings, horizontal, false, 1,
                                           true, false, {}));
                require(pixels(bitmap.Get(), width, height) != resting,
                        "Hover values must change pixels");
                if (dpi == 96 && variant == 2) {
                    const auto original_hover = pixels(bitmap.Get(), width, height);
                    auto capacity_changed = snapshot;
                    capacity_changed.ram_used_bytes.value *= 2;
                    capacity_changed.ram_total_bytes *= 2;
                    checked(renderer.render_to(target.Get(), capacity_changed, settings, horizontal,
                                               false, 1, true, false, {}));
                    require(
                        pixels(bitmap.Get(), width, height) != original_hover,
                        "RAM capacity changes hover pixels while the percentage stays constant");
                    checked(renderer.render_to(target.Get(), capacity_changed, settings, horizontal,
                                               false, 1, false, false, {}));
                    require(pixels(bitmap.Get(), width, height) == resting,
                            "RAM capacity changes no resting graphics at the same percentage");
                    checked(renderer.render_to(target.Get(), snapshot, settings, horizontal, false,
                                               1, true, false, {}));
                }
                if (dpi == 96 && variant == 1) {
                    const auto narrow_hover = pixels(bitmap.Get(), width, height);
                    auto oversized_capacity = snapshot;
                    oversized_capacity.ram_used_bytes.value *= 1000;
                    oversized_capacity.ram_total_bytes *= 1000;
                    checked(renderer.render_to(target.Get(), oversized_capacity, settings,
                                               horizontal, false, 1, true, false, {}));
                    const auto ellipsis = pixels(bitmap.Get(), width, height);
                    require(ellipsis != narrow_hover,
                            "Oversized RAM capacity selects explicit ellipsis fallback");
                    save(wic.Get(), bitmap.Get(), directory / L"96-ram-ellipsis-hover.png");
                    ++rendered;
                    oversized_capacity.ram_used_bytes.value *= 2;
                    oversized_capacity.ram_total_bytes *= 2;
                    checked(renderer.render_to(target.Get(), oversized_capacity, settings,
                                               horizontal, false, 1, true, false, {}));
                    require(pixels(bitmap.Get(), width, height) == ellipsis,
                            "Too-wide capacities keep the same ellipsis plus unchanged percentage");
                    const auto ram_box = layout.blocks[1].bounds;
                    const auto tip = loadbar::metric_tooltip(layout, ram_box.x + 1, ram_box.y + 1,
                                                             oversized_capacity, settings, {});
                    require(tip.find(L"94000.0/126800.0GB 74%") != std::wstring::npos,
                            "Ellipsized RAM retains full truthful values in the tooltip");
                    checked(renderer.render_to(target.Get(), snapshot, settings, horizontal, false,
                                               1, true, false, {}));
                }
                save(wic.Get(), bitmap.Get(),
                     directory / std::format(L"{}-{}-hover.png", dpi, variant));
                ++rendered;
                checked(renderer.render_to(target.Get(), snapshot, settings, horizontal, false, 1,
                                           true, true, {}));
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
                checked(renderer.render_to(target.Get(), snapshot, settings, horizontal, true, 1,
                                           false, false, {}));
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
