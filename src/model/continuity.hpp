#pragma once

#include "model/snapshot.hpp"
#include "model/stream_key.hpp"
#include <map>

namespace loadbar {
// Application-owned and accessed only by its joined worker. Holds one observation per
// identity/metric, not a history. Retry borrows the same state before UI coalescing.
class DisplayContinuity {
  public:
    static constexpr std::size_t kMaximumStreams = 69632;
    static constexpr std::size_t kMaximumMemoryScopes = 4096;
    void observe(Snapshot &snapshot, const Settings &settings = {});

  private:
    void observe(Metric &metric, Gauge kind);
    void memory(Metric &percent, Metric &bytes, std::uint64_t &capacity, std::wstring_view scope);
    void temperature(TemperatureReading &reading, std::wstring_view identity, unsigned kind);
    struct Memory {
        ObservedValue percent, bytes;
        std::uint64_t capacity{};
    };
    struct Selection {
        std::wstring preference, identity, label, scope;
        bool operator==(const Selection &) const = default;
    };
    std::map<std::pair<std::wstring, Gauge>, ObservedValue, StreamKeyLess> values_;
    std::map<std::pair<std::wstring, std::wstring>, Memory, StreamKeyLess> memories_;
    std::map<std::pair<std::wstring, unsigned>, TemperatureReading, StreamKeyLess> temperatures_;
    Selection gpu_, network_;
    std::map<std::wstring, Selection> known_gpus_, known_networks_;
};
} // namespace loadbar
