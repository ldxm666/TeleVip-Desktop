#pragma once

#include "common.h"

namespace tv {

// One OCR'd text line with its bounding box in screen coordinates.
struct OcrLineHit {
    std::wstring text;
    RECT rect{};
};

// Captures `region` (a screen rectangle clamped to `target`'s client area), runs
// Windows OCR over it and returns the recognised lines. Qt builds of Telegram
// expose no accessibility tree at all, so pixel OCR is the only way to tell
// whether a control — notably the translate bar — is actually on screen.
bool ocr_region_lines(HWND target, RECT region, std::vector<OcrLineHit>& lines,
                      std::wstring& error);

class ChatTranslator {
public:
    ChatTranslator();
    ~ChatTranslator();

    bool initialize(HWND owner);
    void shutdown();
    void set_enabled(bool enabled);
    bool enabled() const { return enabled_.load(); }
    void set_target_pid(DWORD pid) { target_pid_ = pid; }
    void set_target_language(bool chinese) { chinese_ = chinese; }
    bool target_is_chinese() const { return chinese_; }
    void translate_now();
    void toggle_overlay();
    void follow_target();

private:
    struct Line {
        std::wstring text;
        RECT rect{};
    };
    struct Payload {
        RECT target{};
        int width = 0;
        int height = 0;
        bool ok = false;
        std::wstring error;
        std::vector<Line> lines;
        std::vector<std::wstring> translations;
        float luminance = 1.0f;
    };
    struct Job {
        HWND target = nullptr;
        std::vector<uint8_t> pixels;
        int width = 0;
        int height = 0;
        RECT screen_rect{};
        float luminance = 1.0f;
        bool chinese = true;
        uint64_t hash = 0;
    };

    static LRESULT CALLBACK message_proc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK button_proc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK overlay_proc(HWND, UINT, WPARAM, LPARAM);

    bool ensure_windows();
    HWND find_target() const;
    void update_button();
    void start_worker();
    void stop_worker();
    void ocr_loop();
    void handle_payload(Payload* payload);
    void hide_overlay();
    void paint_button(HWND hwnd);
    void render_overlay(Payload* payload);

    HWND owner_ = nullptr;
    HWND message_ = nullptr;
    HWND button_ = nullptr;
    HWND overlay_ = nullptr;
    DWORD target_pid_ = 0;
    HWND target_ = nullptr;
    RECT target_rect_{};
    std::atomic<bool> enabled_{true};
    std::atomic<bool> busy_{false};
    std::atomic<bool> stop_{false};
    std::atomic<bool> chinese_{true};
    bool overlay_visible_ = false;
    std::thread ocr_thread_;
    std::mutex job_mutex_;
    std::condition_variable job_cv_;
    std::unique_ptr<Job> pending_job_;
    std::atomic<bool> worker_stop_{false};
    uint64_t last_hash_ = 0;
    RECT last_rect_{};
    int last_width_ = 0;
    int last_height_ = 0;
    float last_luminance_ = 1.0f;
    std::vector<Line> last_lines_;
    std::vector<std::wstring> last_translations_;
    ULONG_PTR gdiplus_token_ = 0;
};

} // namespace tv
