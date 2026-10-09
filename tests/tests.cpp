#include "model/geometry.hpp"
#include "model/model.hpp"
#include "model/snapshot.hpp"
#include "telemetry/collector.hpp"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <stdexcept>

void run_regressions();
void run_placement_tests();
void run_design_tests();
void readout_tests();
void run_metric_upgrade_tests();
void run_continuity_tests();
void run_optimization_model_tests();
void run_optimization_app_tests();
void telemetry_optimization_tests();
void verify_resources(const wchar_t *path, const wchar_t *license);
void discovery_tests();
void temperature_tests();

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression))                                                                         \
            throw std::runtime_error(#expression);                                                 \
    } while (false)

class FakeShell final : public loadbar::ShellPort {
  public:
    int adds{}, removes{};
    bool accepted{true};
    loadbar::Rect queried{};
    bool add() override {
        ++adds;
        return accepted;
    }
    void remove() noexcept override {
        ++removes;
    }
    loadbar::Rect query(loadbar::Rect rectangle, loadbar::Edge) override {
        queried = rectangle;
        return rectangle;
    }
    loadbar::Rect set(loadbar::Rect rectangle, loadbar::Edge) override {
        return rectangle;
    }
};

int wmain(int argc, wchar_t **argv) {
    try {
        if (argc == 4 && std::wstring_view(argv[1]) == L"--artifact") {
            verify_resources(argv[2], argv[3]);
            std::cout << "Embedded resources and native version metadata passed\n";
            return 0;
        }
        if (argc == 2 && std::wstring_view(argv[1]) == L"--live-providers") {
            const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            loadbar::SessionPeaks peaks;
            loadbar::DiskInventory disks;
            loadbar::CpuSamples processors;
            loadbar::Collector collector(peaks, disks, processors);
            collector.configure({});
            const auto &catalog = collector.catalog();
            std::cout << "Discovery: logical processors=" << catalog.processors.size()
                      << ", disks=" << catalog.disks.size() << ", GPUs=" << catalog.gpus.size()
                      << ", interfaces=" << catalog.networks.size() << '\n';
            std::map<std::pair<std::wstring, loadbar::Gauge>, double> observed_peaks;
            const auto check_peak = [&](loadbar::Gauge direction, const loadbar::Metric &metric) {
                if (metric.status != loadbar::Status::valid) {
                    return;
                }
                CHECK(metric.session_peak && *metric.session_peak >= metric.value);
                auto &previous = observed_peaks[{metric.identity, direction}];
                CHECK(*metric.session_peak >= previous);
                previous = *metric.session_peak;
            };
            for (int iteration = 0; iteration < 5; ++iteration) {
                const auto snapshot = collector.sample(loadbar::Clock::now());
                std::size_t valid{};
                for (const auto &cpu : snapshot.processors) {
                    if (cpu.utilization.status == loadbar::Status::valid) {
                        ++valid;
                    }
                }
                std::cout << "Sample " << iteration + 1 << ": valid CPU lanes=" << valid << '\n';
                std::map<std::pair<loadbar::Status, std::wstring>, std::size_t> cpu_statuses;
                for (const auto &cpu : snapshot.processors) {
                    if (cpu.utilization.status != loadbar::Status::valid) {
                        ++cpu_statuses[{cpu.utilization.status, cpu.utilization.detail}];
                    }
                }
                for (const auto &[status, count] : cpu_statuses) {
                    std::wcout << L"CPU status: " << count << L" "
                               << loadbar::status_text(status.first) << L" " << status.second
                               << L'\n';
                }
                for (std::size_t index = 0; index < loadbar::kGaugeCount; ++index) {
                    std::wcout << loadbar::gauge_name(static_cast<loadbar::Gauge>(index)) << L": "
                               << loadbar::status_text(snapshot.gauges[index].status) << L" "
                               << snapshot.gauges[index].detail << L'\n';
                }
                const auto &ram = snapshot.gauges[0];
                CHECK(ram.status == snapshot.ram_used_bytes.status);
                if (ram.status == loadbar::Status::valid) {
                    CHECK(snapshot.ram_total_bytes > 0 &&
                          std::abs(ram.value - 100 * snapshot.ram_used_bytes.value /
                                                   static_cast<double>(snapshot.ram_total_bytes)) <
                              1e-9);
                }
                check_peak(loadbar::Gauge::download, snapshot.gauges[4]);
                check_peak(loadbar::Gauge::upload, snapshot.gauges[5]);
                CHECK(snapshot.disks.size() == collector.catalog().disks.size());
                for (const auto &disk : snapshot.disks) {
                    CHECK(!disk.id.empty() && disk.read.identity == disk.id &&
                          disk.write.identity == disk.id && disk.active.identity == disk.id);
                    check_peak(loadbar::Gauge::disk_read, disk.read);
                    check_peak(loadbar::Gauge::disk_write, disk.write);
                    if (disk.active.status == loadbar::Status::valid) {
                        CHECK(std::isfinite(disk.active.value) && disk.active.value >= 0 &&
                              disk.active.value <= 100);
                    }
                    std::wcout << L"Physical disk: read=" << loadbar::status_text(disk.read.status)
                               << L", write=" << loadbar::status_text(disk.write.status)
                               << L", active=" << loadbar::status_text(disk.active.status) << L'\n';
                    if (disk.active.status == loadbar::Status::error) {
                        std::wcout << L"Active-time diagnostic: " << disk.active.detail << L'\n';
                    }
                }
                if (iteration < 4) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }
            if (SUCCEEDED(initialized)) {
                CoUninitialize();
            }
            return 0;
        }
        loadbar::Rate rate;
        const auto start = loadbar::Clock::now();
        CHECK(rate.sample(100, start).status == loadbar::Status::warming_up);
        CHECK(rate.sample(300, start + std::chrono::seconds(2)).value == 100);
        CHECK(rate.sample(2, start + std::chrono::seconds(3)).status ==
              loadbar::Status::warming_up);
        CHECK(loadbar::fill_fraction(150, 100) == 1);
        CHECK(loadbar::percentage(50, 100, start).value == 50);
        const loadbar::Rect monitor{-1080, -200, 0, 1720};
        CHECK(loadbar::resolve_edge(loadbar::Edge::automatic, monitor) == loadbar::Edge::bottom);
        loadbar::Settings settings;
        CHECK(loadbar::decode_settings(loadbar::encode_settings(settings)) == settings);
        CHECK(!loadbar::decode_settings(L"Loadbar 99"));
        CHECK(loadbar::thickness_pixels(settings, monitor, 96, 64) == 64);
        settings.legacy_percent = 5;
        CHECK(loadbar::migrate_thickness(settings, monitor, 96));
        CHECK(loadbar::thickness_pixels(settings, monitor, 96, 64) == 96);
        FakeShell shell;
        {
            loadbar::AppBar bar(shell);
            for (const auto edge : {loadbar::Edge::left, loadbar::Edge::top, loadbar::Edge::right,
                                    loadbar::Edge::bottom}) {
                CHECK(bar.position(monitor, edge, 96).has_value());
            }
            CHECK(shell.adds == 1);
            bar.remove();
            bar.remove();
            CHECK(shell.removes == 1);
            CHECK(bar.position(monitor, loadbar::Edge::bottom, 96).has_value());
        }
        CHECK(shell.removes == 2);
        CHECK(!loadbar::parse_processor(L"_Total"));
        CHECK(!loadbar::parse_processor(L"0,_Total"));
        CHECK((loadbar::parse_processor(L"1,63") == loadbar::ProcessorId{1, 63}));
        const auto engine =
            loadbar::parse_engine(L"pid_12_luid_0x00000001_0x00000002_phys_0_eng_3_engtype_3D");
        CHECK(engine && engine->luid == 0x100000002ULL && engine->type == L"3D");
        CHECK(!loadbar::parse_engine(L"pid_12_luid_0x1_0x2_phys_0_eng_3_engtype_3D#1"));
        loadbar::Latest<int> handoff;
        int notifications{};
        handoff.publish(1, [&] {
            ++notifications;
            return true;
        });
        handoff.publish(2, [&] {
            ++notifications;
            return true;
        });
        CHECK(notifications == 1 && handoff.take() == 2);
        handoff.close();
        handoff.publish(3, [&] {
            ++notifications;
            return true;
        });
        CHECK(!handoff.take() && notifications == 1);
        run_regressions();
        run_placement_tests();
        run_design_tests();
        readout_tests();
        run_metric_upgrade_tests();
        run_continuity_tests();
        run_optimization_model_tests();
        run_optimization_app_tests();
        telemetry_optimization_tests();
        discovery_tests();
        temperature_tests();
        std::cout << "Model tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    } catch (...) {
        std::fputs("Unknown test failure\n", stderr);
        return 2;
    }
}
