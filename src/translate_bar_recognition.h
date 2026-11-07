#pragma once

#include "overlay.h"

#include <string>
#include <string_view>
#include <vector>
#include <cwctype>
#include <limits>

namespace tv {

enum class BarLabel {
    Missing,
    Pending,
    Original,
};

inline std::wstring normalize_bar_label(std::wstring_view text) {
    std::wstring result;
    result.reserve(text.size());
    for (const wchar_t ch : text) {
        if (std::iswspace(ch) || ch == 0x00A0 || ch == 0x200B || ch == 0x200C ||
            ch == 0x200D || ch == 0x200E || ch == 0x200F || ch == 0xFEFF) continue;
        result.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }
    return result;
}

inline std::wstring normalize_chat_heading(std::wstring_view text) {
    return normalize_bar_label(text);
}

inline BarLabel classify_translate_bar(std::wstring_view text) {
    const auto label = normalize_bar_label(text);
    const auto starts = [&label](std::initializer_list<std::wstring_view> prefixes) {
        for (const auto prefix : prefixes) {
            if (label.rfind(prefix, 0) == 0) return true;
        }
        return false;
    };
    if (starts({L"showoriginal", L"vieworiginal", L"显示原文", L"顯示原文",
                L"查看原文", L"檢視原文", L"检视原文", L"返回原文"})) return BarLabel::Original;
    if (starts({L"translateto", L"translatemessage", L"翻译为", L"翻譯為",
                L"翻译至", L"翻譯至", L"翻译成", L"翻譯成",
                L"翻译消息", L"翻譯消息", L"翻译此", L"翻譯此"})) return BarLabel::Pending;
    return BarLabel::Missing;
}

namespace detail {

inline bool is_bidi_format(wchar_t ch) {
    return ch == 0x200E || ch == 0x200F || ch == 0xFEFF ||
           (ch >= 0x202A && ch <= 0x202E) || (ch >= 0x2066 && ch <= 0x2069);
}

inline bool digits_only(std::wstring_view text) {
    if (text.empty()) return false;
    for (const wchar_t ch : text) {
        if (ch < L'0' || ch > L'9') return false;
    }
    return true;
}

inline std::wstring_view trim_view(std::wstring_view text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::iswspace(text[begin])) ++begin;
    while (end > begin && std::iswspace(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

inline bool ends_with_dash_word(std::wstring_view text, std::wstring_view word) {
    // Matches " <dash> <word>" where dash is - , en dash or em dash.
    if (text.size() < word.size() + 2) return false;
    const size_t tail = text.size() - word.size();
    if (text.substr(tail) != word) return false;
    const wchar_t dash = text[tail - 2];
    if (dash != L'-' && dash != 0x2013 && dash != 0x2014) return false;
    return std::iswspace(text[tail - 1]) != 0;
}

} // namespace detail

// Telegram composes its window title in window/main_window.cpp:887-937 as
//   "(unread) Peer name – (total) – Telegram"
// The unread counters change every few seconds and the bidi marks change with
// the locale, so neither may ever become part of a latch key: a key that drifts
// re-arms the translate toggle and flips the chat back to the original text.
// What is left here is the peer name only.
inline std::wstring normalize_chat_identity(std::wstring_view title) {
    std::wstring text;
    text.reserve(title.size());
    for (const wchar_t ch : title) {
        if (detail::is_bidi_format(ch)) continue;
        text.push_back(ch);
    }

    // "(12) Name" -> "Name"
    {
        const std::wstring_view view = detail::trim_view(text);
        if (!view.empty() && view.front() == L'(') {
            const size_t close = view.find(L')');
            if (close != std::wstring_view::npos && close > 1 &&
                detail::digits_only(view.substr(1, close - 1))) {
                const std::wstring_view rest = detail::trim_view(view.substr(close + 1));
                text.assign(rest.begin(), rest.end());
            }
        }
    }

    // "Name @ Account" -> "Name" (multi-account windows only).
    const size_t at = text.find(L" @ ");
    if (at != std::wstring::npos) text.erase(at);

    // "Name – Telegram" -> "Name"
    {
        std::wstring lowered;
        lowered.reserve(text.size());
        for (const wchar_t ch : text) lowered.push_back(static_cast<wchar_t>(std::towlower(ch)));
        if (detail::ends_with_dash_word(lowered, L"telegram")) {
            text.erase(text.size() - 10); // "<dash> telegram"
            while (!text.empty() && std::iswspace(text.back())) text.pop_back();
        }
    }

    // "Name – (149660)" -> "Name"
    // "Name – (149660)" -> "Name" and "Telegram (3)" -> "Telegram" -> no chat.
    // A trailing counter is a count, not a name: it drifts every few seconds and
    // a key that drifts re-arms the toggle (that is the 2026-11-03 rebound).
    for (;;) {
        const std::wstring_view view = detail::trim_view(text);
        if (view.size() < 3 || view.back() != L')') break;
        const size_t open = view.rfind(L'(');
        if (open == std::wstring_view::npos || open == 0) break;
        if (!detail::digits_only(view.substr(open + 1, view.size() - open - 2))) break;
        size_t cut = open;
        if (cut >= 2 && (view[cut - 2] == L'-' || view[cut - 2] == 0x2013 ||
                         view[cut - 2] == 0x2014)) {
            cut -= 2; // "<space><dash>"
        } else if (std::iswspace(view[cut - 1])) {
            --cut; // "(12)" with no peer prefix: the no-chat window title
        } else {
            break;
        }
        const std::wstring_view head = detail::trim_view(view.substr(0, cut));
        text.assign(head.begin(), head.end());
    }

    std::wstring result;
    result.reserve(text.size());
    bool pending_space = false;
    for (const wchar_t ch : text) {
        if (std::iswspace(ch)) {
            pending_space = !result.empty();
            continue;
        }
        if (pending_space) {
            result.push_back(L' ');
            pending_space = false;
        }
        result.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }
    if (result == L"telegram") return std::wstring();
    return result;
}

struct TranslateBarHit {
    bool found = false;
    bool already = false;
    RECT rect{};
    std::wstring chat;
};

inline TranslateBarHit recognize_translate_bar(
    const std::vector<OcrLineHit> &lines,
    const RECT &clientScreen,
    UINT dpi) {
    TranslateBarHit best;
    const auto px = [dpi](LONG value) { return value * static_cast<LONG>(dpi) / 96; };
    if (dpi < 96 || clientScreen.right <= clientScreen.left ||
        clientScreen.bottom <= clientScreen.top) return best;
    for (const auto& line : lines) {
        const BarLabel label = classify_translate_bar(line.text);
        if (label == BarLabel::Missing) continue;
        const auto& rect = line.rect;
        const LONG center = rect.left + (rect.right - rect.left) / 2;
        // translate_bar.cpp:318-335 centers icon(24 DIP)+label in the pane.
        // Estimate the pane from that relationship, not a fixed sidebar ratio.
        const LONG paneLeft = 2 * (center - px(12)) - clientScreen.right;
        // Row band. The bar is a single row pinned under the chat top bar:
        // measured 103..121 px on the live 144 DPI window, i.e. ~69 DIP from the
        // client top. A loose "top 300 px" window is what let a body message
        // reading "Show Original" (and Telegram's own menu row) pose as the bar.
        if (rect.left < clientScreen.left || rect.right > clientScreen.right ||
            rect.right <= rect.left || rect.bottom - rect.top < px(6) ||
            rect.bottom - rect.top > px(30) ||
            rect.top < clientScreen.top + px(30) ||
            rect.top > clientScreen.top + px(130) ||
            rect.bottom > std::min(clientScreen.bottom, clientScreen.top + px(150)) ||
            paneLeft < clientScreen.left - px(32) || paneLeft > clientScreen.right - px(200)) continue;

        TranslateBarHit candidate;
        candidate.found = true;
        candidate.already = label == BarLabel::Original;
        candidate.rect = rect;
        // Structural gate: a real bar row holds exactly one text line - the
        // label. Two OCR engines reporting the label is not pollution, but any
        // *other* text on the same row means the capture crossed columns (the
        // old full-width scan matched chat-list previews, Telegram's menu row
        // and the GUI's own title bar and clicked garbage).
        bool polluted = false;
        for (const auto& other : lines) {
            if (&other == &line) continue;
            if (classify_translate_bar(other.text) != BarLabel::Missing) continue;
            const LONG overlap = std::min(rect.bottom, other.rect.bottom) -
                                 std::max(rect.top, other.rect.top);
            if (overlap > (rect.bottom - rect.top) / 2) {
                polluted = true;
                break;
            }
        }
        if (polluted) continue;
        LONG headingTop = std::numeric_limits<LONG>::max();
        for (const auto& heading : lines) {
            if (classify_translate_bar(heading.text) != BarLabel::Missing) continue;
            const auto& bounds = heading.rect;
            // The chat name sits at the left of the top bar, before the bar's
            // own left edge. Telegram's search / pin / menu cluster is to the
            // right of that edge, and the old pane+160 DIP window admitted it --
            // the OCR'd 礼物 that became a fake chat identity came from there.
            if (bounds.left < std::max(clientScreen.left, paneLeft - px(12)) ||
                bounds.left >= rect.left || bounds.right > clientScreen.right ||
                bounds.top < clientScreen.top || bounds.top > clientScreen.top + px(82) ||
                // Measured on the live 144 DPI window: the chat name sits at
                // 55..75 px while the bar starts at 103 px, a 32 DIP gap. The old
                // 38 DIP / 22 DIP clearances rejected the real layout and left
                // the bar without a chat identity, so nothing ever clicked.
                bounds.bottom > rect.top - px(4) ||
                rect.top - bounds.top < px(16) || rect.top - bounds.top > px(260) ||
                bounds.bottom <= bounds.top || bounds.bottom - bounds.top > px(30)) continue;
            const auto name = normalize_chat_heading(heading.text);
            if (name.empty()) continue;
            // The custom window caption is outside the chat top bar. It may
            // change with unread counts and must never become a chat identity.
            if (bounds.top < clientScreen.top + px(24) && name.rfind(L"telegram", 0) == 0) continue;
            if (bounds.top < headingTop) {
                headingTop = bounds.top;
                candidate.chat = name;
            }
        }
        // The two OCR engines can disagree about one label. An Original hit
        // always wins: suppressing one click is safer than toggling it off.
        if (!best.found || (candidate.already && !best.already) ||
            (candidate.already == best.already && candidate.rect.top < best.rect.top)) {
            best = std::move(candidate);
        }
    }
    return best;
}

} // namespace tv