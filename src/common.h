#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <bcrypt.h>
#include <winhttp.h>
#include <wincodec.h>
#include <commdlg.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <psapi.h>
#include <timeapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <condition_variable>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ws2_32.lib")

namespace tv {

using Microsoft::WRL::ComPtr;

constexpr wchar_t kAppName[] = L"TeleVip Desktop";
constexpr wchar_t kAppVersion[] = L"7.3.3 · native";
constexpr UINT kIdrHookDll = 101;

constexpr uint32_t FLAG_HIDE_SEEN_PRIVATE       = 1u << 0;
constexpr uint32_t FLAG_HIDE_SEEN_CHANNEL       = 1u << 1;
constexpr uint32_t FLAG_HIDE_ONLINE             = 1u << 2;
constexpr uint32_t FLAG_HIDE_TYPING             = 1u << 3;
constexpr uint32_t FLAG_HIDE_STORY              = 1u << 4;
constexpr uint32_t FLAG_DEBUG                   = 1u << 5;
constexpr uint32_t FLAG_DISABLE_STORIES         = 1u << 6;
constexpr uint32_t FLAG_DOWNLOAD_SPEED          = 1u << 7;
constexpr uint32_t FLAG_HIDE_PROMO              = 1u << 8;
constexpr uint32_t FLAG_HIDE_UPDATE             = 1u << 9;

constexpr uint32_t FLAG_PREMIUM                 = 1u << 10;
constexpr uint32_t FLAG_REMOVE_CONTENT_SAVING   = 1u << 11;
constexpr uint32_t FLAG_DISABLE_NUMBER_ROUNDING = 1u << 12;
constexpr uint32_t FLAG_HIDE_PHONE              = 1u << 13;
constexpr uint32_t FLAG_SHOW_MSG_ID             = 1u << 14;

constexpr uint32_t FLAG_HIDE_PINNED             = 1u << 15;
constexpr uint32_t FLAG_EDIT_HISTORY            = 1u << 16;
constexpr uint32_t FLAG_DELETED_ARCHIVE         = 1u << 17;
constexpr uint32_t FLAG_SHOW_USER_ID            = 1u << 18;
constexpr uint32_t FLAG_HIDE_TL_ERROR           = 1u << 19;
constexpr uint32_t FLAG_SECRET_SAVE             = 1u << 20;
constexpr uint32_t FLAG_SECRET_KEEP             = 1u << 21;
constexpr uint32_t FLAG_SAVE_STORIES            = 1u << 22;

constexpr uint32_t FLAG_TRANSLATE_BATCH          = 1u << 23;

constexpr uint32_t kCodePatchFlags =
    FLAG_REMOVE_CONTENT_SAVING | FLAG_DISABLE_NUMBER_ROUNDING;

static_assert((kCodePatchFlags & FLAG_PREMIUM) == 0,
              "FLAG_PREMIUM must not reach apply_local_patches");
static_assert(FLAG_PREMIUM == (1u << 10), "bit 10 is the contract with premium.rs");

constexpr uint32_t kGhostPresetFlags =
    FLAG_HIDE_SEEN_PRIVATE | FLAG_HIDE_SEEN_CHANNEL | FLAG_HIDE_ONLINE |
    FLAG_HIDE_TYPING | FLAG_HIDE_PHONE | FLAG_HIDE_STORY | FLAG_DISABLE_STORIES;

constexpr uint32_t kPremiumOff = 0;
constexpr uint32_t kPremiumWaiting = 1;
constexpr uint32_t kPremiumHooked = 2;

struct SharedConfig;

inline float clampf(float value, float lo, float hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

inline float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

inline float ease_out_cubic(float t) {
    const float u = 1.0f - clampf(t, 0.0f, 1.0f);
    return 1.0f - u * u * u;
}

inline float ease_out_quint(float t) {
    const float u = 1.0f - clampf(t, 0.0f, 1.0f);
    return 1.0f - u * u * u * u * u;
}

inline float ease_in_out(float t) {
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline float now_seconds() {
    using clock = std::chrono::steady_clock;
    static const auto start = clock::now();
    return std::chrono::duration<float>(clock::now() - start).count();
}

inline uint64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::wstring utf8_to_wide(std::string_view text);
std::string wide_to_utf8(std::wstring_view text);
std::wstring trim(std::wstring value);
std::wstring lower(std::wstring value);
bool starts_with_ci(std::wstring_view text, std::wstring_view prefix);
std::wstring replace_all(std::wstring text, std::wstring_view from, std::wstring_view to);
std::wstring format_w(const wchar_t* fmt, ...);
std::wstring format_w(const std::wstring& fmt, const std::unordered_map<std::wstring, std::wstring>& values);
std::wstring get_local_appdata();
std::wstring get_appdata();
std::wstring get_module_dir();
std::wstring get_exe_path();
std::wstring join_path(std::wstring_view a, std::wstring_view b);
bool file_exists(const std::wstring& path);
uint64_t file_size(const std::wstring& path);
bool ensure_dir(const std::wstring& path);
std::wstring truncate_middle(const std::wstring& text, size_t max_chars);
std::wstring ellipsize(const std::wstring& text, size_t max_chars);
std::wstring wide_time(uint64_t ms, const wchar_t* fmt);
void debug_log(const std::wstring& message);

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float rr, float gg, float bb, float aa = 1.0f) : r(rr), g(gg), b(bb), a(aa) {}

    static Color hex(uint32_t rgb, float alpha = 1.0f) {
        return Color(
            static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
            static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
            static_cast<float>(rgb & 0xFF) / 255.0f,
            alpha);
    }

    static Color rgba(uint32_t rgb, float alpha) { return hex(rgb, alpha); }

    Color with_alpha(float alpha) const { return Color(r, g, b, alpha); }
};

inline Color mix_color(const Color& a, const Color& b, float t) {
    t = clampf(t, 0.0f, 1.0f);
    return Color(lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t), lerpf(a.a, b.a, t));
}

inline Color solid_over(const Color& fg, const Color& bg) {
    return Color(lerpf(bg.r, fg.r, fg.a), lerpf(bg.g, fg.g, fg.a), lerpf(bg.b, fg.b, fg.a), 1.0f);
}

inline D2D1_COLOR_F d2d_color(const Color& c) {
    return D2D1::ColorF(c.r, c.g, c.b, c.a);
}

inline D2D1_COLOR_F d2d_color(const Color& c, float alpha) {
    return D2D1::ColorF(c.r, c.g, c.b, c.a * alpha);
}

struct Rect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    float width() const { return std::max(0.0f, right - left); }
    float height() const { return std::max(0.0f, bottom - top); }
    float cx() const { return (left + right) * 0.5f; }
    float cy() const { return (top + bottom) * 0.5f; }
    bool empty() const { return right <= left || bottom <= top; }

    bool contains(float x, float y) const {
        return x >= left && x <= right && y >= top && y <= bottom;
    }

    bool operator==(const Rect& other) const {
        return left == other.left && top == other.top && right == other.right && bottom == other.bottom;
    }

    bool operator!=(const Rect& other) const { return !(*this == other); }

    Rect inset(float amount) const {
        return Rect{left + amount, top + amount, right - amount, bottom - amount};
    }

    Rect inset(float x, float y) const {
        return Rect{left + x, top + y, right - x, bottom - y};
    }

    Rect offset(float dx, float dy) const {
        return Rect{left + dx, top + dy, right + dx, bottom + dy};
    }

    Rect expand(float amount) const {
        return Rect{left - amount, top - amount, right + amount, bottom + amount};
    }

    D2D1_RECT_F d2d() const { return D2D1::RectF(left, top, right, bottom); }
};

inline Rect make_rect(float left, float top, float width, float height) {
    return Rect{left, top, left + width, top + height};
}

inline Rect centered_rect(float cx, float cy, float width, float height) {
    return Rect{cx - width * 0.5f, cy - height * 0.5f, cx + width * 0.5f, cy + height * 0.5f};
}

struct Spring {
    float value = 0.0f;
    float target = 0.0f;
    float velocity = 0.0f;

    void snap(float v) {
        value = target = v;
        velocity = 0.0f;
    }

    bool update(float dt, float stiffness = 320.0f, float damping = 30.0f) {
        const float delta = target - value;
        if (std::fabs(delta) < 0.0005f && std::fabs(velocity) < 0.0005f) {
            value = target;
            velocity = 0.0f;
            return false;
        }
        dt = clampf(dt, 0.0f, 0.05f);
        const float accel = delta * stiffness - velocity * damping;
        velocity += accel * dt;
        value += velocity * dt;
        if (std::fabs(target - value) < 0.0005f && std::fabs(velocity) < 0.02f) {
            value = target;
            velocity = 0.0f;
        }
        return true;
    }
};

struct Ease {
    float value = 0.0f;
    float target = 0.0f;
    float speed = 14.0f;

    void snap(float v) { value = target = v; }

    bool update(float dt) {
        if (std::fabs(target - value) < 0.0005f) {
            value = target;
            return false;
        }
        dt = clampf(dt, 0.0f, 0.05f);
        value += (target - value) * clampf(dt * speed, 0.0f, 1.0f);
        if (std::fabs(target - value) < 0.0005f) value = target;
        return true;
    }
};

inline std::wstring human_count(uint64_t value) {
    return std::to_wstring(value);
}

} // namespace tv
