#include "app.h"
#include "support.h"
#include "ui_layout.h"
#include "native_button.h"

namespace tv {
namespace {

const DonationAsset kDonationAssets[] = {
    {kWechatQrResource, kWechatOriginalResource, L"TeleVip_WeChat_Support.jpg",
     L"support_wechat", L"support_wechat_hint", L"support_save_wechat"},
    {kAlipayQrResource, kAlipayOriginalResource, L"TeleVip_Alipay_Support.jpg",
     L"support_alipay", L"support_alipay_hint", L"support_save_alipay"},
};

std::wstring win_error(DWORD code) {
    return L"Windows error " + std::to_wstring(code);
}

} // namespace

const DonationAsset& donation_asset(int index) {
    return kDonationAssets[index == 1 ? 1 : 0];
}

bool save_donation_image(HWND owner, int index, bool& cancelled, std::wstring& error) {
    cancelled = false;
    error.clear();
    const auto& asset = donation_asset(index);
    const HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(asset.original_resource), RT_RCDATA);
    const HGLOBAL loaded = resource ? LoadResource(nullptr, resource) : nullptr;
    const DWORD size = resource ? SizeofResource(nullptr, resource) : 0;
    const void* data = loaded ? LockResource(loaded) : nullptr;
    if (!data || size == 0) {
        error = L"Image resource is missing.";
        return false;
    }

    wchar_t path[32768] = {};
    wcscpy_s(path, asset.filename);
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = owner;
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.lpstrFilter = L"JPEG (*.jpg)\0*.jpg\0\0";
    dialog.lpstrDefExt = L"jpg";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) {
        const DWORD code = CommDlgExtendedError();
        cancelled = code == 0;
        if (!cancelled) error = win_error(code);
        return false;
    }

    // Publish a complete original image atomically, including when overwriting.
    const std::wstring temporary = std::wstring(path) + L".televip-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = win_error(GetLastError());
        return false;
    }
    DWORD written = 0;
    const bool write_ok = WriteFile(file, data, size, &written, nullptr) && written == size;
    const DWORD write_error = write_ok ? ERROR_SUCCESS : GetLastError();
    const bool flush_ok = write_ok && FlushFileBuffers(file);
    const DWORD flush_error = flush_ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!write_ok || !flush_ok) {
        DeleteFileW(temporary.c_str());
        error = win_error(write_ok ? flush_error : (write_error ? write_error : ERROR_WRITE_FAULT));
        return false;
    }
    if (!MoveFileExW(temporary.c_str(), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        DeleteFileW(temporary.c_str());
        error = win_error(code);
        return false;
    }
    return true;
}

bool copy_community_link(HWND owner, std::wstring& error) {
    error.clear();
    if (!OpenClipboard(owner)) {
        error = win_error(GetLastError());
        return false;
    }
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(kCommunityUrl));
    void* target = memory ? GlobalLock(memory) : nullptr;
    bool ok = false;
    if (target) {
        memcpy(target, kCommunityUrl, sizeof(kCommunityUrl));
        GlobalUnlock(memory);
        ok = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
    }
    if (!ok) {
        const DWORD code = GetLastError();
        if (memory) GlobalFree(memory);
        error = win_error(code);
    }
    CloseClipboard();
    return ok;
}

bool open_community(HWND owner, std::wstring& error) {
    error.clear();
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(owner, L"open", kCommunityUrl, nullptr, nullptr, SW_SHOWNORMAL));
    if (result > 32) return true;
    error = win_error(static_cast<DWORD>(result));
    return false;
}

void App::create_support_controls() {
    const wchar_t* labels[] = {L"support_entry", L"community_entry", L"support_save_wechat",
                              L"support_save_alipay", L"community_join", L"community_copy"};
    for (int i = 0; i < 6; ++i) {
        auto& button = support_buttons_[i];
        button.id = kSupportFirstControl + i;
        button.key = labels[i];
        button.primary = i == 4;
        button.hwnd = CreateWindowExW(WS_EX_LAYERED, L"BUTTON", i18n_.t(labels[i]).c_str(),
            WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 1, 1, hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(button.id)), instance_, nullptr);
        if (button.hwnd) {
            SetWindowSubclass(button.hwnd, support_button_subclass, 1, reinterpret_cast<DWORD_PTR>(this));
        }
    }
}

LRESULT CALLBACK App::support_button_subclass(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                             UINT_PTR, DWORD_PTR ref) {
    auto* app = reinterpret_cast<App*>(ref);
    auto* button = static_cast<App::SupportButton*>(nullptr);
    for (auto& item : app->support_buttons_) if (item.hwnd == hwnd) button = &item;
    if (button) {
        if (msg == WM_MOUSEMOVE && button->hover.target != 1.0f) {
            button->hover.target = 1.0f;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (msg == WM_MOUSELEAVE) button->hover.target = 0.0f;
        if (msg == WM_LBUTTONDOWN || (msg == WM_KEYDOWN && wparam == VK_SPACE)) button->press.target = 1.0f;
        if (msg == WM_LBUTTONUP || msg == WM_CAPTURECHANGED || msg == WM_KILLFOCUS ||
            (msg == WM_KEYUP && wparam == VK_SPACE)) button->press.target = 0.0f;
    }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_MOUSEMOVE) {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&track);
    } else if (msg == WM_MOUSELEAVE || msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (msg == WM_KEYDOWN && (wparam == VK_ESCAPE || wparam == VK_F5 ||
               (GetKeyState(VK_CONTROL) & 0x8000))) {
        // Keep the window's existing shortcuts available while a native action has focus.
        SendMessageW(app->hwnd_, msg, wparam, lparam);
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

void App::position_support_controls() {
    for (int i = 0; i < 6; ++i) {
        auto& button = support_buttons_[i];
        if (!button.hwnd) continue;
        const Rect rect = i < 2 ? button.rect : button.rect.offset(page_shift_, 0.0f);
        const BYTE alpha = i < 2 ? 255 : static_cast<BYTE>(std::lround(page_opacity_ * 255));
        if (!button.visible || alpha != button.alpha) {
            SetLayeredWindowAttributes(button.hwnd, 0, alpha, LWA_ALPHA);
            button.alpha = alpha;
        }
        const bool visible = !rect.empty() && (i < 2 ? !combo_open_ :
            rect.top >= viewport_.top && rect.bottom <= viewport_.bottom);
        if (!visible) {
            if (GetFocus() == button.hwnd) SetFocus(hwnd_);
            if (button.visible) ShowWindow(button.hwnd, SW_HIDE);
            button.visible = false;
            continue;
        }
        const std::wstring label = i18n_.t(button.key);
        if (label != button.label) {
            SetWindowTextW(button.hwnd, label.c_str());
            button.label = label;
        }
        if (!button.visible || !(rect == button.last_rect)) {
            SetWindowPos(button.hwnd, HWND_TOP, static_cast<int>(std::lround(rect.left * scale_)),
                static_cast<int>(std::lround(rect.top * scale_)),
                static_cast<int>(std::lround(rect.width() * scale_)),
                static_cast<int>(std::lround(rect.height() * scale_)), SWP_NOACTIVATE | SWP_SHOWWINDOW);
            button.last_rect = rect;
        }
        button.visible = true;
        if (theme_t_ < 1.0f) InvalidateRect(button.hwnd, nullptr, FALSE);
    }
}

bool App::paint_support_button(const DRAWITEMSTRUCT& item) {
    if (item.CtlType != ODT_BUTTON || item.CtlID < kSupportFirstControl ||
        item.CtlID >= kSupportFirstControl + 6) return false;
    const auto& button = support_buttons_[item.CtlID - kSupportFirstControl];
    const float hover = reduce_motion_ ? button.hover.target : button.hover.value;
    const float pressed = reduce_motion_ ? button.press.target : button.press.value;
    const bool footer = item.CtlID < kSupportFirstControl + 2;
    return paint_native_button(item, palette_, support_font_, i18n_.t(button.key), scale_,
        hover, pressed, button.primary, footer ? palette_.sidebar : palette_.card, footer);
}

bool App::handle_support_command(int id) {
    if (id < kSupportFirstControl || id >= kSupportFirstControl + 6) return false;
    if (id == kSupportFirstControl) {
        switch_page(static_cast<int>(PageId::Support));
    } else if (id == kSupportFirstControl + 1 || id == kSupportFirstControl + 4) {
        std::wstring error;
        if (!open_community(hwnd_, error)) show_toast(i18n_.t(L"community_open_failed") + L" " + error, palette_.danger);
    } else if (id == kSupportFirstControl + 5) {
        std::wstring error;
        if (copy_community_link(hwnd_, error)) show_toast(i18n_.t(L"community_copied"), palette_.success);
        else show_toast(i18n_.t(L"community_copy_failed") + L" " + error, palette_.danger);
    } else {
        bool cancelled = false;
        std::wstring error;
        if (save_donation_image(hwnd_, id - kSupportFirstControl - 2, cancelled, error)) {
            show_toast(i18n_.t(L"support_saved"), palette_.success);
        } else if (!cancelled) {
            show_toast(i18n_.t(L"support_save_failed") + L" " + error, palette_.danger);
        }
    }
    return true;
}

void App::draw_sidebar_footer() {
    const float y = sidebar_layout(client_h_).footer_top;
    renderer_.fill_rect(Rect{16, y, metrics_.sidebar - 16, y + 1}, palette_.divider);
    TextStyle label;
    label.size = 10;
    label.weight = FontWeight::Medium;
    renderer_.text(i18n_.t(L"support_section"), Rect{20, y + 12, metrics_.sidebar - 20, y + 27},
                   label, palette_.text_tertiary);
    const float width = (metrics_.sidebar - 40) * 0.5f;
    support_buttons_[0].rect = Rect{16, y + 36, 16 + width, y + 72};
    support_buttons_[1].rect = Rect{24 + width, y + 36, metrics_.sidebar - 16, y + 72};
    TextStyle caption;
    caption.size = 9;
    caption.align = TextAlign::Center;
    renderer_.text(L"WhiteSur inspired · native C++", Rect{16, y + 84, metrics_.sidebar - 16, y + 98},
                   caption, palette_.text_tertiary);
}

void App::draw_support(float y, float width) {
    const float x = viewport_.left;
    const float start = y;
    const auto layout = support_layout(width);
    for (int i = 0; i < 2; ++i) {
        const int col = i % layout.columns;
        const int row = i / layout.columns;
        const Rect card{x + col * (layout.card_width + 16), y + row * (layout.card_height + 16),
                        x + col * (layout.card_width + 16) + layout.card_width,
                        y + row * (layout.card_height + 16) + layout.card_height};
        const auto& asset = donation_asset(i);
        draw_card_shell(card, 0, false);
        const Rect icon{card.left + 20, card.top + 19, card.left + 42, card.top + 41};
        renderer_.icon(Icon::Heart, icon, palette_.accent, 1.6f);
        TextStyle title;
        title.size = 15;
        title.weight = FontWeight::SemiBold;
        renderer_.text(i18n_.t(asset.title_key), Rect{card.left + 52, card.top + 18,
                         card.right - 20, card.top + 40}, title, palette_.text);
        TextStyle hint;
        hint.size = 11;
        hint.valign = TextVAlign::Middle;
        renderer_.text(i18n_.t(asset.hint_key), Rect{card.left + 20, card.top + 45,
                         card.right - 20, card.top + 62}, hint, palette_.text_secondary);
        const float qr_x = card.cx() - layout.qr_size * 0.5f;
        const Rect qr{qr_x, card.top + 74, qr_x + layout.qr_size, card.top + 74 + layout.qr_size};
        renderer_.fill_round(qr, 12, Color::hex(0xFFFFFF));
        renderer_.stroke_round(qr, 12, palette_.border_soft);
        if (renderer_.has_image(asset.qr_resource)) {
            renderer_.image(asset.qr_resource, qr.inset(12));
        } else {
            TextStyle missing;
            missing.size = 12;
            missing.align = TextAlign::Center;
            missing.valign = TextVAlign::Middle;
            renderer_.text(i18n_.t(L"support_image_missing"), qr.inset(16), missing, Color::hex(0x555555));
        }
        TextStyle caption;
        caption.size = 10;
        caption.align = TextAlign::Center;
        renderer_.text(i18n_.t(L"support_voluntary"), Rect{card.left + 20, qr.bottom + 10,
                         card.right - 20, qr.bottom + 26}, caption, palette_.text_tertiary);
        support_buttons_[i + 2].rect = Rect{card.left + 20, card.bottom - 48,
                                           card.right - 20, card.bottom - 14};
    }

    y += layout.donation_height + 16;
    const bool compact = width < 740;
    const float community_height = compact ? 150.0f : 112.0f;
    const Rect community{x, y, x + width, y + community_height};
    draw_card_shell(community, 0, false);
    const Rect icon_box{x + 20, y + 20, x + 60, y + 60};
    renderer_.fill_round(icon_box, 10, palette_.accent_soft);
    renderer_.icon(Icon::Telegram, icon_box.inset(9), palette_.accent, 1.6f);
    TextStyle title;
    title.size = 14;
    title.weight = FontWeight::SemiBold;
    const float text_right = compact ? community.right - 20 : community.right - 280;
    renderer_.text(i18n_.t(L"community_title"), Rect{x + 74, y + 18, text_right, y + 41}, title, palette_.text);
    TextStyle description;
    description.size = 11;
    description.ellipsis = true;
    renderer_.text(i18n_.t(L"community_description"), Rect{x + 74, y + 45,
                     text_right, y + 62}, description, palette_.text_secondary);
    description.size = 10.5f;
    renderer_.text(L"t.me/MapAdkillerr", Rect{x + 74, y + 73, text_right, y + 92}, description, palette_.accent);
    const float button_y = compact ? community.bottom - 48 : y + 38;
    const float button_x = compact ? x + 20 : community.right - 260;
    support_buttons_[4].rect = Rect{button_x, button_y, button_x + 128, button_y + 36};
    support_buttons_[5].rect = Rect{button_x + 140, button_y, button_x + 240, button_y + 36};

    TextStyle credit;
    credit.size = 10;
    credit.align = TextAlign::Center;
    credit.wrap = true;
    credit.line_spacing = 1.15f;
    renderer_.text(i18n_.t(L"support_credits"), Rect{x + 12, community.bottom + 18,
                     x + width - 12, community.bottom + 54}, credit, palette_.text_tertiary);
    page_content_height_ = community.bottom + 62 - start;
}

} // namespace tv
