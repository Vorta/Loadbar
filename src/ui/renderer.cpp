#include "ui/renderer.hpp"
#include "model/geometry.hpp"
#include "model/strip_style.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>
namespace loadbar {
namespace {
D2D1_RECT_F rectangle(Box b) {
    return {b.x, b.y, b.x + b.width, b.y + b.height};
}
D2D1_COLOR_F native(Color c) {
    return {c.r, c.g, c.b, c.a};
}
struct GraphicsFailure : std::exception {
    explicit GraphicsFailure(HRESULT result) : code(result) {}
    const char *what() const noexcept override {
        return "Direct2D/DirectWrite resource creation failed";
    }
    HRESULT code;
};
void checked(HRESULT result) {
    if (FAILED(result)) {
        throw GraphicsFailure{result};
    }
}
// Restores the transform and clip even when text creation throws during hover rendering.
class OverlayTransform {
  public:
    OverlayTransform(ID2D1RenderTarget *target, Box bounds, Edge edge) : target_(target) {
        target_->GetTransform(&previous_);
        target_->PushAxisAlignedClip(rectangle(bounds), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (edge == Edge::right) {
            target_->SetTransform(D2D1::Matrix3x2F(0, 1, -1, 0, bounds.x + bounds.width, bounds.y) *
                                  previous_);
        } else if (edge == Edge::left) {
            target_->SetTransform(
                D2D1::Matrix3x2F(0, -1, 1, 0, bounds.x, bounds.y + bounds.height) * previous_);
        }
    }
    ~OverlayTransform() {
        target_->SetTransform(previous_);
        target_->PopAxisAlignedClip();
    }
    OverlayTransform(const OverlayTransform &) = delete;
    OverlayTransform &operator=(const OverlayTransform &) = delete;

  private:
    ID2D1RenderTarget *target_;
    D2D1_MATRIX_3X2_F previous_{};
};
using Geometry = Microsoft::WRL::ComPtr<ID2D1Geometry>;
Geometry icon_geometry(ID2D1Factory *factory, unsigned icon) {
    std::vector<Geometry> parts;
    const auto rounded = [&](float x, float y, float w, float h, float r) {
        Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> g;
        checked(factory->CreateRoundedRectangleGeometry(
            D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), r, r), &g));
        parts.push_back(g);
    };
    const auto circle = [&](float x, float y, float r) {
        Microsoft::WRL::ComPtr<ID2D1EllipseGeometry> g;
        checked(factory->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(x, y), r, r), &g));
        parts.push_back(g);
    };
    const auto path = [&](std::initializer_list<D2D1_POINT_2F> points, bool closed = false) {
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> g;
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        checked(factory->CreatePathGeometry(&g));
        checked(g->Open(&sink));
        sink->BeginFigure(*points.begin(), D2D1_FIGURE_BEGIN_HOLLOW);
        for (auto it = std::next(points.begin()); it != points.end(); ++it) {
            sink->AddLine(*it);
        }
        sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
        checked(sink->Close());
        parts.push_back(g);
    };
    switch (icon) {
    case 0:
        rounded(4, 4, 8, 8, 1.5F);
        rounded(6.5F, 6.5F, 3, 3, 0.5F);
        for (float p : {6.0F, 10.0F}) {
            path({{p, 1.8F}, {p, 4}});
            path({{p, 12}, {p, 14.2F}});
            path({{1.8F, p}, {4, p}});
            path({{12, p}, {14.2F, p}});
        }
        break;
    case 1:
        rounded(1.5F, 4, 13, 6.5F, 1);
        for (float p : {4.2F, 6.7F, 9.3F, 11.8F}) {
            path({{p, 6.3F}, {p, 8.3F}});
        }
        for (float p : {3.5F, 6.0F, 10.0F, 12.5F}) {
            path({{p, 10.5F}, {p, 12.5F}});
        }
        break;
    case 2:
        rounded(1.5F, 3.5F, 11, 8, 1);
        circle(6.5F, 7.5F, 2.2F);
        path({{12.5F, 5}, {14.5F, 5}, {14.5F, 10}, {12.5F, 10}});
        path({{3.5F, 11.5F}, {3.5F, 13.5F}, {8.5F, 13.5F}, {8.5F, 11.5F}});
        break;
    case 3:
        rounded(1.5F, 3, 13, 10, 2);
        path({{1.5F, 9}, {14.5F, 9}});
        break;
    default:
        path({{2.5F, 3.5F},
              {13.5F, 3.5F},
              {13.5F, 10.5F},
              {11, 10.5F},
              {11, 12.5F},
              {5, 12.5F},
              {5, 10.5F},
              {2.5F, 10.5F}},
             true);
        for (float p : {5.5F, 8.0F, 10.5F}) {
            path({{p, 5.5F}, {p, 7}});
        }
        break;
    }
    std::vector<ID2D1Geometry *> raw;
    raw.reserve(parts.size());
    for (const auto &p : parts) {
        raw.push_back(p.Get());
    }
    Microsoft::WRL::ComPtr<ID2D1GeometryGroup> result;
    checked(factory->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, raw.data(),
                                         static_cast<UINT32>(raw.size()), &result));
    return result;
}
} // namespace
bool Renderer::temperature_layout_changed(const Snapshot &snapshot, bool enabled) const {
    if (layout_show_temperatures_ != enabled) {
        return true;
    }
    if (!enabled) {
        return false;
    }
    return std::ranges::any_of(layout_.blocks, [&](const auto &block) {
        const auto *reading = block_temperature(block, snapshot);
        return (block.temperature.width > 0) != (reading && temperature_visible(*reading));
    });
}
bool Renderer::disk_layout_changed(const Snapshot &snapshot, const Settings &settings) const {
    return !std::ranges::equal(
        snapshot.disks, layout_disks_, [&](const auto &disk, const auto &cached) {
            return disk.id == cached.first && disk_visible(disk.id, settings) == cached.second;
        });
}
bool Renderer::invalidate_changes(HWND window, const Snapshot &previous, const Snapshot &next,
                                  const Settings &settings) const {
    const bool topology_changed = !std::ranges::equal(
        previous.processors, next.processors, [](const Processor &a, const Processor &b) {
            return a.id == b.id && a.core == b.core && a.mapped == b.mapped &&
                   a.efficiency_class == b.efficiency_class;
        });
    if (!target_ || !layout_.fits || topology_changed || disk_layout_changed(next, settings) ||
        temperature_layout_changed(next, settings.show_temperatures) ||
        layout_cpu_squares_ != settings.cpu_squares ||
        layout_always_readout_ != settings.always_show_readout ||
        layout_gpu_memory_ != gpu_memory_visible(next) ||
        layout_gpu_visible_ != settings.gpu_visible ||
        layout_network_visible_ != settings.network_visible ||
        !std::ranges::equal(previous.disks, next.disks,
                            [](const DiskReading &a, const DiskReading &b) {
                                return a.id == b.id && a.label == b.label;
                            }) ||
        (settings.gpu_visible && (previous.gpu_label != next.gpu_label ||
                                  previous.gpu_memory_label != next.gpu_memory_label)) ||
        (settings.network_visible && previous.network_label != next.network_label)) {
        InvalidateRect(window, nullptr, FALSE);
        return true;
    }
    std::vector<bool> changed(layout_.blocks.size());
    const auto now = Clock::now();
    const auto differs = [&](MetricView a, MetricView b) {
        return visual_metric_changed(a, b, now, settings.interval_ms);
    };
    for (std::size_t i = 0; i < next.processors.size(); ++i) {
        changed[0] = changed[0] ||
                     differs(previous.processors[i].utilization, next.processors[i].utilization);
    }
    for (std::size_t b = 1; b < layout_.blocks.size(); ++b) {
        for (auto g : layout_.blocks[b].types) {
            if (g == Gauge::count) {
                break;
            }
            changed[b] = changed[b] || differs(block_metric(layout_.blocks[b], g, previous),
                                               block_metric(layout_.blocks[b], g, next));
        }
    }
    changed[1] = changed[1] || previous.ram_total_bytes != next.ram_total_bytes ||
                 differs(previous.ram_used_bytes, next.ram_used_bytes);
    for (std::size_t b = 0; b < layout_.blocks.size(); ++b) {
        const auto *before = block_temperature(layout_.blocks[b], previous);
        const auto *after = block_temperature(layout_.blocks[b], next);
        if (settings.show_temperatures && before && after && temperature_visible(*after)) {
            const auto a = presented_metric(before->metric);
            const auto z = presented_metric(after->metric);
            changed[b] =
                changed[b] || !temperature_visible(*before) ||
                std::round(a.value) != std::round(z.value) ||
                (!high_contrast_ && temperature_appearance(layout_.blocks[b].kind, a.value) !=
                                        temperature_appearance(layout_.blocks[b].kind, z.value));
        }
        if (layout_.blocks[b].kind == 2) {
            changed[b] = changed[b] || differs(previous.gpu_memory_bytes, next.gpu_memory_bytes) ||
                         previous.gpu_memory_capacity != next.gpu_memory_capacity;
        }
    }
    const float dpi_scale = static_cast<float>(GetDpiForWindow(window)) / 96;
    bool invalidated{};
    // Include the whole dependent overlay and three-sigma glow; Windows coalesces these regions.
    for (std::size_t block = 0; block < changed.size(); ++block) {
        if (!changed[block]) {
            continue;
        }
        const auto b = expanded_paint_bounds(layout_.blocks[block].bounds, layout_.content_scale,
                                             dpi_scale * 96);
        const RECT dirty{static_cast<LONG>(std::floor(b.x * dpi_scale)),
                         static_cast<LONG>(std::floor(b.y * dpi_scale)),
                         static_cast<LONG>(std::ceil((b.x + b.width) * dpi_scale)),
                         static_cast<LONG>(std::ceil((b.y + b.height) * dpi_scale))};
        InvalidateRect(window, &dirty, FALSE);
        invalidated = true;
    }
    return invalidated;
}
Color Renderer::system_color(int index) const {
    const auto v = system_colors_ ? system_colors_(index) : GetSysColor(index);
    return {static_cast<float>(GetRValue(v)) / 255, static_cast<float>(GetGValue(v)) / 255,
            static_cast<float>(GetBValue(v)) / 255, 1};
}
void Renderer::preferences_changed() noexcept {
    system_high_contrast_.reset();
    for (auto &f : fonts_) {
        f.Reset();
    }
    text_cache_.clear();
    temperature_cache_.clear();
    readout_cache_.clear();
}
void Renderer::discard() noexcept {
    ++layout_revision_;
    brush_.Reset();
    drawing_.Reset();
    target_.Reset();
    glows_.clear();
}
HRESULT Renderer::prepare(ID2D1RenderTarget *target, float scale, float temperature_size,
                          bool contrast) {
    if (!factory_) {
        target->GetFactory(&factory_);
    }
    if (!write_) {
        checked(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                    reinterpret_cast<IUnknown **>(write_.GetAddressOf())));
    }
    if (drawing_.Get() != target) {
        brush_.Reset();
        glows_.clear();
        drawing_ = target;
    }
    if (!brush_) {
        checked(target->CreateSolidColorBrush(native(kPrimary), &brush_));
    }
    if (!stroke_) {
        Microsoft::WRL::ComPtr<ID2D1StrokeStyle> stroke;
        decltype(icons_) icons;
        auto properties = D2D1::StrokeStyleProperties();
        properties.startCap = D2D1_CAP_STYLE_ROUND;
        properties.endCap = D2D1_CAP_STYLE_ROUND;
        properties.lineJoin = D2D1_LINE_JOIN_ROUND;
        checked(factory_->CreateStrokeStyle(properties, nullptr, 0, &stroke));
        for (unsigned i = 0; i < icons_.size(); ++i) {
            if (resource_probe_) {
                checked(resource_probe_(ResourceKind::icon, i));
            }
            icons[i] = icon_geometry(factory_.Get(), i);
        }
        icons_ = std::move(icons);
        stroke_ = std::move(stroke);
    }
    if (!fonts_[0] || content_scale_ != scale || temperature_size_ != temperature_size) {
        decltype(fonts_) fonts;
        checked(embedded_font_.initialize(write_.Get()));
        const wchar_t *family = L"Geist Mono";
        const std::array sizes{11 * scale, 9 * scale, 10 * scale, 9 * scale, temperature_size};
        for (std::size_t i = 0; i < fonts_.size(); ++i) {
            if (resource_probe_) {
                checked(resource_probe_(ResourceKind::font, static_cast<unsigned>(i)));
            }
            checked(write_->CreateTextFormat(
                family, embedded_font_.collection(),
                i == 1 ? DWRITE_FONT_WEIGHT_NORMAL : DWRITE_FONT_WEIGHT_MEDIUM,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, sizes[i], L"", &fonts[i]));
            checked(fonts[i]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
            checked(fonts[i]->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
        }
        fonts_ = std::move(fonts);
        text_cache_.clear();
        temperature_cache_.clear();
        readout_cache_.clear();
        content_scale_ = scale;
        temperature_size_ = temperature_size;
    }
    high_contrast_ = contrast;
    background_ = contrast ? system_color(COLOR_WINDOW) : kBackground;
    foreground_ = contrast ? system_color(COLOR_WINDOWTEXT) : kPrimary;
    inactive_ = contrast ? system_color(COLOR_BTNFACE) : rgb(0x1A2129);
    return S_OK;
}
void Renderer::draw_icon(std::size_t i, Box b, Color color) {
    drawing_->SetTransform(D2D1::Matrix3x2F::Scale(b.width / 16, b.height / 16) *
                           D2D1::Matrix3x2F::Translation(b.x, b.y));
    brush_->SetColor(native(color));
    drawing_->DrawGeometry(icons_[i].Get(), brush_.Get(), 1.3F, stroke_.Get());
    if (i == 3) {
        drawing_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(11.5F, 11), 0.7F, 0.7F), brush_.Get());
    }
    drawing_->SetTransform(D2D1::Matrix3x2F::Identity());
}
void Renderer::status_mark(Box b, Status status) {
    brush_->SetColor(native(foreground_));
    const float cy = b.y + b.height / 2;
    const float unit = content_scale_;
    if (status == Status::warming_up) {
        for (int i = 0; i < 3; ++i) {
            drawing_->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(b.x + b.width * (static_cast<float>(i) + 1) / 4, cy),
                              0.6F * unit, 0.6F * unit),
                brush_.Get());
        }
    } else if (status == Status::stale) {
        drawing_->DrawLine({b.x + unit, cy}, {b.x + b.width - unit, cy}, brush_.Get(), unit);
    } else {
        drawing_->DrawLine({b.x + unit, b.y + unit}, {b.x + b.width - unit, b.y + b.height - unit},
                           brush_.Get(), unit);
        if (status == Status::error) {
            drawing_->DrawLine({b.x + unit, b.y + b.height - unit},
                               {b.x + b.width - unit, b.y + unit}, brush_.Get(), unit);
        }
    }
}
void Renderer::meter(Box b, Status status, double fraction, Color hue, Color track, float radius) {
    radius *= content_scale_;
    const auto rounded = D2D1::RoundedRect(rectangle(b), radius, radius);
    brush_->SetColor(native(high_contrast_ ? inactive_ : track));
    drawing_->FillRoundedRectangle(rounded, brush_.Get());
    if (status != Status::valid) {
        status_mark(b, status);
        return;
    }
    if (fraction <= 0) {
        return;
    }
    Box fill = b;
    if (horizontal(layout_edge_)) {
        fill.width =
            std::min(b.width, std::max(content_scale_, b.width * static_cast<float>(fraction)));
    } else {
        fill.height =
            std::min(b.height, std::max(content_scale_, b.height * static_cast<float>(fraction)));
        fill.y = b.y + b.height - fill.height;
    }
    drawing_->PushAxisAlignedClip(rectangle(fill), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    brush_->SetColor(native(high_contrast_ ? system_color(COLOR_HIGHLIGHT) : hue));
    drawing_->FillRoundedRectangle(rounded, brush_.Get());
    drawing_->PopAxisAlignedClip();
}
void Renderer::glow(Box b, Color hue, float radius, float alpha) {
    radius *= content_scale_;
    if (high_contrast_) {
        return;
    }
    float dpi{}, dy{};
    drawing_->GetDpi(&dpi, &dy);
    auto found = std::ranges::find_if(glows_, [&](const GlowCache &g) {
        return g.width == b.width && g.height == b.height && g.radius == radius && g.dpi == dpi &&
               g.content_scale == content_scale_;
    });
    if (found == glows_.end()) {
        const double physical_scale = dpi / 96.0;
        const double physical_pad =
            std::round(glow_padding_dips(content_scale_, dpi) * physical_scale);
        const double full_width = std::ceil(b.width * physical_scale) + physical_pad * 2;
        const double full_height = std::ceil(b.height * physical_scale) + physical_pad * 2;
        if (!std::isfinite(full_width) || !std::isfinite(full_height) || full_width <= 0 ||
            full_height <= 0) {
            return;
        }
        // Keep compact masks pixel-identical. Large masks use bounded resolution, mapped
        // back to their original DIP extent; mask/temp/pixels together use at most 768 KiB.
        const double reduction = std::min({1.0, 1024 / full_width, 1024 / full_height,
                                           std::sqrt(65536 / (full_width * full_height))});
        const int width = std::max(1, static_cast<int>(std::floor(full_width * reduction)));
        const int height = std::max(1, static_cast<int>(std::floor(full_height * reduction)));
        if (width > 1024 || height > 1024 || static_cast<std::uint64_t>(width) * height > 65536) {
            return;
        }
        const float scale = static_cast<float>(physical_scale * reduction);
        const float mask_pad = static_cast<float>(physical_pad * reduction);
        const int pad = std::max(1, static_cast<int>(std::ceil(mask_pad)));
        const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        // Bound cached bitmap payloads as well as entry count. Evict only as much as
        // needed; a hit moves to the back below so frequently used shapes survive.
        constexpr std::size_t kGlowBudget = std::size_t{16} * 1024 * 1024;
        const auto bytes = count * sizeof(std::uint32_t);
        std::size_t cached_bytes{};
        for (const auto &entry : glows_) {
            cached_bytes += entry.bytes;
        }
        while (!glows_.empty() && (glows_.size() >= 32 || cached_bytes + bytes > kGlowBudget)) {
            cached_bytes -= glows_.front().bytes;
            glows_.erase(glows_.begin());
        }
        std::vector<float> mask(count), temp(count);
        const float rx = std::min(radius, std::min(b.width, b.height) / 2) * scale;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const float px = static_cast<float>(x) - mask_pad + 0.5F,
                            py = static_cast<float>(y) - mask_pad + 0.5F;
                if (px < 0 || py < 0 || px >= b.width * scale || py >= b.height * scale) {
                    continue;
                }
                const float cx = std::clamp(px, rx, b.width * scale - rx),
                            cy = std::clamp(py, rx, b.height * scale - rx);
                mask[static_cast<std::size_t>(y) * width + x] =
                    (px - cx) * (px - cx) + (py - cy) * (py - cy) <= rx * rx ? 1.0F : 0.0F;
            }
        }
        std::vector<float> kernel(static_cast<std::size_t>(pad) * 2 + 1);
        float sum{};
        for (int k = -pad; k <= pad; ++k) {
            const int offset = k + pad;
            const float v = std::exp(-static_cast<float>(k * k) /
                                     (2 * 16 * scale * scale * content_scale_ * content_scale_));
            kernel[static_cast<std::size_t>(offset)] = v;
            sum += v;
        }
        for (auto &v : kernel) {
            v /= sum;
        }
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float v{};
                for (int k = -pad; k <= pad; ++k) {
                    if (x + k >= 0 && x + k < width) {
                        const int offset = k + pad;
                        v += mask[static_cast<std::size_t>(y) * width + x + k] *
                             kernel[static_cast<std::size_t>(offset)];
                    }
                }
                temp[static_cast<std::size_t>(y) * width + x] = v;
            }
        }
        std::vector<std::uint32_t> pixels(count);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float v{};
                for (int k = -pad; k <= pad; ++k) {
                    if (y + k >= 0 && y + k < height) {
                        const int offset = k + pad, source_y = y + k;
                        v += temp[static_cast<std::size_t>(source_y) * width + x] *
                             kernel[static_cast<std::size_t>(offset)];
                    }
                }
                pixels[static_cast<std::size_t>(y) * width + x] =
                    static_cast<std::uint32_t>(std::clamp(v * 255, 0.0F, 255.0F)) << 24U;
            }
        }
        GlowCache cached{b.width,
                         b.height,
                         radius,
                         dpi,
                         content_scale_,
                         static_cast<float>(full_width / physical_scale),
                         static_cast<float>(full_height / physical_scale),
                         {},
                         bytes};
        if (resource_probe_) {
            checked(resource_probe_(ResourceKind::glow, static_cast<unsigned>(count)));
        }
        checked(drawing_->CreateBitmap(
            D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height)), pixels.data(),
            static_cast<UINT32>(width) * 4,
            D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi,
                dpi),
            &cached.bitmap));
        glows_.push_back(std::move(cached));
        found = std::prev(glows_.end());
    } else if (std::next(found) != glows_.end()) {
        std::rotate(found, std::next(found), glows_.end());
        found = std::prev(glows_.end());
    }
    const float pad = glow_padding_dips(content_scale_, dpi);
    hue.a = alpha;
    brush_->SetColor(native(hue));
    drawing_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    const auto destination = D2D1::RectF(b.x - pad, b.y - pad, b.x - pad + found->extent_width,
                                         b.y - pad + found->extent_height);
    drawing_->FillOpacityMask(found->bitmap.Get(), brush_.Get(), D2D1_OPACITY_MASK_CONTENT_GRAPHICS,
                              &destination);
    drawing_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}
IDWriteTextLayout *Renderer::text_layout(const std::wstring &text, float width, float height,
                                         unsigned font) {
    if (cursor_ >= text_cache_.size()) {
        text_cache_.emplace_back();
    }
    return text_layout(text_cache_[cursor_++], text, width, height, font);
}
IDWriteTextLayout *Renderer::text_layout(TextCache &c, const std::wstring &text, float width,
                                         float height, unsigned font) {
    if (!c.layout || c.text != text || c.width != width || c.height != height || c.font != font) {
        c = {text, width, height, font, {}};
        if (resource_probe_) {
            checked(resource_probe_(ResourceKind::text, font));
        }
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        checked(write_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                                         fonts_[font].Get(), width, height, &layout));
        DWRITE_TEXT_METRICS metrics{};
        checked(layout->GetMetrics(&metrics));
        c.advance = metrics.width;
        if (font < 2) {
            // CPU hover and component readouts are single-line, no-wrap layouts.
            DWRITE_LINE_METRICS line{};
            UINT32 count{};
            checked(layout->GetLineMetrics(&line, 1, &count));
            c.baseline = metrics.top + line.baseline;
        }
        if (font == 4) {
            DWRITE_OVERHANG_METRICS overhang{};
            checked(layout->GetOverhangMetrics(&overhang));
            c.temperature_x = (width - metrics.width) / 2;
            // Negative overhang is whitespace inside the layout box. Translate
            // by its inverse to put the visible glyph bottom on the box bottom.
            c.temperature_bottom_adjustment = -overhang.bottom;
        }
        c.layout = std::move(layout);
    }
    return c.layout.Get();
}
void Renderer::readout(std::size_t block, const Snapshot &s, const Settings &settings,
                       Clock::time_point now) {
    const auto &component = layout_.blocks[block];
    auto box = component.readout;
    const OverlayTransform transform(drawing_.Get(), box, layout_edge_);
    if (!horizontal(layout_edge_)) {
        box = {0, 0, box.height, box.width};
    }
    const auto values = component_readout(component.kind, component.disk, s, settings, now);
    auto &cache = readout_cache_[block];
    const float line_height = 11 * content_scale_;
    const float top = box.y + (box.height - strip_style::kReadoutBand * content_scale_) / 2;
    const auto draw = [&](const ReadoutPart &part, std::size_t slot, float x, float y,
                          unsigned font) {
        if (part.text.empty()) {
            return 0.0F;
        }
        // The column clip owns the final bounds. A stable layout width prevents a
        // changing memory/read value from recreating the following decode/write text.
        auto *text = text_layout(cache[slot], part.text, box.width, line_height, font);
        brush_->SetColor(native(high_contrast_ ? foreground_ : part.color));
        drawing_->DrawTextLayout({x, y - cache[slot].baseline}, text, brush_.Get(),
                                 D2D1_DRAW_TEXT_OPTIONS_CLIP);
        return cache[slot].advance;
    };
    draw(values.primary, 0, box.x, top + strip_style::kReadoutPrimaryBaseline * content_scale_, 0);
    const float secondary_y = top + strip_style::kReadoutSecondaryBaseline * content_scale_;
    const auto advance = draw(values.secondary[0], 1, box.x, secondary_y, 1);
    draw(values.secondary[1], 2, box.x + advance + (advance > 0 ? 5.4F * content_scale_ : 0),
         secondary_y, 1);
}
void Renderer::overlay(std::size_t block, const Snapshot &s, const Settings &settings,
                       Clock::time_point now) {
    auto b = layout_.blocks[block].graphic;
    const OverlayTransform transform(drawing_.Get(), b, layout_edge_);
    if (!horizontal(layout_edge_)) {
        b = {0, 0, b.height, b.width};
    }
    const auto draw = [&](IDWriteTextLayout *text, float x, float y, Color color) {
        brush_->SetColor(native(high_contrast_ ? foreground_ : color));
        drawing_->DrawTextLayout({x, y}, text, brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    if (block == 0) {
        auto value = cpu_summary(s, settings, now);
        const auto split = value.find(L"  P ");
        const auto total = value.substr(0, split);
        const float primary_height = split == std::wstring::npos ? b.height : b.height * 0.55F;
        brush_->SetColor(native(high_contrast_ ? background_ : rgb(0x0E1116, 0.78F)));
        drawing_->FillRoundedRectangle(
            D2D1::RoundedRect(rectangle(b), 2 * content_scale_, 2 * content_scale_), brush_.Get());
        auto *text = text_layout(total, b.width, primary_height, 0);
        checked(text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
        draw(text, b.x, b.y, kPrimary);
        if (split != std::wstring::npos) {
            text = text_layout(value.substr(split + 2), b.width, b.height - primary_height, 1);
            checked(text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
            draw(text, b.x, b.y + primary_height, kSecondary);
        }
        return;
    }
    if (layout_.blocks[block].kind == 1) {
        const auto color = gauge_palette(Gauge::ram).text;
        const float available = b.width - 10 * content_scale_;
        const auto full = ram_summary(s, now, settings.interval_ms);
        auto *text = text_layout(full, 1000 * content_scale_, b.height, 2);
        DWRITE_TEXT_METRICS measured{};
        checked(text->GetMetrics(&measured));
        const bool one_line = measured.width <= available;
        const float overlay_width = one_line ? measured.width + 10 * content_scale_ : b.width;
        const float left = b.x + b.width - overlay_width;
        brush_->SetColor(native(high_contrast_ ? background_ : rgb(0x0E1116, 0.85F)));
        drawing_->FillRoundedRectangle(D2D1::RoundedRect({left, b.y, b.x + b.width, b.y + b.height},
                                                         4 * content_scale_, 4 * content_scale_),
                                       brush_.Get());
        if (one_line) {
            draw(text, left + 5 * content_scale_, b.y, color);
        } else {
            auto capacity = ram_capacity_text(s, now, settings.interval_ms);
            text = text_layout(capacity, 1000 * content_scale_, b.height / 2, 3);
            checked(text->GetMetrics(&measured));
            if (measured.width > available) {
                capacity = L"…"; // Exact capacity is retained in the tooltip and readings list.
            }
            text = text_layout(capacity, available, b.height / 2, 3);
            checked(text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING));
            draw(text, b.x + 5 * content_scale_, b.y, color);
            text = text_layout(compact_value(aged_metric(s.gauges[0], now, settings.interval_ms)),
                               available, b.height / 2, 3);
            checked(text->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING));
            draw(text, b.x + 5 * content_scale_, b.y + b.height / 2, color);
        }
        return;
    }
    struct Part {
        IDWriteTextLayout *text{};
        float width{};
        Color color;
    };
    std::vector<Part> parts;
    float total{};
    for (auto gauge : layout_.blocks[block].types) {
        if (gauge == Gauge::count) {
            break;
        }
        auto metric =
            aged_metric(block_metric(layout_.blocks[block], gauge, s), now, settings.interval_ms);
        if (gauge == Gauge::gpu_memory && presented_metric(metric).status == Status::valid &&
            presented_metric(s.gpu_memory_bytes).status == Status::valid) {
            metric = aged_metric(s.gpu_memory_bytes, now, settings.interval_ms);
        }
        const auto value = compact_value(metric);
        auto *text = text_layout(value, 1000 * content_scale_, b.height, block == 1 ? 2U : 3U);
        DWRITE_TEXT_METRICS measured{};
        checked(text->GetMetrics(&measured));
        const float added = measured.width + (parts.empty() ? 0 : 5 * content_scale_);
        if (total + added + 10 * content_scale_ > b.width) {
            if (parts.empty()) {
                text = text_layout(L"…", b.width - 10 * content_scale_, b.height, 3);
                checked(text->GetMetrics(&measured));
                parts.push_back({text, measured.width, gauge_palette(gauge).text});
                total = measured.width;
            }
            break;
        }
        parts.push_back({text, measured.width, gauge_palette(gauge).text});
        total += added;
    }
    if (parts.empty()) {
        return;
    }
    const float overlay_width = std::min(b.width, total + 10 * content_scale_);
    const float left = b.x + b.width - overlay_width;
    brush_->SetColor(native(high_contrast_ ? background_ : rgb(0x0E1116, 0.85F)));
    drawing_->FillRoundedRectangle(D2D1::RoundedRect({left, b.y, b.x + b.width, b.y + b.height},
                                                     3 * content_scale_, 3 * content_scale_),
                                   brush_.Get());
    float x = left + 5 * content_scale_;
    for (const auto &p : parts) {
        draw(p.text, x, b.y, p.color);
        x += p.width + 5 * content_scale_;
    }
}
HRESULT Renderer::render_to(ID2D1RenderTarget *target, const Snapshot &snapshot,
                            const Settings &settings, Edge edge, bool fallback, float scale,
                            bool hover, bool high_contrast, Clock::time_point now) {
    const bool horizontal = loadbar::horizontal(edge);
    bool begun = false;
    const auto failed_draw = [&](HRESULT code) {
        if (begun) {
            (void)target->EndDraw();
        }
        discard();
        return code;
    };
    try {
        const auto size = target->GetSize();
        float current_dpi{}, current_y_dpi{};
        target->GetDpi(&current_dpi, &current_y_dpi);
        const bool topology_changed = !std::ranges::equal(
            snapshot.processors, topology_, [](const Processor &p, const auto &cached) {
                return std::tie(p.core, p.id, p.mapped, p.efficiency_class) == cached;
            });
        if (disk_layout_changed(snapshot, settings) || topology_changed ||
            temperature_layout_changed(snapshot, settings.show_temperatures) ||
            size.width != layout_width_ || size.height != layout_height_ ||
            scale != layout_scale_ || current_dpi != layout_dpi_ || edge != layout_edge_ ||
            settings.alignment != layout_alignment_ ||
            settings.cpu_squares != layout_cpu_squares_ ||
            settings.always_show_readout != layout_always_readout_ ||
            gpu_memory_visible(snapshot) != layout_gpu_memory_ ||
            settings.gpu_visible != layout_gpu_visible_ ||
            settings.network_visible != layout_network_visible_) {
            layout_ =
                make_snapshot_layout(size.width, size.height, edge, snapshot, scale, settings);
            layout_disks_.clear();
            layout_disks_.reserve(snapshot.disks.size());
            for (const auto &disk : snapshot.disks) {
                layout_disks_.emplace_back(disk.id, disk_visible(disk.id, settings));
            }
            snap_layout(layout_, current_dpi);
            layout_dpi_ = current_dpi;
            topology_.clear();
            topology_.reserve(snapshot.processors.size());
            for (const auto &p : snapshot.processors) {
                topology_.emplace_back(p.core, p.id, p.mapped, p.efficiency_class);
            }
            layout_width_ = size.width;
            layout_height_ = size.height;
            layout_scale_ = scale;
            layout_edge_ = edge;
            layout_alignment_ = settings.alignment;
            layout_gpu_visible_ = settings.gpu_visible;
            layout_cpu_squares_ = settings.cpu_squares;
            layout_gpu_memory_ = gpu_memory_visible(snapshot);
            layout_network_visible_ = settings.network_visible;
            layout_show_temperatures_ = settings.show_temperatures;
            layout_always_readout_ = settings.always_show_readout;
            glows_.clear();
            text_cache_.clear();
            temperature_cache_.clear();
            readout_cache_.clear();
            ++layout_revision_;
        }
        checked(prepare(target, layout_.fits ? layout_.content_scale : scale,
                        layout_.temperature_font_size, high_contrast));
        cursor_ = 0;
        target->BeginDraw();
        begun = true;
        target->Clear(native(background_));
        if (!layout_.fits) {
            auto *text =
                text_layout(L"Layout needs more space — open Settings", size.width, size.height, 2);
            brush_->SetColor(native(foreground_));
            target->DrawTextLayout({0, 0}, text, brush_.Get());
        } else {
            core_readings_.resize(layout_.cores.size());
            block_readings_.resize(layout_.blocks.size());
            if (settings.always_show_readout) {
                readout_cache_.resize(layout_.blocks.size());
            }
            if (settings.show_temperatures) {
                temperature_cache_.resize(layout_.blocks.size());
            }
            for (std::size_t index = 0; index < layout_.cores.size(); ++index) {
                const auto &core = layout_.cores[index];
                const auto m = core_metric(core, snapshot, now, settings.interval_ms);
                core_readings_[index] = {m.value, 0, m.status, false};
                if (m.status == Status::valid && m.value > 85) {
                    glow(core.label, heat_color(m.value),
                         strip_style::core_radius(core.label.width, core.label.height,
                                                  content_scale_) /
                             content_scale_,
                         0.60F);
                }
            }
            for (std::size_t index = 0; index < layout_.blocks.size(); ++index) {
                const auto &block = layout_.blocks[index];
                for (std::size_t i = 0; i < block.types.size(); ++i) {
                    const auto g = block.types[i];
                    if (g == Gauge::count) {
                        break;
                    }
                    const auto m = presented_metric(
                        aged_metric(block_metric(block, g, snapshot), now, settings.interval_ms));
                    auto &reading = block_readings_[index][i];
                    reading = {m.value, graphic_fraction(g, m), m.status, glows(g, m)};
                    if (reading.glow) {
                        glow(block.meters[i], gauge_palette(g).hue, 3, 0.55F);
                    }
                }
            }
            for (std::size_t index = 0; index < layout_.cores.size(); ++index) {
                const auto &core = layout_.cores[index];
                const auto &m = core_readings_[index];
                // Each logical processor has its own solid color, with no utilization bar.
                auto color = inactive_;
                if (high_contrast_) {
                    // Stronger contrast means greater load on both light and dark themes.
                    // Keep text and the cell outline at the unblended system foreground.
                    const auto t = m.status == Status::valid
                                       ? static_cast<float>(fill_fraction(m.value, 100))
                                       : 0.0F;
                    color = {background_.r + (foreground_.r - background_.r) * t,
                             background_.g + (foreground_.g - background_.g) * t,
                             background_.b + (foreground_.b - background_.b) * t, 1};
                } else if (m.status == Status::valid) {
                    color = heat_color(m.value);
                }
                const float radius =
                    strip_style::core_radius(core.label.width, core.label.height, content_scale_);
                const auto shape = D2D1::RoundedRect(rectangle(core.label), radius, radius);
                brush_->SetColor(native(color));
                target->FillRoundedRectangle(shape, brush_.Get());
                if (high_contrast_) {
                    // An idle cell must remain visible without an internal utilization bar.
                    const float inset = content_scale_ / 2;
                    auto outline = shape;
                    outline.rect.left += inset;
                    outline.rect.top += inset;
                    outline.rect.right -= inset;
                    outline.rect.bottom -= inset;
                    outline.radiusX -= inset;
                    outline.radiusY -= inset;
                    brush_->SetColor(native(foreground_));
                    target->DrawRoundedRectangle(outline, brush_.Get(), content_scale_);
                }
                if (m.status != Status::valid) {
                    status_mark(core.label, m.status);
                }
            }
            for (std::size_t index = 0; index < layout_.blocks.size(); ++index) {
                const auto &block = layout_.blocks[index];
                bool valid = false;
                if (block.kind == 0) {
                    for (const auto &p : snapshot.processors) {
                        valid = valid || presented_metric(
                                             aged_metric(p.utilization, now, settings.interval_ms))
                                                 .status == Status::valid;
                    }
                }
                for (std::size_t i = 0; i < block.types.size(); ++i) {
                    const auto g = block.types[i];
                    if (g == Gauge::count) {
                        break;
                    }
                    const auto &m = block_readings_[index][i];
                    const auto palette = gauge_palette(g);
                    const auto fraction = m.fraction;
                    const auto hue = palette.hue;
                    meter(block.meters[i], m.status, fraction,
                          g == Gauge::gpu_decode ? hue : tone(palette.track, hue, fraction),
                          palette.track,
                          block.kind == 1 ? 4.0F
                          : i == 0        ? 3.0F
                                          : 2.0F);
                    valid = valid || m.status == Status::valid;
                }
                auto color = high_contrast_ ? foreground_ : component_icon_color(block.kind);
                const auto *temperature = block_temperature(block, snapshot);
                const bool show_temperature =
                    settings.show_temperatures && temperature && block.temperature.width > 0;
                if (show_temperature) {
                    const auto value = presented_metric(temperature->metric).value;
                    const auto appearance = temperature_appearance(block.kind, value);
                    const auto hue = high_contrast_ ? foreground_ : appearance.text;
                    color = high_contrast_ ? foreground_ : appearance.icon;
                    if (!high_contrast_ && appearance.glow_alpha > 0) {
                        glow(block.icon, hue, 3, appearance.glow_alpha);
                    }
                    const auto &box = block.temperature;
                    const auto rounded = std::round(value);
                    auto &cached = temperature_cache_[index];
                    auto *text =
                        text_layout(cached, std::format(L"{:.0f}°C", rounded == 0 ? 0.0 : rounded),
                                    box.width, box.height, 4);
                    const float y = box.y + (horizontal ? cached.temperature_bottom_adjustment : 0);
                    brush_->SetColor(native(hue));
                    target->DrawTextLayout({box.x + cached.temperature_x, y}, text, brush_.Get());
                    valid = true;
                }
                if (!valid) {
                    color.a = 0.4F;
                }
                draw_icon(block.kind, block.icon, color);
                if (settings.always_show_readout) {
                    readout(index, snapshot, settings, now);
                } else if (hover && settings.show_hover_info) {
                    overlay(index, snapshot, settings, now);
                }
            }
            brush_->SetColor(native(high_contrast_ ? foreground_ : kDivider));
            target->FillRectangle(rectangle(layout_.divider), brush_.Get());
            if (fallback) {
                brush_->SetColor(native(high_contrast_ ? foreground_ : rgb(0xF2B840)));
                target->FillEllipse(D2D1::Ellipse({4 * content_scale_, 4 * content_scale_},
                                                  2 * content_scale_, 2 * content_scale_),
                                    brush_.Get());
            }
        }
        brush_->SetColor(native(high_contrast_ ? foreground_ : kBorder));
        float dpi{}, dy{};
        target->GetDpi(&dpi, &dy);
        const float pixel = 96 / dpi;
        if (horizontal) {
            target->FillRectangle({0, 0, size.width, pixel}, brush_.Get());
            target->FillRectangle({0, size.height - pixel, size.width, size.height}, brush_.Get());
        } else {
            target->FillRectangle({0, 0, pixel, size.height}, brush_.Get());
            target->FillRectangle({size.width - pixel, 0, size.width, size.height}, brush_.Get());
        }
        // Retain hover layouts while at rest. Layout/font changes clear this bounded
        // positional cache, and a changed string replaces its existing slot.
        const auto result = target->EndDraw();
        begun = false;
        if (FAILED(result)) {
            discard();
        }
        return result;
    } catch (const GraphicsFailure &failure) {
        return failed_draw(failure.code);
    } catch (const std::bad_alloc &) {
        return failed_draw(E_OUTOFMEMORY);
    } catch (...) {
        return failed_draw(E_FAIL);
    }
}
HRESULT Renderer::paint(HWND window, const Snapshot &snapshot, const Settings &settings, Edge edge,
                        bool fallback, float scale, bool hover) {
    if (!factory_) {
        const auto hr =
            D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf());
        if (FAILED(hr)) {
            return hr;
        }
    }
    RECT bounds{};
    if (!GetClientRect(window, &bounds)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    const auto size = D2D1::SizeU(static_cast<UINT32>(std::max(1L, bounds.right)),
                                  static_cast<UINT32>(std::max(1L, bounds.bottom)));
    HRESULT result = S_OK;
    if (!target_) {
        result = factory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)),
            D2D1::HwndRenderTargetProperties(window, size), &target_);
    } else if (target_->GetPixelSize().width != size.width ||
               target_->GetPixelSize().height != size.height) {
        result = target_->Resize(size);
    }
    if (FAILED(result)) {
        discard();
        return result;
    }
    const float dpi = static_cast<float>(GetDpiForWindow(window));
    target_->SetDpi(dpi, dpi);
    if (!system_high_contrast_) {
        HIGHCONTRASTW contrast{};
        contrast.cbSize = sizeof(contrast);
        if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0)) {
            system_high_contrast_ = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
        }
    }
    // Keep a local reference: failure recovery may release the member while finishing a draw.
    const auto target = target_;
    return render_to(target.Get(), snapshot, settings, edge, fallback, scale, hover,
                     system_high_contrast_.value_or(false), Clock::now());
}
std::size_t Renderer::tooltip_target(float x, float y) const noexcept {
    const auto contains = [=](Box b) {
        return x >= b.x && y >= b.y && x < b.x + b.width && y < b.y + b.height;
    };
    for (std::size_t i = 0; i < layout_.cores.size(); ++i) {
        if (contains(layout_.cores[i].label)) {
            return i + 1;
        }
    }
    for (std::size_t i = 0; i < layout_.blocks.size(); ++i) {
        if (contains(layout_.blocks[i].bounds)) {
            return layout_.cores.size() + i + 1;
        }
    }
    return 0;
}
std::wstring Renderer::tooltip(float x, float y, const Snapshot &snapshot,
                               const Settings &settings) const {
    return settings.show_tooltips ? metric_tooltip(layout_, x, y, snapshot, settings, Clock::now())
                                  : L"";
}
} // namespace loadbar
