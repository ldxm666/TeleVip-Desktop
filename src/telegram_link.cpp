#include "telegram_link.h"
#include "json_text.h"
#include <future>

namespace tv::telegram_link {
namespace {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value != nullptr) CloseHandle(value); }
};
std::filesystem::path target_path() {
    return std::filesystem::path(get_local_appdata()) / L"TeleVipDesktop" / L"link-target.txt";
}
std::optional<uint64_t> creation(DWORD pid) {
    Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
    if (!process.value) return {};
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process.value, &created, &exited, &kernel, &user)) return {};
    return (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}
bool io(HANDLE pipe, void* bytes, DWORD size, bool write, DWORD& count) {
    Handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event.value) return false;
    OVERLAPPED operation{}; operation.hEvent = event.value;
    const BOOL started = write ? WriteFile(pipe, bytes, size, &count, &operation)
                               : ReadFile(pipe, bytes, size, &count, &operation);
    if (started) return true;
    if (GetLastError() != ERROR_IO_PENDING) return false;
    if (WaitForSingleObject(event.value, 1500) != WAIT_OBJECT_0) {
        CancelIoEx(pipe, &operation);
        // Wait for cancellation completion before releasing OVERLAPPED/buffer.
        GetOverlappedResult(pipe, &operation, &count, TRUE); return false;
    }
    return GetOverlappedResult(pipe, &operation, &count, FALSE) != FALSE;
}
bool registry_string(HKEY root, const std::wstring& path, const wchar_t* name, const std::wstring& value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    // Preserve the original registration once for an explicit manual restore.
    if (!name && path.ends_with(L"shell\\open\\command")) {
        DWORD type = 0, bytes = 0;
        if (RegQueryValueExW(key, L"TeleVipPreviousCommand", nullptr, &type, nullptr, &bytes) == ERROR_FILE_NOT_FOUND) {
            wchar_t old[32768]{}; bytes = sizeof(old);
            if (RegQueryValueExW(key, nullptr, nullptr, &type, reinterpret_cast<BYTE*>(old), &bytes) == ERROR_SUCCESS
                && (type == REG_SZ || type == REG_EXPAND_SZ))
                RegSetValueExW(key, L"TeleVipPreviousCommand", 0, type, reinterpret_cast<BYTE*>(old), bytes);
        }
    }
    const auto result = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * 2));
    RegCloseKey(key); return result == ERROR_SUCCESS;
}
}
bool forward(DWORD pid, const std::wstring& url, std::wstring& error) {
    const auto payload = command(url);
    if (!payload || !creation(pid)) { error = L"Telegram 链接或目标实例不可用"; return false; }
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(L"\\\\.\\pipe\\*", &data);
    if (find == INVALID_HANDLE_VALUE) { error = L"无法连接当前 Telegram 实例"; return false; }
    bool found = false, ok = false;
    do {
        const std::wstring name = data.cFileName;
        // Qt's Telegram pipe is Global\\<32 hex MD5>-<GUID>. Ignore all
        // other pipes before opening, then prove the owning PID in the kernel.
        const auto dash = name.find(L'-');
        if (dash == std::wstring::npos || dash < 32 || name.size() > 200) continue;
        const auto digest = name.substr(dash - 32, 32);
        if (digest.find_first_not_of(L"0123456789abcdefABCDEF") != std::wstring::npos) continue;
        const std::wstring path = L"\\\\.\\pipe\\" + name;
        Handle pipe{CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr)};
        if (pipe.value == INVALID_HANDLE_VALUE) continue;
        ULONG owner = 0;
        if (!GetNamedPipeServerProcessId(pipe.value, &owner) || owner != pid) continue;
        found = true;
        AllowSetForegroundWindow(pid);
        DWORD count = 0;
        if (!io(pipe.value, const_cast<char*>(payload->data()), static_cast<DWORD>(payload->size()), true, count)
            || count != payload->size()) break;
        std::string reply;
        char buffer[256];
        const ULONGLONG until = GetTickCount64() + 2000;
        while (GetTickCount64() < until && reply.size() < 512 && reply.find(';') == std::string::npos) {
            if (!io(pipe.value, buffer, sizeof(buffer), false, count) || count == 0) break;
            reply.append(buffer, count);
        }
        const std::string prefix = "RES:" + std::to_string(pid) + "_";
        ok = reply.starts_with(prefix) && reply.find(';') != std::string::npos;
        break; // Never retry a delivered URL against a different instance.
    } while (FindNextFileW(find, &data));
    FindClose(find);
    if (!ok) error = found ? L"当前 Telegram 暂未响应，请稍后重试" : L"目标 Telegram 未运行，请先选择并注入实例";
    return ok;
}
bool forward_bound(const std::wstring& url, std::wstring& error) {
    std::ifstream file(target_path()); DWORD pid = 0; uint64_t created = 0;
    if (!(file >> pid >> created) || creation(pid) != std::optional<uint64_t>(created)) {
        error = L"已绑定的 Telegram 已退出，请在 TeleVip 中重新选择并注入"; return false;
    }
    return forward(pid, url, error);
}
bool bind(DWORD pid, std::wstring& error) {
    const auto created = creation(pid);
    if (!created) { error = L"无法绑定目标实例"; return false; }
    const auto path = target_path(); std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    { std::ofstream file(path.wstring() + L".tmp", std::ios::trunc); file << pid << '\n' << *created << '\n'; if (!file) return false; }
    if (!MoveFileExW((path.wstring() + L".tmp").c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH)) return false;
    wchar_t exe[32768]{};
    if (!GetModuleFileNameW(nullptr, exe, 32768)) return false;
    const std::wstring launch = L"\"" + std::wstring(exe) + L"\" --telegram-url \"%1\"";
    const std::wstring base = L"Software\\Classes\\";
    const bool ok = registry_string(HKEY_CURRENT_USER, base + L"tg", nullptr, L"URL:Telegram Protocol")
        && registry_string(HKEY_CURRENT_USER, base + L"tg", L"URL Protocol", L"")
        && registry_string(HKEY_CURRENT_USER, base + L"tg\\shell\\open\\command", nullptr, launch)
        && registry_string(HKEY_CURRENT_USER, base + L"tdesktop.tg\\shell\\open\\command", nullptr, launch);
    if (!ok) error = L"实例已绑定，但 Windows 链接关联未能保存";
    return ok;
}
int handle_protocol(const std::wstring& url) {
    std::wstring error;
    if (forward_bound(url, error)) return 0;
    // Report through the existing custom UI, without launching another Telegram.
    HWND window = FindWindowW(L"TeleVipNativeWindow", nullptr);
    if (window) {
        COPYDATASTRUCT data{0x54474C4B, static_cast<DWORD>((error.size() + 1) * sizeof(wchar_t)), error.data()};
        DWORD_PTR result = 0;
        SendMessageTimeoutW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG, 500, &result);
    }
    return 8;
}
}
