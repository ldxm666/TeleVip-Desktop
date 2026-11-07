#pragma once

#include "common.h"
#include "theme.h"

namespace tv {

struct ProcessEntry {
    DWORD pid = 0;
    std::wstring path;
    bool portable = false;
};

struct Settings {
    uint32_t flags = 0;
    std::wstring lang = L"zh";
    bool dark = true;
    Accent accent = Accent::Blue;
    std::wstring telegram_exe;
    bool log_collapsed = false;
    bool overlay_enabled = true;
    bool translate_zh = true;
    bool native_translate = true;
    // Which translate backend Telegram is pointed at:
    // 0 = Telegram's own MTProto provider, no local process involved;
    // 1 = our resident TranslationServer plus the translate-url-template.
    int translate_engine = 0;
};

Settings load_settings();
void save_settings(const Settings& settings);
// In-memory half of load_settings/save_settings, so the on-disk contract
// ("translate_engine": 0|1) is testable without touching %LOCALAPPDATA%.
std::string settings_to_json(const Settings& settings);
Settings settings_from_json(std::string_view json);
std::wstring settings_path();
std::wstring archive_dir();
std::wstring vault_dir();
std::wstring log_path();
std::wstring hook_dll_path();
std::wstring stable_dll_path();
std::wstring extract_hook_dll();

class ConfigMap {
public:
    ~ConfigMap();
    bool open();
    void close();
    bool valid() const { return config_ != nullptr; }
    void set_flags(uint32_t flags);
    uint32_t flags() const;
    uint64_t hits() const;
    uint32_t hook_state() const;
    uint32_t premium_state() const;
    uint32_t pattern_rva() const;

private:
    HANDLE mapping_ = nullptr;
    SharedConfig* config_ = nullptr;
};

class CacheVault {
public:
    ~CacheVault();
    void set_enabled(bool enabled);
    void stop();
    bool running() const { return running_.load(); }
    const std::wstring& folder() const { return folder_; }
    std::vector<std::wstring> roots() const;
    size_t copied() const { return copied_.load(); }

private:
    void loop();
    std::wstring folder_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::atomic<size_t> copied_{0};
    std::mutex mutex_;
    std::condition_variable cv_;
    std::unordered_set<std::wstring> seen_;
};

std::vector<ProcessEntry> enumerate_telegram();
bool process_alive(DWORD pid);
std::wstring process_exe_path(DWORD pid);
bool target_hash_ok(DWORD pid);
bool probe_existing_hook(DWORD pid);
bool hook_module_loaded(DWORD pid);
bool inject_dll(DWORD pid, const std::wstring& dll_path, std::wstring& message);
bool hide_phone(DWORD pid, std::wstring& message);
bool apply_local_patches(DWORD pid, uint32_t flags, std::vector<std::wstring>& messages);

struct InjectionResult {
    bool ok = false;
    DWORD pid = 0;
    std::wstring message;
    uint32_t hook_state = 0;
    bool existing_hook = false;
    std::vector<std::wstring> local_messages;
};

InjectionResult run_injection(DWORD pid, uint32_t flags);
// Starts Telegram itself with CREATE_SUSPENDED and injects before resuming it,
// so hide-online is in force from the very first packet. Used when nothing is
// selected, and by --launch-inject.
InjectionResult launch_and_inject(const std::wstring& exe, uint32_t flags);
void log_note(const std::wstring& message);
std::vector<std::wstring> read_log_tail(size_t lines = 120);
std::wstring hooks_log_text(size_t max_lines = 400);

} // namespace tv
