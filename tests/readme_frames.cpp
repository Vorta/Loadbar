#include "model/session_peaks.hpp"
#include "ui/renderer.hpp"

#include <filesystem>
#include <format>
#include <iostream>
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
    // Match the supplied infographic: eight SMT P-cores and sixteen single-thread E-cores.
    for (unsigned i = 0; i < 32; ++i) {
        snapshot.processors.push_back({{0, i},
                                       i < 16 ? i / 2 : i - 8,
                                       true,
                                       metric(activity[(tick + i / 3) % activity.size()]),
                                       i < 16 ? 1U : 0U});
    }
    constexpr auto gib = 1073741824ULL;
    set_physical_memory(snapshot, 64 * gib, (35 - tick % 4) * gib, now);
    snapshot.gpu_id = L"synthetic-gpu";
    snapshot.gpu_label = L"Example GPU (synthetic)";
    snapshot.gpu_memory_label = L"Dedicated GPU memory";
    snapshot.gauges[1] = metric(activity[(tick + 2) % activity.size()]);
    snapshot.gauges[2] = metric(activity[(tick + 5) % activity.size()] / 2);
    snapshot.gauges[3] = metric(50);
    snapshot.gpu_memory_capacity = 16 * gib;
    snapshot.gpu_memory_bytes = metric(8.0 * gib, Unit::bytes);
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
        loadbar::Renderer renderer;
        for (unsigned tick = 0; tick < 24; ++tick) {
            constexpr unsigned height = 60;
            Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
            checked(wic->CreateBitmap(1400, height, GUID_WICPixelFormat32bppPBGRA,
                                      WICBitmapCacheOnLoad, &bitmap));
            Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
            checked(factory->CreateWicBitmapRenderTarget(
                bitmap.Get(),
                D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(),
                                             96, 96),
                &target));
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
            loadbar::Settings settings;
            settings.edge = loadbar::Edge::top;
            settings.thickness = height;
            checked(renderer.render_to(target.Get(), snapshot, settings, true, false, 1, false,
                                       false, snapshot.timestamp));
            save(wic.Get(), bitmap.Get(), directory / std::format(L"frame-{:02}.png", tick));
        }
        std::cout << "24 synthetic documentation frames; production renderer, no HWND/providers\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
