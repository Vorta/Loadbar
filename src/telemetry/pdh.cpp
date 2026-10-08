#include "telemetry/pdh.hpp"

#include <pdhmsg.h>

#include <cmath>
#include <format>

namespace loadbar {
namespace {
constexpr DWORD kMaximumBuffer = 32 * 1024 * 1024;
Error pdh_error(const wchar_t *operation, PDH_STATUS code) {
    return {operation, static_cast<DWORD>(code)};
}
} // namespace
Metric interpret_counter(double value, DWORD status, bool priming, Clock::time_point now,
                         std::chrono::duration<double> interval, Unit unit) {
    Metric metric{value, Status::valid, unit, now, interval, {}};
    if (priming && status == PDH_CSTATUS_INVALID_DATA) {
        metric.status = Status::warming_up;
        metric.detail = L"Priming counter instance";
    } else if (status != PDH_CSTATUS_VALID_DATA && status != PDH_CSTATUS_NEW_DATA) {
        metric.status = Status::unavailable;
        metric.detail = std::format(L"PDH item status 0x{:08X}", status);
    } else if (!std::isfinite(value) || value < 0) {
        metric.status = Status::error;
        metric.detail = L"Non-finite or negative counter";
    } else if (priming || interval.count() <= 0) {
        metric.status = Status::warming_up;
        metric.detail = L"New or reconnected counter instance";
    }
    return metric;
}
void CounterInstances::begin() noexcept {
    for (auto &[name, presence] : instances_) {
        (void)name;
        presence.current = false;
    }
}
bool CounterInstances::contains(std::wstring_view name) const {
    const auto found = instances_.find(name);
    return found != instances_.end() && found->second.previous;
}
void CounterInstances::observe(std::wstring_view name) {
    auto found = instances_.find(name);
    if (found == instances_.end()) {
        found = instances_.try_emplace(std::wstring(name)).first;
    }
    found->second.current = true;
}
void CounterInstances::finish() {
    std::erase_if(instances_, [](auto &entry) { return !entry.second.current; });
    for (auto &[name, presence] : instances_) {
        (void)name;
        presence.previous = true;
    }
}
PdhQuery::~PdhQuery() {
    reset();
}
void PdhQuery::reset() noexcept {
    if (query_) {
        PdhCloseQuery(query_);
        query_ = nullptr;
    }
    counters_.clear();
    previous_instances_.clear();
    samples_ = 0;
    previous_ = {};
    now_ = {};
}
void PdhQuery::deactivate() noexcept {
    reset();
    std::vector<PDH_HCOUNTER>{}.swap(counters_);
    std::vector<CounterInstances>{}.swap(previous_instances_);
    std::vector<std::byte>{}.swap(buffer_);
}
Result<void> PdhQuery::open(const std::vector<std::wstring> &paths) {
    reset();
    auto code = PdhOpenQueryW(nullptr, 0, &query_);
    if (code != ERROR_SUCCESS) {
        return std::unexpected(pdh_error(L"PdhOpenQuery", code));
    }
    for (const auto &path : paths) {
        PDH_HCOUNTER counter{};
        code = PdhAddEnglishCounterW(query_, path.c_str(), 0, &counter);
        if (code != ERROR_SUCCESS) {
            reset();
            return std::unexpected(pdh_error(L"PdhAddEnglishCounter", code));
        }
        counters_.push_back(counter);
        previous_instances_.emplace_back();
        // Localize before discovery expansion; sample wildcard handles with the array API.
        DWORD size{};
        code = PdhGetCounterInfoW(counter, FALSE, &size, nullptr);
        if (code != PDH_MORE_DATA || size > kMaximumBuffer) {
            reset();
            return std::unexpected(pdh_error(L"PdhGetCounterInfo size", code));
        }
        std::vector<std::byte> metadata(size);
        code = PdhGetCounterInfoW(counter, FALSE, &size,
                                  reinterpret_cast<PDH_COUNTER_INFO_W *>(metadata.data()));
        if (code != ERROR_SUCCESS) {
            reset();
            return std::unexpected(pdh_error(L"PdhGetCounterInfo", code));
        }
        const auto *info = reinterpret_cast<const PDH_COUNTER_INFO_W *>(metadata.data());
        DWORD characters{};
        // NO_INSTANCE is permitted here; successful add/discovery is never evidence of valid data.
        PdhExpandWildCardPathW(nullptr, info->szFullPath, nullptr, &characters, 0);
        if (characters > 0 && characters <= kMaximumBuffer / sizeof(wchar_t)) {
            std::vector<wchar_t> expanded(characters);
            PdhExpandWildCardPathW(nullptr, info->szFullPath, expanded.data(), &characters, 0);
        }
    }
    return {};
}
Result<void> PdhQuery::collect(Clock::time_point now) {
    if (!query_) {
        return std::unexpected(Error{L"PDH query not open", ERROR_INVALID_HANDLE});
    }
    const auto code = PdhCollectQueryData(query_);
    if (code != ERROR_SUCCESS) {
        samples_ = 0;
        for (auto &instances : previous_instances_) {
            instances.clear();
        }
        return std::unexpected(pdh_error(L"PdhCollectQueryData", code));
    }
    previous_ = now_;
    now_ = now;
    if (samples_ < 2) {
        ++samples_;
    }
    return {};
}
Result<void> PdhQuery::array(std::size_t index, Unit unit, std::vector<CounterItem> &result) {
    if (index >= counters_.size()) {
        return std::unexpected(Error{L"PDH counter index", ERROR_INVALID_PARAMETER});
    }
    for (int attempt = 0; attempt < 3; ++attempt) {
        DWORD size{}, count{};
        auto code = PdhGetFormattedCounterArrayW(
            counters_[index], PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count, nullptr);
        if (code != PDH_MORE_DATA) {
            previous_instances_[index].clear();
            if (code == ERROR_SUCCESS && count == 0) {
                result.clear();
                return {};
            }
            if (samples_ < 2 && (code == PDH_INVALID_DATA || code == PDH_CSTATUS_INVALID_DATA)) {
                return std::unexpected(Error{L"Priming PDH counters", ERROR_NOT_READY});
            }
            return std::unexpected(pdh_error(L"PDH array size", code));
        }
        if (size == 0 || size > kMaximumBuffer) {
            return std::unexpected(Error{L"PDH array bounds", ERROR_INVALID_DATA});
        }
        buffer_.resize(size);
        code = PdhGetFormattedCounterArrayW(
            counters_[index], PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count,
            reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer_.data()));
        if (code == PDH_MORE_DATA || code == PDH_INVALID_ARGUMENT) {
            continue;
        }
        if (code != ERROR_SUCCESS) {
            previous_instances_[index].clear();
            if (samples_ < 2 && (code == PDH_INVALID_DATA || code == PDH_CSTATUS_INVALID_DATA)) {
                return std::unexpected(Error{L"Priming PDH counters", ERROR_NOT_READY});
            }
            return std::unexpected(pdh_error(L"PDH array", code));
        }
        if (count > buffer_.size() / sizeof(PDH_FMT_COUNTERVALUE_ITEM_W)) {
            return std::unexpected(Error{L"PDH item bounds", ERROR_INVALID_DATA});
        }
        const auto *items = reinterpret_cast<const PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer_.data());
        auto &instances = previous_instances_[index];
        instances.begin();
        result.resize(count);
        for (DWORD i = 0; i < count; ++i) {
            const auto &value = items[i].FmtValue;
            const std::wstring_view name = items[i].szName ? items[i].szName : L"";
            const bool priming = samples_ < 2 || now_ <= previous_ || !instances.contains(name);
            auto metric = interpret_counter(value.doubleValue, value.CStatus, priming, now_,
                                            now_ - previous_, unit);
            if (metric.status == Status::valid || metric.status == Status::warming_up) {
                instances.observe(name);
            }
            result[i].name.assign(name);
            result[i].metric = std::move(metric);
        }
        instances.finish();
        return {};
    }
    previous_instances_[index].clear();
    return std::unexpected(Error{L"PDH array changed repeatedly", ERROR_RETRY});
}
} // namespace loadbar
