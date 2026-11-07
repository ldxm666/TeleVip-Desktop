#include "core.h"
#include <tlhelp32.h>
namespace tv {
namespace {
std::string json_escape(std::string value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char ch : value) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

std::string read_file_utf8(const std::wstring& path) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) return {};
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

bool json_find_value(const std::string& json, const std::string& key, std::string& value) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size()) return false;
    if (json[pos] == '"') {
        ++pos;
        std::string out;
        while (pos < json.size()) {
            char ch = json[pos++];
            if (ch == '"') break;
            if (ch == '\\' && pos < json.size()) {
                char esc = json[pos++];
                switch (esc) {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case '\\': out.push_back('\\'); break;
                case '"': out.push_back('"'); break;
                case '/': out.push_back('/'); break;
                case 'u': {
                    if (pos + 4 <= json.size()) {
                        const std::string hex = json.substr(pos, 4);
                        pos += 4;
                        wchar_t unit = static_cast<wchar_t>(std::stoul(hex, nullptr, 16));
                        out += wide_to_utf8(std::wstring(1, unit));
                    }
                    break;
                }
                default: out.push_back(esc); break;
                }
            } else {
                out.push_back(ch);
            }
        }
        value = out;
        return true;
    }
    const size_t end = json.find_first_of(",}\r\n", pos);
    std::string raw = json.substr(pos, end - pos);
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.front()))) raw.erase(raw.begin());
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) raw.pop_back();
    value = raw;
    return true;
}

int json_find_int(const std::string& json, const std::string& key, int fallback) {
    std::string value;
    if (!json_find_value(json, key, value)) return fallback;
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

std::wstring local_dir() {
    return join_path(get_local_appdata(), L"TeleVipNativePreview");
}


}
std::wstring settings_path() {
    ensure_dir(local_dir());
    return join_path(local_dir(), L"settings.json");
}

std::wstring archive_dir() {
    ensure_dir(local_dir());
    return local_dir();
}

std::wstring vault_dir() {
    const std::wstring path = join_path(local_dir(), L"secret");
    ensure_dir(path);
    return path;
}

std::wstring log_path() {
    ensure_dir(local_dir());
    return join_path(local_dir(), L"hooks.log");
}

std::wstring hook_dll_path() {
    return join_path(local_dir(), L"televip_hook.dll");
}

Settings settings_from_json(std::string_view view) {
    Settings settings;
    const std::string json(view);
    if (json.empty()) return settings;
    settings.flags = static_cast<uint32_t>(json_find_int(json, "flags", 0));
    std::string value;
    if (json_find_value(json, "lang", value)) settings.lang = utf8_to_wide(value);
    if (json_find_value(json, "appearance", value)) settings.dark = value != "light";
    settings.accent = static_cast<Accent>(json_find_int(json, "accent", 0));
    if (settings.accent < Accent::Blue || settings.accent > Accent::Graphite) settings.accent = Accent::Blue;
    if (json_find_value(json, "telegram_exe", value)) settings.telegram_exe = utf8_to_wide(value);
    settings.log_collapsed = json_find_int(json, "log_collapsed", 0) != 0;
    settings.overlay_enabled = json_find_int(json, "overlay_enabled", 1) != 0;
    settings.translate_zh = json_find_int(json, "translate_zh", 1) != 0;
    settings.native_translate = json_find_int(json, "native_translate", 1) != 0;
    settings.translate_engine = json_find_int(json, "translate_engine", 0);
    // Only the two values we define may reach a launch path; anything hand-edited
    // falls back to Telegram's own provider rather than a backend that may not exist.
    if (settings.translate_engine != 1) settings.translate_engine = 0;
    return settings;
}

Settings load_settings() {
    const std::string json = read_file_utf8(settings_path());
    if (json.empty()) return Settings{};
    return settings_from_json(json);
}

std::string settings_to_json(const Settings& settings) {
    std::string out;
    out.reserve(448);
    out += "{\n";
    out += "  \"flags\": " + std::to_string(settings.flags) + ",\n";
    out += "  \"lang\": \"" + json_escape(wide_to_utf8(settings.lang)) + "\",\n";
    out += "  \"appearance\": \"" + std::string(settings.dark ? "dark" : "light") + "\",\n";
    out += "  \"accent\": " + std::to_string(static_cast<int>(settings.accent)) + ",\n";
    out += "  \"telegram_exe\": \"" + json_escape(wide_to_utf8(settings.telegram_exe)) + "\",\n";
    out += "  \"log_collapsed\": " + std::to_string(settings.log_collapsed ? 1 : 0) + ",\n";
    out += "  \"overlay_enabled\": " + std::to_string(settings.overlay_enabled ? 1 : 0) + ",\n";
    out += "  \"translate_zh\": " + std::to_string(settings.translate_zh ? 1 : 0) + ",\n";
    out += "  \"native_translate\": " + std::to_string(settings.native_translate ? 1 : 0) + ",\n";
    out += "  \"translate_engine\": " +
           std::to_string(settings.translate_engine == 1 ? 1 : 0) + "\n";
    out += "}\n";
    return out;
}

void save_settings(const Settings& settings) {
    ensure_dir(local_dir());
    const std::wstring temp = settings_path() + L".tmp";
    std::ofstream file(std::filesystem::path(temp), std::ios::binary | std::ios::trunc);
    if (!file) return;
    const std::string body = settings_to_json(settings);
    file.write(body.data(), static_cast<std::streamsize>(body.size()));
    const bool written = static_cast<bool>(file);
    file.close();
    if (!written) return;
    MoveFileExW(temp.c_str(), settings_path().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

CacheVault::~CacheVault() {
    stop();
}

std::vector<std::wstring> CacheVault::roots() const {
    std::vector<std::wstring> result;
    const wchar_t* candidates[] = {
        L"Telegram Desktop\\tdata\\user_data\\cache",
        L"Telegram Desktop\\tdata\\user_data\\cache",
    };
    std::vector<std::wstring> possible = {
        join_path(join_path(get_appdata(), L"Telegram Desktop"), L"tdata\\user_data\\cache"),
        join_path(join_path(get_local_appdata(), L"Telegram Desktop"), L"tdata\\user_data\\cache"),


    };
    (void)candidates;
    for (const auto& path : possible) {
        if (std::filesystem::is_directory(std::filesystem::path(path)) &&
            std::find(result.begin(), result.end(), path) == result.end()) {
            result.push_back(path);
        }
    }
    return result;
}

void CacheVault::set_enabled(bool enabled) {
    if (enabled) {
        if (running_.load()) return;
        stop_.store(false);
        running_.store(true);
        worker_ = std::thread([this] { loop(); });
    } else {
        stop();
    }
}

void CacheVault::stop() {
    stop_.store(true);
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    running_.store(false);
}

void CacheVault::loop() {
    if (folder_.empty()) folder_ = vault_dir();
    ensure_dir(folder_);
    const auto cutoff = std::filesystem::file_time_type::clock::now() - std::chrono::seconds(30);
    while (!stop_.load()) {
        for (const auto& root : roots()) {
            std::error_code ec;
            std::filesystem::recursive_directory_iterator it(
                std::filesystem::path(root),
                std::filesystem::directory_options::skip_permission_denied, ec);
            if (ec) continue;
            for (; it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (stop_.load()) break;
                if (ec) {
                    ec.clear();
                    continue;
                }
                if (!it->is_regular_file(ec) || ec) continue;
                const std::wstring extension = lower(it->path().extension().wstring());
                static const std::unordered_set<std::wstring> media = {
                    L".jpg", L".jpeg", L".png", L".webp", L".gif", L".bmp",
                    L".mp4", L".mov", L".m4v", L".webm", L".mkv", L".avi",
                    L".mp3", L".m4a", L".ogg", L".opus", L".flac", L".wav",
                    L".pdf", L".zip", L".rar", L".7z",
                };
                if (!media.contains(extension)) continue;
                const uint64_t size = it->file_size(ec);
                if (ec || size < 4096) continue;
                const auto modified = it->last_write_time(ec);
                if (ec || modified < cutoff) continue;
                const std::wstring key = it->path().wstring() + L"|" + std::to_wstring(size) + L"|" +
                                         std::to_wstring(static_cast<long long>(modified.time_since_epoch().count()));
                {
                    std::lock_guard lock(mutex_);
                    if (seen_.contains(key)) continue;
                    seen_.insert(key);
                    if (seen_.size() > 5000) seen_.clear();
                }
                const auto sys_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    modified - std::filesystem::file_time_type::clock::now() +
                    std::chrono::system_clock::now());
                const time_t stamp = std::chrono::system_clock::to_time_t(sys_time);
                tm local{};
                localtime_s(&local, &stamp);
                wchar_t prefix[64] = {};
                wcsftime(prefix, std::size(prefix), L"%Y%m%d-%H%M%S", &local);
                std::wstring stem = it->path().stem().wstring();
                if (stem.size() > 40) stem = stem.substr(0, 40);
                std::wstring target = join_path(folder_, std::wstring(prefix) + L"_" + stem + extension);
                int suffix = 1;
                while (file_exists(target)) {
                    target = join_path(folder_, std::wstring(prefix) + L"_" + stem + L"_" +
                                                   std::to_wstring(suffix++) + extension);
                }
                std::filesystem::copy_file(it->path(), std::filesystem::path(target),
                                           std::filesystem::copy_options::skip_existing, ec);
                if (!ec) copied_.fetch_add(1);
            }
        }
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, std::chrono::seconds(4), [this] { return stop_.load(); });
    }
    running_.store(false);
}

std::vector<ProcessEntry> enumerate_telegram() {
    std::vector<ProcessEntry> result;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"Telegram.exe") != 0) continue;
            ProcessEntry item;
            item.pid = entry.th32ProcessID;
            item.path = process_exe_path(item.pid);
            const std::wstring path_lower = lower(item.path);
            item.portable = path_lower.find(L"\\portable\\") != std::wstring::npos;
            result.push_back(std::move(item));
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

bool process_alive(DWORD pid) {
    if (pid == 0) return false;
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    DWORD exit_code = 0;
    const bool alive = GetExitCodeProcess(process, &exit_code) && exit_code == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
}

std::wstring process_exe_path(DWORD pid) {
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    wchar_t buffer[2048] = {};
    DWORD size = static_cast<DWORD>(std::size(buffer));
    std::wstring result;
    if (QueryFullProcessImageNameW(process, 0, buffer, &size)) result.assign(buffer, size);
    CloseHandle(process);
    return result;
}

void log_note(const std::wstring& message) {
    ensure_dir(local_dir());
    std::ofstream file(std::filesystem::path(log_path()), std::ios::binary | std::ios::app);
    if (!file) return;
    file << "[gui] " << wide_to_utf8(message) << "\n";
}

std::vector<std::wstring> read_log_tail(size_t lines) {
    std::vector<std::wstring> result;
    const std::wstring path = log_path();
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) return result;
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    const std::streamoff start = std::max<std::streamoff>(0, size - 256 * 1024);
    file.seekg(start, std::ios::beg);
    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (start > 0) {
        const size_t newline = data.find('\n');
        if (newline != std::string::npos) data.erase(0, newline + 1);
    }
    std::wstring wide = utf8_to_wide(data);
    size_t pos = 0;
    while (pos <= wide.size()) {
        const size_t end = wide.find(L'\n', pos);
        std::wstring line = wide.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (!line.empty()) result.push_back(std::move(line));
        if (end == std::wstring::npos) break;
        pos = end + 1;
    }
    if (result.size() > lines) result.erase(result.begin(), result.end() - static_cast<std::ptrdiff_t>(lines));
    return result;
}

std::wstring hooks_log_text(size_t max_lines) {
    const auto lines = read_log_tail(max_lines);
    std::wstring text;
    for (const auto& line : lines) {
        text += line;
        text += L"\n";
    }
    return text;
}


// This adapter is intentionally limited to the public preview target.
// It does not open the production mapping, inject a DLL or modify another process.
std::wstring extract_hook_dll() { return {}; }
std::wstring stable_dll_path() { return {}; }
ConfigMap::~ConfigMap() { close(); }
bool ConfigMap::open() { return false; }
void ConfigMap::close() {}
void ConfigMap::set_flags(uint32_t) {}
uint32_t ConfigMap::flags() const { return 0; }
uint64_t ConfigMap::hits() const { return 0; }
uint32_t ConfigMap::hook_state() const { return 0; }
uint32_t ConfigMap::premium_state() const { return 0; }
uint32_t ConfigMap::pattern_rva() const { return 0; }
bool target_hash_ok(DWORD) { return false; }
bool probe_existing_hook(DWORD) { return false; }
bool hook_module_loaded(DWORD) { return false; }
bool inject_dll(DWORD, const std::wstring&, std::wstring& message) {
    message=L"The private core is not part of the public UI preview"; return false;
}
bool hide_phone(DWORD, std::wstring& message) { return inject_dll(0,{},message); }
bool apply_local_patches(DWORD, uint32_t, std::vector<std::wstring>& messages) {
    messages.push_back(L"Private core unavailable in this target"); return false;
}
InjectionResult run_injection(DWORD pid, uint32_t) {
    InjectionResult result; result.pid=pid; result.message=L"Private core unavailable in this target"; return result;
}
InjectionResult launch_and_inject(const std::wstring&, uint32_t flags) { return run_injection(0,flags); }

}
