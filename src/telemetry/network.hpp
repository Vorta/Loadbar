#pragma once

#include "model/model.hpp"
#include "platform/windows.hpp"

#include <array>
#include <functional>

namespace loadbar {
struct InterfaceCounters {
    std::uint64_t received{}, sent{};
    bool connected{};
};
[[nodiscard]] Result<InterfaceCounters> read_interface(std::uint64_t luid);
class NetworkProvider {
  public:
    using Reader = std::function<Result<InterfaceCounters>(std::uint64_t)>;
    using Now = std::function<Clock::time_point()>;
    explicit NetworkProvider(Reader reader = read_interface, Now now = Clock::now)
        : reader_(std::move(reader)), now_(std::move(now)) {}
    Result<std::array<Metric, 2>> sample(std::uint64_t luid);
    void reset() noexcept;

  private:
    Reader reader_;
    Now now_;
    Rate download_, upload_;
};
} // namespace loadbar
