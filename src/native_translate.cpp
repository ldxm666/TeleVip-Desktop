#include "native_translate.h"

#include "core.h"
#include "tools.h"
#include "translation_request.h"

#include "overlay.h"
#include "translate_bar_recognition.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <roapi.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "runtimeobject.lib")

namespace tv {
namespace {

constexpr size_t kMaxRequest = 96 * 1024;
constexpr int kWorkers = 32;
// Telegram reuses its client socket for every message in a chat, so the server
// keeps the connection open and answers requests in a loop. Closing per request
// forced a fresh TCP handshake 30+ times per chat switch.
constexpr int kKeepAliveRequests = 64;
constexpr int kIdleTimeoutMs = 8000;
constexpr uint16_t kPortAttempts = 10;

// Scanning cadence for the translate bar.
//
// The old loop woke every 260 ms and OCR'd the whole top band - twelve full
// passes in 3.5 s, each one able to fire a click on a frame the user was still
// looking at. That loop *was* half of the rebound bug. The bar cannot appear
// faster than this, and every miss backs off further, so an idle desktop costs
// almost nothing.
constexpr uint64_t kBarScanMs = 800;    // bar on screen: keep watching, but at a sane rate
constexpr uint64_t kIdleScanMs = 900;   // nothing to do (bar absent / window in background)
constexpr uint64_t kMaxIdleScanMs = 2400;
constexpr uint64_t kIdleBackoffMs = 450;

enum class ScanStage { Idle, Bar, Backoff };

inline const wchar_t* scan_stage_name(ScanStage stage) {
    switch (stage) {
    case ScanStage::Bar: return L"bar";
    case ScanStage::Backoff: return L"backoff";
    case ScanStage::Idle: break;
    }
    return L"idle";
}

std::string url_decode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '+') {
            out.push_back(' ');
        } else if (ch == '%' && i + 2 < text.size()) {
            const auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int hi = hex(text[i + 1]);
            const int lo = hex(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
            out.push_back(ch);
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

std::string html_unescape(std::string text) {
    static const std::pair<std::string_view, std::string_view> entities[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""},
        {"&#39;", "'"}, {"&apos;", "'"}, {"&nbsp;", " "},
    };
    std::string out;
    out.reserve(text.size());
    for (size_t pos = 0; pos < text.size();) {
        bool decoded = false;
        if (text[pos] == '&') {
            for (const auto& [from, to] : entities) {
                if (text.compare(pos, from.size(), from) != 0) continue;
                out += to;
                pos += from.size();
                decoded = true;
                break;
            }
        }
        if (!decoded) out += text[pos++];
    }
    return out;
}

std::string json_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 16);
    static constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char ch : text) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20) {
                out += "\\u00";
                out.push_back(hex[ch >> 4]);
                out.push_back(hex[ch & 0xF]);
            } else {
                out.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return out;
}

bool send_all(SOCKET socket, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const int chunk = send(socket, data.data() + sent,
                               static_cast<int>(data.size() - sent), 0);
        if (chunk <= 0) return false;
        sent += static_cast<size_t>(chunk);
    }
    return true;
}

void send_response(SOCKET socket, int status, std::string_view type,
                   const std::string& body, bool keep_alive = true) {
    const char* reason = status == 200 ? "OK" : (status == 400 ? "Bad Request" :
        (status == 502 ? "Bad Gateway" : "Not Found"));
    std::string header = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n";
    header += "Content-Type: ";
    header += type;
    header += "\r\nContent-Length: ";
    header += std::to_string(body.size());
    header += keep_alive ? "\r\nConnection: keep-alive\r\n" : "\r\nConnection: close\r\n";
    header += "Cache-Control: no-store\r\n\r\n";
    send_all(socket, header);
    send_all(socket, body);
}

std::string query_value(std::string_view query, std::string_view key) {
    size_t pos = 0;
    while (pos <= query.size()) {
        const size_t end = query.find('&', pos);
        const std::string_view pair = query.substr(
            pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
        const size_t equals = pair.find('=');
        const std::string_view name = pair.substr(0, equals);
        if (name == key) {
            return url_decode(equals == std::string_view::npos
                                  ? std::string_view()
                                  : pair.substr(equals + 1));
        }
        if (end == std::string_view::npos) break;
        pos = end + 1;
    }
    return {};
}

std::wstring tdata_path(const std::wstring& telegram_exe) {
    if (telegram_exe.empty()) return {};
    std::filesystem::path path(telegram_exe);
    return (path.parent_path() / L"tdata").wstring();
}

std::string read_file(const std::wstring& path) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) return {};
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

bool write_file_atomic(const std::wstring& path, const std::string& data) {
    const std::wstring temp = path + L".tmp";
    {
        std::ofstream file(std::filesystem::path(temp), std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!file) return false;
    }
    return MoveFileExW(temp.c_str(), path.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool is_telegram_window(HWND hwnd) {
    if (!IsWindowVisible(hwnd)) return false;
    wchar_t title[256] = {};
    if (GetWindowTextW(hwnd, title, 256) == 0) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return false;
    const std::wstring path = lower(process_exe_path(pid));
    return path.size() >= 12 && path.substr(path.size() - 12) == L"telegram.exe";
}

struct WindowSearch {
    DWORD pid = 0;
    HWND best = nullptr;
    long long area = 0;
};

BOOL CALLBACK find_window_cb(HWND hwnd, LPARAM param) {
    auto* search = reinterpret_cast<WindowSearch*>(param);
    if (!IsWindowVisible(hwnd)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != search->pid) return TRUE;
    wchar_t title[256] = {};
    if (GetWindowTextW(hwnd, title, 256) == 0) return TRUE;
    DWORD cloaked = 0;
    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) return TRUE;
    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) return TRUE;
    const long long width = rect.right - rect.left;
    const long long height = rect.bottom - rect.top;
    if (width < 420 || height < 320) return TRUE;
    const long long area = width * height;
    if (area > search->area) {
        search->area = area;
        search->best = hwnd;
    }
    return TRUE;
}

HWND telegram_window(DWORD pid) {
    WindowSearch search;
    search.pid = pid;
    EnumWindows(find_window_cb, reinterpret_cast<LPARAM>(&search));
    return search.best;
}

TranslateBarHit find_translate_bar(DWORD pid) {
    TranslateBarHit hit;
    HWND hwnd = telegram_window(pid);
    if (!hwnd) return hit;

    // Qt 版 Telegram 不暴露任何辅助功能树（实测 UIA 与 MSAA 的子孙数量都是 0），
    // 按控件名查找永远返回 found=0。唯一可靠的办法是读屏幕像素：OCR 顶部窄条，
    // 命中「翻译为 / 显示原文 / Translate to」就把那片区域当按钮点下去。
    RECT client{};
    if (!GetClientRect(hwnd, &client) || client.right < 200) return hit;
    POINT origin{0, 0};
    if (!ClientToScreen(hwnd, &origin)) return hit;
    const int band_top = std::max<LONG>(0, static_cast<LONG>(client.top));
    const int dpi = std::max<UINT>(96, GetDpiForWindow(hwnd));
    RECT band{origin.x, origin.y + band_top, origin.x + client.right,
              origin.y + std::min<LONG>(client.bottom, band_top + 220 * dpi / 96)};
    std::vector<OcrLineHit> lines;
    std::wstring error;
    if (!ocr_region_lines(hwnd, band, lines, error)) {
        static thread_local uint64_t last_fail_log = 0;
        const uint64_t now = GetTickCount64();
        if (now - last_fail_log >= 5000) {
            last_fail_log = now;
            debug_log(L"native bar probe failed: " +
                      (error.empty() ? std::wstring(L"no text in band") : error));
        }
        return hit;
    }
    const RECT client_screen{origin.x, origin.y, origin.x + client.right, origin.y + client.bottom};
    hit = recognize_translate_bar(lines, client_screen, dpi);
    // The chat identity stays a pure name here; callers prefix the pid so two
    // Telegram instances can never share a latch.
    if (!hit.found) {
        static thread_local uint64_t last_scan_log = 0;
        const uint64_t now = GetTickCount64();
        if (now - last_scan_log >= 5000) {
            last_scan_log = now;
            // Evidence, never guesswork. The registry calls this dump the only
            // first-hand proof of "the bar is / is not on screen"; the 2026-11-03
            // rewrite dropped it, which made found=0 unarguable. Two fields:
            //  labels= any line the classifier recognised, with its pixel rect. If
            //          this is non-empty while found=0, the geometry gate rejected a
            //          real bar and these numbers name the rule that fired.
            //  band=   raw OCR text, capped, so a silent band proves OCR ran over
            //          the right pixels and Telegram simply showed no bar.
            std::wstring labels;
            std::wstring band_text;
            unsigned shown = 0;
            for (const auto& line : lines) {
                const BarLabel label = classify_translate_bar(line.text);
                const std::wstring rect = L"@" + std::to_wstring(line.rect.left) + L"," +
                                          std::to_wstring(line.rect.top) + L"," +
                                          std::to_wstring(line.rect.right - line.rect.left) + L"x" +
                                          std::to_wstring(line.rect.bottom - line.rect.top);
                if (label != BarLabel::Missing) {
                    labels += std::wstring(label == BarLabel::Original ? L"original" : L"pending") +
                              rect + L" ";
                }
                if (shown < 10 && band_text.size() < 360) {
                    band_text += L"\"" + line.text + L"\"" + rect + L" ";
                    ++shown;
                }
            }
            if (labels.empty()) labels = L"none";
            debug_log(L"native bar not found; lines=" + std::to_wstring(lines.size()) +
                      L" dpi=" + std::to_wstring(dpi) + L" client=" +
                      std::to_wstring(client_screen.left) + L"," +
                      std::to_wstring(client_screen.top) + L" " +
                      std::to_wstring(client_screen.right - client_screen.left) + L"x" +
                      std::to_wstring(client_screen.bottom - client_screen.top) + L" labels=" +
                      labels + L" band=" + band_text);
        }
    }
    return hit;
}

bool foreground_is(HWND hwnd) {
    HWND foreground = GetForegroundWindow();
    return foreground && GetAncestor(foreground, GA_ROOT) == hwnd;
}

struct ProbeContext {
    RECT client{};
    UINT dpi = 0;
    DWORD input_tick = 0;
};

bool probe_context(HWND hwnd, ProbeContext& result) {
    LASTINPUTINFO input{sizeof(input)};
    RECT client{};
    POINT origin{0, 0};
    if (!foreground_is(hwnd) || IsIconic(hwnd) || !GetLastInputInfo(&input) ||
        !GetClientRect(hwnd, &client) || !ClientToScreen(hwnd, &origin)) return false;
    result.client = RECT{origin.x, origin.y, origin.x + client.right, origin.y + client.bottom};
    result.dpi = GetDpiForWindow(hwnd);
    result.input_tick = input.dwTime;
    return result.dpi != 0;
}

bool same_probe_context(const ProbeContext& first, const ProbeContext& second) {
    return EqualRect(&first.client, &second.client) && first.dpi == second.dpi &&
           first.input_tick == second.input_tick;
}


// 不碰物理光标：直接向 Telegram 窗口投递鼠标消息。
// 这是“抢鼠标”的根治——用户的光标不会被拽走。
bool click_client(HWND hwnd, int screen_x, int screen_y) {
    if (!hwnd) return false;
    POINT point{screen_x, screen_y};
    if (!ScreenToClient(hwnd, &point)) return false;
    const LPARAM packed = MAKELPARAM(point.x, point.y);
    DWORD_PTR result = 0;
    constexpr UINT flags = SMTO_ABORTIFHUNG | SMTO_BLOCK;
    if (!SendMessageTimeoutW(hwnd, WM_MOUSEMOVE, 0, packed, flags, 1000, &result)) return false;
    const bool down = SendMessageTimeoutW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, packed,
                                           flags, 1000, &result) != 0;
    // A timeout may still deliver DOWN. Always pair it with UP.
    const bool up = SendMessageTimeoutW(hwnd, WM_LBUTTONUP, 0, packed, flags, 1000, &result) != 0;
    return down && up;
}

void force_foreground(HWND hwnd) {
    if (!hwnd || foreground_is(hwnd)) return;
    HWND foreground = GetForegroundWindow();
    const DWORD foreground_thread = foreground
                                        ? GetWindowThreadProcessId(foreground, nullptr)
                                        : 0;
    const DWORD target_thread = GetWindowThreadProcessId(hwnd, nullptr);
    if (foreground_thread && target_thread && foreground_thread != target_thread) {
        AttachThreadInput(foreground_thread, target_thread, TRUE);
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        AttachThreadInput(foreground_thread, target_thread, FALSE);
    } else {
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
    }
    // SetForegroundWindow 经常被前台锁定拒绝（后台进程调用时几乎必失败），
    // 这时窗口还压在别的程序下面，截图读到的是别人的像素。用 TOPMOST 翻一下
    // 是唯一稳定的抬升手段，翻完立刻取消，窗口不会被钉在最上层。
    if (!foreground_is(hwnd)) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(hwnd);
    }
    Sleep(60);
}

// PW_CLIENTONLY / PW_RENDERFULLCONTENT. Together they are what Windows' own
// capture tooling uses: CLIENTONLY keeps the non-client frame out of the bitmap,
// RENDERFULLCONTENT makes DWM-composited (Qt) windows paint their real surface
// instead of a black rectangle.
constexpr UINT kPwClientOnly = 0x00000001;
constexpr UINT kPwFullContent = 0x00000002;

// A DIB section plus the memory DC it is selected into. PrintWindow needs a real
// DC to paint into; keeping the three GDI objects in one place means no call site
// can leak them or forget to deselect the bitmap.
class Surface {
public:
    Surface(int width, int height) : width_(width), height_(height) {
        if (width < 8 || height < 8 || width > 8192 || height > 8192) return;
        screen_ = CreateDCW(L"DISPLAY", nullptr, nullptr, nullptr);
        if (!screen_) return;
        memory_ = CreateCompatibleDC(screen_);
        if (!memory_) return;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height; // top-down: row 0 is the top row
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        bitmap_ = CreateDIBSection(screen_, &info, DIB_RGB_COLORS, &bits_, nullptr, 0);
        if (!bitmap_ || !bits_) return;
        old_ = SelectObject(memory_, bitmap_);
        // Zeroed first: PrintWindow can return TRUE after painting nothing, and
        // an untouched buffer is how that failure is detected.
        std::memset(bits_, 0, static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        ready_ = true;
    }

    ~Surface() {
        if (memory_ && old_) SelectObject(memory_, old_);
        if (bitmap_) DeleteObject(bitmap_);
        if (memory_) DeleteDC(memory_);
        if (screen_) DeleteDC(screen_);
    }

    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    bool ready() const { return ready_; }
    HDC dc() const { return memory_; }
    const uint8_t* bits() const { return static_cast<const uint8_t*>(bits_); }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    HDC screen_ = nullptr;
    HDC memory_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ old_ = nullptr;
    void* bits_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool ready_ = false;
};

// PrintWindow reports success on a window that has never been composed; the
// bitmap is then uniformly one colour. Real client pixels always vary.
bool has_content(const Surface& surface) {
    const size_t count = static_cast<size_t>(surface.width()) * surface.height();
    if (count < 64) return false;
    const uint8_t* pixels = surface.bits();
    const uint8_t r = pixels[0];
    const uint8_t g = pixels[1];
    const uint8_t b = pixels[2];
    const size_t needed = count / 200 > 8 ? count / 200 : 8;
    size_t distinct = 0;
    for (size_t i = 1; i < count; ++i) {
        const uint8_t* pixel = pixels + i * 4;
        if (pixel[0] == r && pixel[1] == g && pixel[2] == b) continue;
        if (++distinct >= needed) return true;
    }
    return false;
}

void copy_rows(const uint8_t* source, int source_width, RECT source_rect,
               std::vector<uint8_t>& pixels, int width, int height) {
    pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        const uint8_t* from = source +
            (static_cast<size_t>(source_rect.top + y) * static_cast<size_t>(source_width) +
             static_cast<size_t>(source_rect.left)) * 4;
        std::memcpy(pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4,
                    from, static_cast<size_t>(width) * 4);
    }
}

// Renders `target`'s own pixels (never the desktop's) and crops one client-space
// rectangle out of them. Two attempts, because Qt builds differ: a client-only
// bitmap, then a full-window bitmap with the client offset subtracted.
bool print_window_region(HWND target, RECT wanted, std::vector<uint8_t>& pixels,
                         int& width, int& height) {
    RECT client{};
    if (!GetClientRect(target, &client)) return false;
    RECT region = wanted;
    region.left = std::max<LONG>(0, std::min(region.left, client.right));
    region.top = std::max<LONG>(0, std::min(region.top, client.bottom));
    region.right = std::max<LONG>(region.left, std::min(region.right, client.right));
    region.bottom = std::max<LONG>(region.top, std::min(region.bottom, client.bottom));
    width = region.right - region.left;
    height = region.bottom - region.top;
    if (width < 8 || height < 8) return false;

    {
        Surface surface(client.right, client.bottom);
        if (surface.ready() &&
            PrintWindow(target, surface.dc(), kPwClientOnly | kPwFullContent) &&
            has_content(surface) && region.right <= surface.width() &&
            region.bottom <= surface.height()) {
            copy_rows(surface.bits(), surface.width(), region, pixels, width, height);
            return true;
        }
    }

    RECT window{};
    POINT origin{0, 0};
    if (!GetWindowRect(target, &window) || !ClientToScreen(target, &origin)) return false;
    const int full_width = window.right - window.left;
    const int full_height = window.bottom - window.top;
    Surface surface(full_width, full_height);
    if (!surface.ready() || !PrintWindow(target, surface.dc(), kPwFullContent) ||
        !has_content(surface)) return false;
    const int offset_x = origin.x - window.left;
    const int offset_y = origin.y - window.top;
    if (offset_x < 0 || offset_y < 0 || offset_x + region.right > full_width ||
        offset_y + region.bottom > full_height) return false;
    const RECT shifted{region.left + offset_x, region.top + offset_y,
                       region.right + offset_x, region.bottom + offset_y};
    copy_rows(surface.bits(), full_width, shifted, pixels, width, height);
    return true;
}

// FNV-1a over every second pixel, RGB only: PrintWindow hands back alpha = 0 on
// some Qt builds, and hashing alpha would make two identical frames differ.
uint64_t hash_pixels(const std::vector<uint8_t>& pixels, int width, int height) {
    uint64_t value = 1469598103934665603ull;
    const size_t stride = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height; y += 2) {
        const uint8_t* row = pixels.data() + static_cast<size_t>(y) * stride;
        for (int x = 0; x < width; x += 2) {
            value ^= row[x * 4 + 0];
            value *= 1099511628211ull;
            value ^= row[x * 4 + 1];
            value *= 1099511628211ull;
            value ^= row[x * 4 + 2];
            value *= 1099511628211ull;
        }
    }
    return value;
}

// Cheap perceptual hash of a client region (every other pixel, BGRA).
// Two uses, both of them anti-rebound:
//   1. before a click, two consecutive frames must hash equal - never act on a
//      bar that is still animating in or repainting;
//   2. after a click, a hash change proves Telegram actually repainted the bar,
//      so the settle check reads a fresh frame instead of the stale one that
//      produced the second click in the first place.
// The pixels come from PrintWindow, so the GUI (or any other window) lying on
// top of the chat cannot fake "the bar repainted".
bool region_hash(HWND hwnd, RECT region, uint64_t& hash) {
    hash = 0;
    const int width = region.right - region.left;
    const int height = region.bottom - region.top;
    if (width < 8 || height < 4 || width > 4096 || height > 1024) return false;
    if (hwnd) {
        std::vector<uint8_t> pixels;
        int captured_width = 0;
        int captured_height = 0;
        if (capture_client_region(hwnd, region, pixels, captured_width, captured_height) &&
            captured_width == width && captured_height == height) {
            hash = hash_pixels(pixels, width, height);
            return true;
        }
    }
    // PrintWindow refused to render this window. A desktop read is still usable as
    // *evidence* (the caller refuses to click unless the window is foreground and
    // the click point belongs to it), but it is never the primary source.
    HDC screen = CreateDCW(L"DISPLAY", nullptr, nullptr, nullptr);
    if (!screen) return false;
    HDC memory = CreateCompatibleDC(screen);
    if (!memory) {
        DeleteDC(screen);
        return false;
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        DeleteDC(memory);
        DeleteDC(screen);
        return false;
    }
    HGDIOBJ old = SelectObject(memory, bitmap);
    const BOOL copied = BitBlt(memory, 0, 0, width, height, screen, region.left, region.top,
                               SRCCOPY | CAPTUREBLT);
    if (copied) {
        const auto* source = static_cast<const uint8_t*>(bits);
        std::vector<uint8_t> pixels(source, source + static_cast<size_t>(width) * height * 4);
        hash = hash_pixels(pixels, width, height);
    }
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    DeleteDC(screen);
    return copied != FALSE;
}

RECT expand_rect(RECT rect, UINT dpi, LONG dx_dip, LONG dy_dip, const RECT& clamp) {
    const LONG dx = dx_dip * static_cast<LONG>(dpi) / 96;
    const LONG dy = dy_dip * static_cast<LONG>(dpi) / 96;
    rect.left = std::max(clamp.left, rect.left - dx);
    rect.right = std::min(clamp.right, rect.right + dx);
    rect.top = std::max(clamp.top, rect.top - dy);
    rect.bottom = std::min(clamp.bottom, rect.bottom + dy);
    return rect;
}

// Quantized bar position: identifies the physical bar slot even when the OCR'd
// chat name drifts. Same slot + same user input = same toggle, never twice.
uint64_t bar_slot_id(const RECT& rect, UINT dpi) {
    const LONG quantum = std::max<LONG>(2, static_cast<LONG>(dpi) / 48);
    const auto fold = [quantum](LONG value) {
        return static_cast<uint64_t>(static_cast<uint32_t>(value / quantum));
    };
    return (fold(rect.left) << 42) ^ (fold(rect.top) << 21) ^ fold(rect.right - rect.left);
}

// The chat identity a latch may be keyed on. The window title is exact text, so
// it beats OCR of the heading - the heading lives in the top bar and its
// geometry (and therefore its recognition) depends on pinned bars, tabs and DPI.
// window/main_window.cpp:887-937 composes it as "Name – (unread) – Telegram".
std::wstring chat_identity(HWND hwnd, const std::wstring& heading) {
    if (hwnd) {
        wchar_t title[512] = {};
        if (GetWindowTextW(hwnd, title, static_cast<int>(std::size(title))) > 0) {
            const std::wstring identity = normalize_chat_identity(title);
            if (!identity.empty()) return identity;
        }
    }
    return heading;
}

std::wstring latch_key(DWORD pid, const std::wstring& identity) {
    if (identity.empty()) return std::wstring();
    return std::to_wstring(pid) + L":" + identity;
}

void ensure_guard_logger() {
    TranslateClickGuard& guard = shared_translate_guard();
    if (guard.has_logger()) return;
    guard.set_logger([](const std::wstring& line) { debug_log(line); });
}

enum class BarClickOutcome {
    Clicked,
    AlreadyTranslated,
    Denied,
    Unavailable,
};

struct BarClickReport {
    BarClickOutcome outcome = BarClickOutcome::Unavailable;
    int state = 0;        // 0 no bar, 1 pending, 2 original, 3 pending w/o identity
    size_t context = 0;   // hash of the OCR heading, for the state log line
    std::wstring identity;
};

// Cancellation hooks so a settle wait never outlives its worker.
struct BarClickCancel {
    const std::atomic<bool>* stop = nullptr;
    const std::atomic<DWORD>* current_pid = nullptr;
};

bool bar_click_cancelled(const BarClickCancel& cancel, DWORD pid) {
    return (cancel.stop && cancel.stop->load()) ||
           (cancel.current_pid && cancel.current_pid->load() != pid);
}

// A silent early return is how the unattached-agent state (pid=0) survived 509
// guard decisions unnoticed: every one of them logged the same
// 'found=0 chat=""' line. One named reason per stall, at most every 5 s.
void log_stall(const wchar_t* reason) {
    static thread_local const wchar_t* last_reason = nullptr;
    static thread_local uint64_t last_ms = 0;
    const uint64_t now = GetTickCount64();
    if (last_reason == reason && now - last_ms < 5000) return;
    last_reason = reason;
    last_ms = now;
    debug_log(std::wstring(L"native bar click skipped: ") + reason);
}

// One place that may click the toggle, shared by the automatic agent, the
// hotkey, the tool page and the overlay button. Anything that can address the
// bar must pass this latch, or the latch is worthless.
BarClickReport perform_bar_click(DWORD pid, HWND hwnd, bool explicit_request,
                                 const BarClickCancel& cancel) {
    ensure_guard_logger();
    TranslateClickGuard& guard = shared_translate_guard();
    BarClickReport report;
    if (!pid || !hwnd) {
        log_stall(pid ? L"no Telegram window for pid" : L"no target pid (agent not attached)");
        guard.observe({}, false, false, GetTickCount64());
        return report;
    }
    ProbeContext before{}, after{};
    if (!probe_context(hwnd, before)) {
        log_stall(L"probe failed: window not foreground, minimised or unreadable");
        guard.observe({}, false, false, GetTickCount64());
        return report;
    }
    const TranslateBarHit hit = find_translate_bar(pid);
    if (!probe_context(hwnd, after) || !same_probe_context(before, after)) {
        log_stall(L"probe changed during the scan (foreground or input tick moved)");
        guard.observe({}, false, false, GetTickCount64());
        return report;
    }
    report.identity = chat_identity(hwnd, hit.chat);
    report.context = std::hash<std::wstring>{}(hit.chat);
    report.state = !hit.found ? 0 : (report.identity.empty() ? 3 : (hit.already ? 2 : 1));
    const std::wstring key = latch_key(pid, report.identity);
    if (!hit.found) {
        guard.observe(key, false, false, GetTickCount64());
        report.outcome = BarClickOutcome::Unavailable;
        return report;
    }
    const uint64_t slot = bar_slot_id(hit.rect, before.dpi);
    if (!guard.observe(key, true, hit.already, GetTickCount64(), explicit_request,
                       before.input_tick, slot, report.identity)) {
        report.outcome = hit.already ? BarClickOutcome::AlreadyTranslated
                                     : BarClickOutcome::Denied;
        return report;
    }

    // The bar is a toggle: act only on two identical frames, then re-read right
    // before DOWN. The old code clicked from a single frame and toggled the chat
    // back 1.95 s later (native.log: y=126 then y=143 on the same chat).
    const RECT client_screen{after.client.left, after.client.top, after.client.right,
                             after.client.bottom};
    const RECT watch = expand_rect(hit.rect, before.dpi, 60, 10, client_screen);
    uint64_t frame_a = 0;
    uint64_t frame_b = 0;
    if (!region_hash(hwnd, watch, frame_a)) {
        guard.observe(key, false, false, GetTickCount64());
        return report;
    }
    Sleep(90);
    const TranslateBarHit fresh = find_translate_bar(pid);
    if (!region_hash(hwnd, watch, frame_b) || !probe_context(hwnd, after) ||
        !same_probe_context(before, after) || bar_click_cancelled(cancel, pid)) {
        guard.observe(key, false, false, GetTickCount64());
        return report;
    }
    if (fresh.found && fresh.already && fresh.chat == hit.chat) {
        guard.observe(key, true, true, GetTickCount64(), explicit_request, after.input_tick, slot,
                      report.identity);
        report.outcome = BarClickOutcome::AlreadyTranslated;
        return report;
    }
    const int tolerance = std::max(2, static_cast<int>(before.dpi) * 2 / 96);
    if (!fresh.found || fresh.already || fresh.chat != hit.chat ||
        std::abs(fresh.rect.left - hit.rect.left) > tolerance ||
        std::abs(fresh.rect.top - hit.rect.top) > tolerance ||
        std::abs(fresh.rect.right - hit.rect.right) > tolerance ||
        std::abs(fresh.rect.bottom - hit.rect.bottom) > tolerance) {
        guard.observe(key, false, false, GetTickCount64());
        return report;
    }
    if (frame_a != frame_b) {
        // Still repainting. Staying armed is free; clicking a moving target is
        // exactly how a "translate" click lands on "show original".
        debug_log(L"native bar defer: region repainted between frames");
        report.outcome = BarClickOutcome::Denied;
        return report;
    }
    if ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) |
         GetAsyncKeyState(VK_MBUTTON)) & 0x8000) {
        guard.observe(key, false, false, GetTickCount64());
        return report;
    }
    const int cx = fresh.rect.left + (fresh.rect.right - fresh.rect.left) / 2;
    const int cy = fresh.rect.top + (fresh.rect.bottom - fresh.rect.top) / 2;
    const HWND covering = WindowFromPoint(POINT{cx, cy});
    if (!covering || GetAncestor(covering, GA_ROOT) != hwnd) {
        guard.observe(key, false, false, GetTickCount64());
        return report;
    }

    // Mark before dispatch: a timeout can still mean that Qt queued the action.
    guard.delivered();
    const bool ok = click_client(hwnd, cx, cy);
    debug_log(L"native bar single click key=" + guard_hex(guard_hash(key)) + L" at " +
              std::to_wstring(cx) + L"," + std::to_wstring(cy) +
              (ok ? L" (delivered)" : L" (delivery unconfirmed)"));
    report.outcome = BarClickOutcome::Clicked;

    // Settle: wait for a real repaint, then classify once. Never re-scan the
    // pre-click frame - that is the frame that still says "translate".
    const uint64_t deadline = GetTickCount64() + 3000;
    uint64_t previous = frame_b;
    bool repainted = false;
    while (GetTickCount64() < deadline && !bar_click_cancelled(cancel, pid)) {
        Sleep(120);
        uint64_t current = 0;
        if (!region_hash(hwnd, watch, current)) continue;
        if (current != previous) {
            repainted = true;
            previous = current;
            continue;
        }
        if (!repainted) continue;
        const TranslateBarHit settled = find_translate_bar(pid);
        if (settled.found && settled.already) {
            guard.confirm_translated(key, GetTickCount64());
            debug_log(L"native bar settle: confirmed translated");
        } else if (settled.found) {
            guard.settle_failed(key, GetTickCount64());
            debug_log(L"native bar settle: label still pending, latch closed");
        }
        break;
    }
    if (!repainted) {
        debug_log(L"native bar settle: no repaint within window (click may have missed)");
    }
    return report;
}

} // namespace


// Client-space region of `target`, captured through PrintWindow only. Callers use
// this as *evidence* (did the bar repaint?) and must never see another process's
// pixels, so there is deliberately no desktop-BitBlt fallback here: no evidence is
// better than false evidence.
bool capture_client_region(HWND target, RECT region, std::vector<uint8_t>& pixels,
                           int& width, int& height) {
    RECT client{};
    POINT origin{0, 0};
    if (!GetClientRect(target, &client) || !ClientToScreen(target, &origin)) return false;
    RECT wanted = region;
    if (wanted.right <= wanted.left || wanted.bottom <= wanted.top) {
        wanted = RECT{origin.x, origin.y, origin.x + client.right, origin.y + client.bottom};
    }
    return print_window_region(target,
                               RECT{wanted.left - origin.x, wanted.top - origin.y,
                                    wanted.right - origin.x, wanted.bottom - origin.y},
                               pixels, width, height);
}
TranslationServer::~TranslationServer() {
    stop();
}

bool TranslationServer::start(uint16_t preferred_port) {
    if (running_.load()) return true;
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;

    SOCKET listener = INVALID_SOCKET;
    uint16_t chosen = 0;
    for (uint16_t attempt = 0; attempt < kPortAttempts; ++attempt) {
        const uint16_t candidate = static_cast<uint16_t>(preferred_port + attempt);
        SOCKET socket_handle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_handle == INVALID_SOCKET) continue;
        BOOL exclusive = TRUE;
        setsockopt(socket_handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(candidate);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(socket_handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
            listen(socket_handle, SOMAXCONN) == 0) {
            listener = socket_handle;
            chosen = candidate;
            break;
        }
        closesocket(socket_handle);
    }
    if (listener == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    listen_ = listener;
    port_ = chosen;
    running_.store(true);
    accept_thread_ = std::thread([this] { accept_loop(); });
    for (int i = 0; i < kWorkers; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
    // Warm the upstream TLS sessions off the request path so the first chat the
    // user opens does not pay four handshakes in sequence.
    std::thread([] {
        translate_probe_upstream();
        translate_prewarm(4);
    }).detach();
    debug_log(L"native translate server listening on 127.0.0.1:" +
              std::to_wstring(port_));
    return true;
}

void TranslationServer::stop() {
    if (!running_.exchange(false)) return;
    if (listen_ != INVALID_SOCKET) {
        closesocket(listen_);
        listen_ = INVALID_SOCKET;
    }
    queue_cv_.notify_all();
    if (accept_thread_.joinable()) accept_thread_.join();
    {
        std::lock_guard lock(queue_mutex_);
        while (!queue_.empty()) {
            closesocket(queue_.front());
            queue_.pop();
        }
    }
    queue_cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();
    WSACleanup();
}

size_t TranslationServer::cache_size() const {
    std::lock_guard lock(cache_mutex_);
    return cache_.size();
}

void TranslationServer::accept_loop() {
    while (running_.load()) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(listen_, &read_set);
        timeval timeout{0, 400000};
        const int ready = select(0, &read_set, nullptr, nullptr, &timeout);
        if (!running_.load()) break;
        if (ready <= 0) continue;
        sockaddr_in address{};
        int length = sizeof(address);
        SOCKET client = accept(listen_, reinterpret_cast<sockaddr*>(&address), &length);
        if (client == INVALID_SOCKET) continue;
        {
            std::lock_guard lock(queue_mutex_);
            if (queue_.size() >= 96) {
                closesocket(client);
                continue;
            }
            queue_.push(client);
        }
        queue_cv_.notify_one();
    }
}

void TranslationServer::worker_loop() {
    while (true) {
        SOCKET client = INVALID_SOCKET;
        {
            std::unique_lock lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return !running_.load() || !queue_.empty(); });
            if (!running_.load() && queue_.empty()) return;
            client = queue_.front();
            queue_.pop();
        }
        handle_client(client);
    }
}

void TranslationServer::handle_client(SOCKET client) {
    // Telegram keeps its socket alive across a whole chat's worth of messages;
    // answering in a loop removes one TCP round trip per translated line.
    DWORD nodelay = 1;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay),
               sizeof(nodelay));
    DWORD timeout = kIdleTimeoutMs;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
               sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout),
               sizeof(timeout));

    int served = 0;
    while (served < kKeepAliveRequests && running_.load()) {
        std::string request;
        request.reserve(4096);
        char buffer[8192];
        while (request.size() < kMaxRequest) {
            const int got = recv(client, buffer, sizeof(buffer), 0);
            if (got <= 0) {
                closesocket(client);
                return;
            }
            request.append(buffer, static_cast<size_t>(got));
            if (request.find("\r\n\r\n") != std::string::npos) break;
        }
        if (request.empty()) {
            closesocket(client);
            return;
        }
        if (!serve_request(client, request)) {
            closesocket(client);
            return;
        }
        ++served;
    }
    closesocket(client);
}

// Handles one request on an already-open connection. Returns false when the
// connection cannot be reused.
bool TranslationServer::serve_request(SOCKET client, const std::string& request) {
    const size_t line_end = request.find("\r\n");
    if (line_end == std::string::npos) {
        send_response(client, 400, "text/plain", "bad request", false);
        return false;
    }
    const std::string_view line(request.data(), line_end);
    const size_t first_space = line.find(' ');
    const size_t second_space = first_space == std::string_view::npos
                                    ? std::string_view::npos
                                    : line.find(' ', first_space + 1);
    if (first_space == std::string_view::npos || second_space == std::string_view::npos) {
        send_response(client, 400, "text/plain", "bad request", false);
        return false;
    }
    const std::string_view target = line.substr(first_space + 1, second_space - first_space - 1);
    const size_t query_pos = target.find('?');
    const std::string_view path = target.substr(0, query_pos);
    if (path != "/translate" && path != "/") {
        send_response(client, 404, "text/plain", "not found", false);
        return false;
    }

    const auto started = std::chrono::steady_clock::now();
    requests_.fetch_add(1);
    const std::string_view query = query_pos == std::string_view::npos
                                       ? std::string_view()
                                       : target.substr(query_pos + 1);
    std::string raw = html_unescape(query_value(query, "q"));
    const std::wstring from = normalize_translation_language(utf8_to_wide(query_value(query, "from")));
    std::string to = query_value(query, "to");
    if (to.empty()) to = "zh";
    const std::wstring target_language = normalize_translation_language(utf8_to_wide(to));
    const std::wstring text = utf8_to_wide(raw);

    std::wstring translated;
    bool succeeded = true;
    if (!raw.empty()) {
        const std::string key = translation_cache_key(text, from, target_language);
        {
            std::lock_guard lock(cache_mutex_);
            const auto i = cache_.find(key);
            if (i != cache_.end()) {
                translated = utf8_to_wide(i->second);
                hits_.fetch_add(1);
            }
        }
        if (translated.empty()) {
            const auto result = translate_text_to(text, target_language, from);
            if (result.ok) {
                translated = result.text;
                std::lock_guard lock(cache_mutex_);
                if (cache_.size() >= 8192) {
                    // Drop a quarter, not the whole table: a full clear forced
                    // every later request back onto the network.
                    size_t drop = cache_.size() / 4;
                    for (auto it = cache_.begin(); it != cache_.end() && drop > 0; --drop) {
                        it = cache_.erase(it);
                    }
                }
                cache_.emplace(key, wide_to_utf8(translated));
            } else {
                succeeded = false;
                lost_.fetch_add(1);
                debug_log(L"native translate failed: " + result.error);
            }
        }
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    last_ms_.store(static_cast<uint64_t>(elapsed));
    total_ms_.fetch_add(static_cast<uint64_t>(elapsed));
    timed_.fetch_add(1);

    if (!succeeded) {
        send_response(client, 502, "application/json; charset=utf-8",
                       "{\"error\":\"translation temporarily unavailable\"}", false);
        return false;
    }
    const std::string body = "[[[\"" + json_escape(wide_to_utf8(translated)) + "\"]]]";
    send_response(client, 200, "application/json; charset=utf-8", body, true);
    return true;
}

std::wstring native_translate_url(uint16_t port) {
    return L"http://127.0.0.1:" + std::to_wstring(port) +
           L"/translate?q=%q&from=%f&to=%t";
}

bool native_translate_configured(const std::wstring& telegram_exe, uint16_t port) {
    const std::wstring tdata = tdata_path(telegram_exe);
    if (tdata.empty()) return false;
    const std::string json = read_file(tdata + L"\\experimental_options.json");
    if (json.empty()) return false;
    const std::string needle = "127.0.0.1:" + std::to_string(port);
    return json.find("translate-url-template") != std::string::npos &&
           json.find(needle) != std::string::npos;
}

bool configure_native_translate(const std::wstring& telegram_exe, uint16_t port,
                                std::wstring& error) {
    // Writing translate-url-template points Telegram's ONLY translate provider
    // (lang/translate_provider.cpp:37) at our local TranslationServer, so it is only
    // legal while one of our processes is actually listening: --launch-inject with
    // translate_engine == 1, or the GUI with the same setting. Every other path has
    // to call clear_native_translate() instead -- a template aimed at a dead port
    // makes every translation fail silently, Premium bit or not.
    if (port == 0) return clear_native_translate(telegram_exe, error);
    std::wstring exe = telegram_exe;
    if (exe.empty()) exe = L"D:\\Telegram Desktop\\Telegram.exe";
    const std::wstring tdata = tdata_path(exe);
    if (tdata.empty()) {
        error = L"Telegram path is empty";
        return false;
    }
    if (!ensure_dir(tdata)) {
        error = L"tdata folder is not writable";
        return false;
    }
    const std::wstring path = tdata + L"\\experimental_options.json";
    std::string json = read_file(path);
    if (json.size() >= 3 && static_cast<unsigned char>(json[0]) == 0xEF &&
        static_cast<unsigned char>(json[1]) == 0xBB &&
        static_cast<unsigned char>(json[2]) == 0xBF) {
        json.erase(0, 3);
    }
    if (json.find('{') == std::string::npos) json = "{}";
    // Surgical edit: Telegram stores unrelated options in the same object.
    const std::string key = "\"translate-url-template\"";
    const std::string member =
        key + ":\"" + json_escape(wide_to_utf8(native_translate_url(port))) + "\"";
    const size_t key_pos = json.find(key);
    if (key_pos != std::string::npos) {
        const size_t colon = json.find(':', key_pos + key.size());
        size_t value_start = colon == std::string::npos ? std::string::npos : colon + 1;
        while (value_start < json.size() &&
               std::isspace(static_cast<unsigned char>(json[value_start]))) {
            ++value_start;
        }
        if (value_start >= json.size() || json[value_start] != '"') {
            error = L"Malformed translate-url-template value";
            return false;
        }
        size_t value_end = value_start + 1;
        while (value_end < json.size() && json[value_end] != '"') {
            if (json[value_end] == '\\' && value_end + 1 < json.size()) ++value_end;
            ++value_end;
        }
        if (value_end >= json.size()) {
            error = L"Unterminated translate-url-template value";
            return false;
        }
        json.replace(key_pos, value_end + 1 - key_pos, member);
    } else {
        const size_t close = json.rfind('}');
        if (close == std::string::npos) {
            error = L"Malformed experimental_options.json";
            return false;
        }
        size_t before = close;
        while (before > 0 && std::isspace(static_cast<unsigned char>(json[before - 1]))) {
            --before;
        }
        const bool empty_object = before == 0 || json[before - 1] == '{';
        json.insert(close, (empty_object ? std::string() : std::string(", ")) + member);
    }
    if (!write_file_atomic(path, json)) {
        error = L"Could not write experimental_options.json";
        return false;
    }
    if (!native_translate_configured(exe, port)) {
        error = L"experimental_options.json refused the template";
        return false;
    }
    return true;
}

bool clear_native_translate(const std::wstring& telegram_exe, std::wstring& error) {
    std::wstring exe = telegram_exe;
    if (exe.empty()) exe = L"D:\\Telegram Desktop\\Telegram.exe";
    const std::wstring tdata = tdata_path(exe);
    if (tdata.empty()) {
        error = L"Telegram path is empty";
        return false;
    }
    const std::wstring path = tdata + L"\\experimental_options.json";
    if (!file_exists(path)) return true;
    std::string json = read_file(path);
    if (json.empty()) return true;
    if (json.size() >= 3 && static_cast<unsigned char>(json[0]) == 0xEF &&
        static_cast<unsigned char>(json[1]) == 0xBB &&
        static_cast<unsigned char>(json[2]) == 0xBF) {
        json.erase(0, 3);
    }
    const std::string key = "\"translate-url-template\"";
    const size_t key_pos = json.find(key);
    if (key_pos == std::string::npos) return true;
    const size_t colon = json.find(':', key_pos + key.size());
    if (colon == std::string::npos) {
        error = L"Malformed experimental_options.json";
        return false;
    }
    size_t value_start = colon + 1;
    while (value_start < json.size() &&
           std::isspace(static_cast<unsigned char>(json[value_start]))) {
        ++value_start;
    }
    if (value_start >= json.size() || json[value_start] != '"') {
        error = L"Malformed translate-url-template value";
        return false;
    }
    size_t value_end = value_start + 1;
    while (value_end < json.size() && json[value_end] != '"') {
        if (json[value_end] == '\\' && value_end + 1 < json.size()) ++value_end;
        ++value_end;
    }
    if (value_end >= json.size()) {
        error = L"Unterminated translate-url-template value";
        return false;
    }
    size_t erase_begin = key_pos;
    size_t erase_end = value_end + 1;
    size_t before = key_pos;
    while (before > 0 && std::isspace(static_cast<unsigned char>(json[before - 1]))) {
        --before;
    }
    if (before > 0 && json[before - 1] == ',') {
        erase_begin = before - 1;
    } else {
        size_t after = erase_end;
        while (after < json.size() &&
               std::isspace(static_cast<unsigned char>(json[after]))) {
            ++after;
        }
        if (after < json.size() && json[after] == ',') erase_end = after + 1;
    }
    json.erase(erase_begin, erase_end - erase_begin);
    const size_t open = json.find('{');
    const size_t close = json.rfind('}');
    if (open != std::string::npos && close != std::string::npos && open < close &&
        json.substr(open + 1, close - open - 1).find_first_not_of(" \t\r\n") ==
            std::string::npos) {
        json = "{}\n";
    }
    if (!write_file_atomic(path, json)) {
        error = L"Could not write experimental_options.json";
        return false;
    }
    return true;
}

bool restart_telegram(DWORD pid, std::wstring& error) {
    const std::wstring exe = process_exe_path(pid);
    if (exe.empty()) {
        error = L"Telegram path not found";
        return false;
    }
    HWND hwnd = telegram_window(pid);
    if (!hwnd) {
        error = L"Telegram window not found";
        return false;
    }
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    for (int i = 0; i < 30 && process_alive(pid); ++i) {
        Sleep(150);
    }
    if (process_alive(pid)) {
        // Telegram often minimizes to tray on WM_CLOSE. The restart button is
        // an explicit user action, so finish the shutdown if it stayed alive.
        HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (!process) {
            error = L"Telegram did not exit";
            return false;
        }
        TerminateProcess(process, 0);
        WaitForSingleObject(process, 5000);
        CloseHandle(process);
        for (int i = 0; i < 20 && process_alive(pid); ++i) {
            Sleep(100);
        }
        if (process_alive(pid)) {
            error = L"Telegram could not be stopped";
            return false;
        }
    }

    std::wstring command = L"\"" + exe + L"\"";
    std::filesystem::path path(exe);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        path.parent_path().c_str(), &startup, &process)) {
        error = format_w(L"CreateProcess failed (%lu)", GetLastError());
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

bool trigger_native_translate(DWORD pid, bool to_chinese) {
    (void)to_chinese;
    HWND hwnd = telegram_window(pid);
    if (!hwnd) {
        shared_translate_guard().observe({}, false, false, GetTickCount64());
        return false;
    }
    // 截图读的是屏幕像素，Telegram 被挡住时读到的就是别的窗口。
    // 所以先把它抬到前台、等一帧再扫，顺序不能反。
    force_foreground(hwnd);
    Sleep(140);
    if (!foreground_is(hwnd)) {
        debug_log(L"native translate trigger: target not foreground, refusing to read pixels");
        shared_translate_guard().observe({}, false, false, GetTickCount64());
        return false;
    }
    // Manual path (hotkey / tool page / overlay button) goes through the very
    // same latch as the automatic agent: a second press must never toggle the
    // chat back to the original language.
    const BarClickCancel cancel{};
    const BarClickReport report = perform_bar_click(pid, hwnd, true, cancel);
    debug_log(L"native translate trigger: state=" + std::to_wstring(report.state) +
              L" outcome=" + std::to_wstring(static_cast<int>(report.outcome)));
    return report.outcome == BarClickOutcome::Clicked ||
           report.outcome == BarClickOutcome::AlreadyTranslated;
}

bool telegram_chat_rect(DWORD pid, RECT& rect) {
    HWND hwnd = telegram_window(pid);
    if (!hwnd) return false;
    RECT client{};
    if (!GetClientRect(hwnd, &client)) return false;
    POINT origin{0, 0};
    if (!ClientToScreen(hwnd, &origin)) return false;
    // Telegram 的会话列表是固定逻辑宽度的，窗口剩下部分才是聊天区。
    // 实测 1705 px 窗口里聊天区从 x≈1099 开始（约 64.5%），这里按比例取并保留
    // 合理下限，overlay 侧还会再夹一次边界，所以只是粗略裁剪。
    const LONG width = client.right - client.left;
    const LONG height = client.bottom - client.top;
    if (width < 480 || height < 360) return false;
    LONG split = origin.x + static_cast<LONG>(width * 0.645);
    split = std::max<LONG>(split, origin.x + 320);
    split = std::min<LONG>(split, origin.x + width - 320);
    rect = RECT{split, origin.y + 108, origin.x + width, origin.y + height};
    return true;
}

NativeTranslateAgent::~NativeTranslateAgent() {
    stop();
}

void NativeTranslateAgent::set_target(DWORD pid) {
    pid_.store(pid);
    if (enabled_.load()) start();
}

void NativeTranslateAgent::set_enabled(bool enabled) {
    enabled_.store(enabled);
    if (enabled) {
        start();
    } else {
        stop();
    }
}

void NativeTranslateAgent::start() {
    if (running_.exchange(true)) return;
    stop_.store(false);
    worker_ = std::thread([this] { loop(); });
}

void NativeTranslateAgent::stop() {
    if (!running_.exchange(false)) return;
    stop_.store(true);
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    available_.store(false);
}

void NativeTranslateAgent::trigger_now() {
    trigger_requested_.store(true);
    start();
    cv_.notify_all();
}

bool NativeTranslateAgent::click_bar(DWORD pid, bool force) {
    ensure_guard_logger();
    if (!pid) {
        shared_translate_guard().observe({}, false, false, GetTickCount64());
        return false;
    }
    HWND hwnd = telegram_window(pid);
    if (!hwnd) {
        shared_translate_guard().observe({}, false, false, GetTickCount64());
        return false;
    }
    if (force) {
        force_foreground(hwnd);
        Sleep(140);
    }
    const BarClickCancel cancel{&stop_, &pid_};
    const BarClickReport report = perform_bar_click(pid, hwnd, force, cancel);
    if (probe_state_ != report.state || probe_identity_ != report.context) {
        probe_state_ = report.state;
        probe_identity_ = report.context;
        debug_log(L"native bar state=" + std::to_wstring(report.state) + L" context=" +
                  std::to_wstring(report.context) + L" identity=\"" + report.identity + L"\"");
    }
    if (report.outcome == BarClickOutcome::Clicked) clicks_.fetch_add(1);
    const bool usable = report.outcome == BarClickOutcome::Clicked ||
                        report.outcome == BarClickOutcome::AlreadyTranslated ||
                        report.outcome == BarClickOutcome::Denied;
    if (usable) available_.store(true);
    return usable;
}

void NativeTranslateAgent::loop() {
    const HRESULT apartment = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(apartment)) debug_log(L"native OCR apartment initialization failed");
    uint64_t interval_ms = kIdleScanMs;
    std::wstring last_title;   // window title doubles as a chat-switch signal
    ScanStage stage = ScanStage::Idle;
    while (!stop_.load()) {
        {
            std::unique_lock lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(interval_ms),
                         [this] { return stop_.load() || trigger_requested_.load(); });
        }
        if (stop_.load()) break;
        const bool requested = trigger_requested_.exchange(false);
        if (!enabled_.load() && !requested) {
            interval_ms = kIdleScanMs;
            continue;
        }
        const DWORD pid = pid_.load();
        if (guard_pid_ != pid) {
            guard_pid_ = pid;
            shared_translate_guard().reset();
            probe_state_ = -1;
        }
        const HWND hwnd = telegram_window(pid);
        // A title change is a chat switch (or new unread mail): scan promptly
        // instead of waiting out the backoff. Without this, returning to
        // Telegram could sit up to kMaxIdleScanMs before the bar was noticed.
        std::wstring title;
        if (hwnd) {
            wchar_t buffer[512] = {};
            if (GetWindowTextW(hwnd, buffer, static_cast<int>(std::size(buffer))) > 0) {
                title = buffer;
            }
        }
        const bool navigated = title != last_title;
        last_title = title;

        const bool bar_present = click_bar(pid, requested);
        available_.store(bar_present);

        ScanStage next = stage;
        if (bar_present) {
            interval_ms = kBarScanMs;
            next = ScanStage::Bar;
        } else if (requested || navigated || !hwnd || !foreground_is(hwnd) || IsIconic(hwnd)) {
            // Nothing to click, or the window is not even on screen: poll at the
            // floor. probe_context() fails before any OCR runs, so this is cheap.
            interval_ms = kIdleScanMs;
            next = ScanStage::Idle;
        } else {
            // Foreground, no bar: back off so a chat without a translate bar does
            // not cost a full OCR pass every second.
            interval_ms = std::min(kMaxIdleScanMs, interval_ms + kIdleBackoffMs);
            next = ScanStage::Backoff;
        }
        if (next != stage) {
            stage = next;
            debug_log(L"native scan cadence=" + std::to_wstring(interval_ms) +
                      L"ms stage=" + scan_stage_name(stage) + L" title=\"" + title + L"\"");
        }
    }
    if (SUCCEEDED(apartment)) RoUninitialize();
}

} // namespace tv
