#pragma once
#include "model/layout.hpp"
#include "model/presentation.hpp"
#include "platform/windows.hpp"
#include "ui/embedded_font.hpp"
#include <d2d1.h>
#include <dwrite.h>
#include <functional>
#include <tuple>
namespace loadbar {
class Renderer {
  public:
    enum class ResourceKind : std::uint8_t { icon, font, glow, text };
    // Optional failure/retry seam: index for icons/fonts/text, pixel count for glow masks.
    using ResourceProbe = std::function<HRESULT(ResourceKind, unsigned)>;
    // Optional read-only seam for offscreen theme tests; production uses GetSysColor.
    using SystemColorReader = std::function<COLORREF(int)>;
    explicit Renderer(ResourceProbe probe = {}, SystemColorReader colors = {})
        : resource_probe_(std::move(probe)), system_colors_(std::move(colors)) {}
    HRESULT paint(HWND window, const Snapshot &snapshot, const Settings &settings, Edge edge,
                  bool fallback, float text_scale, bool hover = false);
    // The offscreen test uses precisely the production drawing path; no HWND or providers.
    HRESULT render_to(ID2D1RenderTarget *target, const Snapshot &snapshot, const Settings &settings,
                      Edge edge, bool fallback, float text_scale, bool hover, bool high_contrast,
                      Clock::time_point now);
    void discard() noexcept;
    void preferences_changed() noexcept;
    // Returns whether a paint was requested, even when an invisible HWND discards its region.
    bool invalidate_changes(HWND window, const Snapshot &previous, const Snapshot &next,
                            const Settings &settings) const;
    [[nodiscard]] std::wstring tooltip(float x, float y, const Snapshot &snapshot,
                                       const Settings &settings) const;
    [[nodiscard]] std::size_t tooltip_target(float x, float y) const noexcept;
    [[nodiscard]] std::uint64_t layout_revision() const noexcept {
        return layout_revision_;
    }

  private:
    ResourceProbe resource_probe_;
    SystemColorReader system_colors_;
    [[nodiscard]] Color system_color(int index) const;
    HRESULT prepare(ID2D1RenderTarget *target, float content_scale, float temperature_size,
                    bool high_contrast);
    void draw_icon(std::size_t index, Box bounds, Color color);
    void meter(Box bounds, Status status, double fraction, Color hue, Color track, float radius);
    void glow(Box bounds, Color hue, float radius, float alpha);
    void status_mark(Box bounds, Status status);
    void overlay(std::size_t block, const Snapshot &snapshot, const Settings &settings,
                 Clock::time_point now);
    void readout(std::size_t block, const Snapshot &snapshot, const Settings &settings,
                 Clock::time_point now);
    IDWriteTextLayout *text_layout(const std::wstring &text, float width, float height,
                                   unsigned font);
    struct TextCache {
        std::wstring text;
        float width{}, height{};
        unsigned font{};
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        float temperature_x{}, temperature_bottom_adjustment{};
        float advance{};
        float baseline{};
    };
    IDWriteTextLayout *text_layout(TextCache &cache, const std::wstring &text, float width,
                                   float height, unsigned font);
    struct GlowCache {
        float width{}, height{}, radius{}, dpi{}, content_scale{}, extent_width{}, extent_height{};
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        std::size_t bytes{};
    };
    struct Reading {
        double value{}, fraction{};
        Status status{Status::unavailable};
        bool glow{};
    };
    Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
    EmbeddedFont embedded_font_;
    Microsoft::WRL::ComPtr<IDWriteFactory> write_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target_;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> drawing_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> stroke_;
    std::array<Microsoft::WRL::ComPtr<ID2D1Geometry>, 5> icons_;
    std::array<Microsoft::WRL::ComPtr<IDWriteTextFormat>, 5> fonts_;
    std::vector<TextCache> text_cache_;
    // Temperature slots follow component blocks, independently of optional hover text.
    std::vector<TextCache> temperature_cache_;
    std::vector<std::array<TextCache, 3>> readout_cache_;
    std::vector<GlowCache> glows_;
    std::vector<Reading> core_readings_;
    std::vector<std::array<Reading, 3>> block_readings_;
    std::size_t cursor_{};
    Layout layout_;
    std::vector<std::pair<std::wstring, bool>> layout_disks_;
    bool disk_layout_changed(const Snapshot &snapshot, const Settings &settings) const;
    bool temperature_layout_changed(const Snapshot &snapshot, bool enabled) const;
    std::vector<std::tuple<unsigned, ProcessorId, bool, std::optional<unsigned>>> topology_;
    float layout_width_{}, layout_height_{}, layout_scale_{}, layout_dpi_{};
    Edge layout_edge_{Edge::automatic};
    bool layout_gpu_visible_{true}, layout_network_visible_{true};
    bool layout_cpu_squares_{}, layout_gpu_memory_{};
    bool layout_show_temperatures_{true};
    bool layout_always_readout_{};
    Alignment layout_alignment_{};
    float content_scale_{};
    float temperature_size_{};
    bool high_contrast_{};
    std::optional<bool> system_high_contrast_;
    std::uint64_t layout_revision_{};
    Color background_{}, foreground_{}, inactive_{};
};
} // namespace loadbar
