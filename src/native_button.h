#pragma once
#include "theme.h"

namespace tv {

// Publish a complete control in one blit; layered/native child windows must never
// expose the background-clear step or XOR focus rectangles between paint calls.
inline bool paint_native_button(const DRAWITEMSTRUCT& item, const Palette& palette,
    HFONT font, const std::wstring& label, float dpi_scale, float hover, float press,
    bool primary, const Color& surrounding, bool square) {
    const int width = item.rcItem.right - item.rcItem.left;
    const int height = item.rcItem.bottom - item.rcItem.top;
    if (width <= 0 || height <= 0) return false;
    HDC buffer = CreateCompatibleDC(item.hDC);
    HBITMAP bitmap = buffer ? CreateCompatibleBitmap(item.hDC, width, height) : nullptr;
    if (!bitmap) { if (buffer) DeleteDC(buffer); return false; }
    const auto previous = SelectObject(buffer, bitmap);
    auto rgb = [](const Color& c) { return RGB(BYTE(c.r * 255), BYTE(c.g * 255), BYTE(c.b * 255)); };
    RECT rect{0, 0, width, height};
    Color fill = primary ? palette.accent : palette.card;
    fill = mix_color(fill, primary ? palette.accent_hover : palette.card_hover, clampf(hover, 0, 1));
    fill = mix_color(fill, primary ? palette.accent_pressed : palette.card_pressed, clampf(press, 0, 1));
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    if (disabled) fill = primary ? mix_color(palette.accent, palette.card, .40f) : palette.card;
    HBRUSH background = CreateSolidBrush(rgb(surrounding));
    FillRect(buffer, &rect, background); DeleteObject(background);
    HPEN border = CreatePen(PS_SOLID, std::max(1, int(dpi_scale)),
        rgb(primary ? fill : solid_over(palette.border, fill)));
    HBRUSH brush = CreateSolidBrush(rgb(fill));
    const auto old_pen = SelectObject(buffer, border), old_brush = SelectObject(buffer, brush);
    if (square) Rectangle(buffer, 0, 0, width, height);
    else RoundRect(buffer, 0, 0, width, height, int(14 * dpi_scale), int(14 * dpi_scale));
    SelectObject(buffer, old_pen); SelectObject(buffer, old_brush);
    DeleteObject(border); DeleteObject(brush);
    SelectObject(buffer, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(buffer, TRANSPARENT);
    SetTextColor(buffer, rgb(disabled && !primary ? palette.text_tertiary : primary ? palette.on_accent : palette.text));
    if (press > .3f) OffsetRect(&rect, 0, std::max(1, int(dpi_scale)));
    DrawTextW(buffer, label.c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
        const int inset = std::max(2, int(3 * dpi_scale));
        HPEN focus = CreatePen(PS_SOLID, std::max(1, int(dpi_scale)), rgb(primary ? palette.on_accent : palette.accent));
        const auto before_pen = SelectObject(buffer, focus);
        const auto before_brush = SelectObject(buffer, GetStockObject(NULL_BRUSH));
        Rectangle(buffer, inset, inset, width - inset, height - inset);
        SelectObject(buffer, before_pen); SelectObject(buffer, before_brush); DeleteObject(focus);
    }
    BitBlt(item.hDC, item.rcItem.left, item.rcItem.top, width, height, buffer, 0, 0, SRCCOPY);
    SelectObject(buffer, previous); DeleteObject(bitmap); DeleteDC(buffer);
    return true;
}

inline bool paint_native_traffic(const DRAWITEMSTRUCT& item, const Palette& palette,
    float scale, float hover, float press, int index) {
    const int width = item.rcItem.right - item.rcItem.left, height = item.rcItem.bottom - item.rcItem.top;
    HDC dc = CreateCompatibleDC(item.hDC);
    HBITMAP bitmap = dc ? CreateCompatibleBitmap(item.hDC, width, height) : nullptr;
    if (!bitmap) { if (dc) DeleteDC(dc); return false; }
    const auto old_bitmap = SelectObject(dc, bitmap);
    auto rgb = [](Color c) { return RGB(BYTE(c.r * 255), BYTE(c.g * 255), BYTE(c.b * 255)); };
    RECT bounds{0, 0, width, height};
    HBRUSH background = CreateSolidBrush(rgb(palette.titlebar)); FillRect(dc, &bounds, background); DeleteObject(background);
    const Color base = index == 0 ? palette.traffic_red : index == 1 ? palette.traffic_yellow : palette.traffic_green;
    HBRUSH fill = CreateSolidBrush(rgb(mix_color(base, lighten(base, .16f), hover)));
    HPEN outline = CreatePen(PS_SOLID, std::max(1, int(scale)), rgb(solid_over(palette.traffic_border, base)));
    const auto old_pen = SelectObject(dc, outline), old_brush = SelectObject(dc, fill);
    const int cx = width / 2, cy = height / 2, radius = std::max(3, int(6.2f * scale * (1 - .12f * press)));
    Ellipse(dc, cx - radius, cy - radius, cx + radius + 1, cy + radius + 1);
    if (hover > .25f || (item.itemState & (ODS_FOCUS | ODS_SELECTED))) {
        HPEN glyph = CreatePen(PS_SOLID, std::max(1, int(scale)), RGB(60, 45, 35)); SelectObject(dc, glyph);
        const int a = std::max(2, int(2.3f * scale));
        auto line = [&](int x1, int y1, int x2, int y2) { MoveToEx(dc, cx + x1, cy + y1, nullptr); LineTo(dc, cx + x2, cy + y2); };
        if (index == 0) { line(-a, -a, a + 1, a + 1); line(-a, a, a + 1, -a - 1); }
        else { line(-a, 0, a + 1, 0); if (index == 2) line(0, -a, 0, a + 1); }
        SelectObject(dc, outline); DeleteObject(glyph);
    }
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
        HPEN focus = CreatePen(PS_SOLID, std::max(1, int(scale)), rgb(palette.accent));
        SelectObject(dc, focus); SelectObject(dc, GetStockObject(NULL_BRUSH));
        const int focus_radius = int(10 * scale); Ellipse(dc, cx - focus_radius, cy - focus_radius, cx + focus_radius, cy + focus_radius);
        SelectObject(dc, outline); DeleteObject(focus);
    }
    SelectObject(dc, old_pen); SelectObject(dc, old_brush); DeleteObject(outline); DeleteObject(fill);
    BitBlt(item.hDC, item.rcItem.left, item.rcItem.top, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old_bitmap); DeleteObject(bitmap); DeleteDC(dc); return true;
}

} // namespace tv
