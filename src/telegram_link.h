#pragma once
#include "common.h"

namespace tv::telegram_link {
inline std::optional<std::string> command(std::wstring_view url) {
    if (url.empty() || url.size() > 8192 || !(url.starts_with(L"tg://")
        || url.starts_with(L"https://t.me/") || url.starts_with(L"https://telegram.me/"))) return {};
    constexpr char hex[] = "0123456789abcdef";
    std::string result = "OPEN:";
    for (wchar_t c : url) {
        if (c < 32 || c == 127 || c == L'"') return {};
        if (c > 127 || c == L'%' || c == L';') {
            result += '%'; for (int shift = 12; shift >= 0; shift -= 4) result += hex[(c >> shift) & 15];
        } else result += static_cast<char>(c);
    }
    return result + ";CMD:show;";
}
// Forward to the named pipe whose server PID matches the selected process.
// There is deliberately no ShellExecute/CreateProcess fallback for Telegram URLs.
bool forward(DWORD pid, const std::wstring& url, std::wstring& error);
bool forward_bound(const std::wstring& url, std::wstring& error);
bool bind(DWORD pid, std::wstring& error);
int handle_protocol(const std::wstring& url);
}
