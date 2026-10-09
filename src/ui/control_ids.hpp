#pragma once

namespace loadbar {
// WM_COMMAND: child buttons activate only on BN_CLICKED (0). Menu/accelerator commands
// use notification values 0/1; focus and other child notifications must never run actions.
[[nodiscard]] constexpr bool command_activates(unsigned notification, bool from_control) noexcept {
    return from_control ? notification == 0 : notification <= 1;
}
inline constexpr int kApplySettings = 110;
inline constexpr int kCancelSettings = 111;
inline constexpr int kCloseSettings = 112;
inline constexpr int kFirstField = 1000;
inline constexpr int kFooterStatus = 1020;
inline constexpr int kDetailsControl = 1021;
inline constexpr int kDrivesControl = 1022;
inline constexpr int kCpuSquares = 1023;
inline constexpr int kHoverInfo = 1024;
inline constexpr int kTaskManagerClick = 1025;
inline constexpr int kDrivesLabel = 2008;
inline constexpr int kFirstLabel = 2000;
} // namespace loadbar
