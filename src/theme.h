#pragma once

#include "common.h"

namespace tv {

enum class Accent {
    Blue = 0,
    Purple,
    Pink,
    Red,
    Orange,
    Yellow,
    Green,
    Graphite,
};

struct AccentDef {
    Accent id;
    uint32_t color;
    const wchar_t* zh;
    const wchar_t* en;
};

constexpr AccentDef kAccents[] = {
    {Accent::Blue,     0x2E7CF7, L"蓝",   L"Blue"},
    {Accent::Purple,   0x9A57A3, L"紫",   L"Purple"},
    {Accent::Pink,     0xE55E9C, L"粉",   L"Pink"},
    {Accent::Red,      0xED5F5D, L"红",   L"Red"},
    {Accent::Orange,   0xE9873A, L"橙",   L"Orange"},
    {Accent::Yellow,   0xF3BA4B, L"黄",   L"Yellow"},
    {Accent::Green,    0x79B757, L"绿",   L"Green"},
    {Accent::Graphite, 0x8C8C8C, L"石墨", L"Graphite"},
};

inline Color accent_base(Accent accent) {
    for (const auto& def : kAccents) {
        if (def.id == accent) return Color::hex(def.color);
    }
    return Color::hex(0x2E7CF7);
}

struct Palette {
    bool dark = true;
    Color window;
    Color window_top;
    Color sidebar;
    Color content;
    Color card;
    Color card_hover;
    Color card_pressed;
    Color card_alt;
    Color elevated;
    Color border;
    Color border_soft;
    Color divider;
    Color text;
    Color text_secondary;
    Color text_tertiary;
    Color accent;
    Color accent_hover;
    Color accent_pressed;
    Color accent_soft;
    Color on_accent;
    Color success;
    Color success_soft;
    Color danger;
    Color danger_soft;
    Color warning;
    Color warning_soft;
    Color input;
    Color input_focus;
    Color log_bg;
    Color log_text;
    Color shadow;
    Color switch_off;
    Color switch_knob;
    Color nav_hover;
    Color nav_active;
    Color nav_active_text;
    Color titlebar;
    Color traffic_red;
    Color traffic_yellow;
    Color traffic_green;
    Color traffic_border;
};

inline Color lighten(const Color& c, float amount) {
    return Color(
        clampf(c.r + (1.0f - c.r) * amount, 0.0f, 1.0f),
        clampf(c.g + (1.0f - c.g) * amount, 0.0f, 1.0f),
        clampf(c.b + (1.0f - c.b) * amount, 0.0f, 1.0f),
        c.a);
}

inline Color darken(const Color& c, float amount) {
    return Color(c.r * (1.0f - amount), c.g * (1.0f - amount), c.b * (1.0f - amount), c.a);
}


// 主题/强调色切换用：逐通道插值。dark 直接取目标，避免过渡期间明暗判定抖动。
inline Palette blend_palette(const Palette& from, const Palette& to, float t) {
    Palette out = to;
    if (t >= 1.0f) return out;
    const float k = clampf(t, 0.0f, 1.0f);
    out.window = mix_color(from.window, to.window, k);
    out.window_top = mix_color(from.window_top, to.window_top, k);
    out.sidebar = mix_color(from.sidebar, to.sidebar, k);
    out.content = mix_color(from.content, to.content, k);
    out.card = mix_color(from.card, to.card, k);
    out.card_hover = mix_color(from.card_hover, to.card_hover, k);
    out.card_pressed = mix_color(from.card_pressed, to.card_pressed, k);
    out.card_alt = mix_color(from.card_alt, to.card_alt, k);
    out.elevated = mix_color(from.elevated, to.elevated, k);
    out.border = mix_color(from.border, to.border, k);
    out.border_soft = mix_color(from.border_soft, to.border_soft, k);
    out.divider = mix_color(from.divider, to.divider, k);
    out.text = mix_color(from.text, to.text, k);
    out.text_secondary = mix_color(from.text_secondary, to.text_secondary, k);
    out.text_tertiary = mix_color(from.text_tertiary, to.text_tertiary, k);
    out.accent = mix_color(from.accent, to.accent, k);
    out.accent_hover = mix_color(from.accent_hover, to.accent_hover, k);
    out.accent_pressed = mix_color(from.accent_pressed, to.accent_pressed, k);
    out.accent_soft = mix_color(from.accent_soft, to.accent_soft, k);
    out.on_accent = mix_color(from.on_accent, to.on_accent, k);
    out.success = mix_color(from.success, to.success, k);
    out.success_soft = mix_color(from.success_soft, to.success_soft, k);
    out.danger = mix_color(from.danger, to.danger, k);
    out.danger_soft = mix_color(from.danger_soft, to.danger_soft, k);
    out.warning = mix_color(from.warning, to.warning, k);
    out.warning_soft = mix_color(from.warning_soft, to.warning_soft, k);
    out.input = mix_color(from.input, to.input, k);
    out.input_focus = mix_color(from.input_focus, to.input_focus, k);
    out.log_bg = mix_color(from.log_bg, to.log_bg, k);
    out.log_text = mix_color(from.log_text, to.log_text, k);
    out.shadow = mix_color(from.shadow, to.shadow, k);
    out.switch_off = mix_color(from.switch_off, to.switch_off, k);
    out.switch_knob = mix_color(from.switch_knob, to.switch_knob, k);
    out.nav_hover = mix_color(from.nav_hover, to.nav_hover, k);
    out.nav_active = mix_color(from.nav_active, to.nav_active, k);
    out.nav_active_text = mix_color(from.nav_active_text, to.nav_active_text, k);
    out.titlebar = mix_color(from.titlebar, to.titlebar, k);
    out.traffic_red = mix_color(from.traffic_red, to.traffic_red, k);
    out.traffic_yellow = mix_color(from.traffic_yellow, to.traffic_yellow, k);
    out.traffic_green = mix_color(from.traffic_green, to.traffic_green, k);
    out.traffic_border = mix_color(from.traffic_border, to.traffic_border, k);
    return out;
}

inline Palette make_palette(bool dark, Accent accent_id) {
    Palette p;
    p.dark = dark;
    const Color accent = accent_base(accent_id);
    p.accent = accent;
    p.accent_hover = lighten(accent, dark ? 0.18f : 0.12f);
    p.accent_pressed = darken(accent, 0.16f);
    p.on_accent = Color::hex(0xFFFFFF);

    if (dark) {
        p.window = Color::hex(0x2B2B2B);
        p.window_top = Color::hex(0x303030);
        p.sidebar = Color::hex(0x333333);
        p.content = Color::hex(0x2B2B2B);
        p.card = Color::hex(0x242424);
        p.card_hover = Color::hex(0x2C2C2C);
        p.card_pressed = Color::hex(0x202020);
        p.card_alt = Color::hex(0x292929);
        p.elevated = Color::hex(0x3A3A3A);
        p.border = Color::rgba(0xFFFFFF, 0.09f);
        p.border_soft = Color::rgba(0xFFFFFF, 0.055f);
        p.divider = Color::rgba(0xFFFFFF, 0.075f);
        p.text = Color::hex(0xDEDEDE);
        p.text_secondary = Color::hex(0xAFAFAF);
        p.text_tertiary = Color::hex(0x8C8C8C);
        p.accent_soft = Color(accent.r, accent.g, accent.b, 0.20f);
        p.success = Color::hex(0x79B757);
        p.success_soft = Color(0.47f, 0.72f, 0.34f, 0.16f);
        p.danger = Color::hex(0xED5F5D);
        p.danger_soft = Color(0.93f, 0.37f, 0.36f, 0.16f);
        p.warning = Color::hex(0xF3BA4B);
        p.warning_soft = Color(0.95f, 0.73f, 0.29f, 0.16f);
        p.input = Color::rgba(0xFFFFFF, 0.06f);
        p.input_focus = Color::rgba(0xFFFFFF, 0.09f);
        p.log_bg = Color::hex(0x1E1E1E);
        p.log_text = Color::hex(0xD0D0D0);
        p.shadow = Color(0.0f, 0.0f, 0.0f, 0.30f);
        p.switch_off = Color::hex(0x4C4C4E);
        p.switch_knob = Color::hex(0xFFFFFF);
        p.nav_hover = Color::rgba(0xFFFFFF, 0.055f);
        p.nav_active = accent;
        p.nav_active_text = Color::hex(0xFFFFFF);
        p.titlebar = Color::rgba(0x333333, 0.94f);
        p.traffic_red = Color::hex(0xFE6254);
        p.traffic_yellow = Color::hex(0xFDC92D);
        p.traffic_green = Color::hex(0x28D33F);
        p.traffic_border = Color::rgba(0x000000, 0.28f);
    } else {
        p.window = Color::hex(0xF5F5F5);
        p.window_top = Color::hex(0xF8F8F8);
        p.sidebar = Color::hex(0xF0F0F0);
        p.content = Color::hex(0xF5F5F5);
        p.card = Color::hex(0xFFFFFF);
        p.card_hover = Color::hex(0xFAFAFA);
        p.card_pressed = Color::hex(0xF3F3F3);
        p.card_alt = Color::hex(0xFBFBFB);
        p.elevated = Color::hex(0xFFFFFF);
        p.border = Color::rgba(0x000000, 0.085f);
        p.border_soft = Color::rgba(0x000000, 0.05f);
        p.divider = Color::rgba(0x000000, 0.065f);
        p.text = Color::hex(0x242424);
        p.text_secondary = Color::hex(0x565656);
        p.text_tertiary = Color::hex(0x8A8A8A);
        p.accent_soft = Color(accent.r, accent.g, accent.b, 0.12f);
        p.success = Color::hex(0x3E9B37);
        p.success_soft = Color(0.24f, 0.61f, 0.22f, 0.10f);
        p.danger = Color::hex(0xED5F5D);
        p.danger_soft = Color(0.93f, 0.37f, 0.36f, 0.10f);
        p.warning = Color::hex(0xC7901A);
        p.warning_soft = Color(0.95f, 0.73f, 0.29f, 0.12f);
        p.input = Color::rgba(0x000000, 0.045f);
        p.input_focus = Color::rgba(0x000000, 0.03f);
        p.log_bg = Color::hex(0xFBFBFB);
        p.log_text = Color::hex(0x363636);
        p.shadow = Color(0.0f, 0.0f, 0.0f, 0.10f);
        p.switch_off = Color::hex(0xD8D8DC);
        p.switch_knob = Color::hex(0xFFFFFF);
        p.nav_hover = Color::rgba(0x000000, 0.045f);
        p.nav_active = accent;
        p.nav_active_text = Color::hex(0xFFFFFF);
        p.titlebar = Color::rgba(0xF8F8F8, 0.96f);
        p.traffic_red = Color::hex(0xFE6254);
        p.traffic_yellow = Color::hex(0xFDC92D);
        p.traffic_green = Color::hex(0x28D33F);
        p.traffic_border = Color::rgba(0x000000, 0.12f);
    }
    return p;
}

struct Metrics {
    float titlebar = 52.0f;
    float sidebar = 280.0f;
    float pad = 20.0f;
    float gap = 12.0f;
    float card_radius = 14.0f;
    float control_radius = 8.0f;
    float nav_height = 38.0f;
    float header_height = 92.0f;
    float log_height = 158.0f;
    float log_collapsed = 48.0f;
};

} // namespace tv
