#include "model/session_peaks.hpp"
#include "ui/renderer.hpp"

#include <cmath>
#include <filesystem>
#include <format>
#include <iostream>
#include <random>
#include <stdexcept>
#include <wincodec.h>

namespace {
void checked(HRESULT result) {
    if (FAILED(result)) {
        throw std::runtime_error(
            std::format("Frame rendering failed: 0x{:08X}", static_cast<unsigned long>(result)));
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
loadbar::Snapshot fixture(unsigned tick) {
    using namespace loadbar;
    Snapshot snapshot;
    const auto now = Clock::time_point{} + std::chrono::seconds(tick);
    snapshot.timestamp = now;
    const auto metric = [now](double value, Unit unit = Unit::percent) {
        return Metric{value, Status::valid,           unit,
                      now,   std::chrono::seconds(1), L"Synthetic documentation fixture"};
    };
    constexpr std::array activity{12., 24., 44., 68., 88., 73., 42., 18.};
    // One logical processor per core: exactly eight P tiles and sixteen E tiles.
    // A fixed seed makes independent per-tile variation reproducible in both modes.
    std::mt19937 random(0x4C6F6164U + tick);
    for (unsigned i = 0; i < 24; ++i) {
        snapshot.processors.push_back(
            {{0, i}, i, true, metric(static_cast<double>(random() % 101)), i < 8 ? 1U : 0U});
    }
    constexpr auto gib = 1073741824ULL;
    set_physical_memory(snapshot, 64 * gib, (35 - tick % 4) * gib, now);
    snapshot.cpu_temperature.metric = metric(66 + tick % 8, Unit::celsius);
    snapshot.cpu_temperature.sensor = L"Synthetic CPU package";
    snapshot.gpu_id = L"synthetic-gpu";
    snapshot.gpu_label = L"Example GPU (synthetic)";
    snapshot.gpu_memory_label = L"Dedicated GPU memory";
    snapshot.gauges[1] = metric(activity[(tick + 2) % activity.size()]);
    snapshot.gauges[2] = metric(activity[(tick + 5) % activity.size()] / 2);
    snapshot.gauges[3] = metric(50);
    snapshot.gpu_memory_capacity = 16 * gib;
    snapshot.gpu_memory_bytes = metric(8.0 * gib, Unit::bytes);
    snapshot.gpu_temperature.metric = metric(58 + tick % 6, Unit::celsius);
    snapshot.gpu_temperature.sensor = L"Synthetic GPU main sensor";
    snapshot.network_id = L"synthetic-network";
    snapshot.network_label = L"Example network (synthetic)";
    snapshot.gauges[4] =
        metric(activity[tick % activity.size()] * 1024 * 1024, Unit::bytes_per_second);
    snapshot.gauges[5] =
        metric(activity[(tick + 3) % activity.size()] * 128 * 1024, Unit::bytes_per_second);
    snapshot.gauges[4].identity = snapshot.gauges[5].identity = snapshot.network_id;
    for (unsigned disk = 0; disk < 2; ++disk) {
        snapshot.disks.push_back({std::format(L"synthetic-disk-{}", disk),
                                  std::format(L"Example disk {} (synthetic)", disk + 1),
                                  metric(activity[(tick + disk) % activity.size()] * 1024 * 1024,
                                         Unit::bytes_per_second),
                                  metric(activity[(tick + disk + 4) % activity.size()] * 256 * 1024,
                                         Unit::bytes_per_second),
                                  metric(activity[(tick + disk + 2) % activity.size()])});
        auto &reading = snapshot.disks.back();
        reading.read.identity = reading.write.identity = reading.active.identity = reading.id;
        reading.temperature.metric = metric(42 + disk * 5 + tick % 3, Unit::celsius);
        reading.temperature.sensor = L"Synthetic drive composite";
    }
    return snapshot;
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("Usage: loadbar_readme_frames <output-directory>");
        }
        const std::filesystem::path directory(argv[1]);
        std::filesystem::create_directories(directory);
        const ComApartment apartment;
        Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
        checked(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&wic)));
        Microsoft::WRL::ComPtr<ID2D1Factory> factory;
        checked(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()));
        loadbar::SessionPeaks peaks;
        std::array<loadbar::Renderer, 2> renderers;
        constexpr unsigned width = 1920, height = 60;
        constexpr std::array modes{L"graphics-only", L"readout"};
        for (const auto *mode : modes) {
            std::filesystem::create_directories(directory / mode);
        }
        Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
        checked(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA,
                                  WICBitmapCacheOnLoad, &bitmap));
        Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
        checked(factory->CreateWicBitmapRenderTarget(
            bitmap.Get(),
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(), 96,
                                         96),
            &target));
        for (unsigned tick = 0; tick < 24; ++tick) {
            auto snapshot = fixture(tick);
            peaks.observe(snapshot);
            const auto valid_rate = [](const loadbar::Metric &value) {
                if (value.status != loadbar::Status::valid || !value.session_peak) {
                    throw std::runtime_error("Synthetic rate/peak fixture invalid");
                }
            };
            valid_rate(snapshot.gauges[4]);
            valid_rate(snapshot.gauges[5]);
            for (const auto &disk : snapshot.disks) {
                valid_rate(disk.read);
                valid_rate(disk.write);
            }
            std::array<loadbar::Layout, 2> layouts;
            for (std::size_t mode = 0; mode < modes.size(); ++mode) {
                loadbar::Settings settings{.always_show_readout = mode != 0};
                settings.cpu_squares = true;
                settings.edge = loadbar::Edge::top;
                settings.thickness = height;
                layouts[mode] = loadbar::make_snapshot_layout(width, height, loadbar::Edge::top,
                                                              snapshot, 1, settings);
                const auto &layout = layouts[mode];
                if (!layout.fits || std::abs(layout.content_scale - 1.5F) > .001F ||
                    layout.cores.size() != 24 || layout.blocks.size() != 6) {
                    throw std::runtime_error("Documentation pair must fit at full 60-DIP scale");
                }
                unsigned p_tiles{}, e_tiles{};
                for (const auto &core : layout.cores) {
                    const auto &processor = snapshot.processors.at(core.processor);
                    if (processor.efficiency_class == 1U) {
                        ++p_tiles;
                    } else if (processor.efficiency_class == 0U) {
                        ++e_tiles;
                    }
                }
                if (p_tiles != 8 || e_tiles != 16) {
                    throw std::runtime_error("Documentation CPU must show 8 P and 16 E tiles");
                }
                checked(renderers[mode].render_to(target.Get(), snapshot, settings,
                                                  loadbar::Edge::top, false, 1, false, false,
                                                  snapshot.timestamp));
                save(wic.Get(), bitmap.Get(),
                     directory / modes[mode] / std::format(L"frame-{:02}.png", tick));
            }
            for (std::size_t i = 0; i < layouts[0].cores.size(); ++i) {
                const auto &a = layouts[0].cores[i];
                const auto &b = layouts[1].cores[i];
                if (a.processor != b.processor || a.label.width != b.label.width ||
                    a.label.height != b.label.height || a.label.width != a.label.height) {
                    throw std::runtime_error("Readout modes must preserve CPU tile scale");
                }
            }
            for (std::size_t i = 0; i < layouts[0].blocks.size(); ++i) {
                const auto &a = layouts[0].blocks[i];
                const auto &b = layouts[1].blocks[i];
                const bool has_temperature = a.kind == 0 || a.kind == 2 || a.kind == 3;
                if (a.kind != b.kind || a.icon.width != b.icon.width ||
                    a.icon.height != b.icon.height || a.graphic.height != b.graphic.height ||
                    a.temperature.height != b.temperature.height ||
                    (a.temperature.width > 0) != has_temperature || a.readout.width != 0 ||
                    b.readout.width <= 0) {
                    throw std::runtime_error("Readout pair must retain icon/thermal/meter scale");
                }
            }
        }
        std::cout << "Two matching 24-frame sequences at 1920x60 / 96 DPI / 1.5 content scale; "
                     "production renderer, synthetic readings, no HWND/providers\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
