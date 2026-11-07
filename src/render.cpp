#include "render.h"

#include <d2d1effects.h>

namespace tv {

Renderer::~Renderer() {
    for (auto& [key, format] : formats_) {
        if (format) format->Release();
    }
    formats_.clear();
}

bool Renderer::init(HWND hwnd) {
    hwnd_ = hwnd;
    const HRESULT factory_hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf());
    if (FAILED(factory_hr)) {
        return false;
    }
    const HRESULT write_hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                                 reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()));
    if (FAILED(write_hr)) {
        return false;
    }
    D2D1_STROKE_STYLE_PROPERTIES props{};
    props.startCap = D2D1_CAP_STYLE_ROUND;
    props.endCap = D2D1_CAP_STYLE_ROUND;
    props.dashCap = D2D1_CAP_STYLE_ROUND;
    props.lineJoin = D2D1_LINE_JOIN_ROUND;
    props.miterLimit = 2.0f;
    factory_->CreateStrokeStyle(props, nullptr, 0, stroke_style_.GetAddressOf());
    return true;
}

bool Renderer::resize(UINT width, UINT height, float dpi) {
    if (!factory_) {
        return false;
    }
    if (width == 0 || height == 0) return false;
    dpi_ = dpi > 0.0f ? dpi : 96.0f;
    if (target_ && width == width_ && height == height_) {
        target_->SetDpi(dpi_, dpi_);
        return true;
    }
    width_ = width;
    height_ = height;
    target_.Reset();
    visual_layer_.Reset();
    D2D1_RENDER_TARGET_PROPERTIES rt_props = D2D1::RenderTargetProperties();
    rt_props.dpiX = dpi_;
    rt_props.dpiY = dpi_;
    rt_props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE);
    const D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_props = D2D1::HwndRenderTargetProperties(
        hwnd_, D2D1::SizeU(width, height), D2D1_PRESENT_OPTIONS_NONE);
    const HRESULT hr = factory_->CreateHwndRenderTarget(rt_props, hwnd_props, target_.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }
    ++target_generation_;
    target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    brush_.Reset();
    target_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), brush_.GetAddressOf());
    return true;
}

void Renderer::begin(const Color& clear) {
    if (!target_) return;
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    target_->Clear(d2d_color(clear));
    clip_depth_ = 0;
}

void Renderer::end() {
    if (!target_) return;
    const HRESULT hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        target_.Reset();
        visual_layer_.Reset();
        brush_.Reset();
        ++target_generation_;
    }
}

void Renderer::set_brush(const Color& color) {
    if (!brush_) {
        if (!target_) return;
        target_->CreateSolidColorBrush(d2d_color(color), brush_.GetAddressOf());
        return;
    }
    brush_->SetColor(d2d_color(color));
}

void Renderer::fill_rect(const Rect& rect, const Color& color) {
    if (!target_ || rect.empty() || color.a <= 0.001f) return;
    set_brush(color);
    target_->FillRectangle(rect.d2d(), brush_.Get());
}

void Renderer::fill_round(const Rect& rect, float radius, const Color& color) {
    if (!target_ || rect.empty() || color.a <= 0.001f) return;
    set_brush(color);
    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect.d2d(), radius, radius);
    target_->FillRoundedRectangle(rr, brush_.Get());
}

void Renderer::fill_round_gradient(const Rect& rect, float radius, const Color& top, const Color& bottom) {
    if (!target_ || rect.empty()) return;
    ComPtr<ID2D1GradientStopCollection> stops;
    const D2D1_GRADIENT_STOP stop_data[2] = {
        {0.0f, d2d_color(top)},
        {1.0f, d2d_color(bottom)},
    };
    if (FAILED(target_->CreateGradientStopCollection(stop_data, 2, stops.GetAddressOf()))) return;
    ComPtr<ID2D1LinearGradientBrush> gradient;
    const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props = D2D1::LinearGradientBrushProperties(
        D2D1::Point2F(rect.left, rect.top), D2D1::Point2F(rect.left, rect.bottom));
    if (FAILED(target_->CreateLinearGradientBrush(props, stops.Get(), gradient.GetAddressOf()))) return;
    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect.d2d(), radius, radius);
    target_->FillRoundedRectangle(rr, gradient.Get());
}

void Renderer::stroke_round(const Rect& rect, float radius, const Color& color, float width) {
    if (!target_ || rect.empty() || color.a <= 0.001f) return;
    set_brush(color);
    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rect.d2d(), radius, radius);
    target_->DrawRoundedRectangle(rr, brush_.Get(), width, stroke_style_.Get());
}

void Renderer::stroke_line(float x1, float y1, float x2, float y2, const Color& color, float width) {
    if (!target_ || color.a <= 0.001f) return;
    set_brush(color);
    target_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush_.Get(), width, stroke_style_.Get());
}

void Renderer::fill_circle(float cx, float cy, float radius, const Color& color) {
    if (!target_ || radius <= 0.0f) return;
    set_brush(color);
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), brush_.Get());
}

void Renderer::stroke_circle(float cx, float cy, float radius, const Color& color, float width) {
    if (!target_ || radius <= 0.0f) return;
    set_brush(color);
    target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), brush_.Get(), width,
                         stroke_style_.Get());
}

void Renderer::shadow(const Rect& rect, float radius, float spread, const Color& color) {
    if (!target_ || color.a <= 0.001f) return;
    const float expand = spread * 0.65f;
    fill_round(rect.expand(expand).offset(0.0f, 1.5f), radius + expand,
               Color(0.0f, 0.0f, 0.0f, color.a * 0.55f));
    fill_round(rect.expand(spread * 0.22f).offset(0.0f, 1.0f), radius + spread * 0.22f,
               Color(0.0f, 0.0f, 0.0f, color.a * 0.25f));
}

void Renderer::push_clip(const Rect& rect) {
    if (!target_) return;
    target_->PushAxisAlignedClip(rect.d2d(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ++clip_depth_;
}

void Renderer::pop_clip() {
    if (!target_ || clip_depth_ <= 0) return;
    target_->PopAxisAlignedClip();
    --clip_depth_;
}

void Renderer::push_visual(float opacity, float dx, float dy) {
    if (!target_) return;
    target_->GetTransform(&visual_previous_);
    target_->SetTransform(D2D1::Matrix3x2F::Translation(dx, dy) * visual_previous_);
    visual_active_ = false;
    if (opacity < 0.999f) {
        if (!visual_layer_) target_->CreateLayer(nullptr, visual_layer_.GetAddressOf());
        if (visual_layer_) {
            auto parameters = D2D1::LayerParameters();
            parameters.opacity = std::clamp(opacity, 0.0f, 1.0f);
            target_->PushLayer(parameters, visual_layer_.Get());
            visual_active_ = true;
        }
    }
}

void Renderer::pop_visual() {
    if (!target_) return;
    if (visual_active_) target_->PopLayer();
    target_->SetTransform(visual_previous_);
    visual_active_ = false;
}

void Renderer::push_scale(float scale, float cx, float cy) {
    if (!target_) return;
    D2D1_MATRIX_3X2_F current;
    target_->GetTransform(&current);
    transforms_.push_back(current);
    target_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(cx, cy)) * current);
}

void Renderer::pop_transform() {
    if (!target_ || transforms_.empty()) return;
    target_->SetTransform(transforms_.back());
    transforms_.pop_back();
}

bool Renderer::load_image(UINT resource_id, const wchar_t* type) {
    images_.erase(resource_id);
    ImageAsset asset;
    if (!wic_) {
        const HRESULT co_hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                               IID_PPV_ARGS(wic_.GetAddressOf()));
        if (FAILED(co_hr)) return false;
    }
    HRSRC found = FindResourceW(nullptr, MAKEINTRESOURCEW(resource_id), type);
    if (!found) return false;
    HGLOBAL loaded = LoadResource(nullptr, found);
    if (!loaded) return false;
    const DWORD bytes = SizeofResource(nullptr, found);
    const auto* data = static_cast<const BYTE*>(LockResource(loaded));
    if (!data || bytes == 0) return false;
    asset.bytes.assign(data, data + bytes);

    ComPtr<IWICStream> stream;
    if (FAILED(wic_->CreateStream(stream.GetAddressOf()))) return false;
    if (FAILED(stream->InitializeFromMemory(asset.bytes.data(), bytes))) return false;
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic_->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand,
                                             decoder.GetAddressOf()))) {
        return false;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) return false;
    if (FAILED(wic_->CreateFormatConverter(asset.source.GetAddressOf()))) return false;
    const HRESULT conv_hr = asset.source->Initialize(
        frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
        WICBitmapPaletteTypeCustom);
    if (FAILED(conv_hr)) {
        return false;
    }
    images_.emplace(resource_id, std::move(asset));
    return true;
}

void Renderer::image(const Rect& rect, float opacity) {
    image(102, rect, opacity);
}

void Renderer::image(UINT resource_id, const Rect& rect, float opacity) {
    const auto found = images_.find(resource_id);
    if (!target_ || found == images_.end() || rect.empty()) return;
    auto& asset = found->second;
    if (!asset.bitmap || asset.generation != target_generation_) {
        asset.bitmap.Reset();
        if (SUCCEEDED(target_->CreateBitmapFromWicBitmap(asset.source.Get(), nullptr,
                                                         asset.bitmap.GetAddressOf()))) {
            asset.generation = target_generation_;
        }
        if (!asset.bitmap) return;
    }
    const D2D1_RECT_F dest = D2D1::RectF(rect.left, rect.top, rect.right, rect.bottom);
    target_->DrawBitmap(asset.bitmap.Get(), dest, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

IDWriteTextFormat* Renderer::format_for(const TextStyle& style) {
    if (!dwrite_) return nullptr;
    wchar_t key[128] = {};
    swprintf_s(key, L"%.1f|%d|%d|%d|%d|%d|%.2f", style.size, static_cast<int>(style.weight),
               static_cast<int>(style.align), static_cast<int>(style.valign),
               style.wrap ? 1 : 0, style.mono ? 1 : 0, style.line_spacing);
    const auto found = formats_.find(key);
    if (found != formats_.end()) return found->second;
    const wchar_t* family = style.mono ? L"Cascadia Mono" : (style.size >= 17.0f ? L"Segoe UI Variable Display" : L"Segoe UI Variable Text");
    IDWriteTextFormat* format = nullptr;
    if (FAILED(dwrite_->CreateTextFormat(family, nullptr, static_cast<DWRITE_FONT_WEIGHT>(style.weight),
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         style.size, L"", &format))) {
        return nullptr;
    }
    format->SetTextAlignment(style.align == TextAlign::Center ? DWRITE_TEXT_ALIGNMENT_CENTER :
                             style.align == TextAlign::Right ? DWRITE_TEXT_ALIGNMENT_TRAILING :
                                                               DWRITE_TEXT_ALIGNMENT_LEADING);
    format->SetParagraphAlignment(style.valign == TextVAlign::Middle ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER :
                                  style.valign == TextVAlign::Bottom ? DWRITE_PARAGRAPH_ALIGNMENT_FAR :
                                                                       DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    format->SetWordWrapping(style.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (std::fabs(style.line_spacing - 1.0f) > 0.01f) {
        format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
                               style.size * 1.32f * style.line_spacing,
                               style.size * 1.08f * style.line_spacing);
    }
    formats_[key] = format;
    return format;
}

void Renderer::text(std::wstring_view value, const Rect& rect, const TextStyle& style, const Color& color) {
    if (!target_ || value.empty() || rect.empty() || color.a <= 0.001f) return;
    IDWriteTextFormat* format = format_for(style);
    if (!format) return;
    set_brush(color);
    if (style.ellipsis) {
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(dwrite_->CreateTextLayout(value.data(), static_cast<UINT32>(value.size()),
                                             format, rect.width(), rect.height(), layout.GetAddressOf()))) {
            return;
        }
        DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        ComPtr<IDWriteInlineObject> sign;
        if (SUCCEEDED(dwrite_->CreateEllipsisTrimmingSign(format, sign.GetAddressOf()))) {
            layout->SetTrimming(&trimming, sign.Get());
        }
        target_->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout.Get(), brush_.Get(),
                                D2D1_DRAW_TEXT_OPTIONS_CLIP);
        return;
    }
    target_->DrawTextW(value.data(), static_cast<UINT32>(value.size()), format, rect.d2d(), brush_.Get(),
                       D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
}

D2D1_SIZE_F Renderer::measure(std::wstring_view value, const TextStyle& style, float max_width) {
    if (!dwrite_ || value.empty()) return D2D1::SizeF(0.0f, 0.0f);
    IDWriteTextFormat* format = format_for(style);
    if (!format) return D2D1::SizeF(0.0f, 0.0f);
    ComPtr<IDWriteTextLayout> layout;
    const float width = max_width > 1.0f ? max_width : 4096.0f;
    if (FAILED(dwrite_->CreateTextLayout(value.data(), static_cast<UINT32>(value.size()), format, width,
                                         4096.0f, layout.GetAddressOf()))) {
        return D2D1::SizeF(0.0f, 0.0f);
    }
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return D2D1::SizeF(metrics.widthIncludingTrailingWhitespace, metrics.height);
}

float Renderer::text_width(std::wstring_view value, float size, FontWeight weight, bool mono) {
    TextStyle style;
    style.size = size;
    style.weight = weight;
    style.mono = mono;
    style.wrap = false;
    return measure(value, style, 4096.0f).width;
}

size_t Renderer::text_index_at(std::wstring_view value, const TextStyle& style, const Rect& box,
                               float x, float y) {
    if (!dwrite_ || value.empty() || box.empty()) return 0;
    IDWriteTextFormat* format = format_for(style);
    if (!format) return 0;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(value.data(), static_cast<UINT32>(value.size()), format,
                                         box.width(), box.height(), layout.GetAddressOf()))) {
        return 0;
    }
    BOOL trailing = FALSE;
    BOOL inside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    if (FAILED(layout->HitTestPoint(x - box.left, y - box.top, &trailing, &inside, &metrics))) return 0;
    size_t index = metrics.textPosition;
    if (trailing) index += metrics.length;
    return std::min(index, value.size());
}

Rect Renderer::text_caret(std::wstring_view value, const TextStyle& style, const Rect& box, size_t index) {
    if (!dwrite_ || box.empty()) return {};
    IDWriteTextFormat* format = format_for(style);
    if (!format) return {};
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(value.data(), static_cast<UINT32>(value.size()), format,
                                         box.width(), box.height(), layout.GetAddressOf()))) {
        return {};
    }
    FLOAT x = 0.0f;
    FLOAT y = 0.0f;
    DWRITE_HIT_TEST_METRICS metrics{};
    if (FAILED(layout->HitTestTextPosition(static_cast<UINT32>(index), FALSE, &x, &y, &metrics))) {
        return {};
    }
    const float height = metrics.height > 0.0f ? metrics.height : style.size * 1.3f;
    return Rect{box.left + x, box.top + y, box.left + x + 1.5f, box.top + y + height};
}

ComPtr<ID2D1PathGeometry> Renderer::make_geometry() {
    ComPtr<ID2D1PathGeometry> geometry;
    if (factory_) factory_->CreatePathGeometry(geometry.GetAddressOf());
    return geometry;
}

void Renderer::stroke_geometry(ID2D1Geometry* geometry, const Color& color, float width) {
    if (!target_ || !geometry) return;
    set_brush(color);
    target_->DrawGeometry(geometry, brush_.Get(), width, stroke_style_.Get());
}

void Renderer::fill_geometry(ID2D1Geometry* geometry, const Color& color) {
    if (!target_ || !geometry) return;
    set_brush(color);
    target_->FillGeometry(geometry, brush_.Get());
}

void Renderer::icon(Icon icon, const Rect& r, const Color& color, float stroke) {
    if (!target_ || r.empty() || color.a <= 0.001f) return;
    const float sx = r.width() / 24.0f;
    const float sy = r.height() / 24.0f;
    const auto P = [&](float x, float y) { return D2D1::Point2F(r.left + x * sx, r.top + y * sy); };
    const float sw = std::max(1.0f, stroke * std::min(sx, sy));
    ComPtr<ID2D1PathGeometry> geometry = make_geometry();
    if (!geometry) return;
    bool fill_icon = false;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geometry->Open(sink.GetAddressOf()))) return;

    const auto begin = [&](float x, float y, bool filled = false) {
        sink->BeginFigure(P(x, y), filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
    };
    const auto line = [&](float x, float y) { sink->AddLine(P(x, y)); };
    const auto bezier = [&](float x1, float y1, float x2, float y2, float x3, float y3) {
        sink->AddBezier(D2D1::BezierSegment(P(x1, y1), P(x2, y2), P(x3, y3)));
    };
    const auto end = [&](bool closed = true) {
        sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    };
    const auto arc = [&](float x, float y, float rx, float ry, bool sweep) {
        const D2D1_ARC_SEGMENT segment = D2D1::ArcSegment(
            P(x, y), D2D1::SizeF(rx * sx, ry * sy), 0.0f,
            sweep ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
            D2D1_ARC_SIZE_SMALL);
        sink->AddArc(segment);
    };
    const auto arc_large = [&](float x, float y, float rx, float ry, bool sweep) {
        const D2D1_ARC_SEGMENT segment = D2D1::ArcSegment(
            P(x, y), D2D1::SizeF(rx * sx, ry * sy), 0.0f,
            sweep ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
            D2D1_ARC_SIZE_LARGE);
        sink->AddArc(segment);
    };

    switch (icon) {
    case Icon::Ghost:
        begin(5, 19);
        line(5, 10);
        bezier(5, 7, 8, 3, 12, 3);
        bezier(16, 3, 19, 7, 19, 10);
        line(19, 19);
        line(16.4f, 17.2f);
        line(14.0f, 19.0f);
        line(11.8f, 17.2f);
        line(9.6f, 19.0f);
        line(7.3f, 17.2f);
        line(5, 19);
        end(true);
        break;
    case Icon::Network:
        begin(8, 18);
        line(8, 6);
        end(false);
        begin(8, 6);
        line(5.2f, 8.8f);
        line(10.8f, 8.8f);
        end(false);
        begin(16, 6);
        line(16, 18);
        end(false);
        begin(16, 18);
        line(13.2f, 15.2f);
        line(18.8f, 15.2f);
        end(false);
        break;
    case Icon::Stories: {
        begin(12, 3);
        arc(21, 12, 9, 9, true);
        arc(12, 21, 9, 9, true);
        arc(3, 12, 9, 9, true);
        arc(12, 3, 9, 9, true);
        end(true);
        ComPtr<ID2D1PathGeometry> play = make_geometry();
        ComPtr<ID2D1GeometrySink> play_sink;
        if (play && SUCCEEDED(play->Open(play_sink.GetAddressOf()))) {
            play_sink->BeginFigure(P(10, 8), D2D1_FIGURE_BEGIN_FILLED);
            play_sink->AddLine(P(16.5f, 12));
            play_sink->AddLine(P(10, 16));
            play_sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            play_sink->Close();
            fill_geometry(play.Get(), color);
        }
        break;
    }
    case Icon::Account:
        begin(12, 11);
        arc(16, 7, 4, 4, true);
        arc(12, 3, 4, 4, true);
        arc(8, 7, 4, 4, true);
        arc(12, 11, 4, 4, true);
        end(true);
        begin(5, 21);
        bezier(5, 16, 8, 13.5f, 12, 13.5f);
        bezier(16, 13.5f, 19, 16, 19, 21);
        end(false);
        break;
    case Icon::Messages:
        begin(7, 4);
        line(17, 4);
        bezier(19.8f, 4, 21, 5.5f, 21, 8);
        line(21, 14);
        bezier(21, 16.5f, 19.8f, 18, 17, 18);
        line(12, 18);
        line(8, 21);
        line(8, 18);
        line(7, 18);
        bezier(4.2f, 18, 3, 16.5f, 3, 14);
        line(3, 8);
        bezier(3, 5.5f, 4.2f, 4, 7, 4);
        end(true);
        break;
    case Icon::Tools:
        begin(7.5f, 17.5f);
        arc(11.5f, 15.5f, 4.0f, 4.0f, true);
        arc(7.5f, 11.5f, 4.0f, 4.0f, true);
        arc(3.5f, 15.5f, 4.0f, 4.0f, true);
        arc(7.5f, 19.5f, 4.0f, 4.0f, true);
        end(true);
        begin(10.5f, 12.5f);
        line(20, 3);
        line(17.5f, 3);
        line(14.5f, 6);
        end(false);
        break;
    case Icon::Advanced:
        begin(4, 7); line(20, 7); end(false);
        begin(4, 12); line(20, 12); end(false);
        begin(4, 17); line(20, 17); end(false);
        fill_circle(r.left + 9 * sx, r.top + 7 * sy, 2.2f * sx, color);
        fill_circle(r.left + 15 * sx, r.top + 12 * sy, 2.2f * sx, color);
        fill_circle(r.left + 11 * sx, r.top + 17 * sy, 2.2f * sx, color);
        break;
    case Icon::Roadmap:
        begin(4, 6); line(9, 4); line(15, 6); line(20, 4); line(20, 18);
        line(15, 20); line(9, 18); line(4, 20); line(4, 6); end(true);
        begin(9, 4); line(9, 18); end(false);
        begin(15, 6); line(15, 20); end(false);
        break;
    case Icon::Calendar:
        begin(5, 6); line(19, 6); line(19, 20); line(5, 20); line(5, 6); end(true);
        begin(8, 3); line(8, 7); end(false);
        begin(16, 3); line(16, 7); end(false);
        begin(5, 9); line(19, 9); end(false);
        fill_circle(r.left + 9 * sx, r.top + 13 * sy, 1.1f * sx, color);
        fill_circle(r.left + 12 * sx, r.top + 13 * sy, 1.1f * sx, color);
        fill_circle(r.left + 15 * sx, r.top + 13 * sy, 1.1f * sx, color);
        fill_circle(r.left + 9 * sx, r.top + 17 * sy, 1.1f * sx, color);
        fill_circle(r.left + 12 * sx, r.top + 17 * sy, 1.1f * sx, color);
        break;
    case Icon::ChevronDown:
    case Icon::ChevronRight:
    case Icon::ChevronUp:
        if (icon == Icon::ChevronRight) {
            begin(9, 6);
            line(15, 12);
            line(9, 18);
        } else {
            begin(6, icon == Icon::ChevronDown ? 9 : 15);
            line(12, icon == Icon::ChevronDown ? 15 : 9);
            line(18, icon == Icon::ChevronDown ? 9 : 15);
        }
        end(false);
        break;
    case Icon::Refresh: {
        // refresh-cw-alt-3（svgrepo）：两段小弧 + 两个直角尾巴，中间留下对角缺口。
        // 弧一：左下（172.8°）经顶部到右上（320.8°）= 148° 顺时针小弧。
        begin(4.06f, 13.0f);
        arc(18.2f, 6.94f, 8.0f, 8.0f, true);
        line(18.2f, 4.0f);
        end(false);
        // 弧二：右上（-7.2°）经底部到左下（138.6°）= 145.8° 顺时针小弧。
        begin(19.94f, 11.0f);
        arc(6.0f, 17.29f, 8.0f, 8.0f, true);
        line(6.0f, 20.0f);
        end(false);
        // 原 SVG 还有两个 2 单位的短折角，20px 下会和弧线叠在一起变成一团，所以只保留两个长钩。
        break;
    }
    case Icon::Play:
        begin(8, 5, true); line(19, 12); line(8, 19); end(true);
        break;
    case Icon::Sun:
        fill_circle(r.cx(), r.cy(), 4.0f * sx, color);
        for (int i = 0; i < 8; ++i) {
            const float angle = i * 3.14159265f / 4.0f;
            stroke_line(r.cx() + std::cos(angle) * 6.5f * sx, r.cy() + std::sin(angle) * 6.5f * sx,
                        r.cx() + std::cos(angle) * 9.0f * sx, r.cy() + std::sin(angle) * 9.0f * sy,
                        color, sw);
        }
        break;
    case Icon::Moon:
        fill_icon = true;
        begin(16.8f, 3.8f, true);
        arc(16.8f, 20.2f, 8.5f, 8.5f, true);
        arc(16.8f, 3.8f, 7.0f, 7.0f, false);
        end(true);
        break;
    case Icon::Close:
        begin(6, 6); line(18, 18); end(false);
        begin(18, 6); line(6, 18); end(false);
        break;
    case Icon::Minimize:
        begin(6, 12); line(18, 12); end(false);
        break;
    case Icon::Maximize:
        begin(6, 6); line(18, 6); line(18, 18); line(6, 18); line(6, 6); end(true);
        break;
    case Icon::Check:
        begin(5, 12.5f); line(9.5f, 17); line(19, 7); end(false);
        break;
    case Icon::Copy:
        begin(8, 8); line(19, 8); line(19, 20); line(8, 20); line(8, 8); end(true);
        begin(5, 16); line(5, 4); line(16, 4); end(false);
        break;
    case Icon::Folder:
        begin(3, 7); line(10, 7); line(12, 4); line(21, 4); line(21, 19); line(3, 19); line(3, 7);
        end(true);
        break;
    case Icon::Trash:
        begin(5, 7); line(19, 7); end(false);
        begin(9, 7); line(9, 4); line(15, 4); line(15, 7); end(false);
        begin(7, 7); line(8, 20); line(16, 20); line(17, 7); end(false);
        begin(10, 10); line(10.5f, 17); end(false);
        begin(14, 10); line(13.5f, 17); end(false);
        break;
    case Icon::Sparkle:
        begin(12, 2, true);
        line(14.2f, 9.8f); line(22, 12); line(14.2f, 14.2f); line(12, 22);
        line(9.8f, 14.2f); line(2, 12); line(9.8f, 9.8f); line(12, 2);
        end(true);
        break;
    case Icon::Globe:
        begin(12, 3); arc(21, 12, 9, 9, true); arc(12, 21, 9, 9, true);
        arc(3, 12, 9, 9, true); arc(12, 3, 9, 9, true); end(true);
        begin(3, 12); line(21, 12); end(false);
        begin(12, 3); arc(12, 21, 4.5f, 9, true); arc(12, 3, 4.5f, 9, true); end(false);
        break;
    case Icon::Palette:
        begin(12, 3);
        arc(21, 12, 9, 9, true); arc(12, 21, 9, 9, true); arc(3, 12, 9, 9, true);
        arc(12, 3, 9, 9, true); end(true);
        fill_circle(r.left + 8.0f * sx, r.top + 9.0f * sy, 1.4f * sx, color);
        fill_circle(r.left + 12.0f * sx, r.top + 7.0f * sy, 1.4f * sx, color);
        fill_circle(r.left + 16.0f * sx, r.top + 9.5f * sy, 1.4f * sx, color);
        break;
    case Icon::Shield:
        begin(12, 3); line(20, 6); line(20, 12);
        bezier(20, 17, 16, 20, 12, 21); bezier(8, 20, 4, 17, 4, 12);
        line(4, 6); line(12, 3); end(true);
        break;
    case Icon::Search:
        begin(10.5f, 4);
        arc(17, 10.5f, 6.5f, 6.5f, true); arc(10.5f, 17, 6.5f, 6.5f, true);
        arc(4, 10.5f, 6.5f, 6.5f, true); arc(10.5f, 4, 6.5f, 6.5f, true);
        end(true);
        begin(15.5f, 15.5f); line(21, 21); end(false);
        break;
    case Icon::Info:
        begin(12, 3); arc(21, 12, 9, 9, true); arc(12, 21, 9, 9, true);
        arc(3, 12, 9, 9, true); arc(12, 3, 9, 9, true); end(true);
        begin(12, 10.5f); line(12, 17); end(false);
        fill_circle(r.left + 12 * sx, r.top + 7.5f * sy, 1.2f * sx, color);
        break;
    case Icon::Plus:
        begin(12, 5); line(12, 19); end(false);
        begin(5, 12); line(19, 12); end(false);
        break;
    case Icon::Minus:
        begin(5, 12); line(19, 12); end(false);
        break;
    case Icon::Lock:
        begin(6, 11); line(18, 11); line(18, 20); line(6, 20); line(6, 11); end(true);
        begin(9, 11); line(9, 8);
        bezier(9, 4, 15, 4, 15, 8); line(15, 11); end(false);
        break;
    case Icon::Heart:
        begin(12, 21);
        bezier(9, 18, 3, 14, 3, 8);
        bezier(3, 2, 10, 1, 12, 6);
        bezier(14, 1, 21, 2, 21, 8);
        bezier(21, 14, 15, 18, 12, 21);
        end(true);
        break;
    case Icon::Telegram:
        begin(22, 3); line(17, 21); line(10, 15); line(3, 12); line(22, 3); end(true);
        begin(10, 15); line(17, 8); end(false);
        begin(10, 15); line(9, 20); line(13, 17); end(false);
        break;
    case Icon::Terminal:
        begin(3, 5); line(21, 5); line(21, 19); line(3, 19); line(3, 5); end(true);
        begin(6, 9); line(10, 12); line(6, 15); end(false);
        begin(12, 15); line(18, 15); end(false);
        break;
    }
    sink->Close();
    if (fill_icon) fill_geometry(geometry.Get(), color);
    else stroke_geometry(geometry.Get(), color, sw);
}

void Renderer::check(const Rect& rect, const Color& color, float stroke) {
    icon(Icon::Check, rect, color, stroke);
}

void Renderer::chevron(const Rect& rect, const Color& color, bool down, float stroke) {
    icon(down ? Icon::ChevronDown : Icon::ChevronUp, rect, color, stroke);
}

} // namespace tv
