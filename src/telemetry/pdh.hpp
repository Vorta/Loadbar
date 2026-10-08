#pragma once

#include "model/snapshot.hpp"
#include "platform/windows.hpp"

#include <pdh.h>

#include <map>
#include <vector>

namespace loadbar {
[[nodiscard]] Metric interpret_counter(double value, DWORD status, bool priming,
                                       Clock::time_point now,
                                       std::chrono::duration<double> interval, Unit unit);
// Keeps only usable instances from the most recent completed array. Names are owned;
// duplicate rows in one array never prime each other.
class CounterInstances {
  public:
    CounterInstances() = default;
    CounterInstances(const CounterInstances &) = default;
    CounterInstances &operator=(const CounterInstances &) = default;
    // Trackers grow only while opening a query, before they hold instance names.
    // Copy those empty trackers rather than require a nonthrowing MSVC map move
    // (its move constructor allocates a sentinel/proxy).
    ~CounterInstances() = default;
    void begin() noexcept;
    [[nodiscard]] bool contains(std::wstring_view name) const;
    void observe(std::wstring_view name);
    void finish();
    void clear() noexcept {
        instances_.clear();
    }
    [[nodiscard]] std::size_t size() const noexcept {
        return instances_.size();
    }

  private:
    struct Presence {
        bool previous{}, current{};
    };
    std::map<std::wstring, Presence, std::less<>> instances_;
};
class PdhQuery {
  public:
    PdhQuery() = default;
    ~PdhQuery();
    PdhQuery(const PdhQuery &) = delete;
    PdhQuery &operator=(const PdhQuery &) = delete;
    Result<void> open(const std::vector<std::wstring> &paths);
    Result<void> collect(Clock::time_point now);
    Result<void> array(std::size_t index, Unit unit, std::vector<CounterItem> &result);
    void reset() noexcept;

  private:
    PDH_HQUERY query_{};
    std::vector<PDH_HCOUNTER> counters_;
    std::vector<CounterInstances> previous_instances_;
    std::vector<std::byte> buffer_;
    Clock::time_point previous_{}, now_{};
    unsigned samples_{};
};
} // namespace loadbar
