#include "app.h"

namespace tv {
void App::open_community_async() {
    if (link_task_.valid()) return;
    link_task_ = std::async(std::launch::async, [] {
        std::wstring error;
        telegram_link::forward_bound(kCommunityUrl, error);
        return error;
    });
}
void App::poll_news_and_links() {
    if (link_task_.valid() && link_task_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            const auto error = link_task_.get();
            if (!error.empty()) show_toast(error, palette_.warning);
        } catch (...) {
            show_toast(i18n_.is_zh() ? L"Telegram 暂未响应，请稍后重试" : L"Telegram is not responding; try again later", palette_.warning);
        }
    }
    if (auto result = news_client_.poll()) {
        news_checking_ = false;
        news_status_ = result->ok ? (i18n_.is_zh() ? L"已连接公告服务" : L"Connected to news service")
            : (i18n_.is_zh() ? L"暂时无法检查，稍后自动重试" : L"Check unavailable; will retry automatically");
        if (result->model) {
            const bool changed = result->model->revision > news_.revision;
            news_ = std::move(*result->model);
            if (changed && news_.revision > news_client_.acknowledged() && (news_.has_update() || news_.has_announcement())) {
                show_toast(i18n_.is_zh() ? L"收到版本或公告消息，点击右上角查看" : L"New update or announcement · use the badge at the top right", palette_.success);
            } else if (result->manual) {
                show_toast(news_.has_update() ? (i18n_.is_zh() ? L"发现新版本" : L"A new version is available")
                    : (i18n_.is_zh() ? L"当前已是最新版本" : L"You are up to date"), palette_.success);
            }
        } else if (result->manual) show_toast(news_status_, palette_.warning);
        invalidate();
    }
}
float App::draw_news(float y, float width) {
    const float x = viewport_.left;
    const bool zh = i18n_.is_zh();
    const auto localized = [zh](const std::string& cn, const std::string& en) {
        return utf8_to_wide(zh ? (cn.empty() ? en : cn) : (en.empty() ? cn : en));
    };
    TextStyle heading; heading.size = 15; heading.weight = FontWeight::SemiBold; heading.ellipsis = true;
    TextStyle body; body.size = 11.5f; body.wrap = true; body.line_spacing = 1.2f;
    TextStyle caption; caption.size = 10; caption.ellipsis = true;
    const auto title = localized(news_.title_zh, news_.title_en);
    const auto announcement = localized(news_.body_zh, news_.body_en);
    const auto notes = localized(news_.notes_zh, news_.notes_en);
    const float content_width = std::max(160.0f, width - 40);
    const auto height = [&](const std::wstring& text) { return text.empty() ? 0.0f : renderer_.measure(text, body, content_width).height + 14; };
    const float ann_h = height(announcement), notes_h = height(notes);
    const bool has_ann = news_.has_announcement();
    const bool compact = width < 480 && has_ann && news_.revision > news_client_.acknowledged();
    const float total = 160 + (compact ? 40 : 0) + (has_ann ? 32 + ann_h : 0) + (!notes.empty() ? 32 + notes_h : 0);
    const Rect card{x, y, x + width, y + total};
    draw_card_shell(card, 0, news_.has_update());
    renderer_.icon(Icon::Info, Rect{x + 20, y + 19, x + 42, y + 41}, palette_.accent, 1.6f);
    renderer_.text(zh ? L"版本与公告" : L"Updates & announcements", Rect{x + 52, y + 17, card.right - 18, y + 42}, heading, palette_.text);
    const auto version = (zh ? L"当前版本 v" : L"Installed v") + utf8_to_wide(news::kVersion)
        + (news_.has_update() ? (zh ? L" · 新版本 v" : L" · Available v") + utf8_to_wide(news_.version) : L"");
    renderer_.text(version, Rect{x + 20, y + 51, card.right - 20, y + 71}, caption, palette_.text_secondary);
    renderer_.text(news_status_.empty() ? (zh ? L"后台检查 · 不影响授权与消息功能" : L"Background checks · independent of licensing and messages") : news_status_,
        Rect{x + 20, y + 76, card.right - 20, y + 95}, caption, palette_.text_tertiary);
    float cursor = y + 108;
    draw_button(L"news:check", Rect{x + 20, cursor, x + 148, cursor + 32},
        news_checking_ ? (zh ? L"正在检查…" : L"Checking…") : (zh ? L"检查更新与公告" : L"Check for news"), ButtonKind::Secondary, Icon::Refresh, !news_checking_);
    draw_button(L"news:update", Rect{x + 160, cursor, x + 280, cursor + 32},
        news_.has_update() ? (zh ? L"获取新版本" : L"Get update") : (zh ? L"正式版下载页" : L"Stable releases"), ButtonKind::Soft, Icon::Folder);
    cursor += 50 + (compact ? 40 : 0);
    if (has_ann) {
        renderer_.text(title.empty() ? (zh ? L"服务器公告" : L"Announcement") : title,
            Rect{x + 20, cursor, card.right - 20, cursor + 26}, heading, palette_.text);
        cursor += 32;
        renderer_.text(announcement, Rect{x + 20, cursor, card.right - 20, cursor + ann_h}, body, palette_.text_secondary);
        cursor += ann_h;
    }
    if (!notes.empty()) {
        renderer_.text(zh ? L"版本说明" : L"Release notes", Rect{x + 20, cursor, card.right - 20, cursor + 26}, heading, palette_.text);
        cursor += 32;
        renderer_.text(notes, Rect{x + 20, cursor, card.right - 20, cursor + notes_h}, body, palette_.text_secondary);
    }
    // Reading is explicit: merely opening the page does not dismiss a notification.
    if (has_ann && news_.revision > news_client_.acknowledged()) {
        const float read_y = compact ? y + 148 : y + 105;
        draw_button(L"news:read", Rect{card.right - 116, read_y, card.right - 20, read_y + 32},
            zh ? L"标为已读" : L"Mark read", ButtonKind::Secondary, Icon::Check);
    }
    return total;
}
}
