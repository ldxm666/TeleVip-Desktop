#pragma once
#include "translate_click_guard.h"

#include "common.h"

#include <chrono>
#include <queue>

namespace tv {

constexpr uint16_t kNativeTranslatePort = 48620;

// Local HTTP endpoint used by Telegram's custom translation provider.
// Telegram sends GET /translate?q=...&from=..&to=.. and expects JSON or text.
class TranslationServer {
public:
    ~TranslationServer();

    bool start(uint16_t preferred_port = kNativeTranslatePort);
    void stop();

    bool running() const { return running_.load(); }
    uint16_t port() const { return port_; }
    uint64_t requests() const { return requests_.load(); }
    uint64_t cache_hits() const { return hits_.load(); }
    uint64_t failures() const { return lost_.load(); }
    uint64_t last_ms() const { return last_ms_.load(); }
    uint64_t avg_ms() const {
        const uint64_t n = timed_.load();
        return n ? total_ms_.load() / n : 0;
    }
    size_t cache_size() const;

private:
    void accept_loop();
    void worker_loop();
    void handle_client(SOCKET client);
    bool serve_request(SOCKET client, const std::string& request);

    std::atomic<bool> running_{false};
    SOCKET listen_ = INVALID_SOCKET;
    uint16_t port_ = 0;
    std::thread accept_thread_;
    std::vector<std::thread> workers_;
    std::queue<SOCKET> queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<uint64_t> requests_{0};
    std::atomic<uint64_t> hits_{0};
    std::atomic<uint64_t> lost_{0};
    std::atomic<uint64_t> last_ms_{0};
    std::atomic<uint64_t> total_ms_{0};
    std::atomic<uint64_t> timed_{0};
    mutable std::mutex cache_mutex_;
    std::unordered_map<std::string, std::string> cache_;
};

std::wstring native_translate_url(uint16_t port);
bool native_translate_configured(const std::wstring& telegram_exe, uint16_t port);
bool configure_native_translate(const std::wstring& telegram_exe, uint16_t port,
                                std::wstring& error);
// Removes the translate-url-template override so Telegram falls back to its own
// provider (MTProto messages.translateText, or the platform one). Required
// whenever the local TranslationServer is not going to be listening -- a stale
// template points every translation at a dead port and the feature looks broken.
bool clear_native_translate(const std::wstring& telegram_exe, std::wstring& error);
bool restart_telegram(DWORD pid, std::wstring& error);

// Single-shot CLI diagnostic. The GUI routes all requests through the agent
// so explicit requests cannot race its automatic OCR/click worker.
bool trigger_native_translate(DWORD pid, bool to_chinese);
bool telegram_chat_rect(DWORD pid, RECT& rect);

// Copies a client-space region of `target` as BGRA pixels (top-down rows),
// rendered with PrintWindow. Because it renders the window's own surface, a
// window lying on top of Telegram can never be read - that is what produced the
// "TeleVip | 礼物 | ..." OCR text that turned unrelated rows into fake translate
// bars. Returns false when the window refuses to render itself; callers treat
// that as "no evidence" rather than falling back to a desktop grab.
bool capture_client_region(HWND target, RECT region, std::vector<uint8_t>& pixels,
                           int& width, int& height);

// Keeps native translation resident: watches the active Telegram chat and
// presses the native translate bar as soon as it appears.
class NativeTranslateAgent {
public:
    ~NativeTranslateAgent();

    void set_target(DWORD pid);
    void set_enabled(bool enabled);
    bool enabled() const { return enabled_.load(); }
    void set_target_language(bool chinese) { chinese_.store(chinese); }
    void trigger_now();
    uint64_t clicks() const { return clicks_.load(); }
    bool available() const { return available_.load(); }

private:
    void start();
    void stop();
    void loop();
    bool click_bar(DWORD pid, bool force);

    std::atomic<DWORD> pid_{0};
    std::atomic<bool> enabled_{true};
    std::atomic<bool> chinese_{true};
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> available_{false};

    // All recognition and input run on worker_, including explicit requests.
    // The latch itself is process-wide (shared_translate_guard) so the hotkey,
    // the tool page and the overlay button can never click a second time behind
    // the agent's back.
    std::atomic<bool> trigger_requested_{false};
    DWORD guard_pid_ = 0; // Only accessed by worker_.
    int probe_state_ = -1;
    size_t probe_identity_ = 0;
    std::atomic<uint64_t> clicks_{0};
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace tv
