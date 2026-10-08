#pragma once

#include "model/snapshot.hpp"
#include "model/stream_key.hpp"
#include <map>

namespace loadbar {
// Application-owned, exclusively accessed by its one joined sampling worker. A replacement
// collector borrows the same object; disconnect, resume and settings changes never clear it.
class SessionPeaks {
  public:
    static constexpr std::size_t kMaximumStreams = 4096;
    void observe(Snapshot &snapshot);
    void observe(Gauge direction, Metric &metric);

  private:
    struct Peak {
        double value{};
        Clock::time_point last{};
    };
    std::map<std::pair<std::wstring, Gauge>, Peak, StreamKeyLess> peaks_;
};
} // namespace loadbar
