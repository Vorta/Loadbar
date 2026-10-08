#pragma once

#include "model/geometry.hpp"
#include "platform/windows.hpp"

#include <vector>

namespace loadbar {
[[nodiscard]] std::vector<Monitor> enumerate_monitors();
[[nodiscard]] Settings load_settings();
[[nodiscard]] Result<void> save_settings(const Settings &settings);
[[nodiscard]] Result<Handle> acquire_instance();
class WindowsShell final : public ShellPort {
  public:
    HWND window{};
    UINT callback{};
    bool add() override;
    void remove() noexcept override;
    Rect query(Rect rectangle, Edge edge) override;
    Rect set(Rect rectangle, Edge edge) override;
    void notify(DWORD message) const;

  private:
    Rect position(DWORD message, Rect rectangle, Edge edge) const;
};
} // namespace loadbar
