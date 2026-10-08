#include "telemetry/network.hpp"
#include <algorithm>

// IP Helper's documented include order requires Winsock before IP declarations.
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
// clang-format on

namespace loadbar {
Result<InterfaceCounters> read_interface(std::uint64_t luid) {
    MIB_IF_ROW2 row{.AccessType = NET_IF_ACCESS_BROADCAST,
                    .OperStatus = IfOperStatusUnknown,
                    .AdminStatus = NET_IF_ADMIN_STATUS_DOWN,
                    .ConnectionType = NET_IF_CONNECTION_DEMAND};
    row.InterfaceLuid.Value = luid;
    const auto code = GetIfEntry2(&row);
    if (code != NO_ERROR) {
        return std::unexpected(Error{L"GetIfEntry2", code});
    }
    return InterfaceCounters{row.InOctets, row.OutOctets, row.OperStatus == IfOperStatusUp};
}
Result<std::array<Metric, 2>> NetworkProvider::sample(std::uint64_t luid) {
    if (luid_ != luid) {
        reset();
        luid_ = luid;
    }
    if (last_error_ && now_() < retry_) {
        return std::unexpected(*last_error_);
    }
    const auto counters = reader_(luid);
    // Timestamp the completed observation, not the start of unrelated CPU/disk collection.
    const auto observed = now_();
    if (!counters) {
        download_.reset();
        upload_.reset();
        last_error_ = counters.error();
        retry_ = observed + std::chrono::seconds(delay_);
        delay_ = std::min(delay_ * 2, 30U);
        return std::unexpected(*last_error_);
    }
    last_error_.reset();
    retry_ = {};
    delay_ = 1;
    if (!counters->connected) {
        download_.reset();
        upload_.reset();
        const Metric unavailable{0,  Status::unavailable,         Unit::bytes_per_second, observed,
                                 {}, L"Interface is disconnected"};
        return std::array{unavailable, unavailable};
    }
    return std::array{download_.sample(counters->received, observed),
                      upload_.sample(counters->sent, observed)};
}
void NetworkProvider::reset() noexcept {
    download_.reset();
    upload_.reset();
    luid_.reset();
    last_error_.reset();
    retry_ = {};
    delay_ = 1;
}
} // namespace loadbar
