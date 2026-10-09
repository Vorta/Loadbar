#pragma once

#include <algorithm>

namespace loadbar::strip_style {
// The supplied 2x export represents a 1200 x 60-DIP strip. The layout engine's
// compact scale is 40 DIPs, so reference dimensions are normalized by 1.5.
inline constexpr float kReferenceScale = 1.5F;
inline constexpr float kMargin = 16 / kReferenceScale;
inline constexpr float kColumn = 32 / kReferenceScale;
inline constexpr float kIcon = 18 / kReferenceScale;
inline constexpr float kGraphicGap = 10 / kReferenceScale;
inline constexpr float kHeader = kColumn + kGraphicGap;
inline constexpr float kWidgetGap = 16 / kReferenceScale;
inline constexpr float kDividerWidth = 1 / kReferenceScale;
inline constexpr float kVerticalTail = kGraphicGap + kIcon;
inline constexpr float kTemperatureShift = 8 / kReferenceScale;
inline constexpr float kTemperatureGap = 3 / kReferenceScale;
inline constexpr float kCoreRadius = 4 / kReferenceScale;
// Fixed columns prevent sample values or unit changes from moving the graphics.
inline constexpr float kRamReadoutWidth = 56;
inline constexpr float kGpuReadoutWidth = 62;
inline constexpr float kDiskReadoutWidth = 72;
inline constexpr float kNetworkReadoutWidth = 42;
inline constexpr float kCpuReadoutWidth = 30;
inline constexpr float kHybridReadoutWidth = 52;
inline constexpr float kReadoutGap = 6;
inline constexpr float kReadoutWidgetGap = 8 / kReferenceScale;
inline constexpr float kReadoutBand = 22;
inline constexpr float kReadoutPrimaryBaseline = 9;
inline constexpr float kReadoutSecondaryBaseline = 20;

[[nodiscard]] inline float device_readout_width(unsigned kind) noexcept {
    switch (kind) {
    case 1:
        return kRamReadoutWidth;
    case 2:
        return kGpuReadoutWidth;
    case 3:
        return kDiskReadoutWidth;
    case 4:
        return kNetworkReadoutWidth;
    default:
        return 0;
    }
}

[[nodiscard]] inline float core_radius(float width, float height, float content_scale) noexcept {
    // The 60-DIP reference has 4-DIP P corners and 2-DIP E corners.
    return std::min(kCoreRadius * content_scale, std::min(width, height) * (2.0F / 9));
}

[[nodiscard]] inline float temperature_font_size(float content_scale, float text_scale) noexcept {
    return std::max(9 * text_scale, 11 * content_scale / kReferenceScale);
}
} // namespace loadbar::strip_style
