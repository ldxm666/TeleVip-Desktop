#pragma once

#include "common.h"
#include "core.h"
#include "i18n.h"
#include "motion.h"
#include "license_lifecycle.h"
#include "native_translate.h"
#include "overlay.h"
#include "render.h"
#include "support.h"
#include "theme.h"
#include "tools.h"
#include "archive_loader.h"
#include "news.h"
#include "telegram_link.h"
#include <future>

namespace tv {

enum class PageId : int {
    Ghost = 0,
    Network,
    Stories,
    Account,
    Messages,
    Tools,
    Advanced,
    Roadmap,
    Support,
    Count,
};

enum class StatusKind {
    Idle,
    Selected,
    Injecting,
    Injected,
    ExistingHook,
    SignatureMissing,
    HookEnableFailed,
    InitNoResponse,
    ProcessExited,
    NoProcess,
    Started,
    Scanning,
    Failed,
};

enum class ButtonKind {
    Primary,
    Secondary,
    Ghost,
    Soft,
};

struct PageDef {
    const wchar_t* key;
    const wchar_t* tab_key;
    Icon icon;
    const wchar_t* title_key;
    const wchar_t* sub_key;
    bool workspace;
};

struct FeatureDef {
    const wchar_t* title_key;
    const wchar_t* desc_key;
    uint32_t bit;
};

struct HitTarget {
    std::wstring id;
    std::wstring hover;
    Rect rect;
    LPCWSTR cursor = IDC_ARROW;
    bool titlebar = false;
};

struct ToastState {
    std::wstring text;
    Color color;
    double born = 0.0;
    bool visible = false;
    bool leaving = false;
};

struct AsyncText {
    bool ok = false;
    std::wstring message;
};

class App {
public:
    explicit App(HINSTANCE instance);
    ~App();

    bool create();
    HWND hwnd() const { return hwnd_; }
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void on_message(UINT msg, WPARAM wparam, LPARAM lparam, LRESULT& result, bool& handled);

    void convert_calendar();
    void start_translation();
    void copy_translation();
    void read_clipboard();
    void toggle_log();
    void show_toast(const std::wstring& text, const Color& color);
    void invalidate();

private:
    struct TranslateContext {
        std::wstring text;
        bool to_chinese = true;
        HWND hwnd = nullptr;
    };

    struct LocalContext {
        bool ok = true;
        std::vector<std::wstring> messages;
    };

    void set_theme();
    void set_language(Lang lang);
    void apply_flags(uint32_t flags, bool persist);
    void sync_vault();
    void save_settings_soon();
    void save_settings_now();
    void refresh_processes();
    void select_process(int index);
    void ensure_process_valid();
    void inject();
    void start_local_patch(DWORD pid, uint32_t flags);
    void open_folder(const std::wstring& path);
    void refresh_archive();
    void read_logs(bool force = false);
    void tick();
    void layout_and_paint();
    void layout_titlebar();
    void layout_page();
    void layout_log();
    void layout_combo();
    void layout_toast();
    void draw_titlebar();
    void draw_sidebar();
    void draw_page_header();
    void draw_page();
    void draw_log();
    void draw_combo();
    void draw_toast();
    void draw_feature_card(const Rect& rect, const FeatureDef& feature, const std::wstring& key,
                           float hover, float toggle);
    void draw_switch(const Rect& rect, uint32_t bit, float hover, const std::wstring& parent_hover);
    void draw_button(const std::wstring& id, const Rect& rect, const std::wstring& label,
                     ButtonKind kind, Icon icon = Icon::Info, bool enabled = true);
    void draw_card_shell(const Rect& rect, float hover, bool accent_on);
    void draw_badge(const Rect& rect, const std::wstring& text, const Color& fg, const Color& bg);
    void draw_input_shell(const Rect& rect, bool focused);
    void draw_nav();
    void draw_status_card();
    void draw_connection_card();
    void draw_preset_row(float y);
    void draw_archive(float y, float width);
    void draw_tools(float y, float width);
    void draw_roadmap(float y, float width);
    void draw_accent_picker(float y, float width);
    void draw_support(float y, float width);
    float draw_news(float y, float width);
    void open_community_async();
    void poll_news_and_links();
    void draw_sidebar_footer();
    void create_support_controls();
    void position_support_controls();
    bool handle_support_command(int id);
    bool paint_support_button(const DRAWITEMSTRUCT& item);
    static LRESULT CALLBACK support_button_subclass(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                                    UINT_PTR id, DWORD_PTR ref);

    void add_hit(const std::wstring& id, const Rect& rect, LPCWSTR cursor = IDC_ARROW,
                 const std::wstring& hover = {}, bool titlebar = false);
    void add_title_hit(const std::wstring& id, const Rect& rect, LPCWSTR cursor = IDC_HAND);
    HitTarget* hit_test(float x, float y);
    void update_hover(float x, float y);
    void handle_click(const HitTarget& hit);
    void scroll_by(float amount);
    void switch_page(int page);
    void cycle_page(int delta);
    void set_status(StatusKind kind, DWORD pid = 0, const std::wstring& name = {});
    std::wstring status_text() const;
    Color status_color() const;
    // Resolves the toggle plus the hook's report into kPremiumOff/Waiting/Hooked.
    uint32_t premium_effective_state() const;
    std::wstring premium_status_text() const;
    std::wstring page_title(PageId page) const;
    std::wstring page_subtitle(PageId page) const;
    std::wstring feature_title(const FeatureDef& feature) const;
    std::wstring feature_desc(const FeatureDef& feature) const;
    float card_width() const;
    Rect next_card(float x, float y, float width, float height = 92.0f);
    bool page_is(PageId page) const { return page_ == static_cast<int>(page); }

    void create_controls();
    void update_control_font();
    void update_control_colors();
    void position_controls();
    void hide_controls();
    void set_edit_text(HWND edit, const std::wstring& text);
    std::wstring get_edit_text(HWND edit) const;
    void focus_editor(HWND edit);
    static LRESULT CALLBACK edit_subclass(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                          UINT_PTR id, DWORD_PTR ref);
    void draw_translation_editor(const Rect& box, bool focused);
    void handle_editor_char(wchar_t ch);
    void handle_editor_key(WPARAM key, bool ctrl, bool shift);

    void handle_inject_done(InjectionResult* result);
    void handle_local_done(LocalContext* context);
    void handle_translate_done(TranslateContext* context, TranslateResult* result);

    static std::vector<FeatureDef> ghost_features();
    static std::vector<FeatureDef> network_features();
    static std::vector<FeatureDef> story_features();
    static std::vector<FeatureDef> account_features();
    static std::vector<FeatureDef> message_features();
    static std::vector<FeatureDef> advanced_features();

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;
    Renderer renderer_;
    I18n i18n_;
    Palette palette_;
    // 主题切换过渡：保留起始配色，每帧插值到目标。
    Palette theme_from_{};
    float theme_t_ = 1.0f;
    void advance_theme(float dt);
    void update_motion_preferences();
    void finish_motion();
    Metrics metrics_;
    ConfigMap config_;
    CacheVault vault_;
    ChatTranslator translator_;
    TranslationServer translate_server_;
    NativeTranslateAgent native_agent_;
    Settings settings_;
    ArchiveLoader archive_loader_{[](size_t offset) { return load_archive(5, offset); }};
    bool preview_ = false;
    license::SuspendedFeatures license_features_;
    bool reduce_motion_ = false;
    float page_opacity_ = 1.0f;
    float page_shift_ = 0.0f;

    struct SupportButton {
        int id = 0;
        HWND hwnd = nullptr;
        const wchar_t* key = L"";
        std::wstring label;
        Rect rect;
        Rect last_rect;
        bool primary = false;
        bool visible = false;
        motion::Spring hover;
        motion::Spring press;
        BYTE alpha = 255;
    };
    SupportButton support_buttons_[6];
    HFONT support_font_ = nullptr;

    std::vector<PageDef> pages_;
    int page_ = static_cast<int>(PageId::Ghost);
    int previous_page_ = -1;
    motion::Tween page_anim_;
    float page_offsets_[static_cast<int>(PageId::Count)] = {};
    bool nav_ready_ = false;
    motion::Spring nav_pill_;
    Rect nav_rects_[static_cast<int>(PageId::Count)] = {};

    std::vector<HitTarget> hits_;
    std::vector<HitTarget> title_hits_;
    std::wstring hover_id_;
    std::wstring pressed_id_;
    std::unordered_map<std::wstring, motion::Spring> hover_anim_;
    std::unordered_map<std::wstring, motion::Spring> press_anim_;
    std::unordered_map<uint32_t, motion::Spring> toggle_anim_;
    motion::Tween combo_anim_{motion::kFast};
    int combo_hover_ = -1;
    bool combo_open_ = false;
    Rect combo_rect_;
    Rect combo_popover_rect_;
    std::vector<Rect> combo_items_;

    float scale_ = 1.0f;
    float dpi_ = 96.0f;
    float client_w_ = 1260.0f;
    float client_h_ = 860.0f;
    float scroll_ = 0.0f;
    float scroll_target_ = 0.0f;
    float scroll_max_ = 0.0f;
    float page_content_height_ = 0.0f;
    Rect viewport_;
    bool cursor_tracking_ = false;

    std::vector<ProcessEntry> processes_;
    int selected_index_ = -1;
    DWORD selected_pid_ = 0;
    bool injecting_ = false;
    uint32_t flags_ = 0;
    uint32_t hook_state_ = 0;
    uint32_t premium_state_ = 0; // last premium_state read from the shared mapping
    bool hook_active_ = false;
    double last_phone_refresh_ = 0.0;
    double last_log_read_ = 0.0;
    double last_archive_refresh_ = 0.0;
    size_t archive_offset_ = 0;
    double last_save_ = 0.0;
    double last_follow_ = 0.0;
    double last_tick_time_ = 0.0;
    bool settings_dirty_ = false;

    StatusKind status_ = StatusKind::Idle;
    DWORD status_pid_ = 0;
    std::wstring status_name_;
    std::wstring inject_detail_;
    std::wstring hook_detail_;
    double status_phase_ = 0.0;
    double hits_display_ = 0.0;
    uint64_t last_hits_shown_ = UINT64_MAX;

    std::vector<std::wstring> log_lines_;
    bool log_collapsed_ = false;

    std::vector<ArchiveRow> archive_;
    CalendarResult calendar_;
    bool translating_ = false;
    bool translate_to_chinese_ = true;
    bool native_configured_ = false;
    ToastState toast_;
    motion::Spring toast_slide_;
    double toast_time_ = 0.0;

    HWND date_edit_ = nullptr;
    HFONT edit_font_ = nullptr;
    HBRUSH edit_brush_ = nullptr;
    Rect date_shell_;
    Rect trans_shell_;
    Rect output_shell_;
    Rect date_edit_rect_;
    std::wstring trans_placeholder_;
    std::wstring trans_text_;
    std::wstring translation_output_;
    size_t trans_caret_ = 0;
    bool trans_focused_ = false;
    bool date_visible_ = false;
    bool trans_visible_ = false;
    bool output_visible_ = false;
    bool last_date_visible_ = false;
    bool positioning_controls_ = false;
    Rect last_date_edit_rect_;
    news::Client news_client_;
    news::Model news_;
    bool news_checking_ = false;
    std::wstring news_status_;
    std::future<std::wstring> link_task_;
    double caret_phase_ = 0.0;
};

} // namespace tv
