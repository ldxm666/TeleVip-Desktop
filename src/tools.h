#pragma once

#include "common.h"

namespace tv {

struct CalendarResult {
    bool ok = false;
    std::wstring gregorian;
    std::wstring hijri;
    std::wstring persian;
    std::wstring error;
};

CalendarResult convert_calendar(const std::wstring& iso_date, bool chinese);
std::wstring today_iso();

struct TranslateResult {
    bool ok = false;
    std::wstring text;
    std::wstring error;
};

// Cache counters for the Tools card: how much of the work never touched the
// network on this run.
struct TranslateStats {
    uint64_t memory_hits = 0;
    uint64_t disk_hits = 0;
    uint64_t network = 0;
    uint64_t skipped = 0;
    size_t memory_entries = 0;
    size_t disk_entries = 0;
};

TranslateResult translate_text(const std::wstring& text, bool to_chinese);
TranslateResult translate_text_to(const std::wstring& text, const std::wstring& target,
                                  const std::wstring& source = L"auto");
// 翻译源：Auto 先跑上次成功的那条，失败自动换另一条。
enum class TranslateProvider { Auto = 0, Google = 1, Fallback = 2 };
void set_translate_provider(TranslateProvider provider);
TranslateProvider translate_provider();
TranslateStats translate_stats();
// Opens this thread's WinHTTP session + TLS connection ahead of first use.
void translate_warm_connection();
// Warms `connections` pooled upstream connections in parallel.
void translate_prewarm(int connections);
// 启动时各试一次，把 Auto 模式指向当前真正能跑通的那条源。
void translate_probe_upstream();
std::wstring url_encode_utf8(std::wstring_view text);

struct ArchiveRow {
    bool edit = false;
    int64_t ts = 0;
    int64_t peer = 0;
    int msg = 0;
    int date = 0;
    int64_t from = 0;
    int64_t ordinal = 0;
    bool durable = false;
    std::wstring source;
    std::wstring old_text;
    std::wstring new_text;
    std::wstring text;
};

std::vector<ArchiveRow> load_archive(size_t limit = 200, size_t offset = 0);

} // namespace tv
