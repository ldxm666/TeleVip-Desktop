#pragma once

#include "common.h"

namespace tv {

enum class FontWeight {
    Regular = 400,
    Medium = 500,
    SemiBold = 600,
    Bold = 700,
};

enum class TextAlign {
    Left,
    Center,
    Right,
};

enum class TextVAlign {
    Top,
    Middle,
    Bottom,
};

enum class Icon {
    Ghost,
    Network,
    Stories,
    Account,
    Messages,
    Tools,
    Advanced,
    Roadmap,
    Calendar,
    ChevronDown,
    ChevronRight,
    ChevronUp,
    Refresh,
    Play,
    Sun,
    Moon,
    Close,
    Minimize,
    Maximize,
    Check,
    Copy,
    Folder,
    Trash,
    Sparkle,
    Globe,
    Palette,
    Shield,
    Search,
    Info,
    Plus,
    Minus,
    Lock,
    Terminal,
    Heart,
    Telegram,
};

struct TextStyle {
    float size = 13.0f;
    FontWeight weight = FontWeight::Regular;
    TextAlign align = TextAlign::Left;
    TextVAlign valign = TextVAlign::Top;
    bool wrap = false;
    bool mono = false;
    bool ellipsis = false;
    float line_spacing = 1.0f;
};

class Renderer {
public:
    ~Renderer();
    bool init(HWND hwnd);
    bool resize(UINT width, UINT height, float dpi);
    void begin(const Color& clear);
    void end();
    bool valid() const { return factory_ != nullptr; }
    ID2D1HwndRenderTarget* target() const { return target_.Get(); }
    float dpi() const { return dpi_; }

    void fill_rect(const Rect& rect, const Color& color);
    void fill_round(const Rect& rect, float radius, const Color& color);
    void fill_round_gradient(const Rect& rect, float radius, const Color& top, const Color& bottom);
    void stroke_round(const Rect& rect, float radius, const Color& color, float width = 1.0f);
    void stroke_line(float x1, float y1, float x2, float y2, const Color& color, float width = 1.0f);
    void fill_circle(float cx, float cy, float radius, const Color& color);
    void stroke_circle(float cx, float cy, float radius, const Color& color, float width = 1.0f);
    void shadow(const Rect& rect, float radius, float spread, const Color& color);
    void push_clip(const Rect& rect);
    void pop_clip();
    void push_visual(float opacity, float dx = 0, float dy = 0);
    void pop_visual();
    void push_scale(float scale, float cx, float cy);
    void pop_transform();

    bool load_image(UINT resource_id, const wchar_t* type = L"RCDATA");
    bool has_image(UINT resource_id = 102) const { return images_.find(resource_id) != images_.end(); }
    void image(const Rect& rect, float opacity = 1.0f);
    void image(UINT resource_id, const Rect& rect, float opacity = 1.0f);

    void text(std::wstring_view value, const Rect& rect, const TextStyle& style, const Color& color);
    D2D1_SIZE_F measure(std::wstring_view value, const TextStyle& style, float max_width = 4096.0f);
    float text_width(std::wstring_view value, float size, FontWeight weight = FontWeight::Regular,
                     bool mono = false);
    size_t text_index_at(std::wstring_view value, const TextStyle& style, const Rect& box,
                         float x, float y);
    Rect text_caret(std::wstring_view value, const TextStyle& style, const Rect& box, size_t index);

    void icon(Icon icon, const Rect& rect, const Color& color, float stroke = 1.55f);
    void check(const Rect& rect, const Color& color, float stroke = 1.8f);
    void chevron(const Rect& rect, const Color& color, bool down, float stroke = 1.7f);

private:
    IDWriteTextFormat* format_for(const TextStyle& style);
    void set_brush(const Color& color);
    ComPtr<ID2D1PathGeometry> make_geometry();
    void stroke_geometry(ID2D1Geometry* geometry, const Color& color, float width);
    void fill_geometry(ID2D1Geometry* geometry, const Color& color);

    HWND hwnd_ = nullptr;
    ComPtr<ID2D1Factory> factory_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<ID2D1HwndRenderTarget> target_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<ID2D1StrokeStyle> stroke_style_;
    ComPtr<ID2D1Layer> visual_layer_;
    D2D1_MATRIX_3X2_F visual_previous_ = D2D1::Matrix3x2F::Identity();
    bool visual_active_ = false;
    std::vector<D2D1_MATRIX_3X2_F> transforms_;
    ComPtr<IWICImagingFactory> wic_;
    struct ImageAsset {
        ComPtr<IWICFormatConverter> source;
        ComPtr<ID2D1Bitmap> bitmap;
        std::vector<BYTE> bytes;
        UINT generation = 0;
    };
    std::unordered_map<UINT, ImageAsset> images_;
    UINT target_generation_ = 0;
    std::unordered_map<std::wstring, IDWriteTextFormat*> formats_;
    float dpi_ = 96.0f;
    UINT width_ = 0;
    UINT height_ = 0;
    int clip_depth_ = 0;
};

} // namespace tv
