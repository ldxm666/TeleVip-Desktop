#pragma once
#include "json_text.h"
#include <array>
#include <string>
#include <optional>
#include <algorithm>

namespace tv::news {
inline constexpr char kVersion[] = "2026.11.11.1";
inline std::optional<std::array<unsigned, 4>> version_parts(const std::string& value) {
    std::array<unsigned, 4> parts{}; size_t pos = 0;
    for (unsigned i = 0; i < 4; ++i) {
        size_t digits = 0;
        while (pos < value.size() && value[pos] >= '0' && value[pos] <= '9') {
            if (++digits > 4) return {}; parts[i] = parts[i] * 10 + value[pos++] - '0';
        }
        if (!digits || (i < 3 && (pos >= value.size() || value[pos++] != '.'))) return {};
    }
    if (pos != value.size() || parts[0] < 2026 || parts[1] < 1 || parts[1] > 12 || parts[2] < 1 || parts[2] > 31) return {};
    return parts;
}
inline bool official_release(const std::string& url) {
    const std::string prefix = "https://github.com/ldxm666/TeleVip-Desktop/releases/";
    if (url == prefix + "latest") return true;
    return url.starts_with(prefix + "tag/v") && version_parts(url.substr(prefix.size() + 5)).has_value();
}
struct Model {
    std::string revision, published_at, version, update_url, announcement_id;
    std::string title_zh, title_en, body_zh, body_en, notes_zh, notes_en;
    bool has_update() const {
        auto remote = version_parts(version), local = version_parts(kVersion);
        return remote && local && *remote > *local && official_release(update_url);
    }
    bool has_announcement() const { return !announcement_id.empty() && (!body_zh.empty() || !body_en.empty()); }
};
inline std::optional<Model> parse(const std::string& text) {
    if (text.size() > 12000 || json_string_member(text, "schema") != "televip-news-1") return {};
    Model m;
    m.revision = json_string_member(text, "revision"); m.published_at = json_string_member(text, "published_at");
    const auto digits = [](const std::string& x, size_t n) {
        return x.size() == n && std::all_of(x.begin(), x.end(), [](char c) { return c >= '0' && c <= '9'; });
    };
    if (!digits(m.revision, 13) || !digits(m.published_at, 10)) return {};
    m.version = json_string_member(text, "version"); m.update_url = json_string_member(text, "update_url");
    if ((!m.version.empty() && !version_parts(m.version)) || (m.version.empty() != m.update_url.empty())
        || (!m.update_url.empty() && !official_release(m.update_url))) return {};
    m.announcement_id = json_string_member(text, "announcement_id");
    if (m.announcement_id.size() > 80 || !std::all_of(m.announcement_id.begin(), m.announcement_id.end(),
        [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'; })) return {};
    m.title_zh = json_string_member(text, "title_zh"); m.title_en = json_string_member(text, "title_en");
    m.body_zh = json_string_member(text, "body_zh"); m.body_en = json_string_member(text, "body_en");
    m.notes_zh = json_string_member(text, "notes_zh"); m.notes_en = json_string_member(text, "notes_en");
    if (m.title_zh.size() > 720 || m.title_en.size() > 720 || m.body_zh.size() > 7200 || m.body_en.size() > 7200
        || m.notes_zh.size() > 7200 || m.notes_en.size() > 7200) return {};
    if (m.announcement_id.empty() && (!m.title_zh.empty() || !m.title_en.empty() || !m.body_zh.empty() || !m.body_en.empty())) return {};
    return m;
}
}
