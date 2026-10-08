#pragma once

#include "model/geometry.hpp"
#include "model/layout.hpp"

namespace loadbar {
// Shell negotiation is bounded. The caller owns the reservation and removes it on failure.
[[nodiscard]] std::optional<Rect> place_readable_bar(AppBar &bar, const Monitor &monitor,
                                                     const Settings &settings,
                                                     const std::vector<Processor> &processors,
                                                     float text_scale, std::size_t disk_count);
} // namespace loadbar
