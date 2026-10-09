#pragma once
#include "platform/windows.hpp"
#include <dwrite_3.h>

namespace loadbar {
class EmbeddedFont {
  public:
    ~EmbeddedFont();
    EmbeddedFont() = default;
    EmbeddedFont(const EmbeddedFont &) = delete;
    EmbeddedFont &operator=(const EmbeddedFont &) = delete;
    HRESULT initialize(IDWriteFactory *factory);
    [[nodiscard]] IDWriteFontCollection *collection() const noexcept {
        return collection_.Get();
    }

  private:
    void reset() noexcept;
    Microsoft::WRL::ComPtr<IDWriteFactory5> factory_;
    Microsoft::WRL::ComPtr<IDWriteInMemoryFontFileLoader> loader_;
    Microsoft::WRL::ComPtr<IDWriteFontCollection1> collection_;
    bool registered_{};
};
void show_licenses(HWND owner);
} // namespace loadbar
