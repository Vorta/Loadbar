#pragma once

#include "platform/windows.hpp"

#include <memory>

namespace loadbar {
class TextPreferences {
  public:
    TextPreferences(HWND target, UINT message);
    ~TextPreferences();
    TextPreferences(const TextPreferences &) = delete;
    TextPreferences &operator=(const TextPreferences &) = delete;
    [[nodiscard]] float scale() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace loadbar
