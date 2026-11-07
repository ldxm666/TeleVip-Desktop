#include "common.h"

#include <cstdarg>

namespace tv {

std::wstring utf8_to_wide(std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::string wide_to_utf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring trim(std::wstring value) {
    auto not_space = [](wchar_t ch) { return ch != L' ' && ch != L'\t' && ch != L'\r' && ch != L'\n'; };
    auto begin = std::find_if(value.begin(), value.end(), not_space);
    auto end = std::find_if(value.rbegin(), value.rend(), not_space).base();
    if (begin >= end) return {};
    return std::wstring(begin, end);
}

std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return value;
}

bool starts_with_ci(std::wstring_view text, std::wstring_view prefix) {
    if (text.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (towlower(text[i]) != towlower(prefix[i])) return false;
    }
    return true;
}

std::wstring replace_all(std::wstring text, std::wstring_view from, std::wstring_view to) {
    if (from.empty()) return text;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::wstring::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

std::wstring format_w(const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    const int count = _vscwprintf(fmt, copy);
    va_end(copy);
    if (count < 0) {
        va_end(args);
        return {};
    }
    std::wstring out(static_cast<size_t>(count), L'\0');
    vswprintf_s(out.data(), out.size() + 1, fmt, args);
    va_end(args);
    return out;
}

std::wstring format_w(const std::wstring& fmt, const std::unordered_map<std::wstring, std::wstring>& values) {
    std::wstring out = fmt;
    for (const auto& [key, value] : values) {
        out = replace_all(std::move(out), L"{" + key + L"}", value);
    }
    return out;
}

std::wstring get_local_appdata() {
    wchar_t buffer[MAX_PATH * 2] = {};
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    if (size > 0 && size < std::size(buffer)) return buffer;
    PWSTR known = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &known))) {
        std::wstring result(known);
        CoTaskMemFree(known);
        return result;
    }
    return L".";
}

std::wstring get_appdata() {
    wchar_t buffer[MAX_PATH * 2] = {};
    const DWORD size = GetEnvironmentVariableW(L"APPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    if (size > 0 && size < std::size(buffer)) return buffer;
    return L".";
}

std::wstring get_module_dir() {
    wchar_t buffer[MAX_PATH * 2] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    std::wstring path(buffer, length);
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring(L".") : path.substr(0, slash);
}

std::wstring get_exe_path() {
    wchar_t buffer[MAX_PATH * 2] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    return std::wstring(buffer, length);
}

std::wstring join_path(std::wstring_view a, std::wstring_view b) {
    std::wstring out(a);
    if (!out.empty() && out.back() != L'\\' && out.back() != L'/') out.push_back(L'\\');
    out.append(b);
    return out;
}

bool file_exists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

uint64_t file_size(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
    return (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

bool ensure_dir(const std::wstring& path) {
    if (path.empty()) return false;
    const int result = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS;
}

std::wstring truncate_middle(const std::wstring& text, size_t max_chars) {
    if (text.size() <= max_chars || max_chars < 8) return text;
    const size_t head = max_chars / 2 - 1;
    const size_t tail = max_chars - head - 1;
    return text.substr(0, head) + L"…" + text.substr(text.size() - tail);
}

std::wstring ellipsize(const std::wstring& text, size_t max_chars) {
    if (text.size() <= max_chars) return text;
    return text.substr(0, max_chars > 1 ? max_chars - 1 : 0) + L"…";
}

std::wstring wide_time(uint64_t ms, const wchar_t* fmt) {
    const time_t seconds = static_cast<time_t>(ms / 1000);
    tm local{};
    localtime_s(&local, &seconds);
    wchar_t buffer[128] = {};
    wcsftime(buffer, std::size(buffer), fmt, &local);
    return buffer;
}

void debug_log(const std::wstring& message) {
    const std::wstring folder = join_path(get_local_appdata(), L"TeleVipDesktop");
    ensure_dir(folder);
    std::ofstream file(std::filesystem::path(join_path(folder, L"native.log")),
                       std::ios::binary | std::ios::app);
    if (!file) return;
    file << wide_to_utf8(format_w(L"[%llu] ", now_ms())) << wide_to_utf8(message) << "\n";
    OutputDebugStringW(message.c_str());
    OutputDebugStringW(L"\n");
}

} // namespace tv
