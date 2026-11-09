#include "license.h"
#include "app.h"

#include "ui_layout.h"
#include <cmath>

#include <cstdlib>

namespace tv {
namespace {

constexpr wchar_t kWindowClass[] = L"TeleVipNativeWindow";
constexpr int kIdiAppIcon = 1;
constexpr int kIdrLogoPng = 102;
constexpr UINT WM_APP_INJECT_DONE = WM_APP + 10;
constexpr UINT WM_APP_LOCAL_DONE = WM_APP + 12;
constexpr UINT WM_APP_TRANSLATE_DONE = WM_APP + 13;
constexpr UINT WM_APP_REFRESH_PROCESSES = WM_APP + 14;
constexpr UINT_PTR kTickTimer = 1;
constexpr float kPi = 3.14159265358979323846f;

const PageDef kPages[] = {
    {L"ghost", L"tab_ghost", Icon::Ghost, L"page_ghost_title", L"page_ghost_sub", false},
    {L"network", L"tab_network", Icon::Network, L"page_network_title", L"page_network_sub", false},
    {L"stories", L"tab_stories", Icon::Stories, L"page_stories_title", L"page_stories_sub", false},
    {L"account", L"tab_account", Icon::Account, L"page_account_title", L"page_account_sub", false},
    {L"messages", L"tab_messages", Icon::Messages, L"page_messages_title", L"page_messages_sub", false},
    {L"tools", L"tab_tools", Icon::Tools, L"page_tools_title", L"page_tools_sub", true},
    {L"advanced", L"tab_advanced", Icon::Advanced, L"page_advanced_title", L"page_advanced_sub", true},
    {L"roadmap", L"tab_roadmap", Icon::Roadmap, L"page_roadmap_title", L"page_roadmap_sub", true},
    {L"support", L"tab_support", Icon::Heart, L"page_support_title", L"page_support_sub", true},
};

COLORREF to_colorref(const Color& c) {
    return RGB(static_cast<BYTE>(c.r * 255.0f), static_cast<BYTE>(c.g * 255.0f),
               static_cast<BYTE>(c.b * 255.0f));
}

Color fade(Color c, float alpha) {
    c.a *= alpha;
    return c;
}

std::wstring repeat_n(const std::wstring& text, int count) {
    std::wstring out;
    for (int i = 0; i < count; ++i) out += text;
    return out;
}

} // namespace

App::App(HINSTANCE instance) : instance_(instance) {
    pages_.assign(std::begin(kPages), std::end(kPages));
}

App::~App() {
    if (edit_font_) DeleteObject(edit_font_);
    if (support_font_) DeleteObject(support_font_);
    if (edit_brush_) DeleteBrush(edit_brush_);
    timeEndPeriod(1);
}

bool App::create() {
    wchar_t preview_env[8] = {};
    preview_ = GetEnvironmentVariableW(L"TV_UI_PREVIEW", preview_env, 7) > 0 && _wtoi(preview_env) != 0;
    settings_ = preview_ ? Settings{} : load_settings();
    if (preview_) settings_.native_translate = false;
    wchar_t page_env[16] = {};
    if (GetEnvironmentVariableW(L"TV_PAGE", page_env, 15) > 0) {
        page_ = std::clamp(_wtoi(page_env), 0, static_cast<int>(PageId::Count) - 1);
    }
    wchar_t dark_env[8] = {};
    if (GetEnvironmentVariableW(L"TV_DARK", dark_env, 7) > 0) {
        settings_.dark = _wtoi(dark_env) != 0;
    }
    wchar_t lang_env[16] = {};
    if (GetEnvironmentVariableW(L"TV_LANG", lang_env, 15) > 0) {
        settings_.lang = std::wstring(lang_env) == L"en" ? L"en" : L"zh";
    }
    i18n_.set_lang(settings_.lang == L"en" ? Lang::English : Lang::Chinese);
    palette_ = make_palette(settings_.dark, settings_.accent);
    page_anim_.snap(1.0f);
    flags_ = settings_.flags;
    log_collapsed_ = settings_.log_collapsed;
    translate_to_chinese_ = settings_.translate_zh;

    if (!preview_) {
        config_.open();
        config_.set_flags(flags_);
        vault_.set_enabled((flags_ & (FLAG_SAVE_STORIES | FLAG_SECRET_SAVE | FLAG_SECRET_KEEP)) != 0);
    }

    INITCOMMONCONTROLSEX common_controls{sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&common_controls);
    timeBeginPeriod(1);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &App::wnd_proc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(kIdiAppIcon));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = kWindowClass;
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    dpi_ = static_cast<float>(GetDpiForSystem());
    scale_ = dpi_ / 96.0f;
    int target_w = 1260;
    int target_h = 860;
    wchar_t width_env[16] = {};
    wchar_t height_env[16] = {};
    if (GetEnvironmentVariableW(L"TV_W", width_env, 15) > 0) target_w = std::max(1080, _wtoi(width_env));
    if (GetEnvironmentVariableW(L"TV_H", height_env, 15) > 0) target_h = std::max(760, _wtoi(height_env));
    if (target_h <= 800) log_collapsed_ = true;
    const int px_w = static_cast<int>(target_w * scale_);
    const int px_h = static_cast<int>(target_h * scale_);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = std::min(px_w, static_cast<int>((work.right - work.left) * 0.94f));
    const int height = std::min(px_h, static_cast<int>((work.bottom - work.top) * 0.94f));
    const int x = work.left + ((work.right - work.left) - width) / 2;
    const int y = work.top + ((work.bottom - work.top) - height) / 2;

    hwnd_ = CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, L"TeleVip Desktop",
                            WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU |
                                WS_CLIPCHILDREN,
                            x, y, width, height, nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;

    const int small_icon = GetSystemMetrics(SM_CXSMICON);
    const int big_icon = GetSystemMetrics(SM_CXICON);
    if (HICON icon_small = static_cast<HICON>(LoadImageW(
            instance_, MAKEINTRESOURCEW(kIdiAppIcon), IMAGE_ICON, small_icon, small_icon, LR_DEFAULTCOLOR))) {
        SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon_small));
    }
    if (HICON icon_big = static_cast<HICON>(LoadImageW(
            instance_, MAKEINTRESOURCEW(kIdiAppIcon), IMAGE_ICON, big_icon, big_icon, LR_DEFAULTCOLOR))) {
        SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon_big));
    }

    dpi_ = static_cast<float>(GetDpiForWindow(hwnd_));
    scale_ = dpi_ / 96.0f;
    renderer_.init(hwnd_);
    renderer_.load_image(static_cast<UINT>(kIdrLogoPng), MAKEINTRESOURCEW(10));
    renderer_.load_image(kWechatQrResource, RT_RCDATA);
    renderer_.load_image(kAlipayQrResource, RT_RCDATA);
    create_controls();
    create_support_controls();
    update_control_colors();
    if (!preview_) translator_.initialize(hwnd_);
    // OCR 悬浮翻译已彻底停用，只保留 Telegram 内置翻译。
    translator_.set_enabled(false);
    translator_.set_target_language(settings_.translate_zh);
    // translate_engine 1 means this process *is* the translate backend, so the
    // server has to be up and the template written. Engine 0 (Telegram's own
    // MTProto provider) must never leave an override behind: a template aimed at a
    // port that dies with this window is what broke translation in the first place.
    if (!preview_ && settings_.translate_engine == 1) {
        if (translate_server_.start(kNativeTranslatePort)) {
            debug_log(L"translate: GUI server on 127.0.0.1:" +
                      std::to_wstring(translate_server_.port()) + L", engine=local");
        } else {
            debug_log(L"translate: GUI server failed to listen, falling back to engine=builtin");
        }
    } else if (!preview_) {
        std::wstring clear_error;
        if (!clear_native_translate(settings_.telegram_exe, clear_error)) {
            debug_log(L"translate: template not cleared: " + clear_error);
        }
    }
    native_agent_.set_enabled(settings_.native_translate);
    if (!preview_) RegisterHotKey(hwnd_, 0x54, MOD_CONTROL | MOD_ALT, 'T');
    update_motion_preferences();
    set_theme();
    if (!preview_) refresh_processes();
    else set_status(StatusKind::NoProcess);
    if (settings_.native_translate && selected_pid_ != 0 && translate_server_.running()) {
        native_agent_.set_target(selected_pid_);
        const std::wstring exe = process_exe_path(selected_pid_);
        if (!exe.empty()) {
            native_configured_ = native_translate_configured(exe, translate_server_.port());
            if (!native_configured_) {
                std::wstring error;
                if (configure_native_translate(exe, translate_server_.port(), error)) {
                    native_configured_ = true;
                    show_toast(i18n_.t(L"native_configured"), palette_.success);
                }
            }
        }
    }
    convert_calendar();
    if (!preview_) {
        refresh_archive();
        read_logs(true);
    }
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    SetFocus(hwnd_);
    return true;
}

LRESULT CALLBACK App::wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        app = static_cast<App*>(create->lpCreateParams);
        if (app) {
            app->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
    }
    if (app) {
        bool handled = false;
        LRESULT result = 0;
        app->on_message(msg, wparam, lparam, result, handled);
        if (handled) return result;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void App::on_message(UINT msg, WPARAM wparam, LPARAM lparam, LRESULT& result, bool& handled) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        hdc_ = BeginPaint(hwnd_, &paint);
        layout_and_paint();
        EndPaint(hwnd_, &paint);
        hdc_ = nullptr;
        handled = true;
        result = 0;
        return;
    }
    case WM_ERASEBKGND:
        handled = true;
        result = 1;
        return;
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) {
            RECT client{};
            GetClientRect(hwnd_, &client);
            client_w_ = static_cast<float>(client.right - client.left) / scale_;
            client_h_ = static_cast<float>(client.bottom - client.top) / scale_;
            renderer_.resize(static_cast<UINT>(client.right - client.left),
                             static_cast<UINT>(client.bottom - client.top), dpi_);
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        handled = true;
        result = 0;
        return;
    case WM_SETTINGCHANGE:
        update_motion_preferences();
        invalidate();
        break;
    case WM_DPICHANGED: {
        dpi_ = static_cast<float>(HIWORD(wparam));
        scale_ = dpi_ / 96.0f;
        RECT* suggested = reinterpret_cast<RECT*>(lparam);
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        update_control_font();
        update_motion_preferences();
        update_control_colors();
        RECT client{};
        GetClientRect(hwnd_, &client);
        client_w_ = static_cast<float>(client.right) / scale_;
        client_h_ = static_cast<float>(client.bottom) / scale_;
        renderer_.resize(static_cast<UINT>(client.right), static_cast<UINT>(client.bottom), dpi_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        handled = true;
        result = 0;
        return;
    }
    case WM_NCCALCSIZE:
        if (wparam) {
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
            if (IsZoomed(hwnd_)) {
                MONITORINFO monitor{sizeof(monitor)};
                GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor);
                params->rgrc[0] = monitor.rcWork;
            }
            handled = true;
            result = 0;
            return;
        }
        break;
    case WM_NCHITTEST: {
        if (IsZoomed(hwnd_)) {
            handled = true;
            result = HTCLIENT;
            return;
        }
        POINT screen{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        RECT window{};
        GetWindowRect(hwnd_, &window);
        const int border = static_cast<int>(6.0f * scale_);
        const bool left = screen.x < window.left + border;
        const bool right = screen.x >= window.right - border;
        const bool top = screen.y < window.top + border;
        const bool bottom = screen.y >= window.bottom - border;
        if (top && left) { handled = true; result = HTTOPLEFT; return; }
        if (top && right) { handled = true; result = HTTOPRIGHT; return; }
        if (bottom && left) { handled = true; result = HTBOTTOMLEFT; return; }
        if (bottom && right) { handled = true; result = HTBOTTOMRIGHT; return; }
        if (left) { handled = true; result = HTLEFT; return; }
        if (right) { handled = true; result = HTRIGHT; return; }
        if (top) { handled = true; result = HTTOP; return; }
        if (bottom) { handled = true; result = HTBOTTOM; return; }
        POINT client = screen;
        ScreenToClient(hwnd_, &client);
        const float x = static_cast<float>(client.x) / scale_;
        const float y = static_cast<float>(client.y) / scale_;
        if (y < metrics_.titlebar) {
            for (const auto& hit : title_hits_) {
                if (hit.rect.contains(x, y)) {
                    handled = true;
                    result = HTCLIENT;
                    return;
                }
            }
            handled = true;
            result = HTCAPTION;
            return;
        }
        handled = true;
        result = HTCLIENT;
        return;
    }
    case WM_NCLBUTTONDBLCLK:
        if (wparam == HTCAPTION) {
            ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
            handled = true;
            result = 0;
            return;
        }
        break;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize.x = static_cast<LONG>(1080.0f * scale_);
        info->ptMinTrackSize.y = static_cast<LONG>(760.0f * scale_);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor);
        info->ptMaxPosition.x = monitor.rcWork.left;
        info->ptMaxPosition.y = monitor.rcWork.top;
        info->ptMaxSize.x = monitor.rcWork.right - monitor.rcWork.left;
        info->ptMaxSize.y = monitor.rcWork.bottom - monitor.rcWork.top;
        handled = true;
        result = 0;
        return;
    }
    case WM_MOUSEMOVE: {
        if (!cursor_tracking_) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&track);
            cursor_tracking_ = true;
        }
        update_hover(static_cast<float>(GET_X_LPARAM(lparam)) / scale_,
                     static_cast<float>(GET_Y_LPARAM(lparam)) / scale_);
        handled = true;
        result = 0;
        return;
    }
    case WM_MOUSELEAVE:
        cursor_tracking_ = false;
        hover_id_.clear();
        for (auto& [id, spring] : hover_anim_) spring.target = 0.0f;
        combo_hover_ = -1;
        InvalidateRect(hwnd_, nullptr, FALSE);
        handled = true;
        result = 0;
        return;
    case WM_LBUTTONDOWN: {
        const float x = static_cast<float>(GET_X_LPARAM(lparam)) / scale_;
        const float y = static_cast<float>(GET_Y_LPARAM(lparam)) / scale_;
        if (HitTarget* hit = hit_test(x, y)) {
            if (hit->id == L"edit:trans") {
                TextStyle body;
                body.size = 10.4f;
                body.wrap = true;
                body.line_spacing = 1.12f;
                trans_focused_ = true;
                SetFocus(hwnd_);
                trans_caret_ = renderer_.text_index_at(trans_text_, body, trans_shell_.inset(9.0f, 6.0f), x, y);
                InvalidateRect(hwnd_, nullptr, FALSE);
                handled = true;
                result = 0;
                return;
            }
            trans_focused_ = false;
            pressed_id_ = hit->id;
            SetCapture(hwnd_);
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (combo_open_) {
            combo_open_ = false;
            combo_anim_.target = 0.0f;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        handled = true;
        result = 0;
        return;
    }
    case WM_LBUTTONUP: {
        const float x = static_cast<float>(GET_X_LPARAM(lparam)) / scale_;
        const float y = static_cast<float>(GET_Y_LPARAM(lparam)) / scale_;
        if (GetCapture() == hwnd_) ReleaseCapture();
        HitTarget* hit = hit_test(x, y);
        const std::wstring pressed = pressed_id_;
        pressed_id_.clear();
        if (hit && hit->id == pressed && !pressed.empty()) {
            HitTarget copy = *hit;
            handle_click(copy);
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        handled = true;
        result = 0;
        return;
    }
    case WM_HOTKEY:
        if (wparam == 0x54) {
            native_agent_.trigger_now();
            handled = true;
            result = 0;
            return;
        }
        break;
    case WM_MOUSEWHEEL: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd_, &point);
        const float x = static_cast<float>(point.x) / scale_;
        const float y = static_cast<float>(point.y) / scale_;
        if (viewport_.contains(x, y) && scroll_max_ > 0.0f) {
            scroll_by(-static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA * 62.0f);
        }
        handled = true;
        result = 0;
        return;
    }
    case WM_KEYDOWN: {
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (ctrl && wparam == VK_TAB) {
            cycle_page(shift ? -1 : 1);
            handled = true;
            result = 0;
            return;
        }
        if (ctrl && wparam >= L'1' && wparam <= L'9') {
            switch_page(static_cast<int>(wparam - L'1'));
            handled = true;
            result = 0;
            return;
        }
        if (trans_focused_) {
            if (wparam == VK_ESCAPE) {
                trans_focused_ = false;
                InvalidateRect(hwnd_, nullptr, FALSE);
            } else {
                handle_editor_key(wparam, ctrl, shift);
            }
            handled = true;
            result = 0;
            return;
        }
        if (wparam == VK_F5 && !preview_) {
            refresh_processes();
            handled = true;
            result = 0;
            return;
        }
        if (wparam == VK_ESCAPE) {
            if (combo_open_) {
                combo_open_ = false;
                combo_anim_.target = 0.0f;
            } else {
                toast_.leaving = true;
                toast_slide_.target = 0.0f;
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            handled = true;
            result = 0;
            return;
        }
        break;
    }
    case WM_CHAR:
        if (trans_focused_) {
            handle_editor_char(static_cast<wchar_t>(wparam));
            handled = true;
            result = 0;
            return;
        }
        break;
    case WM_IME_CHAR:
        if (trans_focused_) {
            handle_editor_char(static_cast<wchar_t>(wparam));
            handled = true;
            result = 0;
            return;
        }
        break;
    case WM_COMMAND: {
        if (HIWORD(wparam) == BN_CLICKED && handle_support_command(LOWORD(wparam))) {
            handled = true;
            result = 0;
            return;
        }
        const HWND control = reinterpret_cast<HWND>(lparam);
        if (control == date_edit_) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        handled = true;
        result = 0;
        return;
    }
    case WM_DRAWITEM: {
        const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (item && paint_support_button(*item)) {
            handled = true;
            result = TRUE;
            return;
        }
        break;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        HWND control = reinterpret_cast<HWND>(lparam);
        if (control != date_edit_) {
            handled = true;
            result = 0;
            return;
        }
        const Color background = solid_over(palette_.input, palette_.card);
        SetTextColor(dc, to_colorref(palette_.text));
        SetBkColor(dc, to_colorref(background));
        SetBkMode(dc, OPAQUE);
        handled = true;
        result = reinterpret_cast<LRESULT>(edit_brush_);
        return;
    }
    case WM_POWERBROADCAST:
#ifdef TV_CLOUD_BUILD
        if (wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND)
            license::request_refresh();
#endif
        break;
    case WM_TIMER:
        if (wparam == kTickTimer) {
            tick();
            handled = true;
            result = 0;
            return;
        }
        if (wparam == 2) {
            KillTimer(hwnd_, 2);
            refresh_processes();
            handled = true;
            result = 0;
            return;
        }
        break;
    case WM_APP_INJECT_DONE: {
        auto* done = reinterpret_cast<InjectionResult*>(lparam);
        handle_inject_done(done);
        handled = true;
        result = 0;
        return;
    }
    case WM_APP_LOCAL_DONE: {
        auto* done = reinterpret_cast<LocalContext*>(lparam);
        handle_local_done(done);
        handled = true;
        result = 0;
        return;
    }
    case WM_APP_TRANSLATE_DONE: {
        auto* context = reinterpret_cast<TranslateContext*>(wparam);
        auto* translated = reinterpret_cast<TranslateResult*>(lparam);
        handle_translate_done(context, translated);
        handled = true;
        result = 0;
        return;
    }
    case WM_APP_REFRESH_PROCESSES:
        refresh_processes();
        handled = true;
        result = 0;
        return;
    case WM_CLOSE:
        save_settings_now();
        vault_.stop();
        native_agent_.set_enabled(false);
        translate_server_.stop();
        {
            // Process is leaving: the override must not outlive the server it points
            // at, or the next Telegram start talks to a dead port.
            std::wstring clear_error;
            if (!preview_) clear_native_translate(settings_.telegram_exe, clear_error);
        }
        translator_.shutdown();
        DestroyWindow(hwnd_);
        handled = true;
        result = 0;
        return;
    case WM_DESTROY:
        KillTimer(hwnd_, kTickTimer);
        UnregisterHotKey(hwnd_, 0x54);
        PostQuitMessage(0);
        handled = true;
        result = 0;
        return;
    default:
        break;
    }
}

void App::invalidate() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::finish_motion() {
    page_anim_.snap(page_anim_.target);
    nav_pill_.snap(nav_pill_.target);
    combo_anim_.snap(combo_anim_.target);
    toast_slide_.snap(toast_slide_.target);
    for (auto& [id, spring] : hover_anim_) spring.snap(spring.target);
    for (auto& [id, spring] : press_anim_) spring.snap(pressed_id_ == id ? 1.0f : 0.0f);
    for (auto& [bit, spring] : toggle_anim_) spring.snap((flags_ & bit) ? 1.0f : 0.0f);
    for (auto& button : support_buttons_) {
        button.hover.snap(button.hover.target);
        button.press.snap(button.press.target);
    }
    scroll_ = scroll_target_;
    if (theme_t_ < 1.0f) {
        theme_t_ = 1.0f;
        palette_ = make_palette(settings_.dark, settings_.accent);
        update_control_colors();
        for (auto& button : support_buttons_) if (button.visible) InvalidateRect(button.hwnd, nullptr, FALSE);
    }
}

void App::update_motion_preferences() {
    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    wchar_t override_value[8]{};
    GetEnvironmentVariableW(L"TV_REDUCED_MOTION", override_value, 8);
    reduce_motion_ = !animations || override_value[0] == L'1';
    MONITORINFOEXW monitor{};
    monitor.cbSize = sizeof(monitor);
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    UINT hz = 60;
    if (GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor) &&
        EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode)) {
        hz = std::clamp<UINT>(mode.dmDisplayFrequency, 60, 144);
    }
    SetTimer(hwnd_, kTickTimer, std::max<UINT>(7, 1000 / hz), nullptr);
    if (reduce_motion_) finish_motion();
}

void App::advance_theme(float dt) {
    if (theme_t_ >= 1.0f) return;
    theme_t_ = clampf(theme_t_ + dt / motion::kScene, 0.0f, 1.0f);
    // ease-out cubic：快起步、缓落定，看起来像“退伍”而不是生硬切探
    const float k = 1.0f - std::pow(1.0f - theme_t_, 3.0f);
    palette_ = blend_palette(theme_from_, make_palette(settings_.dark, settings_.accent), k);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::set_theme() {
    theme_from_ = palette_;
    theme_t_ = reduce_motion_ ? 1.0f : 0.0f;
    palette_ = reduce_motion_ ? make_palette(settings_.dark, settings_.accent) : theme_from_;
    const BOOL dark = settings_.dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark));
    const DWORD corner = IsZoomed(hwnd_) ? 1u : 2u;
    DwmSetWindowAttribute(hwnd_, 33, &corner, sizeof(corner));
    update_control_colors();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::set_language(Lang lang) {
    i18n_.set_lang(lang);
    settings_.lang = (lang == Lang::Chinese) ? L"zh" : L"en";
    trans_placeholder_ = i18n_.t(L"tools_translate_placeholder");
    save_settings_soon();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::apply_flags(uint32_t flags, bool persist) {
    flags_ = flags;
    config_.set_flags(flags_);
    settings_.flags = flags_;
    sync_vault();
    if (persist) save_settings_soon();
}

void App::sync_vault() {
    const bool enabled = (flags_ & (FLAG_SAVE_STORIES | FLAG_SECRET_SAVE | FLAG_SECRET_KEEP)) != 0;
    vault_.set_enabled(enabled);
}

void App::save_settings_soon() {
    settings_dirty_ = true;
}

void App::save_settings_now() {
    if (preview_) return;
    settings_.flags = license_features_.preferred(flags_);
    settings_.dark = palette_.dark;
    settings_.log_collapsed = log_collapsed_;
    settings_.overlay_enabled = false;
    settings_.translate_zh = translator_.target_is_chinese();
    settings_.lang = i18n_.is_zh() ? L"zh" : L"en";
    tv::save_settings(settings_);
    settings_dirty_ = false;
    last_save_ = now_seconds();
}

void App::set_status(StatusKind kind, DWORD pid, const std::wstring& name) {
    status_ = kind;
    status_pid_ = pid;
    status_name_ = name;
}

std::wstring App::status_text() const {
    if (license_features_.paused())
        return i18n_.is_zh() ? L"授权连接恢复中 · 已保留开关" : L"Reconnecting · preferences kept";
    std::unordered_map<std::wstring, std::wstring> values;
    values[L"pid"] = std::to_wstring(status_pid_);
    values[L"name"] = status_name_;
    switch (status_) {
    case StatusKind::Idle: return i18n_.t(L"status_idle");
    case StatusKind::Selected: return i18n_.t(L"status_selected", values);
    case StatusKind::Injecting: return i18n_.t(L"status_injecting", values);
    case StatusKind::Injected: return i18n_.t(L"status_injected", values);
    case StatusKind::ExistingHook: return i18n_.t(L"status_already_hooked");
    case StatusKind::SignatureMissing: return i18n_.t(L"status_signature_not_found");
    case StatusKind::HookEnableFailed: return i18n_.t(L"status_hook_enable_failed");
    case StatusKind::InitNoResponse: return i18n_.t(L"status_init_no_response");
    case StatusKind::ProcessExited: return i18n_.t(L"status_process_exited");
    case StatusKind::NoProcess: return i18n_.t(L"status_no_process");
    case StatusKind::Started: return i18n_.t(L"status_started", values);
    case StatusKind::Scanning: return i18n_.t(L"status_scanning");
    case StatusKind::Failed: return i18n_.t(L"inject_failed");
    }
    return i18n_.t(L"status_idle");
}

Color App::status_color() const {
    if (license_features_.paused()) return palette_.warning;
    switch (status_) {
    case StatusKind::Injected: return palette_.success;
    case StatusKind::Injecting:
    case StatusKind::Started:
    case StatusKind::Scanning: return palette_.accent;
    case StatusKind::ExistingHook: return palette_.warning;
    case StatusKind::SignatureMissing:
    case StatusKind::HookEnableFailed:
    case StatusKind::InitNoResponse:
    case StatusKind::ProcessExited:
    case StatusKind::Failed: return palette_.danger;
    default: return palette_.text_tertiary;
    }
}

std::wstring App::page_title(PageId page) const {
    return i18n_.t(pages_[static_cast<int>(page)].title_key);
}

std::wstring App::page_subtitle(PageId page) const {
    return i18n_.t(pages_[static_cast<int>(page)].sub_key);
}

std::wstring App::feature_title(const FeatureDef& feature) const {
    return i18n_.t(feature.title_key);
}

std::wstring App::feature_desc(const FeatureDef& feature) const {
    return i18n_.t(feature.desc_key);
}

std::wstring App::premium_status_text() const {
    switch (premium_effective_state()) {
    case kPremiumHooked: return i18n_.t(L"premium_status_hooked");
    case kPremiumWaiting: return i18n_.t(L"premium_status_waiting");
    default: return i18n_.t(L"premium_status_off");
    }
}

uint32_t App::premium_effective_state() const {
    // The mapping only reports what the hook last wrote; intent wins. With the toggle
    // down the feature is off no matter what a previous session left in the mapping.
    if ((flags_ & FLAG_PREMIUM) == 0) return kPremiumOff;
    // No selected client means nothing can carry the bit, whatever the stale mapping says.
    if (selected_pid_ == 0) return kPremiumWaiting;
    // Only the exact "detour live" code means hooked: 3 (signature not found)
    // and 4 (detour/enable failed) must read as waiting, never as a green light.
    return premium_state_ == kPremiumHooked ? kPremiumHooked : kPremiumWaiting;
}

float App::card_width() const {
    return (client_w_ - metrics_.sidebar - metrics_.pad * 2.0f - metrics_.gap) * 0.5f;
}

void App::refresh_processes() {
    if (injecting_) return;
    const DWORD previous = selected_pid_;
    processes_ = enumerate_telegram();
    if (processes_.empty()) {
        selected_index_ = -1;
        selected_pid_ = 0;
        set_status(StatusKind::NoProcess);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    int preferred = 0;
    for (int i = 0; i < static_cast<int>(processes_.size()); ++i) {
        if (processes_[i].portable) {
            preferred = i;
            break;
        }
    }
    if (previous != 0) {
        for (int i = 0; i < static_cast<int>(processes_.size()); ++i) {
            if (processes_[i].pid == previous) {
                preferred = i;
                break;
            }
        }
    }
    select_process(preferred);
}

void App::select_process(int index) {
    if (index < 0 || index >= static_cast<int>(processes_.size())) return;
    selected_index_ = index;
    selected_pid_ = processes_[index].pid;
    translator_.set_target_pid(selected_pid_);
    native_agent_.set_target(selected_pid_);
    native_agent_.set_enabled(settings_.native_translate);
    set_status(StatusKind::Selected, selected_pid_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::ensure_process_valid() {
    if (selected_pid_ != 0 && !process_alive(selected_pid_)) {
        selected_pid_ = 0;
        selected_index_ = -1;
        refresh_processes();
        set_status(StatusKind::ProcessExited);
        show_toast(i18n_.t(L"status_process_exited"), palette_.danger);
    }
}

void App::inject() {
    if (injecting_) return;
    if (selected_pid_ == 0) {
        // Nothing is selected: start Telegram ourselves and inject while its main
        // thread is still suspended. Injecting after the window shows up leaves a
        // window in which the server already saw us online.
        std::wstring exe = settings_.telegram_exe;
        if (exe.empty()) exe = L"D:\\Telegram Desktop\\Telegram.exe";
        injecting_ = true;
        set_status(StatusKind::Injecting, 0);
        config_.set_flags(flags_);
        const uint32_t flags = flags_;
        InvalidateRect(hwnd_, nullptr, FALSE);
        std::thread([this, exe, flags] {
            auto* result = new InjectionResult(launch_and_inject(exe, flags));
            PostMessageW(hwnd_, WM_APP_INJECT_DONE, 0, reinterpret_cast<LPARAM>(result));
        }).detach();
        return;
    }
    if (!process_alive(selected_pid_)) {
        refresh_processes();
        set_status(StatusKind::ProcessExited);
        show_toast(i18n_.t(L"status_process_exited"), palette_.danger);
        return;
    }
    const std::wstring dll = stable_dll_path();
    if (!file_exists(dll)) {
        show_toast(i18n_.t(L"msg_dll_missing"), palette_.danger);
        return;
    }
    if (probe_existing_hook(selected_pid_) || hook_module_loaded(selected_pid_)) {
        set_status(StatusKind::ExistingHook, selected_pid_);
        show_toast(i18n_.t(L"msg_existing_hook_refuse"), palette_.warning);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (!target_hash_ok(selected_pid_)) {
        set_status(StatusKind::Failed);
        show_toast(i18n_.t(L"msg_unsupported_build"), palette_.danger);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    injecting_ = true;
    set_status(StatusKind::Injecting, selected_pid_);
    config_.set_flags(flags_);
    const DWORD pid = selected_pid_;
    const uint32_t flags = flags_;
    InvalidateRect(hwnd_, nullptr, FALSE);
    std::thread([this, pid, flags] {
        auto* result = new InjectionResult(run_injection(pid, flags));
        PostMessageW(hwnd_, WM_APP_INJECT_DONE, 0, reinterpret_cast<LPARAM>(result));
    }).detach();
}

void App::start_local_patch(DWORD pid, uint32_t flags) {
    if (pid == 0) return;
    std::thread([this, pid, flags] {
        auto* context = new LocalContext();
        context->ok = apply_local_patches(pid, flags, context->messages);
        if (flags & FLAG_HIDE_PHONE) {
            std::wstring message;
            hide_phone(pid, message);
            context->messages.push_back(message);
        }
        PostMessageW(hwnd_, WM_APP_LOCAL_DONE, 0, reinterpret_cast<LPARAM>(context));
    }).detach();
}

void App::handle_inject_done(InjectionResult* raw) {
    std::unique_ptr<InjectionResult> result(raw);
    injecting_ = false;
    if (!result) return;
    hook_state_ = result->hook_state;
    hook_active_ = result->hook_state == 2 || result->hook_state == 4;
    inject_detail_ = result->message;
    for (const auto& line : result->local_messages) log_note(L"local: " + line);

    if (result->ok && hook_active_) {
        if (result->hook_state == 4) {
            set_status(StatusKind::ExistingHook, result->pid);
            show_toast(i18n_.t(L"status_already_hooked"), palette_.warning);
        } else {
            set_status(StatusKind::Injected, result->pid);
            std::unordered_map<std::wstring, std::wstring> values;
            values[L"pid"] = std::to_wstring(result->pid);
            show_toast(i18n_.t(L"msg_injected_toast", values), palette_.success);
        }
    } else if (result->ok && result->hook_state == 3) {
        set_status(StatusKind::SignatureMissing, result->pid);
        show_toast(i18n_.t(L"status_signature_not_found"), palette_.danger);
    } else if (result->ok && result->hook_state == 1) {
        set_status(StatusKind::HookEnableFailed, result->pid);
        show_toast(i18n_.t(L"status_hook_enable_failed"), palette_.danger);
    } else if (result->ok) {
        set_status(StatusKind::InitNoResponse, result->pid);
        show_toast(i18n_.t(L"status_init_no_response"), palette_.danger);
    } else {
        set_status(StatusKind::Failed);
        show_toast(i18n_.t(L"inject_failed") + L" · " + result->message, palette_.danger);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::handle_local_done(LocalContext* raw) {
    std::unique_ptr<LocalContext> context(raw);
    if (!context) return;
    for (const auto& line : context->messages) log_note(L"local: " + line);
    if (!context->messages.empty()) {
        show_toast(i18n_.t(L"msg_patched"), context->ok ? palette_.success : palette_.warning);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::open_folder(const std::wstring& path) {
    ensure_dir(path);
    ShellExecuteW(hwnd_, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void App::refresh_archive() {
    archive_loader_.request(archive_offset_);
    last_archive_refresh_ = now_seconds();
}

void App::read_logs(bool force) {
    const double now = now_seconds();
    if (!force && now - last_log_read_ < 0.45) return;
    last_log_read_ = now;
    auto lines = read_log_tail(400);
    if (lines != log_lines_) {
        log_lines_ = std::move(lines);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void App::show_toast(const std::wstring& text, const Color& color) {
    toast_.text = text;
    toast_.color = color;
    toast_.born = now_seconds();
    toast_.visible = true;
    toast_.leaving = false;
    toast_slide_.target = 1.0f;
    if (toast_slide_.value <= 0.001f) toast_slide_.snap(0.0f);
    toast_slide_.velocity = 4.0f;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::scroll_by(float amount) {
    scroll_target_ = clampf(scroll_target_ + amount, 0.0f, scroll_max_);
}

void App::switch_page(int page) {
    page = std::clamp(page, 0, static_cast<int>(PageId::Count) - 1);
    if (page == page_) return;
    previous_page_ = page_;
    page_ = page;
    scroll_ = 0.0f;
    scroll_target_ = 0.0f;
    page_anim_.snap(reduce_motion_ ? 1.0f : 0.0f);
    page_anim_.target = 1.0f;
    if (nav_ready_) nav_pill_.target = nav_rects_[page_].top;
    hide_controls();
    if (!preview_ && page == static_cast<int>(PageId::Messages)) refresh_archive();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::cycle_page(int delta) {
    const int count = static_cast<int>(PageId::Count);
    switch_page((page_ + delta + count) % count);
}

void App::toggle_log() {
    log_collapsed_ = !log_collapsed_;
    settings_.log_collapsed = log_collapsed_;
    save_settings_soon();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::handle_click(const HitTarget& hit) {
    const std::wstring& id = hit.id;
    if (id == L"win:close") {
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        return;
    }
    if (id == L"win:min") {
        ShowWindow(hwnd_, SW_MINIMIZE);
        return;
    }
    if (id == L"win:max") {
        ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
        return;
    }
    if (id == L"lang") {
        set_language(i18n_.is_zh() ? Lang::English : Lang::Chinese);
        return;
    }
    if (id == L"theme") {
        settings_.dark = !palette_.dark;
        set_theme();
        save_settings_soon();
        return;
    }
    if (id.rfind(L"nav:", 0) == 0) {
        switch_page(std::stoi(id.substr(4)));
        return;
    }
    if (id == L"community:open") {
        std::wstring error;
        if (!open_community(hwnd_, error)) show_toast(i18n_.t(L"community_open_failed") + L" " + error, palette_.danger);
        return;
    }
    if (preview_ && id != L"logs:toggle" && id.rfind(L"accent:", 0) != 0) return;
#ifdef TV_CLOUD_BUILD
    if (!preview_ && !license::authorized() && id != L"logs:toggle") {
        show_toast(license::status_text(), palette_.danger);
        return;
    }
#endif
    if (id == L"combo") {
        combo_open_ = !combo_open_;
        if (combo_open_) combo_anim_.snap(reduce_motion_ ? 1.0f : 0.0f);
        combo_anim_.target = combo_open_ ? 1.0f : 0.0f;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (id.rfind(L"combo:", 0) == 0) {
        select_process(std::stoi(id.substr(6)));
        combo_open_ = false;
        combo_anim_.target = 0.0f;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (id == L"refresh") {
        refresh_processes();
        return;
    }
    if (id == L"launch") {
        std::wstring path = settings_.telegram_exe;
        if (path.empty() || !file_exists(path)) {
            wchar_t file[MAX_PATH] = {};
            OPENFILENAMEW dialog{};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = hwnd_;
            dialog.lpstrFilter = L"Telegram.exe\0Telegram.exe\0Executables\0*.exe\0";
            dialog.lpstrFile = file;
            dialog.nMaxFile = MAX_PATH;
            dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&dialog)) path = file;
        }
        if (path.empty()) return;
        settings_.telegram_exe = path;
        save_settings_soon();
        const std::wstring workdir = join_path(std::filesystem::path(path).parent_path().wstring(), L"data");
        ensure_dir(workdir);
        const std::wstring command = L"\"" + path + L"\" -workdir \"" + workdir + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        std::wstring mutable_command = command;
        if (CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                           workdir.c_str(), &startup, &process)) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            set_status(StatusKind::Started, 0, std::filesystem::path(path).filename().wstring());
            SetTimer(hwnd_, 2, 2200, nullptr);
        } else {
            show_toast(i18n_.t(L"msg_launch_failed"), palette_.danger);
        }
        return;
    }
    if (id == L"inject") {
        inject();
        return;
    }
    if (id == L"preset:ghost") {
        apply_flags(flags_ | kGhostPresetFlags, true);
        for (const auto& feature : ghost_features()) toggle_anim_[feature.bit].target = 1.0f;
        toggle_anim_[FLAG_DISABLE_STORIES].target = 1.0f;
        toggle_anim_[FLAG_HIDE_STORY].target = 1.0f;
        show_toast(i18n_.t(L"ghost_preset"), palette_.accent);
        return;
    }
    if (id == L"preset:off") {
        apply_flags(0, true);
        for (auto& [bit, spring] : toggle_anim_) spring.target = 0.0f;
        show_toast(i18n_.t(L"all_off"), palette_.text_secondary);
        return;
    }
    if (id.rfind(L"sw:", 0) == 0) {
        const uint32_t bit = static_cast<uint32_t>(std::stoul(id.substr(3)));
        const bool on = (flags_ & bit) == 0;
        apply_flags(on ? (flags_ | bit) : (flags_ & ~bit), true);
        toggle_anim_[bit].target = on ? 1.0f : 0.0f;
        if ((bit & (kCodePatchFlags | FLAG_HIDE_PHONE | FLAG_TRANSLATE_BATCH)) && selected_pid_) {
            start_local_patch(selected_pid_, flags_);
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (id == L"vault") {
        open_folder(vault_dir());
        return;
    }
    if (id == L"archive:refresh") {
        archive_offset_ = 0;
        refresh_archive();
        return;
    }
    if (id == L"archive:newer") {
        if (archive_offset_ >= 5) archive_offset_ -= 5;
        refresh_archive();
        return;
    }
    if (id == L"archive:older") {
        archive_offset_ += 5;
        refresh_archive();
        return;
    }
    if (id == L"archive:open") {
        open_folder(archive_dir());
        return;
    }
    if (id == L"logs:open") {
        open_folder(archive_dir());
        return;
    }
    if (id == L"logs:clear") {
        std::ofstream file(std::filesystem::path(log_path()), std::ios::trunc);
        file.close();
        log_lines_.clear();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (id == L"logs:toggle") {
        toggle_log();
        return;
    }
    if (id.rfind(L"accent:", 0) == 0) {
        const int value = std::stoi(id.substr(7));
        settings_.accent = static_cast<Accent>(value);
        set_theme();
        save_settings_soon();
        return;
    }
    if (id == L"convert") {
        convert_calendar();
        return;
    }
    if (id == L"today") {
        set_edit_text(date_edit_, today_iso());
        convert_calendar();
        return;
    }
    if (id == L"tr:zh") {
        translate_to_chinese_ = true;
        settings_.translate_zh = true;
        translator_.set_target_language(true);
        native_agent_.set_target_language(true);
        save_settings_soon();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (id == L"tr:en") {
        translate_to_chinese_ = false;
        settings_.translate_zh = false;
        translator_.set_target_language(false);
        native_agent_.set_target_language(false);
        save_settings_soon();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (id == L"overlay:toggle") {
        settings_.overlay_enabled = !settings_.overlay_enabled;
        // OCR 悬浮翻译已彻底停用，只保留 Telegram 内置翻译。
    translator_.set_enabled(false);
        save_settings_soon();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (id == L"engine:builtin" || id == L"engine:local") {
        const int wanted = id == L"engine:local" ? 1 : 0;
        if (settings_.translate_engine == wanted) return;
        settings_.translate_engine = wanted;
        save_settings_soon();
        std::unordered_map<std::wstring, std::wstring> values;
        // Name the port that will actually be used next time, not always the default.
        values[L"port"] = std::to_wstring(
            translate_server_.running() ? translate_server_.port() : kNativeTranslatePort);
        // Takes effect on the next launch: the running process keeps advertising
        // whatever it already wrote, so an open chat cannot lose its backend.
        show_toast(i18n_.t(wanted == 1 ? L"engine_local_toast" : L"engine_builtin_toast", values),
                   wanted == 1 ? palette_.success : palette_.text_secondary);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (id == L"batch:toggle") {
        const bool on = (flags_ & FLAG_TRANSLATE_BATCH) == 0;
        // apply_local_patches rewrites the two same-length compares inside the live
        // client, so this needs no restart: the next batch already uses the new caps.
        apply_flags(on ? (flags_ | FLAG_TRANSLATE_BATCH)
                       : (flags_ & ~FLAG_TRANSLATE_BATCH), true);
        if (selected_pid_) start_local_patch(selected_pid_, flags_);
        show_toast(i18n_.t(on ? L"batch_on" : L"batch_off"),
                   on ? palette_.success : palette_.text_secondary);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (id == L"native:toggle") {
        settings_.native_translate = !settings_.native_translate;
        native_agent_.set_enabled(settings_.native_translate);
        save_settings_soon();
        show_toast(i18n_.t(settings_.native_translate ? L"native_on" : L"native_off"),
                   settings_.native_translate ? palette_.success : palette_.text_tertiary);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (id == L"native:setup") {
        if (!translate_server_.running()) {
            show_toast(i18n_.t(settings_.translate_engine == 1 ? L"native_service_off"
                                                              : L"engine_needs_local"),
                       palette_.danger);
            return;
        }
        std::wstring exe = selected_pid_ ? process_exe_path(selected_pid_)
                                         : settings_.telegram_exe;
        if (exe.empty()) {
            show_toast(i18n_.t(L"native_no_target"), palette_.warning);
            return;
        }
        std::wstring error;
        if (configure_native_translate(exe, translate_server_.port(), error)) {
            native_configured_ = true;
            show_toast(i18n_.t(L"native_configured"), palette_.success);
        } else {
            std::unordered_map<std::wstring, std::wstring> values;
            values[L"message"] = error;
            show_toast(i18n_.t(L"native_failed", values), palette_.danger);
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (id == L"native:restart") {
        if (!translate_server_.running()) {
            show_toast(i18n_.t(settings_.translate_engine == 1 ? L"native_service_off"
                                                              : L"engine_needs_local"),
                       palette_.danger);
            return;
        }
        std::wstring exe = selected_pid_ ? process_exe_path(selected_pid_)
                                         : settings_.telegram_exe;
        if (exe.empty()) {
            show_toast(i18n_.t(L"native_no_target"), palette_.warning);
            return;
        }
        std::wstring error;
        configure_native_translate(exe, translate_server_.port(), error);
        if (!selected_pid_ || !restart_telegram(selected_pid_, error)) {
            std::unordered_map<std::wstring, std::wstring> values;
            values[L"message"] = error;
            show_toast(i18n_.t(L"native_failed", values), palette_.danger);
            return;
        }
        native_agent_.set_enabled(false);
        show_toast(i18n_.t(L"native_restarted"), palette_.success);
        PostMessageW(hwnd_, WM_APP_REFRESH_PROCESSES, 0, 0);
        return;
    }
    if (id == L"native:trigger") {
        native_agent_.trigger_now();
        return;
    }
    if (id == L"provider:auto" || id == L"provider:google" || id == L"provider:fallback") {
        if (id == L"provider:google") set_translate_provider(TranslateProvider::Google);
        else if (id == L"provider:fallback") set_translate_provider(TranslateProvider::Fallback);
        else set_translate_provider(TranslateProvider::Auto);
        return;
    }
    if (id == L"overlay:translate") {
        native_agent_.trigger_now();
        return;
    }
    if (id == L"translate") {
        start_translation();
        return;
    }
    if (id == L"clip") {
        read_clipboard();
        return;
    }
    if (id == L"copy") {
        copy_translation();
        return;
    }
}

void App::tick() {
    const double now = now_seconds();
    if (auto result = archive_loader_.poll()) {
        if (!result->failed && result->offset == archive_offset_) {
            if (result->rows.empty() && archive_offset_ >= 5) {
                archive_offset_ -= 5;
                refresh_archive();
            } else {
                archive_ = std::move(result->rows);
                invalidate();
            }
        }
    }
    const float dt = last_tick_time_ > 0.0 ? static_cast<float>(now - last_tick_time_) : 0.016f;
    last_tick_time_ = now;
    const float clamped_dt = clampf(dt, 0.001f, 0.05f);
    bool animating = false;
    if (reduce_motion_ || IsIconic(hwnd_)) finish_motion();
    for (auto& [id, spring] : press_anim_) {
        spring.target = pressed_id_ == id ? 1.0f : 0.0f;
        if (spring.update(clamped_dt, motion::kPress)) animating = true;
    }
    for (auto& button : support_buttons_) {
        if (!button.visible) continue;
        bool changed = button.hover.update(clamped_dt, motion::kHover);
        changed |= button.press.update(clamped_dt, motion::kPress);
        if (changed) InvalidateRect(button.hwnd, nullptr, FALSE);
    }
    if (theme_t_ < 1.0f) { advance_theme(clamped_dt); animating = true; }
    if (page_anim_.update(clamped_dt)) animating = true;
    if (nav_pill_.update(clamped_dt, motion::kNavigation)) animating = true;
    for (auto& [id, spring] : hover_anim_) {
        if (spring.update(clamped_dt, motion::kHover)) animating = true;
    }
    for (auto& [bit, spring] : toggle_anim_) {
        if (spring.update(clamped_dt, motion::kToggle)) animating = true;
    }
    if (combo_anim_.update(clamped_dt)) animating = true;
    if (toast_slide_.update(clamped_dt, motion::kToast)) animating = true;
    if (std::fabs(scroll_ - scroll_target_) > 0.2f) {
        scroll_ += (scroll_target_ - scroll_) * std::min(1.0f, clamped_dt * 18.0f);
        if (std::fabs(scroll_ - scroll_target_) <= 0.2f) scroll_ = scroll_target_;
        animating = true;
    }

    if (toast_.visible) {
        if (!toast_.leaving && now - toast_.born > 3.1) {
            toast_.leaving = true;
            toast_slide_.target = 0.0f;
        }
        if (toast_.leaving && toast_slide_.value < 0.02f) {
            toast_.visible = false;
        }
    }

    if (preview_) {
        if (animating) invalidate();
        return;
    }

#ifdef TV_CLOUD_BUILD
    if (!license::authorized()) {
        if (license_features_.suspend(flags_)) {
            flags_ = 0;
            config_.set_flags(0);
            vault_.set_enabled(false);
            for (auto& [bit, spring] : toggle_anim_) spring.target = 0.0f;
            show_toast(license::status_text(), palette_.danger);
        }
        if (animating) invalidate();
        return;
    }
    if (const auto restored = license_features_.resume()) {
        apply_flags(*restored, false);
        for (auto& [bit, spring] : toggle_anim_)
            spring.target = (flags_ & bit) ? 1.0f : 0.0f;
        show_toast(i18n_.is_zh() ? L"授权连接已恢复，已恢复原开关" : L"Authorization restored · preferences restored",
                   palette_.success);
        animating = true;
    }
#endif

    const double target_hits = static_cast<double>(config_.hits());
    if (std::fabs(target_hits - hits_display_) > 0.05) {
        hits_display_ += (target_hits - hits_display_) * std::min(1.0, dt * 9.0);
    }
    const uint64_t shown_hits = static_cast<uint64_t>(std::llround(hits_display_));
    if (shown_hits != last_hits_shown_) {
        last_hits_shown_ = shown_hits;
        if (!animating) invalidate();
    }

    if (!injecting_) {
        const uint32_t state = config_.hook_state();
        if (state != hook_state_) {
            hook_state_ = state;
            hook_active_ = state == 2 || state == 4;
            animating = true;
        }
        // One read of the shared mapping per tick, same path as hook_state. The hook
        // writes premium_state at the setFlags detour; nothing scans Telegram memory.
        const uint32_t premium = config_.premium_state();
        if (premium != premium_state_) {
            premium_state_ = premium;
            animating = true;
        }
    }
    if (now - last_log_read_ > 0.45) read_logs();
    // Auto-attach for native translate. The documented launch order is GUI first
    // (it writes the translate-url-template Telegram reads while starting), Telegram
    // second. At create() the process list is therefore empty, selected_pid_ stays 0
    // and NativeTranslateAgent scans pid=0 for the rest of the session: 509 guard
    // decisions all read 'found=0 chat=""' and the feature looks dead. Re-probe until
    // a Telegram process appears instead of waiting for F5.
    if (settings_.native_translate && !injecting_ && selected_pid_ == 0) {
        static double last_attach_probe = 0.0;
        if (now - last_attach_probe > 2.0) {
            last_attach_probe = now;
            refresh_processes();
        }
    }
    if (selected_pid_ && !injecting_ && now - last_archive_refresh_ > 2.2) {
        ensure_process_valid();
    }
    // The sidecar archive is useful even when Telegram has exited or no target
    // process is selected. Refresh it independently so deletes written by the
    // injected hook show up without a manual refresh, and remain visible after
    // the GUI is restarted.
    if (page_is(PageId::Messages) && archive_offset_ == 0 && !injecting_ && now - last_archive_refresh_ > 2.2) {
        refresh_archive();
    }
    if (settings_dirty_ && now - last_save_ > 0.8) save_settings_now();
    if (now - last_follow_ > 0.8) {
        last_follow_ = now;
        translator_.follow_target();
    }
    if (animating) invalidate();
}

void App::create_controls() {
    date_edit_ = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | ES_LEFT | ES_AUTOHSCROLL | ES_NOHIDESEL,
        0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(1001), instance_, nullptr);
    if (!date_edit_) return;
    update_control_font();
    update_control_colors();
    trans_placeholder_ = i18n_.t(L"tools_translate_placeholder");
    SendMessageW(date_edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
    SetWindowSubclass(date_edit_, edit_subclass, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowTextW(date_edit_, today_iso().c_str());
    ShowWindow(date_edit_, SW_HIDE);
}

void App::update_control_font() {
    if (edit_font_) DeleteObject(edit_font_);
    if (support_font_) DeleteObject(support_font_);
    support_font_ = CreateFontW(-MulDiv(12, static_cast<int>(dpi_), 96), 0, 0, 0, FW_MEDIUM,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    const int height = -MulDiv(12, static_cast<int>(dpi_), 72);
    edit_font_ = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
    if (date_edit_) SendMessageW(date_edit_, WM_SETFONT, reinterpret_cast<WPARAM>(edit_font_), TRUE);
}

void App::update_control_colors() {
    if (edit_brush_) DeleteBrush(edit_brush_);
    edit_brush_ = CreateSolidBrush(to_colorref(solid_over(palette_.input, palette_.card)));
    if (date_edit_) {
        SetWindowTheme(date_edit_, L" ", L" ");
        InvalidateRect(date_edit_, nullptr, TRUE);
    }
}

void App::position_controls() {
    if (positioning_controls_) return;
    positioning_controls_ = true;
    struct Guard {
        bool& flag;
        ~Guard() { flag = false; }
    } guard{positioning_controls_};
    const bool should_show = page_ == static_cast<int>(PageId::Tools) && date_visible_ &&
                             !date_edit_rect_.empty() &&
                             viewport_.contains(date_shell_.cx(), date_shell_.cy());
    if (!should_show) {
        if (IsWindowVisible(date_edit_)) ShowWindow(date_edit_, SW_HIDE);
        last_date_visible_ = false;
        last_date_edit_rect_ = {};
        return;
    }
    if (last_date_visible_ && date_edit_rect_ == last_date_edit_rect_ && IsWindowVisible(date_edit_)) return;
    const int px = static_cast<int>(date_edit_rect_.left * scale_);
    const int py = static_cast<int>(date_edit_rect_.top * scale_);
    const int pw = static_cast<int>(date_edit_rect_.width() * scale_);
    const int ph = static_cast<int>(date_edit_rect_.height() * scale_);
    last_date_visible_ = true;
    last_date_edit_rect_ = date_edit_rect_;
    SetWindowPos(date_edit_, HWND_TOP, px, py, pw, ph, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void App::hide_controls() {
    last_date_visible_ = false;
    last_date_edit_rect_ = Rect{};
    if (date_edit_) ShowWindow(date_edit_, SW_HIDE);
    if (GetFocus() == date_edit_) SetFocus(hwnd_);
    trans_focused_ = false;
}

void App::set_edit_text(HWND edit, const std::wstring& text) {
    if (!edit) return;
    SetWindowTextW(edit, text.c_str());
}

std::wstring App::get_edit_text(HWND edit) const {
    if (!edit) return {};
    const int length = GetWindowTextLengthW(edit);
    if (length <= 0) return {};
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(edit, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

void App::focus_editor(HWND edit) {
    if (!edit) return;
    SetFocus(edit);
    SendMessageW(edit, EM_SETSEL, 0, -1);
}

LRESULT CALLBACK App::edit_subclass(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                    UINT_PTR id, DWORD_PTR ref) {
    auto* app = reinterpret_cast<App*>(ref);
    if (msg == WM_KEYDOWN) {
        if (wparam == VK_RETURN) {
            app->convert_calendar();
            return 0;
        }
        if (wparam == VK_ESCAPE) {
            SetFocus(app->hwnd_);
            return 0;
        }
    }
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
        InvalidateRect(app->hwnd_, nullptr, FALSE);
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

void App::convert_calendar() {
    const std::wstring text = trim(get_edit_text(date_edit_));
    calendar_ = tv::convert_calendar(text, i18n_.is_zh());
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::start_translation() {
    if (translating_) return;
    const std::wstring text = trim(trans_text_);
    if (text.empty()) {
        show_toast(i18n_.t(L"tools_translate_empty"), palette_.danger);
        return;
    }
    translating_ = true;
    translation_output_ = i18n_.t(L"tools_translate_working");
    InvalidateRect(hwnd_, nullptr, FALSE);
    auto* context = new TranslateContext{text, translate_to_chinese_, hwnd_};
    std::thread([this, context] {
        auto* result = new TranslateResult(translate_text(context->text, context->to_chinese));
        PostMessageW(hwnd_, WM_APP_TRANSLATE_DONE, reinterpret_cast<WPARAM>(context),
                     reinterpret_cast<LPARAM>(result));
    }).detach();
}

void App::handle_translate_done(TranslateContext* context, TranslateResult* result) {
    std::unique_ptr<TranslateContext> context_guard(context);
    std::unique_ptr<TranslateResult> result_guard(result);
    translating_ = false;
    if (!result) return;
    if (result->ok) {
        translation_output_ = result->text;
    } else {
        std::unordered_map<std::wstring, std::wstring> values;
        values[L"message"] = result->error;
        const std::wstring text = i18n_.t(L"tools_translate_failed", values);
        translation_output_ = text;
        show_toast(text, palette_.danger);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::read_clipboard() {
    if (!OpenClipboard(hwnd_)) return;
    const HANDLE data = GetClipboardData(CF_UNICODETEXT);
    std::wstring text;
    if (data) {
        const auto* pointer = static_cast<const wchar_t*>(GlobalLock(data));
        if (pointer) {
            text = pointer;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    if (text.empty()) {
        show_toast(i18n_.t(L"tools_translate_empty"), palette_.danger);
        return;
    }
    trans_text_ = replace_all(std::move(text), L"\r\n", L"\n");
    trans_text_ = replace_all(std::move(trans_text_), L"\r", L"\n");
    trans_caret_ = trans_text_.size();
    trans_focused_ = true;
    SetFocus(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::copy_translation() {
    const std::wstring text = translation_output_;
    if (text.empty()) {
        show_toast(i18n_.t(L"msg_nothing"), palette_.danger);
        return;
    }
    if (!OpenClipboard(hwnd_)) return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void* target = GlobalLock(memory);
        if (target) {
            memcpy(target, text.c_str(), bytes);
            GlobalUnlock(memory);
            SetClipboardData(CF_UNICODETEXT, memory);
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
    show_toast(i18n_.t(L"msg_copied"), palette_.success);
}

void App::handle_editor_char(wchar_t ch) {
    if (!trans_focused_) return;
    if (ch == L'\r') ch = L'\n';
    if (ch == L'\b') {
        if (trans_caret_ > 0) {
            trans_text_.erase(trans_caret_ - 1, 1);
            --trans_caret_;
        }
    } else if (ch == L'\n' || ch == L'\t' || ch >= 32) {
        trans_text_.insert(trans_caret_, 1, ch);
        ++trans_caret_;
    } else {
        return;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::handle_editor_key(WPARAM key, bool ctrl, bool shift) {
    if (!trans_focused_) return;
    (void)shift;
    if (ctrl && key == VK_RETURN) {
        start_translation();
        return;
    }
    if (ctrl && key == L'V') {
        read_clipboard();
        return;
    }
    if (ctrl && (key == L'C' || key == L'X')) {
        if (!trans_text_.empty()) {
            if (OpenClipboard(hwnd_)) {
                EmptyClipboard();
                const size_t bytes = (trans_text_.size() + 1) * sizeof(wchar_t);
                HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
                if (memory) {
                    void* target = GlobalLock(memory);
                    if (target) {
                        memcpy(target, trans_text_.c_str(), bytes);
                        GlobalUnlock(memory);
                        SetClipboardData(CF_UNICODETEXT, memory);
                    } else {
                        GlobalFree(memory);
                    }
                }
                CloseClipboard();
            }
            if (key == L'X') {
                trans_text_.clear();
                trans_caret_ = 0;
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }
    switch (key) {
    case VK_LEFT:
        if (trans_caret_ > 0) --trans_caret_;
        break;
    case VK_RIGHT:
        if (trans_caret_ < trans_text_.size()) ++trans_caret_;
        break;
    case VK_HOME:
        trans_caret_ = 0;
        break;
    case VK_END:
        trans_caret_ = trans_text_.size();
        break;
    case VK_DELETE:
        if (trans_caret_ < trans_text_.size()) trans_text_.erase(trans_caret_, 1);
        break;
    case VK_ESCAPE:
        trans_focused_ = false;
        break;
    default:
        return;
    }
    trans_caret_ = std::min(trans_caret_, trans_text_.size());
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::draw_translation_editor(const Rect& box, bool focused) {
    TextStyle body;
    body.size = 10.4f;
    body.wrap = true;
    body.line_spacing = 1.12f;
    const Rect text_rect = box.inset(9.0f, 6.0f);
    renderer_.push_clip(text_rect);
    if (trans_text_.empty()) {
        TextStyle placeholder = body;
        placeholder.size = 9.8f;
        renderer_.text(trans_placeholder_, text_rect, placeholder, palette_.text_tertiary);
    } else {
        renderer_.text(trans_text_, text_rect, body, palette_.text);
    }
    if (focused) {
        const Rect caret = renderer_.text_caret(trans_text_, body, text_rect, trans_caret_);
        if (!caret.empty()) renderer_.fill_rect(Rect{caret.left, caret.top, caret.left + 1.5f, caret.bottom},
                                               palette_.accent);
    }
    renderer_.pop_clip();
}

void App::add_hit(const std::wstring& id, const Rect& rect, LPCWSTR cursor,
                  const std::wstring& hover, bool titlebar) {
    if (rect.empty()) return;
    HitTarget hit;
    hit.id = id;
    hit.rect = rect;
    hit.cursor = cursor;
    hit.hover = hover;
    hit.titlebar = titlebar;
    if (titlebar) title_hits_.push_back(std::move(hit));
    else hits_.push_back(std::move(hit));
}

HitTarget* App::hit_test(float x, float y) {
    for (auto it = title_hits_.rbegin(); it != title_hits_.rend(); ++it) {
        if (it->rect.contains(x, y)) return &*it;
    }
    for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
        if (it->rect.contains(x, y)) return &*it;
    }
    return nullptr;
}

void App::update_hover(float x, float y) {
    HitTarget* hit = hit_test(x, y);
    std::wstring target;
    LPCWSTR cursor = IDC_ARROW;
    combo_hover_ = -1;
    if (hit) {
        target = hit->hover.empty() ? hit->id : hit->hover;
        cursor = hit->cursor ? hit->cursor : IDC_ARROW;
        if (hit->id.rfind(L"combo:", 0) == 0) {
            try { combo_hover_ = std::stoi(hit->id.substr(6)); } catch (...) {}
        }
    }
    if (target != hover_id_) {
        hover_id_ = target;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    for (auto& [id, spring] : hover_anim_) {
        spring.target = (id == hover_id_) ? 1.0f : 0.0f;
    }
    SetCursor(LoadCursorW(nullptr, cursor));
}

void App::layout_and_paint() {
    if (reduce_motion_) finish_motion();
    if (!renderer_.valid()) {
        renderer_.init(hwnd_);
        renderer_.load_image(static_cast<UINT>(kIdrLogoPng), MAKEINTRESOURCEW(10));
        renderer_.load_image(kWechatQrResource, RT_RCDATA);
        renderer_.load_image(kAlipayQrResource, RT_RCDATA);
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    const UINT px_w = static_cast<UINT>(std::max(0L, client.right - client.left));
    const UINT px_h = static_cast<UINT>(std::max(0L, client.bottom - client.top));
    dpi_ = static_cast<float>(GetDpiForWindow(hwnd_));
    scale_ = dpi_ / 96.0f;
    client_w_ = static_cast<float>(px_w) / scale_;
    client_h_ = static_cast<float>(px_h) / scale_;
    if (!renderer_.resize(px_w, px_h, dpi_)) return;
    renderer_.begin(palette_.window);
    hits_.clear();
    title_hits_.clear();
    for (auto& button : support_buttons_) button.rect = {};
    layout_titlebar();
    draw_titlebar();
    draw_sidebar();
    layout_page();
    layout_log();
    layout_combo();
    layout_toast();
    renderer_.end();
    position_controls();
    position_support_controls();
}

void App::layout_titlebar() {
    // Button rectangles are filled while drawing; nothing to precompute here.
}

void App::draw_titlebar() {
    const Rect bar{0.0f, 0.0f, client_w_, metrics_.titlebar};
    renderer_.fill_rect(bar, palette_.titlebar);
    renderer_.fill_rect(Rect{0.0f, metrics_.titlebar - 1.0f, client_w_, metrics_.titlebar},
                        palette_.border_soft);
    renderer_.fill_rect(Rect{0.0f, 0.0f, client_w_, 1.0f}, palette_.dark
                        ? Color(1.0f, 1.0f, 1.0f, 0.05f)
                        : Color(1.0f, 1.0f, 1.0f, 0.55f));

    struct Traffic {
        const wchar_t* id;
        Color color;
        const wchar_t* glyph;
    };
    const Traffic lights[] = {
        {L"win:close", palette_.traffic_red, L"×"},
        {L"win:min", palette_.traffic_yellow, L"−"},
        {L"win:max", palette_.traffic_green, L"+"},
    };
    float cx = 20.0f;
    for (const auto& light : lights) {
        const Rect circle{cx - 6.2f, metrics_.titlebar * 0.5f - 6.2f,
                          cx + 6.2f, metrics_.titlebar * 0.5f + 6.2f};
        const bool hover = hover_id_ == light.id;
        const float hover_t = reduce_motion_ ? (hover ? 1.0f : 0.0f) : hover_anim_[light.id].value;
        const float press_t = reduce_motion_ ? (pressed_id_ == light.id ? 1.0f : 0.0f) : press_anim_[light.id].value;
        const float radius = 6.2f * (1.0f - .12f * press_t);
        const Color fill = mix_color(light.color, lighten(light.color, .16f), hover_t);
        renderer_.fill_circle(circle.cx(), circle.cy(), radius, mix_color(fill, darken(fill, .10f), press_t));
        renderer_.stroke_circle(circle.cx(), circle.cy(), radius, palette_.traffic_border, 0.8f);
        if (hover_t > .01f) {
            TextStyle glyph;
            glyph.size = 9.0f;
            glyph.weight = FontWeight::Bold;
            glyph.align = TextAlign::Center;
            glyph.valign = TextVAlign::Middle;
            renderer_.text(light.glyph, circle, glyph, Color(0.0f, 0.0f, 0.0f, 0.62f * hover_t));
        }
        add_hit(light.id, circle.expand(4.0f), IDC_HAND, {}, true);
        cx += 20.0f;
    }

    TextStyle title_style;
    title_style.size = 12.5f;
    title_style.weight = FontWeight::SemiBold;
    title_style.align = TextAlign::Center;
    title_style.valign = TextVAlign::Middle;
    const std::wstring title = L"TeleVip Desktop";
    renderer_.text(title, Rect{0.0f, 0.0f, client_w_, metrics_.titlebar},
                   title_style, palette_.text_secondary);

    const float right = client_w_ - 20.0f;
    const Rect theme_button{right - 34.0f, 12.0f, right, 40.0f};
    const Rect lang_button{right - 34.0f - 8.0f - 82.0f, 12.0f, right - 42.0f, 40.0f};

    auto draw_chrome_button = [&](const std::wstring& id, const Rect& rect, const std::wstring& label,
                                  Icon icon, bool show_label) {
        const bool hover = hover_id_ == id;
        renderer_.fill_round(rect, 8.0f, hover ? palette_.card_hover : palette_.card);
        renderer_.stroke_round(rect, 8.0f, palette_.border_soft, 1.0f);
        const float icon_x = show_label ? rect.left + 12.0f : rect.cx() - 8.0f;
        renderer_.icon(icon, Rect{icon_x, rect.cy() - 8.0f, icon_x + 16.0f, rect.cy() + 8.0f},
                       hover ? palette_.accent : palette_.text_secondary, 1.45f);
        if (show_label) {
            TextStyle style;
            style.size = 11.0f;
            style.weight = FontWeight::Medium;
            style.valign = TextVAlign::Middle;
            renderer_.text(label, Rect{rect.left + 33.0f, rect.top, rect.right - 8.0f, rect.bottom},
                           style, palette_.text);
        }
        add_hit(id, rect, IDC_HAND, {}, true);
    };
    draw_chrome_button(L"lang", lang_button, i18n_.is_zh() ? L"中文" : L"EN", Icon::Globe, true);
    {
        const bool hover = hover_id_ == L"theme";
        renderer_.fill_round(theme_button, 8.0f, hover ? palette_.card_hover : palette_.card);
        renderer_.stroke_round(theme_button, 8.0f, palette_.border_soft, 1.0f);
        TextStyle glyph;
        glyph.size = 16.0f;
        glyph.align = TextAlign::Center;
        glyph.valign = TextVAlign::Middle;
        renderer_.text(palette_.dark ? L"☀" : L"☾", theme_button, glyph,
                       hover ? palette_.accent : palette_.text_secondary);
        add_hit(L"theme", theme_button, IDC_HAND, {}, true);
    }

    // 作者署名：贴着语言按钮左侧，弱对比，不抢标题与鼠标操作区域。
    const float credit_right = lang_button.left - 18.0f;
    const float credit_cy = metrics_.titlebar * 0.5f;
    const std::wstring handle = L"@MapAdkillerr";
    TextStyle credit_style;
    credit_style.size = 9.5f;
    credit_style.weight = FontWeight::Medium;
    credit_style.align = TextAlign::Right;
    credit_style.valign = TextVAlign::Middle;
    const float handle_w = renderer_.text_width(handle, credit_style.size, credit_style.weight);
    const Rect handle_box{credit_right - handle_w - 28.0f, credit_cy - 7.0f, credit_right, credit_cy + 7.0f};
    renderer_.text(handle, handle_box, credit_style, palette_.text_secondary.with_alpha(0.75f));
    renderer_.text(L"TG", Rect{handle_box.left, handle_box.top, handle_box.right - handle_w - 1.5f,
                               handle_box.bottom},
                   credit_style, palette_.text_tertiary.with_alpha(0.9f));
    add_hit(L"community:open", handle_box.expand(5.0f), IDC_HAND, {}, true);
    renderer_.fill_rect(Rect{credit_right + 7.0f, credit_cy - 6.0f, credit_right + 8.0f, credit_cy + 6.0f},
                        palette_.border_soft.with_alpha(0.75f));
}

void App::draw_nav() {
    const float x = 12.0f;
    const float width = metrics_.sidebar - 24.0f;
    // 小窗口（实测 1260x860 @150%）里导航 + 两张卡片原来会溢出底部裁切，
    // 这里把节奏收紧，把空间让给底部进程卡。
    const auto layout = sidebar_layout(client_h_);
    float y = layout.nav_top;
    auto section = [&](const wchar_t* key, float at) {
        TextStyle style;
        style.size = 9.5f;
        style.weight = FontWeight::Bold;
        renderer_.text(i18n_.t(key), Rect{22.0f, at, metrics_.sidebar - 22.0f, at + 15.0f},
                       style, palette_.text_tertiary);
    };
    section(L"nav_core", y);
    y += 18.0f;
    for (int i = 0; i < 5; ++i) {
        nav_rects_[i] = Rect{x, y, x + width, y + layout.nav_height};
        y += layout.nav_height;
    }
    y += 10.0f;
    section(L"nav_workspace", y);
    y += 18.0f;
    for (int i = 5; i < static_cast<int>(PageId::Count); ++i) {
        nav_rects_[i] = Rect{x, y, x + width, y + layout.nav_height};
        y += layout.nav_height;
    }
    if (!nav_ready_) {
        nav_pill_.snap(nav_rects_[page_].top);
        nav_ready_ = true;
    }
    if (nav_pill_.target != nav_rects_[page_].top) nav_pill_.target = nav_rects_[page_].top;

    const Rect pill{x, nav_pill_.value, x + width, nav_pill_.value + layout.nav_height};
    renderer_.fill_round(pill, 9.0f, palette_.accent);
    for (int i = 0; i < static_cast<int>(PageId::Count); ++i) {
        const Rect item = nav_rects_[i];
        const bool active = i == page_;
        const bool hover = hover_id_ == (L"nav:" + std::to_wstring(i));
        if (hover && !active) renderer_.fill_round(item, 8.0f, palette_.nav_hover);
        const float proximity = clampf(1.0f - std::fabs(item.cy() - pill.cy()) / 28.0f, 0.0f, 1.0f);
        const Color fg = active ? palette_.nav_active_text
                                : mix_color(palette_.text_secondary, palette_.nav_active_text, proximity);
        renderer_.icon(pages_[i].icon,
                       Rect{item.left + 14.0f, item.cy() - 8.0f, item.left + 30.0f, item.cy() + 8.0f},
                       fg, 1.55f);
        TextStyle style;
        style.size = 12.0f;
        style.weight = active ? FontWeight::SemiBold : FontWeight::Medium;
        style.valign = TextVAlign::Middle;
        renderer_.text(i18n_.t(pages_[i].tab_key),
                       Rect{item.left + 40.0f, item.top, item.right - 10.0f, item.bottom}, style, fg);
        add_hit(L"nav:" + std::to_wstring(i), item, IDC_HAND);
    }
}

void App::draw_connection_card() {
    // 八点网格版式：卡内毅 16 / 行间 8，所有元素左右对齐到同一条线。
    const float x = 16.0f;
    const float width = metrics_.sidebar - 32.0f;
    const float y = sidebar_layout(client_h_).connection_top;
    const Rect card{x, y, x + width, y + 158.0f};
    draw_card_shell(card, 0.0f, false);
    const float left = card.left + 16.0f;
    const float right = card.right - 16.0f;
    const float inner_w = right - left;

    TextStyle section;
    section.size = 11.0f;
    section.weight = FontWeight::SemiBold;
    section.valign = TextVAlign::Middle;
    renderer_.text(i18n_.t(L"connection"), Rect{left, card.top + 12.0f, right - 30.0f,
                                               card.top + 32.0f},
                   section, palette_.text);

    const Rect refresh_button{right - 24.0f, card.top + 12.0f, right, card.top + 32.0f};
    const bool refresh_hover = hover_id_ == L"refresh";
    if (refresh_hover) {
        renderer_.fill_round(refresh_button, 6.0f, palette_.nav_hover);
    }
    renderer_.icon(Icon::Refresh,
                   Rect{refresh_button.cx() - 10.0f, refresh_button.cy() - 10.0f, refresh_button.cx() + 10.0f, refresh_button.cy() + 10.0f},
                   refresh_hover ? palette_.accent : palette_.text_tertiary, 1.45f);
    add_hit(L"refresh", refresh_button, IDC_HAND);

    combo_rect_ = Rect{left, card.top + 40.0f, right, card.top + 72.0f};
    draw_input_shell(combo_rect_, false);
    std::wstring process_text = i18n_.t(L"status_no_process");
    if (selected_index_ >= 0 && selected_index_ < static_cast<int>(processes_.size())) {
        const auto& entry = processes_[selected_index_];
        process_text = L"PID " + std::to_wstring(entry.pid);
    }
    TextStyle combo_style;
    combo_style.size = 10.5f;
    combo_style.weight = FontWeight::Medium;
    combo_style.valign = TextVAlign::Middle;
    renderer_.text(process_text,
                   Rect{combo_rect_.left + 12.0f, combo_rect_.top, combo_rect_.right - 34.0f,
                        combo_rect_.bottom},
                   combo_style, palette_.text);
    renderer_.icon(Icon::ChevronDown,
                   Rect{combo_rect_.right - 22.0f, combo_rect_.cy() - 6.0f,
                        combo_rect_.right - 10.0f, combo_rect_.cy() + 6.0f},
                   palette_.text_tertiary, 1.6f);
    add_hit(L"combo", combo_rect_, IDC_HAND);

    const float row_y = combo_rect_.bottom + 6.0f;
    const float launch_w = 104.0f;
    draw_button(L"launch", Rect{left, row_y, left + launch_w, row_y + 34.0f},
                i18n_.t(L"launch"), ButtonKind::Secondary, Icon::Play);
    draw_button(L"inject", Rect{left + launch_w + 8.0f, row_y, right, row_y + 34.0f},
                injecting_ ? i18n_.t(L"inject_busy") : i18n_.t(L"inject"),
                ButtonKind::Primary, Icon::Info, !injecting_);

    // 分隔线 + 一行状态：不用胶囊，用小圆点 + 字，看起来像正经产品。
    const float foot_top = row_y + 40.0f;
    renderer_.fill_rect(Rect{left, foot_top, right, foot_top + 1.0f}, palette_.divider);
    const float foot_y = foot_top + 6.0f;

    Color dot = status_color();
    if (!license_features_.paused() && (hook_active_ || status_ == StatusKind::Injected)) dot = palette_.success;
    renderer_.fill_circle(left + 3.0f, foot_y + 7.0f, 3.0f, dot);
    TextStyle foot;
    foot.size = 9.0f;
    foot.valign = TextVAlign::Middle;
    foot.ellipsis = true;
    renderer_.text(status_text(), Rect{left + 12.0f, foot_y, right - 74.0f, foot_y + 14.0f},
                   foot, palette_.text_secondary);

    const bool hook_on = hook_active_ && !license_features_.paused();
    const bool hook_bad = hook_state_ == 3 || hook_state_ == 1;
    const std::wstring hook_text = license_features_.paused()
        ? (i18n_.is_zh() ? L"已暂停" : L"Paused") : hook_on ? i18n_.t(L"hook_active")
                                           : (hook_bad ? i18n_.t(L"hook_missing")
                                                       : i18n_.t(L"hook_none"));
    const Color hook_fg = license_features_.paused() ? palette_.warning : hook_on ? palette_.success
                                  : (hook_bad ? palette_.danger : palette_.text_tertiary);
    TextStyle hook_style;
    hook_style.size = 8.8f;
    hook_style.weight = FontWeight::Medium;
    hook_style.align = TextAlign::Right;
    hook_style.valign = TextVAlign::Middle;
    renderer_.text(hook_text, Rect{right - 74.0f, foot_y, right - 10.0f, foot_y + 14.0f},
                   hook_style, hook_fg);
    renderer_.fill_circle(right - 4.0f, foot_y + 7.0f, 2.6f, hook_fg);
}

void App::draw_status_card() {
    const float x = 16.0f;
    const float width = metrics_.sidebar - 32.0f;
    const float y = nav_rects_[7].bottom + 12.0f + 196.0f + 8.0f;
    const Rect card{x, y, x + width, y + 100.0f};
    draw_card_shell(card, 0.0f, false);
    const float left = card.left + 14.0f;
    TextStyle section;
    section.size = 10.5f;
    section.weight = FontWeight::Bold;
    renderer_.text(i18n_.t(L"status"), Rect{left, card.top + 9.0f, card.right - 14.0f, card.top + 23.0f},
                   section, palette_.text_secondary);

    Color dot = status_color();
    if (hook_active_ || status_ == StatusKind::Injected) dot = palette_.success;
    renderer_.fill_circle(left + 4.0f, card.top + 33.0f, 3.4f, dot);
    TextStyle status_style;
    status_style.size = 10.0f;
    status_style.valign = TextVAlign::Middle;
    status_style.ellipsis = true;
    renderer_.text(status_text(), Rect{left + 14.0f, card.top + 23.0f, card.right - 12.0f, card.top + 47.0f},
                   status_style, palette_.text_secondary);
    renderer_.fill_rect(Rect{left, card.top + 50.0f, card.right - 14.0f, card.top + 51.0f},
                        palette_.divider);

    TextStyle hits_style;
    hits_style.size = 25.0f;
    hits_style.weight = FontWeight::Bold;
    renderer_.text(std::to_wstring(static_cast<uint64_t>(std::llround(hits_display_))),
                   Rect{left, card.top + 54.0f, left + 120.0f, card.top + 82.0f},
                   hits_style, palette_.text);
    TextStyle caption;
    caption.size = 9.0f;
    renderer_.text(i18n_.t(L"hits"), Rect{left, card.top + 80.0f, left + 120.0f, card.top + 91.0f},
                   caption, palette_.text_tertiary);

    const bool active = hook_active_;
    const bool missing = hook_state_ == 3 || hook_state_ == 1;
    const std::wstring hook_text = active ? i18n_.t(L"hook_active")
                                         : (missing ? i18n_.t(L"hook_missing") : i18n_.t(L"hook_none"));
    const Color hook_fg = active ? palette_.success : (missing ? palette_.danger : palette_.text_tertiary);
    const Color hook_bg = active ? palette_.success_soft : (missing ? palette_.danger_soft : palette_.input);
    TextStyle hook_label;
    hook_label.size = 9.0f;
    hook_label.align = TextAlign::Right;
    renderer_.text(i18n_.t(L"hook"), Rect{card.right - 110.0f, card.top + 55.0f, card.right - 14.0f,
                                         card.top + 67.0f}, hook_label, palette_.text_tertiary);
    const Rect pill{card.right - 84.0f, card.top + 68.0f, card.right - 14.0f, card.top + 88.0f};
    renderer_.fill_round(pill, 10.0f, hook_bg);
    TextStyle pill_style;
    pill_style.size = 9.5f;
    pill_style.weight = FontWeight::Bold;
    pill_style.align = TextAlign::Center;
    pill_style.valign = TextVAlign::Middle;
    renderer_.text(hook_text, pill, pill_style, hook_fg);
}

void App::draw_sidebar() {
    const Rect sidebar{0.0f, metrics_.titlebar, metrics_.sidebar, client_h_};
    renderer_.fill_rect(sidebar, palette_.sidebar);
    renderer_.fill_rect(Rect{metrics_.sidebar - 1.0f, metrics_.titlebar, metrics_.sidebar, client_h_},
                        palette_.border_soft);

    const Rect logo{20.0f, 68.0f, 56.0f, 104.0f};
    renderer_.shadow(logo, 10.0f, 4.0f, palette_.accent.with_alpha(0.12f));
    if (renderer_.has_image()) {
        renderer_.image(logo);
    } else {
        renderer_.fill_round(logo, 10.0f, palette_.accent);
        TextStyle logo_style;
        logo_style.size = 12.0f;
        logo_style.weight = FontWeight::Bold;
        logo_style.align = TextAlign::Center;
        logo_style.valign = TextVAlign::Middle;
        renderer_.text(L"TV", logo, logo_style, palette_.on_accent);
    }

    TextStyle title_style;
    title_style.size = 17.0f;
    title_style.weight = FontWeight::SemiBold;
    renderer_.text(L"TeleVip", Rect{68.0f, 67.0f, metrics_.sidebar - 16.0f, 90.0f},
                   title_style, palette_.text);
    TextStyle tagline;
    tagline.size = 9.5f;
    tagline.valign = TextVAlign::Middle;
    renderer_.text(i18n_.t(L"sidebar_tagline"), Rect{68.0f, 89.0f, metrics_.sidebar - 16.0f, 105.0f},
                   tagline, palette_.text_tertiary);

    draw_nav();
    draw_connection_card();
    draw_sidebar_footer();

}

void App::draw_card_shell(const Rect& rect, float hover, bool accent_on) {
    const Rect shifted = rect.offset(0.0f, reduce_motion_ ? 0.0f : -hover * motion::kHoverLift);
    const Color drop = palette_.shadow.with_alpha(palette_.shadow.a * (0.38f + hover * 0.22f));
    renderer_.fill_round(Rect{shifted.left + 2.0f, shifted.top + 2.0f, shifted.right - 2.0f,
                              shifted.bottom + 3.0f}, metrics_.card_radius, drop);
    const Color fill = mix_color(palette_.card, palette_.card_hover, hover);
    renderer_.fill_round(shifted, metrics_.card_radius, fill);
    Color border = mix_color(palette_.border, palette_.accent, hover * 0.28f);
    if (accent_on) border = mix_color(border, palette_.accent.with_alpha(0.5f), 0.35f);
    renderer_.stroke_round(shifted, metrics_.card_radius, border, 1.0f);
}

void App::draw_badge(const Rect& rect, const std::wstring& text, const Color& fg, const Color& bg) {
    renderer_.fill_round(rect, rect.height() * 0.5f, bg);
    TextStyle style;
    style.size = 9.0f;
    style.weight = FontWeight::Bold;
    style.align = TextAlign::Center;
    style.valign = TextVAlign::Middle;
    renderer_.text(text, rect, style, fg);
}

void App::draw_input_shell(const Rect& rect, bool focused) {
    renderer_.fill_round(rect, 8.0f, focused ? palette_.input_focus : palette_.input);
    renderer_.stroke_round(rect, 8.0f, focused ? palette_.accent : palette_.border_soft, focused ? 1.4f : 1.0f);
}

void App::draw_button(const std::wstring& id, const Rect& rect, const std::wstring& label,
                      ButtonKind kind, Icon icon, bool enabled) {
    auto& spring = hover_anim_[id];
    spring.target = (hover_id_ == id || pressed_id_ == id) ? 1.0f : 0.0f;
    const float hover = enabled ? spring.value : 0.0f;
    Rect draw = rect;
    auto& press = press_anim_[id];
    press.target = enabled && pressed_id_ == id ? 1.0f : 0.0f;
    const float scale = reduce_motion_ ? 1.0f : 1.0f - (1.0f - motion::kPressScale) * clampf(press.value, 0.0f, 1.0f);
    renderer_.push_scale(scale, rect.cx(), rect.cy());
    Color fill;
    Color border = Color(0, 0, 0, 0);
    Color fg;
    switch (kind) {
    case ButtonKind::Primary:
        fill = mix_color(palette_.accent, palette_.accent_hover, hover);
        fg = palette_.on_accent;
        break;
    case ButtonKind::Secondary:
        fill = mix_color(palette_.card, palette_.card_hover, hover);
        border = palette_.border_soft;
        fg = palette_.text;
        break;
    case ButtonKind::Ghost:
        fill = hover > 0.0f ? palette_.nav_hover : Color(0, 0, 0, 0);
        fg = palette_.accent;
        break;
    case ButtonKind::Soft:
        fill = mix_color(palette_.accent_soft, palette_.accent.with_alpha(0.2f), hover);
        fg = palette_.accent;
        break;
    }
    if (!enabled) {
        fill = fill.with_alpha(fill.a * 0.45f);
        fg = fg.with_alpha(0.45f);
    }
    if (fill.a > 0.001f) renderer_.fill_round(draw, 8.0f, fill);
    if (border.a > 0.001f) renderer_.stroke_round(draw, 8.0f, border, 1.0f);
    const bool has_icon = icon != Icon::Info;
    TextStyle style;
    style.size = 11.0f;
    style.weight = kind == ButtonKind::Primary ? FontWeight::SemiBold : FontWeight::Medium;
    style.valign = TextVAlign::Middle;
    style.align = has_icon ? TextAlign::Left : TextAlign::Center;
    if (has_icon) {
        renderer_.icon(icon, Rect{draw.left + 12.0f, draw.cy() - 7.0f, draw.left + 26.0f, draw.cy() + 7.0f},
                       fg, 1.4f);
        renderer_.text(label, Rect{draw.left + 32.0f, draw.top, draw.right - 8.0f, draw.bottom}, style, fg);
    } else {
        renderer_.text(label, Rect{draw.left + 6.0f, draw.top, draw.right - 6.0f, draw.bottom}, style, fg);
    }
    renderer_.pop_transform();
    if (enabled) add_hit(id, rect, IDC_HAND);
}

std::vector<FeatureDef> App::ghost_features() {
    return {
        {L"feature_seen_private_title", L"feature_seen_private_desc", FLAG_HIDE_SEEN_PRIVATE},
        {L"feature_seen_channel_title", L"feature_seen_channel_desc", FLAG_HIDE_SEEN_CHANNEL},
        {L"feature_online_title", L"feature_online_desc", FLAG_HIDE_ONLINE},
        {L"feature_typing_title", L"feature_typing_desc", FLAG_HIDE_TYPING},
    };
}

std::vector<FeatureDef> App::network_features() {
    return {
        {L"feature_download_speed_title", L"feature_download_speed_desc", FLAG_DOWNLOAD_SPEED},
        {L"feature_hide_promo_title", L"feature_hide_promo_desc", FLAG_HIDE_PROMO},
        {L"feature_hide_update_title", L"feature_hide_update_desc", FLAG_HIDE_UPDATE},
    };
}

std::vector<FeatureDef> App::story_features() {
    return {
        {L"feature_disable_stories_title", L"feature_disable_stories_desc", FLAG_DISABLE_STORIES},
        {L"feature_story_title", L"feature_story_desc", FLAG_HIDE_STORY},
        {L"feature_save_stories_title", L"feature_save_stories_desc", FLAG_SAVE_STORIES},
        {L"feature_secret_save_title", L"feature_secret_save_desc", FLAG_SECRET_SAVE},
        {L"feature_secret_keep_title", L"feature_secret_keep_desc", FLAG_SECRET_KEEP},
    };
}

std::vector<FeatureDef> App::account_features() {
    return {
        {L"feature_premium_title", L"feature_premium_desc", FLAG_PREMIUM},
        {L"feature_user_id_title", L"feature_user_id_desc", FLAG_SHOW_USER_ID},
    };
}

std::vector<FeatureDef> App::message_features() {
    return {
        {L"feature_deleted_title", L"feature_deleted_desc", FLAG_DELETED_ARCHIVE},
        {L"feature_msg_id_title", L"feature_msg_id_desc", FLAG_SHOW_MSG_ID},
        {L"feature_edit_history_title", L"feature_edit_history_desc", FLAG_EDIT_HISTORY},
    };
}

std::vector<FeatureDef> App::advanced_features() {
    return {
        {L"feature_debug_title", L"feature_debug_desc", FLAG_DEBUG},
        {L"feature_hide_pinned_title", L"feature_hide_pinned_desc", FLAG_HIDE_PINNED},
        {L"feature_hide_phone_title", L"feature_hide_phone_desc", FLAG_HIDE_PHONE},
        {L"feature_content_saving_title", L"feature_content_saving_desc", FLAG_REMOVE_CONTENT_SAVING},
        {L"feature_number_rounding_title", L"feature_number_rounding_desc", FLAG_DISABLE_NUMBER_ROUNDING},
        {L"feature_tl_error_title", L"feature_tl_error_desc", FLAG_HIDE_TL_ERROR},
    };
}

void App::draw_feature_card(const Rect& rect, const FeatureDef& feature, const std::wstring& key,
                            float hover, float toggle) {
    const bool on = (flags_ & feature.bit) != 0;
    draw_card_shell(rect, hover, on);
    const Rect shifted = rect.offset(0.0f, reduce_motion_ ? 0.0f : -hover * motion::kHoverLift);
    const Color bar = on ? palette_.accent : mix_color(palette_.border_soft, palette_.accent, hover * 0.35f);
    renderer_.fill_round(Rect{shifted.left + 1.0f, shifted.top + 15.0f, shifted.left + 4.0f,
                              shifted.bottom - 15.0f}, 1.5f, bar);
    TextStyle title_style;
    title_style.size = 13.0f;
    title_style.weight = FontWeight::SemiBold;
    title_style.ellipsis = true;
    renderer_.text(feature_title(feature), Rect{shifted.left + 18.0f, shifted.top + 12.0f,
                                                shifted.right - 70.0f, shifted.top + 32.0f},
                   title_style, palette_.text);
    TextStyle desc_style;
    desc_style.size = 11.0f;
    desc_style.wrap = true;
    desc_style.line_spacing = 1.1f;
    renderer_.text(feature_desc(feature), Rect{shifted.left + 18.0f, shifted.top + 35.0f,
                                               shifted.right - 74.0f, shifted.bottom - 8.0f},
                   desc_style, palette_.text_secondary);
    add_hit(L"card:" + key, rect, IDC_ARROW);
    draw_switch(Rect{shifted.right - 16.0f - 46.0f, shifted.cy() - 13.0f,
                     shifted.right - 16.0f, shifted.cy() + 13.0f},
                feature.bit, hover, L"card:" + key);
}

void App::draw_switch(const Rect& rect, uint32_t bit, float hover, const std::wstring& parent_hover) {
    auto& spring = toggle_anim_[bit];
    const bool on = (flags_ & bit) != 0;
    if (spring.value == 0.0f && spring.target == 0.0f && on) spring.snap(1.0f);
    spring.target = on ? 1.0f : 0.0f;
    const float t = clampf(spring.value, 0.0f, 1.0f);
    const Color off = hover > 0.2f ? lighten(palette_.switch_off, 0.08f) : palette_.switch_off;
    renderer_.fill_round(rect, rect.height() * 0.5f, mix_color(off, palette_.accent, t));
    if (t > 0.02f) {
        renderer_.stroke_round(rect, rect.height() * 0.5f, palette_.accent.with_alpha(0.28f * t), 1.0f);
    }
    const float knob_x = rect.left + 13.0f + 20.0f * t;
    renderer_.fill_circle(knob_x, rect.cy() + 0.5f, 11.0f, Color(0, 0, 0, 0.18f));
    renderer_.fill_circle(knob_x, rect.cy(), 10.0f, palette_.switch_knob);
    renderer_.fill_circle(knob_x, rect.cy() - 1.0f, 7.5f, Color(1, 1, 1, palette_.dark ? 0.12f : 0.35f));
    add_hit(L"sw:" + std::to_wstring(bit), rect.expand(5.0f), IDC_HAND, parent_hover);
}

void App::draw_preset_row(float y) {
    const float x = viewport_.left;
    TextStyle label;
    label.size = 9.5f;
    label.weight = FontWeight::Bold;
    label.valign = TextVAlign::Middle;
    renderer_.text(i18n_.t(L"presets"), Rect{x, y, x + 80.0f, y + 30.0f}, label, palette_.text_tertiary);
    const float button_w = 108.0f;
    draw_button(L"preset:ghost", Rect{x + 76.0f, y, x + 76.0f + button_w, y + 30.0f},
                i18n_.t(L"ghost_preset"), ButtonKind::Soft, Icon::Sparkle);
    draw_button(L"preset:off", Rect{x + 76.0f + button_w + 8.0f, y,
                                    x + 76.0f + button_w * 2.0f + 8.0f, y + 30.0f},
                i18n_.t(L"all_off"), ButtonKind::Secondary, Icon::Info);
}

void App::draw_page_header() {
    const float x = metrics_.sidebar + 24.0f;
    const float y = metrics_.titlebar + 24.0f;
    TextStyle title;
    title.size = 26.0f;
    title.weight = FontWeight::Bold;
    renderer_.text(page_title(static_cast<PageId>(page_)),
                   Rect{x, y, client_w_ - 150.0f, y + 36.0f}, title, palette_.text);
    TextStyle subtitle;
    subtitle.size = 11.5f;
    subtitle.wrap = true;
    subtitle.line_spacing = 1.12f;
    renderer_.text(page_subtitle(static_cast<PageId>(page_)),
                   Rect{x, y + 41.0f, client_w_ - 24.0f, y + 72.0f}, subtitle, palette_.text_secondary);
    const Rect tag{client_w_ - 24.0f - 92.0f, y + 3.0f, client_w_ - 24.0f, y + 27.0f};
    draw_badge(tag, i18n_.t(page_is(PageId::Support) ? L"support_badge" :
               (pages_[page_].workspace ? L"nav_workspace" : L"mpt_badge")), palette_.accent, palette_.accent_soft);
}

void App::layout_page() {
    const float log_height = log_collapsed_ ? metrics_.log_collapsed : metrics_.log_height;
    const float log_top = page_is(PageId::Support) ? client_h_ - 10.0f : client_h_ - log_height - 16.0f;
    viewport_ = Rect{metrics_.sidebar + 24.0f, metrics_.titlebar + 104.0f,
                     client_w_ - 24.0f, log_top - 14.0f};
    page_opacity_ = reduce_motion_ ? 1.0f : clampf(page_anim_.value, 0.0f, 1.0f);
    page_shift_ = reduce_motion_ ? 0.0f : motion::kPageTravel * (1.0f - page_opacity_) *
        (page_ >= previous_page_ ? 1.0f : -1.0f);
    renderer_.push_visual(page_opacity_, page_shift_ * 0.5f);
    draw_page_header();
    renderer_.pop_visual();
    renderer_.push_clip(viewport_);
    renderer_.push_visual(page_opacity_, page_shift_);
    date_visible_ = trans_visible_ = output_visible_ = false;
    draw_page();
    renderer_.pop_visual();
    renderer_.pop_clip();
    scroll_max_ = std::max(0.0f, page_content_height_ - viewport_.height());
    scroll_target_ = clampf(scroll_target_, 0.0f, scroll_max_);
    scroll_ = clampf(scroll_, 0.0f, scroll_max_);
    if (scroll_max_ > 1.0f) {
        const float track_h = viewport_.height() - 8.0f;
        const float thumb_h = std::max(30.0f, track_h * (viewport_.height() / page_content_height_));
        const float progress = scroll_ / scroll_max_;
        const float thumb_y = viewport_.top + 4.0f + (track_h - thumb_h) * progress;
        renderer_.fill_round(Rect{client_w_ - 20.0f, thumb_y, client_w_ - 16.0f, thumb_y + thumb_h},
                             2.0f, palette_.text_tertiary.with_alpha(0.28f));
    }
}

void App::draw_page() {
    const float x = viewport_.left;
    const float width = viewport_.width();
    const float cw = (width - metrics_.gap) * 0.5f;
    const float start = viewport_.top - scroll_;
    float y = start;
    page_content_height_ = 0.0f;

    if (page_is(PageId::Ghost)) {
        draw_preset_row(y);
        y += 46.0f;
        const auto features = ghost_features();
        for (int i = 0; i < 4; ++i) {
            const int col = i % 2;
            const int row = i / 2;
            const Rect card{x + col * (cw + metrics_.gap), y + row * 104.0f,
                            x + col * (cw + metrics_.gap) + cw, y + row * 104.0f + 92.0f};
            const std::wstring key = L"ghost:" + std::to_wstring(i);
            const float hover = hover_anim_[L"card:" + key].value;
            draw_feature_card(card, features[i], key, hover, toggle_anim_[features[i].bit].value);
        }
        page_content_height_ = y + 2 * 104.0f - start + 10.0f;
        return;
    }
    if (page_is(PageId::Network)) {
        const auto features = network_features();
        for (int i = 0; i < 3; ++i) {
            const int col = i % 2;
            const int row = i / 2;
            const Rect card{x + col * (cw + metrics_.gap), y + row * 104.0f,
                            x + col * (cw + metrics_.gap) + cw, y + row * 104.0f + 92.0f};
            const std::wstring key = L"net:" + std::to_wstring(i);
            draw_feature_card(card, features[i], key, hover_anim_[L"card:" + key].value,
                              toggle_anim_[features[i].bit].value);
        }
        const Rect note{x + cw + metrics_.gap, y + 104.0f, x + width, y + 196.0f};
        draw_card_shell(note, hover_anim_[L"card:netnote"].value, false);
        renderer_.icon(Icon::Shield, Rect{note.left + 18.0f, note.top + 20.0f, note.left + 38.0f,
                                          note.top + 40.0f}, palette_.accent, 1.5f);
        TextStyle title;
        title.size = 12.0f;
        title.weight = FontWeight::SemiBold;
        renderer_.text(i18n_.t(L"network_note_title"),
                       Rect{note.left + 48.0f, note.top + 16.0f, note.right - 16.0f, note.top + 34.0f},
                       title, palette_.text);
        TextStyle desc;
        desc.size = 9.5f;
        desc.wrap = true;
        renderer_.text(i18n_.t(L"network_note_desc"),
                       Rect{note.left + 18.0f, note.top + 48.0f, note.right - 16.0f, note.bottom - 10.0f},
                       desc, palette_.text_secondary);
        add_hit(L"card:netnote", note);
        page_content_height_ = y + 2 * 104.0f - start + 10.0f;
        return;
    }
    if (page_is(PageId::Stories)) {
        const auto features = story_features();
        for (int i = 0; i < 5; ++i) {
            const int col = i % 2;
            const int row = i / 2;
            const Rect card{x + col * (cw + metrics_.gap), y + row * 104.0f,
                            x + col * (cw + metrics_.gap) + cw, y + row * 104.0f + 92.0f};
            const std::wstring key = L"story:" + std::to_wstring(i);
            draw_feature_card(card, features[i], key, hover_anim_[L"card:" + key].value,
                              toggle_anim_[features[i].bit].value);
        }
        const Rect vault{x, y + 3 * 104.0f, x + width, y + 3 * 104.0f + 66.0f};
        draw_card_shell(vault, hover_anim_[L"card:vault"].value, false);
        renderer_.icon(Icon::Folder, Rect{vault.left + 16.0f, vault.top + 22.0f, vault.left + 38.0f,
                                          vault.top + 44.0f}, palette_.accent, 1.5f);
        TextStyle title;
        title.size = 11.5f;
        title.weight = FontWeight::SemiBold;
        renderer_.text(i18n_.t(L"vault_title"), Rect{vault.left + 48.0f, vault.top + 14.0f,
                                                     vault.right - 110.0f, vault.top + 34.0f},
                       title, palette_.text);
        TextStyle desc;
        desc.size = 9.0f;
        desc.ellipsis = true;
        renderer_.text(i18n_.t(L"vault_desc"), Rect{vault.left + 48.0f, vault.top + 34.0f,
                                                    vault.right - 110.0f, vault.top + 52.0f},
                       desc, palette_.text_tertiary);
        add_hit(L"card:vault", vault);
        draw_button(L"vault", Rect{vault.right - 100.0f, vault.top + 18.0f, vault.right - 14.0f,
                                   vault.top + 48.0f}, i18n_.t(L"vault_open"), ButtonKind::Secondary);
        page_content_height_ = y + 3 * 104.0f + 66.0f - start + 10.0f;
        return;
    }
    if (page_is(PageId::Account)) {
        const auto features = account_features();
        for (int i = 0; i < static_cast<int>(features.size()); ++i) {
            const Rect card{x + i * (cw + metrics_.gap), y, x + i * (cw + metrics_.gap) + cw,
                            y + 92.0f};
            const std::wstring key = L"account:" + std::to_wstring(i);
            draw_feature_card(card, features[i], key, hover_anim_[L"card:" + key].value,
                              toggle_anim_[features[i].bit].value);
        }
        const Rect note{x, y + 104.0f, x + width, y + 104.0f + 150.0f};
        draw_card_shell(note, hover_anim_[L"card:premiumnote"].value, false);
        draw_badge(Rect{note.left + 18.0f, note.top + 14.0f, note.left + 102.0f, note.top + 34.0f},
                   i18n_.t(L"self_flag"), palette_.accent, palette_.accent_soft);
        TextStyle desc;
        desc.size = 9.8f;
        desc.wrap = true;
        desc.line_spacing = 1.08f;
        renderer_.text(i18n_.t(L"premium_note"),
                       Rect{note.left + 18.0f, note.top + 44.0f, note.right - 18.0f,
                            note.top + 88.0f},
                       desc, palette_.text_secondary);
        renderer_.text(i18n_.t(L"premium_note_server"),
                       Rect{note.left + 18.0f, note.top + 90.0f, note.right - 18.0f,
                            note.top + 112.0f},
                       desc, palette_.text_tertiary);
        renderer_.fill_rect(Rect{note.left + 18.0f, note.bottom - 34.0f, note.right - 18.0f,
                                 note.bottom - 33.0f}, palette_.divider);
        const uint32_t premium = premium_effective_state();
        TextStyle premium_status;
        premium_status.size = 10.0f;
        premium_status.weight = FontWeight::SemiBold;
        renderer_.text(premium_status_text(),
                       Rect{note.left + 18.0f, note.bottom - 28.0f, note.right - 18.0f,
                            note.bottom - 10.0f},
                       premium_status,
                       premium == kPremiumHooked
                           ? palette_.success
                           : (premium == kPremiumWaiting ? palette_.warning : palette_.text_tertiary));
        add_hit(L"card:premiumnote", note);
        page_content_height_ = y + 104.0f + 150.0f - start + 10.0f;
        return;
    }
    if (page_is(PageId::Messages)) {
        const auto features = message_features();
        for (int i = 0; i < 3; ++i) {
            const int col = i % 2;
            const int row = i / 2;
            const Rect card{x + col * (cw + metrics_.gap), y + row * 104.0f,
                            x + col * (cw + metrics_.gap) + cw, y + row * 104.0f + 92.0f};
            const std::wstring key = L"msg:" + std::to_wstring(i);
            draw_feature_card(card, features[i], key, hover_anim_[L"card:" + key].value,
                              toggle_anim_[features[i].bit].value);
        }
        const Rect sidecar{x + cw + metrics_.gap, y + 104.0f, x + width, y + 196.0f};
        draw_card_shell(sidecar, hover_anim_[L"card:sidecar"].value, false);
        draw_badge(Rect{sidecar.left + 16.0f, sidecar.top + 14.0f, sidecar.left + 94.0f,
                        sidecar.top + 34.0f}, i18n_.t(L"sidecar"), palette_.accent, palette_.accent_soft);
        TextStyle desc;
        desc.size = 9.5f;
        desc.wrap = true;
        renderer_.text(i18n_.t(L"archive_desc"), Rect{sidecar.left + 16.0f, sidecar.top + 44.0f,
                                                      sidecar.right - 16.0f, sidecar.bottom - 10.0f},
                       desc, palette_.text_secondary);
        add_hit(L"card:sidecar", sidecar);
        const float archive_y = y + 208.0f;
        draw_archive(archive_y, width);
        return;
    }
    if (page_is(PageId::Tools)) {
        draw_tools(y, width);
        return;
    }
    if (page_is(PageId::Advanced)) {
        const auto features = advanced_features();
        for (int i = 0; i < 1; ++i) {
            const Rect card{x + i * (cw + metrics_.gap), y, x + i * (cw + metrics_.gap) + cw, y + 92.0f};
            const std::wstring key = L"adv:" + std::to_wstring(i);
            draw_feature_card(card, features[i], key, hover_anim_[L"card:" + key].value,
                              toggle_anim_[features[i].bit].value);
        }
        const Rect info{x + cw + metrics_.gap, y, x + width, y + 92.0f};
        draw_card_shell(info, hover_anim_[L"card:info"].value, false);
        TextStyle title;
        title.size = 12.0f;
        title.weight = FontWeight::SemiBold;
        renderer_.text(i18n_.t(L"advanced_info_title"), Rect{info.left + 18.0f, info.top + 16.0f,
                                                             info.right - 16.0f, info.top + 34.0f},
                       title, palette_.text);
        TextStyle desc;
        desc.size = 9.2f;
        desc.mono = true;
        desc.ellipsis = true;
        renderer_.text(i18n_.t(L"advanced_info_desc"), Rect{info.left + 18.0f, info.top + 38.0f,
                                                            info.right - 16.0f, info.top + 54.0f},
                       desc, palette_.text_secondary);
        char detail[128] = {};
        snprintf(detail, sizeof(detail), "state=%u  rva=0x%X  flags=0x%X  premium=%u", hook_state_,
                 config_.pattern_rva(), flags_, premium_state_);
        renderer_.text(utf8_to_wide(detail), Rect{info.left + 18.0f, info.top + 60.0f,
                                                  info.right - 16.0f, info.top + 78.0f},
                       desc, palette_.text_tertiary);
        add_hit(L"card:info", info);
        for (int i = 1; i < 6; ++i) {
            const int col = (i - 1) % 2;
            const int row = 1 + (i - 1) / 2;
            const Rect card{x + col * (cw + metrics_.gap), y + row * 104.0f,
                            x + col * (cw + metrics_.gap) + cw, y + row * 104.0f + 92.0f};
            const std::wstring key = L"adv:" + std::to_wstring(i);
            draw_feature_card(card, features[i], key, hover_anim_[L"card:" + key].value,
                              toggle_anim_[features[i].bit].value);
        }
        const Rect note{x + cw + metrics_.gap, y + 3 * 104.0f, x + width, y + 3 * 104.0f + 92.0f};
        draw_card_shell(note, hover_anim_[L"card:advnote"].value, false);
        add_hit(L"card:advnote", note);
        renderer_.icon(Icon::Shield, Rect{note.left + 18.0f, note.top + 20.0f, note.left + 38.0f,
                                          note.top + 40.0f}, palette_.accent, 1.5f);
        TextStyle note_title;
        note_title.size = 11.5f;
        note_title.weight = FontWeight::SemiBold;
        renderer_.text(i18n_.t(L"advanced_note_title"), Rect{note.left + 48.0f, note.top + 15.0f,
                                                             note.right - 16.0f, note.top + 34.0f},
                       note_title, palette_.text);
        TextStyle note_desc;
        note_desc.size = 9.2f;
        note_desc.wrap = true;
        renderer_.text(i18n_.t(L"advanced_note_desc"), Rect{note.left + 16.0f, note.top + 44.0f,
                                                            note.right - 16.0f, note.bottom - 8.0f},
                       note_desc, palette_.text_secondary);
        const float picker_y = y + 4 * 104.0f;
        draw_accent_picker(picker_y, width);
        page_content_height_ = picker_y + 92.0f - start + 10.0f;
        return;
    }
    if (page_is(PageId::Roadmap)) {
        draw_roadmap(y, width);
        return;
    }
    if (page_is(PageId::Support)) {
        draw_support(y, width);
        return;
    }
}

void App::draw_archive(float y, float width) {
    const size_t count = std::min<size_t>(archive_.size(), 5);
    float row_height = 38.0f;
    row_height += static_cast<float>(count) * 54.0f;
    const float height = std::max(210.0f, 50.0f + row_height + 8.0f);
    const Rect card{viewport_.left, y, viewport_.left + width, y + height};
    draw_card_shell(card, hover_anim_[L"card:archive"].value, false);
    add_hit(L"card:archive", card);
    TextStyle title;
    title.size = 13.0f;
    title.weight = FontWeight::SemiBold;
    renderer_.text(i18n_.t(L"archive_title"),
                   Rect{card.left + 16.0f, card.top + 12.0f, card.left + 190.0f, card.top + 32.0f},
                   title, palette_.text);
    std::unordered_map<std::wstring, std::wstring> values;
    values[L"count"] = std::to_wstring(archive_.size());
    TextStyle count_style;
    count_style.size = 9.0f;
    count_style.align = TextAlign::Right;
    count_style.valign = TextVAlign::Middle;
    renderer_.text(i18n_.t(L"archive_count", values),
                   Rect{card.right - 448.0f, card.top + 12.0f, card.right - 364.0f, card.top + 32.0f},
                   count_style, palette_.text_tertiary);
    values[L"page"] = std::to_wstring(archive_offset_ / 5 + 1);
    renderer_.text(i18n_.t(L"archive_page", values),
                   Rect{card.right - 358.0f, card.top + 12.0f, card.right - 298.0f, card.top + 32.0f},
                   count_style, palette_.text_tertiary);
    draw_button(L"archive:newer", Rect{card.right - 292.0f, card.top + 10.0f, card.right - 244.0f,
                                      card.top + 36.0f}, i18n_.t(L"archive_newer"), ButtonKind::Secondary);
    draw_button(L"archive:older", Rect{card.right - 240.0f, card.top + 10.0f, card.right - 192.0f,
                                      card.top + 36.0f}, i18n_.t(L"archive_older"), ButtonKind::Secondary);
    draw_button(L"archive:refresh", Rect{card.right - 184.0f, card.top + 10.0f, card.right - 112.0f,
                                        card.top + 36.0f}, i18n_.t(L"archive_refresh"), ButtonKind::Secondary);
    draw_button(L"archive:open", Rect{card.right - 104.0f, card.top + 10.0f, card.right - 16.0f,
                                     card.top + 36.0f}, i18n_.t(L"archive_open"), ButtonKind::Ghost);
    const Rect list{card.left + 12.0f, card.top + 46.0f, card.right - 12.0f, card.bottom - 10.0f};
    renderer_.fill_round(list, 8.0f, palette_.log_bg);
    renderer_.stroke_round(list, 8.0f, palette_.border_soft, 1.0f);
    renderer_.push_clip(list);
    if (archive_.empty()) {
        TextStyle empty;
        empty.size = 9.8f;
        renderer_.text(i18n_.t(L"archive_empty"),
                       Rect{list.left + 12.0f, list.top + 12.0f, list.right - 12.0f, list.top + 36.0f},
                       empty, palette_.text_tertiary);
    } else {
        float row_y = list.top + 4.0f;
        for (size_t i = 0; i < count; ++i) {
            const auto& row = archive_[i];
            const float rh = 54.0f;
            TextStyle meta;
            meta.size = 8.8f;
            meta.mono = true;
            meta.valign = TextVAlign::Middle;
            const std::wstring stamp = wide_time(static_cast<uint64_t>(row.ts), L"%m-%d %H:%M:%S");
            renderer_.text(stamp, Rect{list.left + 10.0f, row_y, list.left + 92.0f, row_y + rh},
                           meta, palette_.text_tertiary);
            const Rect badge{list.left + 96.0f, row_y + (rh - 18.0f) * 0.5f, list.left + 176.0f,
                             row_y + (rh - 18.0f) * 0.5f + 18.0f};
            draw_badge(badge, i18n_.t(row.edit ? L"archive_kind_edit" : L"archive_kind_deleted"),
                       row.edit ? palette_.accent : palette_.danger,
                       row.edit ? palette_.accent_soft : palette_.danger_soft);
            std::wstring source;
            if (!row.edit) {
                if (row.source == L"self" || row.source == L"local") source = i18n_.t(L"archive_source_local");
                else if (row.source == L"remote") source = i18n_.t(L"archive_source_remote");
                else if (row.source == L"ttl") source = i18n_.t(L"archive_source_ttl");
            }
            renderer_.text(L"peer=" + std::to_wstring(row.peer) + L"  msg=" + std::to_wstring(row.msg) +
                           (source.empty() ? L"" : L"  [" + source + L"]"),
                           Rect{list.left + 184.0f, row_y, list.right - 10.0f, row_y + 20.0f},
                           meta, palette_.text_secondary);
            TextStyle body;
            body.size = 9.2f;
            body.ellipsis = true;
            body.valign = TextVAlign::Middle;
            if (row.edit) {
                renderer_.text(L"old: " + row.old_text, Rect{list.left + 184.0f, row_y + 20.0f,
                                                             list.right - 10.0f, row_y + 38.0f},
                               body, palette_.text_tertiary);
                renderer_.text(L"new: " + row.new_text, Rect{list.left + 184.0f, row_y + 36.0f,
                                                             list.right - 10.0f, row_y + 52.0f},
                               body, palette_.text);
            } else {
                const wchar_t* explanation = L"archive_delete_legacy";
                if (row.durable) {
                    if (row.source == L"self" || row.source == L"local") explanation = L"archive_delete_local";
                    else if (row.source == L"remote") explanation = L"archive_delete_remote";
                    else if (row.source == L"ttl") explanation = L"archive_delete_ttl";
                }
                std::wstring description = i18n_.t(explanation);
                if (row.ordinal > 0) {
                    std::unordered_map<std::wstring, std::wstring> sequence;
                    sequence[L"sequence"] = std::to_wstring(row.ordinal);
                    description = i18n_.t(L"archive_sequence", sequence) + L"  ·  " + description;
                }
                renderer_.text(description, Rect{list.left + 184.0f, row_y + 18.0f,
                                                  list.right - 10.0f, row_y + 36.0f}, body, palette_.text_secondary);
                renderer_.text(row.text.empty() ? i18n_.t(L"archive_no_text") : row.text,
                               Rect{list.left + 184.0f, row_y + 36.0f, list.right - 10.0f, row_y + 52.0f},
                               body, palette_.text);
            }
            row_y += rh;
            if (i + 1 < count) {
                renderer_.fill_rect(Rect{list.left + 8.0f, row_y - 1.0f, list.right - 8.0f, row_y},
                                    palette_.divider);
            }
        }
    }
    renderer_.pop_clip();
    page_content_height_ = (y + height) - viewport_.top + scroll_ + 12.0f;
}

void App::draw_tools(float y, float width) {
    const float gap = metrics_.gap;
    const float left_w = width;
    const float height = 352.0f;
    const Rect calendar{viewport_.left, y, viewport_.left + left_w, y + height};

    draw_card_shell(calendar, hover_anim_[L"card:calendar"].value, false);
    add_hit(L"card:calendar", calendar);
    TextStyle title;
    title.size = 13.0f;
    title.weight = FontWeight::SemiBold;
    renderer_.text(i18n_.t(L"tools_calendar_title"),
                   Rect{calendar.left + 16.0f, calendar.top + 13.0f, calendar.right - 16.0f,
                        calendar.top + 33.0f}, title, palette_.text);
    TextStyle desc;
    desc.size = 9.4f;
    desc.wrap = true;
    renderer_.text(i18n_.t(L"tools_calendar_desc"),
                   Rect{calendar.left + 16.0f, calendar.top + 35.0f, calendar.right - 16.0f,
                        calendar.top + 62.0f}, desc, palette_.text_secondary);
    TextStyle label;
    label.size = 9.0f;
    label.weight = FontWeight::Medium;
    renderer_.text(i18n_.t(L"tools_date"), Rect{calendar.left + 16.0f, calendar.top + 68.0f,
                                                calendar.right - 16.0f, calendar.top + 84.0f},
                   label, palette_.text_tertiary);
    date_shell_ = Rect{calendar.left + 16.0f, calendar.top + 86.0f, calendar.right - 16.0f,
                       calendar.top + 120.0f};
    draw_input_shell(date_shell_, GetFocus() == date_edit_);
    date_edit_rect_ = Rect{date_shell_.left + 8.0f, date_shell_.top + 6.0f, date_shell_.right - 8.0f,
                           date_shell_.bottom - 6.0f};
    date_visible_ = true;
    draw_button(L"convert", Rect{calendar.left + 16.0f, calendar.top + 130.0f,
                                 calendar.left + 136.0f, calendar.top + 160.0f},
                i18n_.t(L"tools_convert"), ButtonKind::Primary);
    draw_button(L"today", Rect{calendar.left + 142.0f, calendar.top + 130.0f,
                               calendar.left + 206.0f, calendar.top + 160.0f},
                i18n_.t(L"tools_today"), ButtonKind::Secondary);

    auto result_row = [&](int index, const wchar_t* key, const std::wstring& value, const Color& color) {
        const float row_y = calendar.top + 176.0f + index * 26.0f;
        TextStyle key_style;
        key_style.size = 9.2f;
        renderer_.text(i18n_.t(key), Rect{calendar.left + 16.0f, row_y, calendar.left + 100.0f,
                                          row_y + 22.0f}, key_style, palette_.text_tertiary);
        TextStyle value_style;
        value_style.size = 10.2f;
        value_style.weight = FontWeight::Medium;
        renderer_.text(value, Rect{calendar.left + 104.0f, row_y, calendar.right - 16.0f, row_y + 22.0f},
                       value_style, color);
    };
    if (calendar_.ok) {
        result_row(0, L"tools_gregorian", calendar_.gregorian, palette_.text);
        result_row(1, L"tools_hijri", calendar_.hijri, palette_.text);
        result_row(2, L"tools_persian", calendar_.persian, palette_.text);
    } else {
        result_row(0, L"tools_gregorian", L"—", palette_.text_tertiary);
        result_row(1, L"tools_hijri", L"—", palette_.text_tertiary);
        result_row(2, L"tools_persian", L"—", palette_.text_tertiary);
        TextStyle error;
        error.size = 9.2f;
        renderer_.text(i18n_.t(L"tools_invalid"),
                       Rect{calendar.left + 16.0f, calendar.top + 262.0f, calendar.right - 16.0f,
                            calendar.top + 282.0f}, error, palette_.danger);
    }
    TextStyle note;
    note.size = 8.8f;
    note.ellipsis = true;
    renderer_.text(i18n_.t(L"tools_hijri_note"),
                   Rect{calendar.left + 16.0f, calendar.bottom - 30.0f, calendar.right - 110.0f,
                        calendar.bottom - 12.0f}, note, palette_.text_tertiary);
    renderer_.icon(Icon::Calendar, Rect{calendar.right - 88.0f, calendar.bottom - 84.0f,
                                        calendar.right - 20.0f, calendar.bottom - 16.0f},
                   palette_.accent.with_alpha(0.075f), 1.8f);

    const Rect native{viewport_.left, y + height + 12.0f, viewport_.left + width,
                      y + height + 12.0f + 196.0f};
    draw_card_shell(native, hover_anim_[L"card:native"].value, false);
    add_hit(L"card:native", native);
    renderer_.text(i18n_.t(L"native_title"),
                   Rect{native.left + 16.0f, native.top + 12.0f, native.right - 16.0f,
                        native.top + 32.0f}, title, palette_.text);
    renderer_.text(i18n_.t(L"native_desc"),
                   Rect{native.left + 16.0f, native.top + 34.0f, native.right - 16.0f,
                        native.top + 56.0f}, desc, palette_.text_secondary);

    const bool native_on = settings_.native_translate;
    const bool server_on = translate_server_.running();
    std::unordered_map<std::wstring, std::wstring> native_values;
    native_values[L"port"] = std::to_wstring(translate_server_.port());
    native_values[L"requests"] = std::to_wstring(translate_server_.requests());
    native_values[L"cache"] = std::to_wstring(translate_server_.cache_size());
    std::wstring status_text = server_on ? i18n_.t(L"native_service", native_values)
                                         : i18n_.t(L"native_service_off");
    if (server_on && translate_server_.requests() > 0) {
        status_text += L" · " + i18n_.t(L"native_requests", native_values);
    }
    TextStyle status_style;
    status_style.size = 9.2f;
    status_style.weight = FontWeight::Medium;
    renderer_.fill_circle(native.left + 20.0f, native.top + 69.0f, 3.2f,
                          server_on ? palette_.success : palette_.danger);
    renderer_.text(status_text, Rect{native.left + 30.0f, native.top + 60.0f,
                                     native.right - 16.0f, native.top + 78.0f},
                   status_style, server_on ? palette_.success : palette_.text_tertiary);

    // 实测数据放在卡片上：平均响应、缓存命中、落盘词条数。
    // 这三项直接反映翻译到底快不快，不用再靠感觉。
    const TranslateStats tstats = translate_stats();
    native_values[L"avg"] = std::to_wstring(translate_server_.avg_ms());
    native_values[L"hits"] = std::to_wstring(tstats.memory_hits + tstats.disk_hits);
    native_values[L"saved"] = std::to_wstring(tstats.disk_entries);
    TextStyle speed_style;
    speed_style.size = 9.2f;
    speed_style.weight = FontWeight::Medium;
    renderer_.fill_circle(native.left + 20.0f, native.top + 88.0f, 3.2f,
                          tstats.network == 0 ? palette_.text_tertiary : palette_.accent);
    renderer_.text(i18n_.t(L"native_speed", native_values),
                   Rect{native.left + 30.0f, native.top + 80.0f, native.right - 16.0f,
                        native.top + 98.0f},
                   speed_style, palette_.text_secondary);

    // 引擎选择：Google 需要代理，备用源可直连；「自动」启动时各试一次选快的。
    TextStyle pick_label;
    pick_label.size = 9.2f;
    pick_label.valign = TextVAlign::Middle;
    renderer_.text(i18n_.t(L"provider_label"),
                   Rect{native.left + 16.0f, native.top + 106.0f, native.left + 62.0f,
                        native.top + 136.0f},
                   pick_label, palette_.text_tertiary);
    const TranslateProvider current = translate_provider();
    draw_button(L"provider:auto", Rect{native.left + 70.0f, native.top + 106.0f,
                                       native.left + 136.0f, native.top + 136.0f},
                i18n_.t(L"provider_auto"),
                current == TranslateProvider::Auto ? ButtonKind::Soft : ButtonKind::Secondary,
                Icon::Sparkle);
    draw_button(L"provider:google", Rect{native.left + 142.0f, native.top + 106.0f,
                                         native.left + 220.0f, native.top + 136.0f},
                i18n_.t(L"provider_google"),
                current == TranslateProvider::Google ? ButtonKind::Soft : ButtonKind::Secondary,
                Icon::Globe);
    draw_button(L"provider:fallback", Rect{native.left + 226.0f, native.top + 106.0f,
                                           native.left + 312.0f, native.top + 136.0f},
                i18n_.t(L"provider_fallback"),
                current == TranslateProvider::Fallback ? ButtonKind::Soft : ButtonKind::Secondary,
                Icon::Shield);

    // 翻译引擎：谁来出译文。内置 = Telegram 自己的 MTProto provider（不驻留进程），
    // 本地缓存 = 本程序的 TranslationServer（重开聊天直接命中缓存）。同一个控件、同一行，
    // 右对齐放在上游选择器之后，窄窗口下也不会和左边的 Google/备用源挤在一起。
    const float engine_right = native.right - 16.0f;
    const float engine_local_w = 92.0f;
    const float engine_builtin_w = 62.0f;
    const float engine_gap = 6.0f;
    const float engine_builtin_right = engine_right - engine_local_w - engine_gap;
    const float engine_left = engine_builtin_right - engine_builtin_w - engine_gap;
    const bool engine_local = settings_.translate_engine == 1;
    TextStyle engine_style;
    engine_style.size = 9.2f;
    engine_style.valign = TextVAlign::Middle;
    engine_style.align = TextAlign::Right;
    renderer_.text(i18n_.t(L"engine_label"),
                   Rect{engine_left - 104.0f, native.top + 106.0f, engine_left - 10.0f,
                        native.top + 136.0f},
                   engine_style, palette_.text_tertiary);
    draw_button(L"engine:builtin",
                Rect{engine_left, native.top + 106.0f, engine_builtin_right, native.top + 136.0f},
                i18n_.t(L"engine_builtin"),
                engine_local ? ButtonKind::Secondary : ButtonKind::Soft);
    draw_button(L"engine:local",
                Rect{engine_builtin_right + engine_gap, native.top + 106.0f, engine_right,
                     native.top + 136.0f},
                i18n_.t(L"engine_local"),
                engine_local ? ButtonKind::Soft : ButtonKind::Secondary);

    const float by = native.top + 148.0f;
    // 批量：一条 messages.translateText 能塞多少条。20 -> 100 是客户端侧唯一的提速
    // 旋钮，代价只是单条 RPC 变大；服务端若不收，点回去即可，立即生效。
    {
        const float batch_w = 128.0f;
        const float batch_right = native.right - 16.0f;
        const float batch_left = batch_right - batch_w;
        const bool batch_on = (flags_ & FLAG_TRANSLATE_BATCH) != 0;
        draw_button(L"batch:toggle", Rect{batch_left, by, batch_right, by + 30.0f},
                    i18n_.t(batch_on ? L"batch_on_label" : L"batch_off_label"),
                    batch_on ? ButtonKind::Soft : ButtonKind::Secondary);
    }
    draw_button(L"native:toggle", Rect{native.left + 16.0f, by, native.left + 132.0f, by + 30.0f},
                i18n_.t(native_on ? L"native_on" : L"native_off"),
                native_on ? ButtonKind::Soft : ButtonKind::Secondary, Icon::Globe);
    draw_button(L"native:setup", Rect{native.left + 142.0f, by, native.left + 274.0f, by + 30.0f},
                i18n_.t(L"native_setup"), ButtonKind::Secondary, Icon::Sparkle);
    draw_button(L"native:restart", Rect{native.left + 284.0f, by, native.left + 426.0f, by + 30.0f},
                i18n_.t(L"native_restart"), ButtonKind::Secondary, Icon::Refresh);
    draw_button(L"native:trigger", Rect{native.left + 436.0f, by, native.left + 586.0f, by + 30.0f},
                i18n_.t(L"native_trigger"), ButtonKind::Primary, Icon::Sparkle);

    const Rect quick{viewport_.left, native.bottom + 12.0f, viewport_.left + width,
                     native.bottom + 12.0f + 96.0f};
    draw_card_shell(quick, hover_anim_[L"card:quick"].value, false);
    add_hit(L"card:quick", quick);
    TextStyle quick_title;
    quick_title.size = 12.0f;
    quick_title.weight = FontWeight::SemiBold;
    renderer_.text(i18n_.t(L"quick_title"), Rect{quick.left + 16.0f, quick.top + 14.0f,
                                                  quick.right - 16.0f, quick.top + 32.0f},
                   quick_title, palette_.text);
    TextStyle quick_desc;
    quick_desc.size = 9.0f;
    quick_desc.ellipsis = true;
    renderer_.text(i18n_.t(L"quick_desc"), Rect{quick.left + 16.0f, quick.top + 34.0f,
                                                 quick.right - 16.0f, quick.top + 52.0f},
                   quick_desc, palette_.text_secondary);
    const float quick_by = quick.top + 58.0f;
    const float bw = 104.0f;
    draw_button(L"vault", Rect{quick.left + 16.0f, quick_by, quick.left + 16.0f + bw, quick_by + 28.0f},
                i18n_.t(L"vault_short"), ButtonKind::Secondary, Icon::Folder);
    draw_button(L"archive:open", Rect{quick.left + 24.0f + bw, quick_by, quick.left + 24.0f + bw * 2.0f, quick_by + 28.0f},
                i18n_.t(L"archive_short"), ButtonKind::Secondary, Icon::Folder);
    draw_button(L"logs:open", Rect{quick.left + 32.0f + bw * 2.0f, quick_by,
                                   quick.left + 32.0f + bw * 3.0f, quick_by + 28.0f},
                i18n_.t(L"logs_short"), ButtonKind::Secondary, Icon::Terminal);
    draw_button(L"archive:refresh", Rect{quick.left + 40.0f + bw * 3.0f, quick_by,
                                         quick.left + 40.0f + bw * 4.0f, quick_by + 28.0f},
                i18n_.t(L"archive_refresh"), ButtonKind::Secondary, Icon::Refresh);
    page_content_height_ = (quick.bottom) - viewport_.top + scroll_ + 12.0f;
}

void App::draw_roadmap(float y, float width) {
    TextStyle note;
    note.size = 10.0f;
    note.wrap = true;
    renderer_.text(i18n_.t(L"roadmap_note"), Rect{viewport_.left, y, viewport_.left + width, y + 38.0f},
                   note, palette_.text_secondary);
    y += 46.0f;
    const std::pair<const wchar_t*, const wchar_t*> rows[] = {
        {L"pending_secret_delete_title", L"pending_secret_delete_desc"},
        {L"pending_disable_channel_swipe_title", L"pending_disable_channel_swipe_desc"},
        {L"pending_disable_profile_swipe_title", L"pending_disable_profile_swipe_desc"},
        {L"pending_tl_error_title", L"pending_tl_error_desc"},
    };
    for (int i = 0; i < 4; ++i) {
        const Rect card{viewport_.left, y + i * 78.0f, viewport_.left + width, y + i * 78.0f + 68.0f};
        draw_card_shell(card, hover_anim_[L"card:roadmap:" + std::to_wstring(i)].value, false);
        TextStyle title;
        title.size = 11.5f;
        title.weight = FontWeight::SemiBold;
        renderer_.text(i18n_.t(rows[i].first), Rect{card.left + 16.0f, card.top + 12.0f,
                                                    card.right - 80.0f, card.top + 31.0f},
                       title, palette_.text);
        TextStyle desc;
        desc.size = 9.2f;
        desc.wrap = true;
        renderer_.text(i18n_.t(rows[i].second), Rect{card.left + 16.0f, card.top + 33.0f,
                                                     card.right - 80.0f, card.bottom - 8.0f},
                       desc, palette_.text_secondary);
        draw_badge(Rect{card.right - 62.0f, card.cy() - 11.0f, card.right - 14.0f, card.cy() + 11.0f},
                   i18n_.t(L"pending_badge"), palette_.text_tertiary, palette_.input);
        add_hit(L"card:roadmap:" + std::to_wstring(i), card);
    }
    page_content_height_ = y + 4 * 78.0f - viewport_.top + scroll_ + 10.0f;
}

void App::draw_accent_picker(float y, float width) {
    const Rect card{viewport_.left, y, viewport_.left + width, y + 88.0f};
    draw_card_shell(card, 0.0f, false);
    add_hit(L"card:accent", card);
    TextStyle title;
    title.size = 12.5f;
    title.weight = FontWeight::SemiBold;
    renderer_.text(i18n_.t(L"accent"), Rect{card.left + 18.0f, card.top + 14.0f, card.right - 300.0f,
                                            card.top + 32.0f}, title, palette_.text);
    TextStyle desc;
    desc.size = 9.2f;
    desc.ellipsis = true;
    renderer_.text(i18n_.t(L"appearance_desc"), Rect{card.left + 18.0f, card.top + 38.0f,
                                                     card.right - 300.0f, card.top + 58.0f},
                   desc, palette_.text_secondary);
    float x = card.right - 18.0f - 8.0f * 30.0f;
    for (int i = 0; i < 8; ++i) {
        const Color color = accent_base(kAccents[i].id);
        const float cx = x + i * 30.0f + 10.0f;
        const float cy = card.cy();
        const bool selected = static_cast<int>(settings_.accent) == i;
        if (selected) {
            renderer_.stroke_circle(cx, cy, 13.0f, palette_.text.with_alpha(0.75f), 1.5f);
        }
        renderer_.fill_circle(cx, cy, 9.0f, color);
        renderer_.stroke_circle(cx, cy, 9.0f, Color(0, 0, 0, 0.12f), 0.8f);
        add_hit(L"accent:" + std::to_wstring(i), Rect{cx - 13.0f, cy - 13.0f, cx + 13.0f, cy + 13.0f},
                IDC_HAND);
    }
}

void App::draw_log() {
    if (page_is(PageId::Support)) return;
    const float height = log_collapsed_ ? metrics_.log_collapsed : metrics_.log_height;
    const float y = client_h_ - height - 16.0f;
    const float x = metrics_.sidebar + 24.0f;
    const Rect card{x, y, client_w_ - 24.0f, y + height};
    draw_card_shell(card, 0.0f, false);
    TextStyle title;
    title.size = 12.0f;
    title.weight = FontWeight::SemiBold;
    title.valign = TextVAlign::Middle;
    renderer_.text(i18n_.t(L"log"), Rect{card.left + 16.0f, card.top + 8.0f, card.left + 130.0f,
                                         card.top + 36.0f}, title, palette_.text);
    draw_button(L"logs:open", Rect{card.right - 174.0f, card.top + 10.0f, card.right - 96.0f,
                                   card.top + 34.0f}, i18n_.t(L"action_open"), ButtonKind::Secondary,
                Icon::Folder);
    draw_button(L"logs:clear", Rect{card.right - 90.0f, card.top + 10.0f, card.right - 48.0f,
                                    card.top + 34.0f}, i18n_.t(L"clear"), ButtonKind::Secondary);
    draw_button(L"logs:toggle", Rect{card.right - 40.0f, card.top + 10.0f, card.right - 12.0f,
                                     card.top + 34.0f}, L"", ButtonKind::Ghost,
                log_collapsed_ ? Icon::ChevronDown : Icon::ChevronUp);
    if (!log_collapsed_) {
        const Rect box{card.left + 12.0f, card.top + 42.0f, card.right - 12.0f, card.bottom - 10.0f};
        renderer_.fill_round(box, 8.0f, palette_.log_bg);
        renderer_.stroke_round(box, 8.0f, palette_.border_soft, 1.0f);
        renderer_.push_clip(box);
        if (log_lines_.empty()) {
            TextStyle empty;
            empty.size = 9.5f;
            renderer_.text(i18n_.t(L"log_empty"), Rect{box.left + 10.0f, box.top + 8.0f,
                                                       box.right - 10.0f, box.top + 28.0f},
                           empty, palette_.text_tertiary);
        } else {
            TextStyle line_style;
            line_style.size = 8.9f;
            line_style.mono = true;
            line_style.valign = TextVAlign::Bottom;
            const float line_height = 14.2f;
            const int visible = static_cast<int>((box.height() - 12.0f) / line_height);
            const int start = std::max(0, static_cast<int>(log_lines_.size()) - visible);
            float line_y = box.top + 6.0f;
            for (int i = start; i < static_cast<int>(log_lines_.size()); ++i) {
                renderer_.text(log_lines_[i], Rect{box.left + 9.0f, line_y, box.right - 9.0f,
                                                   line_y + line_height},
                               line_style, palette_.log_text);
                line_y += line_height;
            }
        }
        renderer_.pop_clip();
    }
}

void App::draw_combo() {
    const float alpha = combo_anim_.value;
    if (alpha <= 0.01f && !combo_open_) return;
    const int count = static_cast<int>(processes_.size());
    const float item_h = 34.0f;
    const float height = std::max(44.0f, count * item_h + 10.0f);
    float y = combo_rect_.bottom + 6.0f;
    if (y + height > client_h_ - 14.0f) y = combo_rect_.top - height - 6.0f;
    const Rect popover{combo_rect_.left, y, combo_rect_.right, y + height};
    combo_popover_rect_ = popover;
    renderer_.shadow(popover, 10.0f, 8.0f, palette_.shadow.with_alpha(palette_.shadow.a * alpha));
    renderer_.fill_round(popover, 10.0f, fade(palette_.elevated, alpha));
    renderer_.stroke_round(popover, 10.0f, fade(palette_.border, alpha), 1.0f);
    if (count == 0) {
        TextStyle empty;
        empty.size = 9.8f;
        empty.valign = TextVAlign::Middle;
        renderer_.text(i18n_.t(L"status_no_process"),
                       Rect{popover.left + 12.0f, popover.top, popover.right - 12.0f, popover.bottom},
                       empty, fade(palette_.text_tertiary, alpha));
        return;
    }
    for (int i = 0; i < count; ++i) {
        const Rect item{popover.left + 5.0f, popover.top + 5.0f + i * item_h,
                        popover.right - 5.0f, popover.top + 5.0f + (i + 1) * item_h};
        const bool selected = i == selected_index_;
        const bool hover = i == combo_hover_;
        if (selected) renderer_.fill_round(item, 7.0f, fade(palette_.accent_soft, alpha));
        else if (hover) renderer_.fill_round(item, 7.0f, fade(palette_.nav_hover, alpha));
        TextStyle style;
        style.size = 10.2f;
        style.weight = FontWeight::Medium;
        style.valign = TextVAlign::Middle;
        const auto& entry = processes_[i];
        const std::wstring text = L"PID " + std::to_wstring(entry.pid) + L"  ·  " +
                                  i18n_.t(entry.portable ? L"badge_portable" : L"badge_live");
        renderer_.text(text, Rect{item.left + 10.0f, item.top, item.right - 28.0f, item.bottom},
                       style, fade(selected ? palette_.accent : palette_.text, alpha));
        if (selected) {
            renderer_.check(Rect{item.right - 22.0f, item.cy() - 7.0f, item.right - 8.0f, item.cy() + 7.0f},
                            fade(palette_.accent, alpha), 1.6f);
        }
        if (alpha > 0.55f) add_hit(L"combo:" + std::to_wstring(i), item, IDC_HAND);
    }
}

void App::draw_toast() {
    const float value = clampf(toast_slide_.value, 0.0f, 1.0f);
    if (!toast_.visible && value <= 0.01f) return;
    const float width = std::min(370.0f, client_w_ - 48.0f);
    const float height = 56.0f;
    const float x = client_w_ - 24.0f - width;
    const float base_y = client_h_ - (log_collapsed_ ? metrics_.log_collapsed : metrics_.log_height) - 30.0f - height;
    const float y = std::max(72.0f, base_y) + (1.0f - value) * 14.0f;
    const Rect card{x, y, x + width, y + height};
    renderer_.shadow(card, 12.0f, 10.0f, palette_.shadow.with_alpha(palette_.shadow.a * value));
    renderer_.fill_round(card, 12.0f, fade(palette_.card, value));
    renderer_.stroke_round(card, 12.0f, fade(palette_.border, value), 1.0f);
    const Color accent = toast_.color;
    const Color soft = (accent.g > accent.r) ? palette_.success_soft : palette_.danger_soft;
    renderer_.fill_circle(card.left + 28.0f, card.cy(), 13.0f, fade(soft, value));
    const Icon glyph = (accent.g > accent.r) ? Icon::Check : Icon::Close;
    renderer_.icon(glyph, Rect{card.left + 21.0f, card.cy() - 7.0f, card.left + 35.0f, card.cy() + 7.0f},
                   fade(accent, value), 1.8f);
    TextStyle style;
    style.size = 10.0f;
    style.wrap = true;
    style.valign = TextVAlign::Middle;
    renderer_.text(toast_.text, Rect{card.left + 48.0f, card.top + 7.0f, card.right - 14.0f,
                                     card.bottom - 10.0f}, style, fade(palette_.text, value));
    const double elapsed = now_seconds() - toast_.born;
    const float remain = clampf(1.0f - static_cast<float>(elapsed / 3.1), 0.0f, 1.0f);
    renderer_.fill_round(Rect{card.left + 14.0f, card.bottom - 5.0f,
                              card.left + 14.0f + (card.width() - 28.0f) * remain, card.bottom - 3.0f},
                         1.0f, fade(accent, value * 0.7f));
}

void App::layout_log() {
    draw_log();
}

void App::layout_combo() {
    draw_combo();
}

void App::layout_toast() {
    draw_toast();
}

} // namespace tv
