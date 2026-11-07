#include "overlay.h"
#include "core.h"
#include "native_translate.h"
#include "tools.h"

#include <gdiplus.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Security.Cryptography.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "windowsapp.lib")

using namespace Gdiplus;
using namespace winrt;
using namespace winrt::Windows::Globalization;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Media::Ocr;
using namespace winrt::Windows::Security::Cryptography;

namespace tv {
namespace {

constexpr wchar_t kMessageClass[] = L"TeleVipTranslateMessage";
constexpr wchar_t kButtonClass[] = L"TeleVipTranslateFab";
constexpr wchar_t kOverlayClass[] = L"TeleVipTranslateOverlay";
constexpr UINT WM_APP_OVERLAY_READY = WM_APP + 20;
constexpr wchar_t kOverlayFont[] = L"Segoe UI Variable Text";

bool is_telegram_process(DWORD pid) {
    const std::wstring path = lower(process_exe_path(pid));
    return path.size() >= 12 && path.substr(path.size() - 12) == L"telegram.exe";
}

struct FindContext {
    DWORD pid = 0;
    HWND best = nullptr;
    long long area = 0;
};

BOOL CALLBACK find_window_cb(HWND hwnd, LPARAM param) {
    auto* context = reinterpret_cast<FindContext*>(param);
    if (!IsWindowVisible(hwnd)) return TRUE;
    wchar_t title[256] = {};
    if (GetWindowTextW(hwnd, title, 256) == 0) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (context->pid != 0 && pid != context->pid) return TRUE;
    if (context->pid == 0 && !is_telegram_process(pid)) return TRUE;
    DWORD cloaked = 0;
    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) return TRUE;
    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) return TRUE;
    const long long width = rect.right - rect.left;
    const long long height = rect.bottom - rect.top;
    if (width < 420 || height < 320) return TRUE;
    const long long area = width * height;
    if (area > context->area) {
        context->area = area;
        context->best = hwnd;
    }
    return TRUE;
}


float average_luminance(const std::vector<uint8_t>& pixels) {
    double sum = 0.0;
    size_t count = 0;
    for (size_t i = 0; i + 3 < pixels.size(); i += 4 * 32) {
        sum += (pixels[i] + pixels[i + 1] + pixels[i + 2]) / 3.0;
        ++count;
    }
    return count ? static_cast<float>(sum / count / 255.0) : 1.0f;
}

bool capture_region(HWND target, RECT region, std::vector<uint8_t>& pixels, int& width,
                    int& height, RECT& screen_rect, float& luminance) {
    RECT client{};
    if (!GetClientRect(target, &client)) return false;
    POINT origin{0, 0};
    if (!ClientToScreen(target, &origin)) return false;
    const RECT client_screen{origin.x, origin.y, origin.x + client.right,
                             origin.y + client.bottom};
    RECT capture = region;
    if (capture.right <= capture.left || capture.bottom <= capture.top) {
        capture = client_screen;
    }
    capture.left = std::max(capture.left, client_screen.left);
    capture.top = std::max(capture.top, client_screen.top);
    capture.right = std::min(capture.right, client_screen.right);
    capture.bottom = std::min(capture.bottom, client_screen.bottom);
    width = capture.right - capture.left;
    height = capture.bottom - capture.top;
    if (width < 100 || height < 100 || width > 4096 || height > 4096) return false;
    screen_rect = capture;

    // PrintWindow first: it renders the target's own surface, so a TeleVip window
    // (or any other window) sitting on top of Telegram can never be read. The
    // desktop BitBlt below used to do exactly that, and the OCR then saw the GUI's
    // own branding ("TeleVip | 礼物 | ...") as if it were chat text - which is how
    // unrelated rows became clickable "translate bars".
    if (capture_client_region(target, capture, pixels, width, height)) {
        luminance = average_luminance(pixels);
        return true;
    }

    // Fallback for a window that refuses to render itself (minimised, exotic
    // compositor). Correct only while the window is unobstructed; the translate
    // bar path already refuses to click unless the target is foreground.

    // CreateDC(L"DISPLAY") 返回整个虚拟桌面，多显示器时 GetDC(nullptr) 只有
    // 主屏 —— Telegram 放在副屏上就会拍到别的窗口的像素，OCR 结果全是垃圾。
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
    const BOOL copied = BitBlt(memory, 0, 0, width, height, screen, capture.left, capture.top,
                               SRCCOPY | CAPTUREBLT);
    if (!copied) {
        SelectObject(memory, old);
        DeleteObject(bitmap);
        DeleteDC(memory);
        DeleteDC(screen);
        return false;
    }
    pixels.resize(static_cast<size_t>(width) * height * 4);
    memcpy(pixels.data(), bits, pixels.size());
    luminance = average_luminance(pixels);
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    DeleteDC(screen);
    return true;
}


std::vector<std::wstring> split_lines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    size_t position = 0;
    while (position <= text.size()) {
        const size_t end = text.find(L'\n', position);
        std::wstring line = text.substr(position, end == std::wstring::npos ? std::wstring::npos : end - position);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        lines.push_back(std::move(line));
        if (end == std::wstring::npos) break;
        position = end + 1;
    }
    return lines;
}

} // namespace


bool ocr_region_lines(HWND target, RECT region, std::vector<OcrLineHit>& lines,
                      std::wstring& error) {
    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;
    RECT screen_rect{};
    float luminance = 1.0f;
    if (!capture_region(target, region, pixels, width, height, screen_rect, luminance)) {
        error = L"capture failed";
        return false;
    }
    try {
        const auto buffer = CryptographicBuffer::CreateFromByteArray(
            array_view<uint8_t const>(pixels.data(), static_cast<uint32_t>(pixels.size())));
        const auto bitmap = SoftwareBitmap::CreateCopyFromBuffer(
            buffer, BitmapPixelFormat::Bgra8, width, height, BitmapAlphaMode::Premultiplied);
        // Both engines get a pass: the Telegram UI language may be either one,
        // and a strip this small costs ~20-30 ms per pass.
        static thread_local OcrEngine zh_engine = nullptr;
        static thread_local OcrEngine en_engine = nullptr;
        for (int pass = 0; pass < 2; ++pass) {
            OcrEngine& engine = pass == 0 ? zh_engine : en_engine;
            if (!engine) {
                engine = OcrEngine::TryCreateFromLanguage(
                    Language(pass == 0 ? L"zh-Hans-CN" : L"en-US"));
            }
            if (!engine) continue;
            const auto result = engine.RecognizeAsync(bitmap).get();
            for (const auto& line : result.Lines()) {
                std::wstring text = trim(std::wstring(line.Text().c_str()));
                if (text.size() < 2) continue;
                RECT rect{};
                bool has_rect = false;
                for (const auto& word : line.Words()) {
                    const auto bounds = word.BoundingRect();
                    RECT word_rect{
                        static_cast<LONG>(bounds.X), static_cast<LONG>(bounds.Y),
                        static_cast<LONG>(bounds.X + bounds.Width),
                        static_cast<LONG>(bounds.Y + bounds.Height)};
                    if (!has_rect) {
                        rect = word_rect;
                        has_rect = true;
                    } else {
                        rect.left = std::min(rect.left, word_rect.left);
                        rect.top = std::min(rect.top, word_rect.top);
                        rect.right = std::max(rect.right, word_rect.right);
                        rect.bottom = std::max(rect.bottom, word_rect.bottom);
                    }
                }
                if (!has_rect) continue;
                OcrLineHit hit;
                hit.text = std::move(text);
                hit.rect = RECT{rect.left + screen_rect.left, rect.top + screen_rect.top,
                                rect.right + screen_rect.left, rect.bottom + screen_rect.top};
                lines.push_back(std::move(hit));
            }
        }
    } catch (...) {
        error = L"ocr exception";
        return false;
    }
    return !lines.empty();
}

ChatTranslator::ChatTranslator() = default;

ChatTranslator::~ChatTranslator() {
    shutdown();
}

bool ChatTranslator::initialize(HWND owner) {
    owner_ = owner;
    if (gdiplus_token_ == 0) {
        GdiplusStartupInput input;
        if (GdiplusStartup(&gdiplus_token_, &input, nullptr) != Ok) gdiplus_token_ = 0;
    }
    WNDCLASSEXW message_class{};
    message_class.cbSize = sizeof(message_class);
    message_class.lpfnWndProc = &ChatTranslator::message_proc;
    message_class.hInstance = GetModuleHandleW(nullptr);
    message_class.lpszClassName = kMessageClass;
    RegisterClassExW(&message_class);
    message_ = CreateWindowExW(0, kMessageClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                               GetModuleHandleW(nullptr), this);
    if (message_) start_worker();
    return message_ != nullptr;
}

void ChatTranslator::shutdown() {
    stop_.store(true);
    stop_worker();
    if (button_) DestroyWindow(button_);
    if (overlay_) DestroyWindow(overlay_);
    if (message_) DestroyWindow(message_);
    button_ = overlay_ = message_ = nullptr;
    target_ = nullptr;
    overlay_visible_ = false;
    if (gdiplus_token_) {
        GdiplusShutdown(gdiplus_token_);
        gdiplus_token_ = 0;
    }
}

void ChatTranslator::set_enabled(bool enabled) {
    enabled_.store(enabled);
    if (!enabled) {
        hide_overlay();
        if (button_) ShowWindow(button_, SW_HIDE);
    } else {
        follow_target();
    }
}

void ChatTranslator::toggle_overlay() {
    if (overlay_visible_) hide_overlay();
    else translate_now();
}

void ChatTranslator::follow_target() {
    if (!enabled_.load() || !message_) return;
    if (!ensure_windows()) return;
    HWND found = find_target();
    if (!found) {
        if (button_) ShowWindow(button_, SW_HIDE);
        hide_overlay();
        target_ = nullptr;
        return;
    }
    RECT client{};
    GetClientRect(found, &client);
    POINT origin{0, 0};
    ClientToScreen(found, &origin);
    const RECT screen_rect{origin.x, origin.y, origin.x + client.right, origin.y + client.bottom};
    const bool moved = target_ != found || memcmp(&screen_rect, &target_rect_, sizeof(RECT)) != 0;
    target_ = found;
    target_rect_ = screen_rect;
    if (moved) hide_overlay();
    update_button();
}

bool ChatTranslator::ensure_windows() {
    if (!message_) return false;
    if (!button_) {
        WNDCLASSEXW button_class{};
        button_class.cbSize = sizeof(button_class);
        button_class.lpfnWndProc = &ChatTranslator::button_proc;
        button_class.hInstance = GetModuleHandleW(nullptr);
        button_class.hCursor = LoadCursorW(nullptr, IDC_HAND);
        button_class.lpszClassName = kButtonClass;
        RegisterClassExW(&button_class);
        button_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                                  kButtonClass, L"", WS_POPUP, 0, 0, 46, 46,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), this);
        if (button_) {
            HRGN region = CreateRoundRectRgn(0, 0, 47, 47, 14, 14);
            SetWindowRgn(button_, region, TRUE);
        }
    }
    if (!overlay_) {
        WNDCLASSEXW overlay_class{};
        overlay_class.cbSize = sizeof(overlay_class);
        overlay_class.lpfnWndProc = &ChatTranslator::overlay_proc;
        overlay_class.hInstance = GetModuleHandleW(nullptr);
        overlay_class.lpszClassName = kOverlayClass;
        RegisterClassExW(&overlay_class);
        overlay_ = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            kOverlayClass, L"", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr,
            GetModuleHandleW(nullptr), this);
    }
    return button_ != nullptr && overlay_ != nullptr;
}

HWND ChatTranslator::find_target() const {
    FindContext context;
    context.pid = target_pid_;
    EnumWindows(find_window_cb, reinterpret_cast<LPARAM>(&context));
    return context.best;
}

void ChatTranslator::update_button() {
    if (!button_ || !target_) return;
    const int x = target_rect_.right - 64;
    const int y = target_rect_.bottom - 168;
    SetWindowPos(button_, HWND_TOPMOST, x, y, 46, 46, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(button_, nullptr, TRUE);
}

void ChatTranslator::paint_button(HWND hwnd) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    RECT rect{};
    GetClientRect(hwnd, &rect);
    HBRUSH brush = CreateSolidBrush(RGB(46, 124, 247));
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    HFONT font = CreateFontW(-22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH, L"Segoe UI Variable Text");
    HGDIOBJ old = SelectObject(dc, font);
    const wchar_t* glyph = overlay_visible_ ? L"×" : L"译";
    DrawTextW(dc, glyph, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, old);
    DeleteObject(font);
    EndPaint(hwnd, &paint);
}

LRESULT CALLBACK ChatTranslator::message_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<ChatTranslator*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_APP_OVERLAY_READY && self) {
        self->handle_payload(reinterpret_cast<Payload*>(lparam));
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK ChatTranslator::button_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<ChatTranslator*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_PAINT:
        if (self) self->paint_button(hwnd);
        return 0;
    case WM_LBUTTONUP:
        if (self) self->translate_now();
        return 0;
    case WM_RBUTTONUP:
        if (self) ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK ChatTranslator::overlay_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        EndPaint(hwnd, &paint);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void ChatTranslator::translate_now() {
    // 只走 Telegram 内置翻译。原生栏不可用时直接返回，
    // 不再截图 + OCR + 画悬浮层 —— 用户明确不要屏幕悬浮翻译。
    trigger_native_translate(target_pid_, chinese_.load());
}

void ChatTranslator::start_worker() {
    if (ocr_thread_.joinable()) return;
    worker_stop_.store(false);
    ocr_thread_ = std::thread([this] { ocr_loop(); });
}

void ChatTranslator::stop_worker() {
    worker_stop_.store(true);
    job_cv_.notify_all();
    if (ocr_thread_.joinable()) ocr_thread_.join();
}

void ChatTranslator::ocr_loop() {
    init_apartment(apartment_type::multi_threaded);
    OcrEngine zh_engine = nullptr;
    OcrEngine en_engine = nullptr;
    while (true) {
        std::unique_ptr<Job> job;
        {
            std::unique_lock lock(job_mutex_);
            job_cv_.wait(lock, [this] {
                return worker_stop_.load() || pending_job_ != nullptr;
            });
            if (worker_stop_.load()) break;
            job = std::move(pending_job_);
        }
        if (!job) continue;

        auto* payload = new Payload();
        payload->target = job->screen_rect;
        payload->width = job->width;
        payload->height = job->height;
        payload->luminance = job->luminance;

        if (job->hash == last_hash_ && !last_lines_.empty() && !last_translations_.empty()) {
            payload->lines = last_lines_;
            payload->translations = last_translations_;
            payload->ok = true;
            PostMessageW(message_, WM_APP_OVERLAY_READY, 0,
                         reinterpret_cast<LPARAM>(payload));
            continue;
        }

        try {
            OcrEngine& engine = job->chinese ? zh_engine : en_engine;
            if (!engine) {
                engine = OcrEngine::TryCreateFromLanguage(
                    Language(job->chinese ? L"zh-Hans-CN" : L"en-US"));
            }
            if (!engine) engine = OcrEngine::TryCreateFromUserProfileLanguages();
            if (!engine) {
                payload->error = job->chinese ? L"系统缺少 OCR 语言包"
                                              : L"OCR language pack missing";
            } else {
                const auto buffer = CryptographicBuffer::CreateFromByteArray(
                    array_view<uint8_t const>(job->pixels.data(),
                                              static_cast<uint32_t>(job->pixels.size())));
                const auto bitmap = SoftwareBitmap::CreateCopyFromBuffer(
                    buffer, BitmapPixelFormat::Bgra8, job->width, job->height,
                    BitmapAlphaMode::Premultiplied);
                const auto result = engine.RecognizeAsync(bitmap).get();
                std::wstring query;
                for (const auto& line : result.Lines()) {
                    std::wstring text = line.Text().c_str();
                    text = trim(std::move(text));
                    if (text.size() < 2) continue;
                    RECT rect{};
                    bool has_rect = false;
                    for (const auto& word : line.Words()) {
                        const auto bounds = word.BoundingRect();
                        RECT word_rect{
                            static_cast<LONG>(bounds.X), static_cast<LONG>(bounds.Y),
                            static_cast<LONG>(bounds.X + bounds.Width),
                            static_cast<LONG>(bounds.Y + bounds.Height)};
                        if (!has_rect) {
                            rect = word_rect;
                            has_rect = true;
                        } else {
                            rect.left = std::min(rect.left, word_rect.left);
                            rect.top = std::min(rect.top, word_rect.top);
                            rect.right = std::max(rect.right, word_rect.right);
                            rect.bottom = std::max(rect.bottom, word_rect.bottom);
                        }
                    }
                    if (!has_rect || rect.right - rect.left < 12 ||
                        rect.bottom - rect.top < 8) {
                        continue;
                    }
                    query += text;
                    query += L'\n';
                    payload->lines.push_back(Line{std::move(text), rect});
                    if (payload->lines.size() >= 80) break;
                }
                if (payload->lines.empty()) {
                    payload->error = job->chinese ? L"未识别到可翻译文本"
                                                  : L"No text to translate";
                } else {
                    const auto translated = translate_text(query, job->chinese);
                    if (!translated.ok) {
                        payload->error = translated.error;
                    } else {
                        payload->translations = split_lines(translated.text);
                        if (payload->translations.size() < payload->lines.size()) {
                            payload->translations.resize(payload->lines.size());
                        }
                        payload->ok = true;
                        last_hash_ = job->hash;
                        last_lines_ = payload->lines;
                        last_translations_ = payload->translations;
                    }
                }
            }
        } catch (const hresult_error& error) {
            payload->error = error.message().c_str();
        } catch (...) {
            payload->error = L"OCR failed";
        }
        PostMessageW(message_, WM_APP_OVERLAY_READY, 0, reinterpret_cast<LPARAM>(payload));
    }
    uninit_apartment();
}

void ChatTranslator::handle_payload(Payload* payload) {
    busy_.store(false);
    std::unique_ptr<Payload> guard(payload);
    if (!payload) return;
    if (!payload->ok) {
        hide_overlay();
        return;
    }
    render_overlay(payload);
}

void ChatTranslator::hide_overlay() {
    if (overlay_) ShowWindow(overlay_, SW_HIDE);
    overlay_visible_ = false;
    if (button_) InvalidateRect(button_, nullptr, TRUE);
}

void ChatTranslator::render_overlay(Payload* payload) {
    if (!overlay_ || !payload) return;
    const int width = payload->width;
    const int height = payload->height;
    if (width <= 0 || height <= 0) return;
    // CreateDC(L"DISPLAY") 返回整个虚拟桌面，多显示器时 GetDC(nullptr) 只有
    // 主屏 —— Telegram 放在副屏上就会拍到别的窗口的像素，OCR 结果全是垃圾。
    HDC screen = CreateDCW(L"DISPLAY", nullptr, nullptr, nullptr);
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(memory, bitmap);
    {
        Graphics graphics(memory);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        const bool light = payload->luminance > 0.52f;
        const Gdiplus::Color panel = light ? Gdiplus::Color(235, 255, 255, 255) : Gdiplus::Color(232, 22, 22, 26);
        const Gdiplus::Color foreground = light ? Gdiplus::Color(255, 24, 24, 28) : Gdiplus::Color(255, 242, 242, 246);
        const Gdiplus::Color accent = Gdiplus::Color(255, 46, 124, 247);
        const Gdiplus::Color border = light ? Gdiplus::Color(60, 0, 0, 0) : Gdiplus::Color(60, 255, 255, 255);
        const size_t count = std::min(payload->lines.size(), payload->translations.size());
        for (size_t i = 0; i < count; ++i) {
            const std::wstring& text = payload->translations[i];
            if (text.empty()) continue;
            RectF area(static_cast<REAL>(payload->lines[i].rect.left - 3),
                       static_cast<REAL>(payload->lines[i].rect.top - 3),
                       static_cast<REAL>(payload->lines[i].rect.right - payload->lines[i].rect.left + 8),
                       static_cast<REAL>(payload->lines[i].rect.bottom - payload->lines[i].rect.top + 6));
            if (area.Width < 60) area.Width = 60;
            if (area.Width > width - 10) area.Width = static_cast<REAL>(width - 10);
            if (area.X < 4) area.X = 4;
            const REAL font_size = std::clamp(static_cast<REAL>(payload->lines[i].rect.bottom -
                                                                 payload->lines[i].rect.top) * 0.78f,
                                              11.0f, 20.0f);
            Font font(kOverlayFont, font_size, FontStyleRegular, UnitPixel);
            StringFormat format;
            format.SetFormatFlags(StringFormatFlagsLineLimit);
            format.SetTrimming(StringTrimmingEllipsisCharacter);
            RectF measured;
            graphics.MeasureString(text.c_str(), -1, &font, area, &format, &measured);
            if (measured.Height + 10 > area.Height) area.Height = measured.Height + 10;
            if (area.GetBottom() > height - 4) area.Y = std::max(4.0f, height - area.Height - 4);
            GraphicsPath path;
            const REAL radius = 6.0f;
            path.AddArc(area.X, area.Y, radius * 2, radius * 2, 180, 90);
            path.AddArc(area.GetRight() - radius * 2, area.Y, radius * 2, radius * 2, 270, 90);
            path.AddArc(area.GetRight() - radius * 2, area.GetBottom() - radius * 2, radius * 2, radius * 2,
                        0, 90);
            path.AddArc(area.X, area.GetBottom() - radius * 2, radius * 2, radius * 2, 90, 90);
            path.CloseFigure();
            SolidBrush panel_brush(panel);
            Pen border_pen(border, 1.0f);
            graphics.FillPath(&panel_brush, &path);
            graphics.DrawPath(&border_pen, &path);
            SolidBrush accent_brush(accent);
            graphics.FillRectangle(&accent_brush, area.X + 2, area.Y + 4, static_cast<REAL>(3),
                                   area.Height - 8);
            SolidBrush text_brush(foreground);
            RectF text_area(area.X + 9, area.Y + 3, area.Width - 14, area.Height - 6);
            graphics.DrawString(text.c_str(), -1, &font, text_area, &format, &text_brush);
        }
    }
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    POINT destination{payload->target.left, payload->target.top};
    SIZE size{width, height};
    POINT source{0, 0};
    SetWindowPos(overlay_, HWND_TOPMOST, destination.x, destination.y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    UpdateLayeredWindow(overlay_, screen, &destination, &size, memory, &source, 0, &blend, ULW_ALPHA);
    ShowWindow(overlay_, SW_SHOWNOACTIVATE);
    overlay_visible_ = true;
    if (button_) InvalidateRect(button_, nullptr, TRUE);
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    DeleteDC(screen);
}

} // namespace tv