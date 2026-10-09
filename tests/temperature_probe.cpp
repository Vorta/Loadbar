#include "telemetry/discovery.hpp"
#include "telemetry/temperature.hpp"
#include "telemetry/vendor_temperature.hpp"
#include <chrono>
#include <iostream>
#include <thread>

// Explicit read-only hardware diagnostic. No HWND, AppBar, settings writes, or load generation.
int wmain(int argc, wchar_t **argv) {
    std::wcout << std::unitbuf;
    if ((argc != 2 && argc != 3) || std::wstring_view(argv[1]) != L"--read-only" ||
        (argc == 3 && std::wstring_view(argv[2]) != L"--vendors")) {
        std::cerr << "Usage: loadbar_temperature_probe --read-only [--vendors]\n";
        return 2;
    }
    try {
        auto discovery = loadbar::discover_devices();
        const auto gpu = loadbar::select_device(discovery.catalog.gpus, L"");
        loadbar::Settings settings;
        loadbar::TemperatureProviders provider;
        provider.configure(discovery.catalog, gpu, settings, true,
                           discovery.disk_error.has_value());
        loadbar::Snapshot snapshot;
        for (const auto &device : discovery.catalog.disks) {
            snapshot.disks.push_back({device.id, L"Drive"});
        }
        for (unsigned sample = 0; sample < 6; ++sample) {
            if (sample) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            provider.sample(snapshot, loadbar::Clock::now());
        }
        std::wcout << L"CPU: "
                   << loadbar::temperature_details(snapshot.cpu_temperature, loadbar::Clock::now(),
                                                   1000)
                   << L'\n';
        std::wcout << L"GPU: "
                   << loadbar::temperature_details(snapshot.gpu_temperature, loadbar::Clock::now(),
                                                   1000)
                   << L'\n';
        for (std::size_t i = 0; i < snapshot.disks.size(); ++i) {
            std::wcout << L"Drive " << i + 1 << L": "
                       << loadbar::temperature_details(snapshot.disks[i].temperature,
                                                       loadbar::Clock::now(), 1000)
                       << L'\n';
        }
        if (argc == 3) {
            for (const auto &device : discovery.catalog.gpus) {
                std::wcout << L"Probing vendor " << device.vendor_id << L"...\n";
                loadbar::VendorGpuTemperature vendor;
                const auto started = loadbar::Clock::now();
                const auto reading = vendor.sample(device, started);
                const auto elapsed =
                    std::chrono::duration<double, std::milli>(loadbar::Clock::now() - started)
                        .count();
                std::wcout << L"Vendor " << device.vendor_id << L": "
                           << loadbar::temperature_details(reading, loadbar::Clock::now(), 1000)
                           << L"; first bind/read milliseconds: " << elapsed << L'\n';
            }
        }
        std::wcout << L"Availability only; no accuracy or performance gate established.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
