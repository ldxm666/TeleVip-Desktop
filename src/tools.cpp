#include "tools.h"
#include "core.h"
#include "translation_request.h"
#include "json_text.h"

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

namespace tv {
namespace {

int64_t gregorian_to_jdn(int year, int month, int day) {
    const int a = (14 - month) / 12;
    const int y = year + 4800 - a;
    const int m = month + 12 * a - 3;
    return day + (153 * m + 2) / 5 + 365ll * y + y / 4 - y / 100 + y / 400 - 32045;
}

void jdn_to_hijri(int64_t jdn, int& year, int& month, int& day) {
    int64_t value = jdn - 1948440 + 10632;
    const int64_t n = (value - 1) / 10631;
    value = value - 10631 * n + 354;
    const int64_t j = ((10985 - value) / 5316) * ((50 * value) / 17719) +
                      (value / 5670) * ((43 * value) / 15238);
    value = value - ((30 - j) / 15) * ((17719 * j) / 50) -
            (j / 16) * ((15238 * j) / 43) + 29;
    month = static_cast<int>((24 * value) / 709);
    day = static_cast<int>(value - (709 * month) / 24);
    year = static_cast<int>(30 * n + j - 30);
}

void gregorian_to_persian(int gy_in, int gm_in, int gd_in, int& py, int& pm, int& pd) {
    int gy = gy_in - 1600;
    const int gm = gm_in - 1;
    const int gd = gd_in - 1;
    static const int month_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int64_t day_no = 365ll * gy + (gy + 3) / 4 - (gy + 99) / 100 + (gy + 399) / 400;
    day_no += month_days[gm] + gd;
    if (gm_in > 2 && ((gy_in % 4 == 0 && gy_in % 100 != 0) || gy_in % 400 == 0)) ++day_no;
    day_no -= 79;
    const int64_t cycles = day_no / 12053;
    day_no %= 12053;
    py = 979 + 33 * cycles + 4 * (day_no / 1461);
    day_no %= 1461;
    if (day_no >= 366) {
        py += (day_no - 1) / 365;
        day_no = (day_no - 1) % 365;
    }
    if (day_no < 186) {
        pm = 1 + static_cast<int>(day_no / 31);
        pd = 1 + static_cast<int>(day_no % 31);
    } else {
        pm = 7 + static_cast<int>((day_no - 186) / 30);
        pd = 1 + static_cast<int>((day_no - 186) % 30);
    }
}

bool is_leap(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int days_in_month(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap(year)) return 29;
    if (month < 1 || month > 12) return 0;
    return days[month - 1];
}

bool parse_iso(const std::wstring& text, int& year, int& month, int& day) {
    if (text.size() != 10 || text[4] != L'-' || text[7] != L'-') return false;
    try {
        year = std::stoi(text.substr(0, 4));
        month = std::stoi(text.substr(5, 2));
        day = std::stoi(text.substr(8, 2));
    } catch (...) {
        return false;
    }
    return year >= 1900 && year <= 2200 && month >= 1 && month <= 12 &&
           day >= 1 && day <= days_in_month(year, month);
}

std::string json_string(const std::string& line, const std::string& key) {
    return json_string_member(line, key);
}

int64_t json_int(const std::string& line, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = line.find(needle);
    if (pos == std::string::npos) return 0;
    pos = line.find(':', pos + needle.size());
    if (pos == std::string::npos) return 0;
    ++pos;
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) ++pos;
    const size_t end = line.find_first_of(",}\r\n", pos);
    try {
        return std::stoll(line.substr(pos, end - pos));
    } catch (...) {
        return 0;
    }
}

std::vector<std::string> read_tail_lines(const std::wstring& path, size_t limit) {
    std::vector<std::string> lines;
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) return lines;
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    const std::streamoff start = std::max<std::streamoff>(0, size - 8 * 1024 * 1024);
    file.seekg(start, std::ios::beg);
    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (start > 0) {
        const size_t newline = data.find('\n');
        if (newline != std::string::npos) data.erase(0, newline + 1);
    }
    size_t pos = 0;
    while (pos < data.size()) {
        const size_t end = data.find('\n', pos);
        std::string line = data.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(std::move(line));
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    if (lines.size() > limit) lines.erase(lines.begin(), lines.end() - static_cast<std::ptrdiff_t>(limit));
    return lines;
}

std::string json_unescape(const std::string& text) {
    const std::string quoted = "\"" + text + "\"";
    size_t pos = 0;
    return read_json_string(quoted, pos).value_or(std::string());
}

// A WinHTTP connection handle is a keep-alive pool key, not a live socket.
// Sharing a *small* set of them matters more than anything else here: each new
// connection makes the local proxy open a fresh CONNECT tunnel and pay a full
// upstream TLS handshake, which measured ~500 ms per message. A thread-local
// connection per worker meant a chat switch opened a dozen tunnels at once.
constexpr int kUpstreamConnections = 24;

std::once_flag g_session_once;
HINTERNET g_session = nullptr;

HINTERNET shared_session() {
    std::call_once(g_session_once, [] {
        // Resolve the proxy *before* WinHttpOpen and pass it to the open call.
        // Setting WINHTTP_OPTION_PROXY afterwards left WinHTTP in a mode where
        // it re-negotiated the CONNECT tunnel + TLS handshake on every request
        // (~400 ms each through the local proxy measured on this box).
        std::wstring proxy;
        std::wstring bypass;
        std::wstring pac;
        WINHTTP_CURRENT_USER_IE_PROXY_CONFIG proxy_config{};
        if (WinHttpGetIEProxyConfigForCurrentUser(&proxy_config)) {
            if (proxy_config.lpszProxy) proxy = proxy_config.lpszProxy;
            if (proxy_config.lpszProxyBypass) bypass = proxy_config.lpszProxyBypass;
            if (proxy_config.lpszAutoConfigUrl) pac = proxy_config.lpszAutoConfigUrl;
            if (proxy_config.lpszProxy) GlobalFree(proxy_config.lpszProxy);
            if (proxy_config.lpszProxyBypass) GlobalFree(proxy_config.lpszProxyBypass);
            if (proxy_config.lpszAutoConfigUrl) GlobalFree(proxy_config.lpszAutoConfigUrl);
        }
        g_session = WinHttpOpen(
            L"TeleVipNative/1.0",
            proxy.empty() ? WINHTTP_ACCESS_TYPE_DEFAULT_PROXY : WINHTTP_ACCESS_TYPE_NAMED_PROXY,
            proxy.empty() ? WINHTTP_NO_PROXY_NAME : proxy.c_str(),
            (bypass.empty() ? WINHTTP_NO_PROXY_BYPASS : bypass.c_str()), 0);
        if (!g_session) return;
        WinHttpSetTimeouts(g_session, 4000, 4000, 8000, 8000);
        if (!pac.empty()) {
            WINHTTP_AUTOPROXY_OPTIONS auto_options{};
            auto_options.dwFlags = WINHTTP_AUTOPROXY_CONFIG_URL;
            auto_options.lpszAutoConfigUrl = pac.c_str();
            auto_options.fAutoLogonIfChallenged = TRUE;
            WINHTTP_PROXY_INFO proxy_info{};
            if (WinHttpGetProxyForUrl(g_session, L"https://translate.googleapis.com/",
                                      &auto_options, &proxy_info)) {
                WinHttpSetOption(g_session, WINHTTP_OPTION_PROXY, &proxy_info,
                                 sizeof(proxy_info));
                if (proxy_info.lpszProxy) GlobalFree(proxy_info.lpszProxy);
                if (proxy_info.lpszProxyBypass) GlobalFree(proxy_info.lpszProxyBypass);
            }
        }
        DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_ALL;
        WinHttpSetOption(g_session, WINHTTP_OPTION_DECOMPRESSION, &decompress,
                         sizeof(decompress));
    });
    return g_session;
}

// Fixed-size pool of keep-alive connections. Callers that cannot get a slot
// wait instead of opening a new tunnel, which is what keeps the proxy happy.
class UpstreamPool {
public:
    explicit UpstreamPool(std::wstring host) : host_(std::move(host)) {}

    HINTERNET acquire() {
        std::unique_lock lock(mutex_);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        for (;;) {
            if (!idle_.empty()) {
                HINTERNET handle = idle_.back();
                idle_.pop_back();
                return handle;
            }
            if (created_ < kUpstreamConnections) {
                ++created_;
                lock.unlock();
                HINTERNET handle = WinHttpConnect(shared_session(), host_.c_str(),
                                                  INTERNET_DEFAULT_HTTPS_PORT, 0);
                if (handle) return handle;
                lock.lock();
                --created_;
                ready_.notify_one();
                return nullptr;
            }
            if (ready_.wait_until(lock, deadline) == std::cv_status::timeout) return nullptr;
        }
    }

    void release(HINTERNET handle, bool healthy) {
        if (!handle) return;
        std::lock_guard lock(mutex_);
        if (healthy) {
            idle_.push_back(handle);
        } else {
            WinHttpCloseHandle(handle);
            --created_;
        }
        ready_.notify_one();
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::wstring host_;
    std::vector<HINTERNET> idle_;
    int created_ = 0;
};

UpstreamPool& google_pool() {
    static UpstreamPool pool(L"translate.googleapis.com");
    return pool;
}

// Edge 免费翻译端点：国内通常可直连，Google 则必须绕代理。
UpstreamPool& memory_pool() {
    static UpstreamPool pool(L"api.mymemory.translated.net");
    return pool;
}

// 通用请求：主机由连接池带入，方法 / 额外头 / body 可选。
bool http_request(UpstreamPool& pool, const wchar_t* method, LPCWSTR headers,
                  const std::string& payload, const std::wstring& path, std::string& body,
                  std::wstring& error) {
    const auto started = std::chrono::steady_clock::now();
    if (!shared_session()) {
        error = format_w(L"WinHttpOpen failed (%lu)", GetLastError());
        return false;
    }
    HINTERNET connection = pool.acquire();
    const auto acquired = std::chrono::steady_clock::now();
    if (!connection) {
        error = L"no upstream connection available";
        return false;
    }
    HINTERNET request = WinHttpOpenRequest(connection, method, path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!request) {
        error = format_w(L"WinHttpOpenRequest failed (%lu)", GetLastError());
        pool.release(connection, false);
        return false;
    }
    const auto open_request = std::chrono::steady_clock::now();
    bool ok = false;
    long long send_ms = 0;
    long long recv_ms = 0;
    do {
        const auto t_send = std::chrono::steady_clock::now();
        const DWORD payload_size = static_cast<DWORD>(payload.size());
        if (!WinHttpSendRequest(request, headers ? headers : WINHTTP_NO_ADDITIONAL_HEADERS,
                                headers ? static_cast<DWORD>(-1L) : 0,
                                payload.empty() ? WINHTTP_NO_REQUEST_DATA
                                                : const_cast<char*>(payload.data()),
                                payload_size, payload_size, 0)) {
            error = format_w(L"WinHttpSendRequest failed (%lu)", GetLastError());
            break;
        }
        const auto t_sent = std::chrono::steady_clock::now();
        send_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_sent - t_send).count();
        if (!WinHttpReceiveResponse(request, nullptr)) {
            error = format_w(L"WinHttpReceiveResponse failed (%lu)", GetLastError());
            break;
        }
        const auto t_recv = std::chrono::steady_clock::now();
        recv_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_recv - t_sent).count();
        DWORD status = 0;
        DWORD status_size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX);
        if (status < 200 || status >= 300) {
            error = format_w(L"HTTP status %lu", status);
            break;
        }
        DWORD available = 0;
        while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0) break;
            chunk.resize(read);
            body += chunk;
            if (body.size() > 2 * 1024 * 1024) break;
        }
        ok = !body.empty();
        if (!ok) error = L"Empty response";
    } while (false);
    WinHttpCloseHandle(request);
    // Return the pool slot either way; an unhealthy one is disposed so the next
    // request re-handshakes instead of reusing a broken tunnel.
    pool.release(connection, ok);
    const auto done = std::chrono::steady_clock::now();
    const auto ms = [](auto a, auto b) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
    };
    const auto total = ms(started, done);
    if (total > 200) {
        debug_log(L"upstream slow " + std::to_wstring(total) + L"ms (pool " +
                  std::to_wstring(ms(started, acquired)) + L", send " +
                  std::to_wstring(send_ms) + L", recv " + std::to_wstring(recv_ms) +
                  L", read " + std::to_wstring(ms(open_request, done) - send_ms - recv_ms) +
                  L")" + (ok ? L"" : L" failed"));
    }
    return ok;
}

std::string parse_translation(const std::string& json) {
    size_t pos = json.find("[[");
    if (pos == std::string::npos) return {};
    pos += 2;
    std::string result;
    while (pos < json.size()) {
        while (pos < json.size() && (std::isspace(static_cast<unsigned char>(json[pos])) || json[pos] == ',')) ++pos;
        if (pos >= json.size() || json[pos] == ']') break;
        if (json[pos] != '[') break;
        ++pos;
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
        if (pos >= json.size() || json[pos] != '"') break;
        ++pos;
        std::string raw;
        bool escaped = false;
        while (pos < json.size()) {
            const char ch = json[pos++];
            if (!escaped) {
                if (ch == '"') break;
                if (ch == '\\') {
                    escaped = true;
                    raw.push_back(ch);
                } else {
                    raw.push_back(ch);
                }
            } else {
                escaped = false;
                raw.push_back(ch);
                if (ch == 'u' && pos + 4 <= json.size()) {
                    raw.append(json, pos, 4);
                    pos += 4;
                }
            }
        }
        result += json_unescape(raw);
        int depth = 1;
        bool in_string = false;
        bool slash = false;
        while (pos < json.size() && depth > 0) {
            const char ch = json[pos++];
            if (in_string) {
                if (slash) slash = false;
                else if (ch == '\\') slash = true;
                else if (ch == '"') in_string = false;
            } else {
                if (ch == '"') in_string = true;
                else if (ch == '[') ++depth;
                else if (ch == ']') --depth;
            }
        }
    }
    return result;
}

} // namespace

std::wstring today_iso() {
    const time_t now = std::time(nullptr);
    tm local{};
    localtime_s(&local, &now);
    return format_w(L"%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

CalendarResult convert_calendar(const std::wstring& iso_date, bool chinese) {
    CalendarResult result;
    int year = 0, month = 0, day = 0;
    if (!parse_iso(trim(iso_date), year, month, day)) {
        result.error = L"invalid";
        return result;
    }
    const int64_t jdn = gregorian_to_jdn(year, month, day);
    int hy = 0, hm = 0, hd = 0;
    jdn_to_hijri(jdn, hy, hm, hd);
    int py = 0, pm = 0, pd = 0;
    gregorian_to_persian(year, month, day, py, pm, pd);
    static const wchar_t* weekdays_zh[] = {L"星期日", L"星期一", L"星期二", L"星期三", L"星期四", L"星期五", L"星期六"};
    static const wchar_t* weekdays_en[] = {L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday"};
    tm local{};
    local.tm_year = year - 1900;
    local.tm_mon = month - 1;
    local.tm_mday = day;
    local.tm_hour = 12;
    mktime(&local);
    const int weekday = local.tm_wday >= 0 && local.tm_wday < 7 ? local.tm_wday : 0;
    result.gregorian = format_w(L"%04d-%02d-%02d  %s", year, month, day,
                                chinese ? weekdays_zh[weekday] : weekdays_en[weekday]);
    result.hijri = format_w(L"%04d-%02d-%02d", hy, hm, hd);
    result.persian = format_w(L"%04d-%02d-%02d", py, pm, pd);
    result.ok = true;
    return result;
}

std::wstring url_encode_utf8(std::wstring_view text) {
    const std::string utf8 = wide_to_utf8(text);
    static const wchar_t* hex = L"0123456789ABCDEF";
    std::wstring out;
    out.reserve(utf8.size() * 3);
    for (unsigned char ch : utf8) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out.push_back(static_cast<wchar_t>(ch));
        } else if (ch == ' ') {
            out += L"%20";
        } else {
            out.push_back(L'%');
            out.push_back(hex[ch >> 4]);
            out.push_back(hex[ch & 0xF]);
        }
    }
    return out;
}

bool http_get(const std::wstring& path, std::string& body, std::wstring& error) {
    return http_request(google_pool(), L"GET", nullptr, {}, path, body, error);
}

namespace {

// ---- translation cache ------------------------------------------------------
// Three layers: L1 in-memory LRU, L2 a persistent disk table, plus an in-flight
// de-duplicator so a chat switch that asks for the same line twice still makes
// exactly one upstream call. Hits measure 1-3 ms; the network path is 120-400.

constexpr size_t kMemoryCacheCap = 6000;
constexpr size_t kDiskCacheCap = 40000;
constexpr uint64_t kDiskCacheBytes = 24ull * 1024 * 1024;

std::mutex g_translate_cache_mutex;
std::unordered_map<std::string, std::wstring> g_translate_cache;
std::atomic<uint64_t> g_translate_l1_hits{0};
std::atomic<uint64_t> g_translate_disk_hits{0};
std::atomic<uint64_t> g_translate_network{0};
std::atomic<uint64_t> g_translate_skipped{0};

std::string escape_field(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char ch : text) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

std::string unescape_field(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            const char next = text[++i];
            out.push_back(next == 'n' ? '\n' : (next == 't' ? '\t' : next));
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

// Append-only tab-separated table. A chat that was read once translates
// instantly on every later visit, including after a restart.
class DiskTranslateCache {
public:
    bool lookup(const std::string& key, std::wstring& out) {
        std::lock_guard lock(mutex_);
        load_locked();
        const auto i = entries_.find(key);
        if (i == entries_.end()) return false;
        out = i->second;
        return true;
    }

    void store(const std::string& key, const std::wstring& value) {
        {
            std::lock_guard lock(mutex_);
            load_locked();
            if (entries_.size() >= kDiskCacheCap) return;
            entries_[key] = value;
            pending_ += escape_field(key);
            pending_.push_back('\t');
            pending_ += escape_field(wide_to_utf8(value));
            pending_.push_back('\n');
            ++pending_entries_;
        }
        task_once();
    }

    size_t size() {
        std::lock_guard lock(mutex_);
        load_locked();
        return entries_.size();
    }

private:
    void load_locked() {
        if (loaded_) return;
        loaded_ = true;
        path_ = join_path(join_path(get_local_appdata(), L"TeleVipDesktop"),
                          L"translate-cache.tsv");
        std::ifstream file(path_, std::ios::binary);
        if (!file) return;
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            const size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            const std::string key = unescape_field(std::string_view(line).substr(0, tab));
            entries_[key] = utf8_to_wide(unescape_field(std::string_view(line).substr(tab + 1)));
            if (entries_.size() >= kDiskCacheCap) break;
        }
    }

    void task_once() {
        if (task_started_.exchange(true)) return;
        std::thread([this] {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::milliseconds(700));
                flush();
            }
        }).detach();
    }

    void flush() {
        std::string pending;
        {
            std::lock_guard lock(mutex_);
            if (pending_.empty()) return;
            pending.swap(pending_);
            pending_entries_ = 0;
        }
        ensure_dir(join_path(get_local_appdata(), L"TeleVipDesktop"));
        const bool rewrite = file_size(path_) > kDiskCacheBytes;
        std::ofstream file(path_, rewrite ? (std::ios::binary | std::ios::trunc)
                                          : (std::ios::binary | std::ios::app));
        if (!file) return;
        if (rewrite) {
            std::lock_guard lock(mutex_);
            for (const auto& [key, value] : entries_) {
                file << escape_field(key) << '\t' << escape_field(wide_to_utf8(value)) << '\n';
            }
        } else {
            file << pending;
        }
    }

    std::mutex mutex_;
    std::unordered_map<std::string, std::wstring> entries_;
    std::string pending_;
    size_t pending_entries_ = 0;
    std::wstring path_;
    bool loaded_ = false;
    std::atomic<bool> task_started_{false};
};

DiskTranslateCache& disk_cache() {
    static DiskTranslateCache cache;
    return cache;
}

// Groups concurrent requests for the same string: the first caller does the
// work, everyone else waits and shares the answer.
struct InflightSlot {
    std::mutex mutex;
    std::condition_variable ready;
    bool done = false;
    std::wstring value;
};

std::mutex g_inflight_mutex;
std::unordered_map<std::string, std::shared_ptr<InflightSlot>> g_inflight;

void remember(const std::string& key, const std::wstring& value) {
    {
        std::lock_guard lock(g_translate_cache_mutex);
        if (g_translate_cache.size() >= kMemoryCacheCap) {
            // Evict a quarter instead of wiping the table: a full clear made the
            // next chat re-request everything it had just learned.
            size_t drop = g_translate_cache.size() / 4;
            for (auto i = g_translate_cache.begin();
                 i != g_translate_cache.end() && drop > 0; ++drop) {
                i = g_translate_cache.erase(i);
            }
        }
        g_translate_cache[key] = value;
    }
    disk_cache().store(key, value);
}

// ---- 引擎选择：Google / Microsoft Edge ---------------------------------
// Google 在国内必须绕代理，Edge 通常可直连。两条源能跑分流也能互为兜底。
std::atomic<int> g_provider{0};  // 0=Auto 1=Google 2=Microsoft
// 上一次哪条源通了：Auto 模式直接走它，不用每次先撞墙。
std::atomic<bool> g_google_ok{true};
std::string json_escape_text(std::string_view text) {
    // JSON only needs " , backslash and control chars escaped. Built with
    // numeric codes so the source stays free of escape-sequence noise.
    static const char* kHex = "0123456789abcdef";
    const char backslash = static_cast<char>(92);
    const char quote = static_cast<char>(34);
    std::string out;
    out.reserve(text.size() + 16);
    for (const unsigned char ch : text) {
        if (ch == 34) {
            out.push_back(backslash);
            out.push_back(quote);
        } else if (ch == 92) {
            out.push_back(backslash);
            out.push_back(backslash);
        } else if (ch == 10) {
            out.push_back(backslash);
            out.push_back('n');
        } else if (ch == 13) {
            out.push_back(backslash);
            out.push_back('r');
        } else if (ch == 9) {
            out.push_back(backslash);
            out.push_back('t');
        } else if (ch < 0x20) {
            out.push_back(backslash);
            out.push_back('u');
            out.push_back('0');
            out.push_back('0');
            out.push_back(kHex[(ch >> 4) & 0xF]);
            out.push_back(kHex[ch & 0xF]);
        } else {
            out.push_back(static_cast<char>(ch));
        }
    }
    return out;
}

std::wstring mymemory_language(const std::wstring& target) {
    if (target.rfind(L"zh", 0) == 0) {
        return lower(target).find(L"tw") != std::wstring::npos ? L"zh-TW" : L"zh-CN";
    }
    return target;
}

// MyMemory 免 key 可直连（实测 ~1.1s），定位为兜底而非主力。
// Microsoft Edge 那条免费端点已经 404，Bing 页面端点需要抓 IG/IID，
// 所以第二源先用它；以后要接 Azure 官方 key，只需换掉本函数。
bool secondary_translate(const std::wstring& text, const std::wstring& target,
                         const std::wstring& source,
                         std::wstring& out, std::wstring& error) {
    if (source == L"auto") {
        error = L"Fallback requires a detected source language";
        return false;
    }
    const std::wstring query = L"/get?q=" + url_encode_utf8(text) + L"&langpair=" +
        url_encode_utf8(mymemory_language(source) + L"|" + mymemory_language(target));
    std::string body;
    if (!http_request(memory_pool(), L"GET", nullptr, {}, query, body, error)) return false;
    const int64_t status = json_int(body, "responseStatus");
    if (status != 200) {
        error = L"Fallback rejected the translation (" + std::to_wstring(status) + L")";
        return false;
    }
    const std::string value = json_string(body, "translatedText");
    out = utf8_to_wide(value);
    if (out.empty()) {
        error = L"fallback empty translation";
        return false;
    }
    return true;
}

bool google_translate(const std::wstring& text, const std::wstring& target,
                       const std::wstring& source,
                      std::wstring& out, std::wstring& error) {
    const std::wstring query =
        L"/translate_a/single?client=gtx&sl=" + url_encode_utf8(source) + L"&tl=" + url_encode_utf8(target) +
        L"&dt=t&q=" + url_encode_utf8(text);
    std::string body;
    if (!http_get(query, body, error)) return false;
    out = utf8_to_wide(parse_translation(body));
    if (out.empty()) {
        error = L"google empty translation";
        return false;
    }
    return true;
}

// 按设置选源；Auto 先试当前更快的那一条，失败再换另一条。
bool fetch_upstream(const std::wstring& text, const std::wstring& target,
                     const std::wstring& source, std::wstring& out,
                    std::wstring& error) {
    const int mode = g_provider.load();
    if (mode == 1) return google_translate(text, target, source, out, error);
    if (mode == 2) return secondary_translate(text, target, source, out, error);

    std::wstring first_error;
    const bool google_first = g_google_ok.load();
    if (google_first) {
        if (google_translate(text, target, source, out, error)) {
            g_google_ok.store(true);
            return true;
        }
        first_error = error;
        g_google_ok.store(false);
        if (secondary_translate(text, target, source, out, error)) {
            g_google_ok.store(false);
            return true;
        }
    } else {
        if (secondary_translate(text, target, source, out, error)) return true;
        first_error = error;
        if (google_translate(text, target, source, out, error)) {
            g_google_ok.store(true);
            return true;
        }
    }
    if (error.empty()) error = first_error;
    return false;
}

std::wstring translate_cached(const std::wstring& text, const std::wstring& target,
                              const std::wstring& source,
                              std::wstring& error) {
    if (!needs_translation(text, target, source)) {
        g_translate_skipped.fetch_add(1);
        return text;
    }
    const std::string key = translation_cache_key(text, source, target);
    {
        std::lock_guard lock(g_translate_cache_mutex);
        const auto i = g_translate_cache.find(key);
        if (i != g_translate_cache.end()) {
            g_translate_l1_hits.fetch_add(1);
            return i->second;
        }
    }
    {
        std::wstring cached;
        if (disk_cache().lookup(key, cached)) {
            g_translate_disk_hits.fetch_add(1);
            std::lock_guard lock(g_translate_cache_mutex);
            g_translate_cache[key] = cached;
            return cached;
        }
    }

    std::shared_ptr<InflightSlot> slot;
    bool owner = false;
    {
        std::lock_guard lock(g_inflight_mutex);
        auto i = g_inflight.find(key);
        if (i == g_inflight.end()) {
            slot = std::make_shared<InflightSlot>();
            g_inflight[key] = slot;
            owner = true;
        } else {
            slot = i->second;
        }
    }
    if (!owner) {
        std::unique_lock lock(slot->mutex);
        slot->ready.wait(lock, [&slot] { return slot->done; });
        if (slot->value.empty()) error = L"Duplicate request failed";
        return slot->value;
    }

    std::wstring result;
    const auto finish = [&] {
        {
            std::lock_guard lock(slot->mutex);
            slot->value = result;
            slot->done = true;
        }
        slot->ready.notify_all();
        std::lock_guard lock(g_inflight_mutex);
        g_inflight.erase(key);
    };

    g_translate_network.fetch_add(1);
    if (!fetch_upstream(text, target, source, result, error)) {
        // 一次重试：并发深时代理会偶尔丢隧道。
        std::wstring retry_error;
        if (!fetch_upstream(text, target, source, result, retry_error)) {
            if (error.empty()) error = retry_error;
            if (error.empty()) error = L"upstream unavailable";
            finish();
            return {};
        }
    }
    remember(key, result);
    finish();
    return result;
}

} // namespace

TranslateResult translate_text_to(const std::wstring& text, const std::wstring& target,
                                  const std::wstring& source) {
    TranslateResult result;
    result.text = translate_cached(text, normalize_translation_language(target),
                                   normalize_translation_language(source), result.error);
    result.ok = !result.text.empty();
    return result;
}

TranslateResult translate_text(const std::wstring& text, bool to_chinese) {
    return translate_text_to(text, to_chinese ? L"zh-CN" : L"en");
}

void translate_warm_connection() {
    // Establishes a pooled connection's TLS session ahead of the first real
    // request. A cold connection paid a 300-400 ms handshake, and a chat switch
    // hands work to several requests at once.
    std::string body;
    std::wstring error;
    http_get(L"/translate_a/single?client=gtx&sl=auto&tl=en&dt=t&q=warmup", body, error);
}

void translate_probe_upstream() {
    std::wstring out;
    std::wstring error;
    const auto time_one = [&](bool microsoft) -> long long {
        out.clear();
        error.clear();
        const auto start = std::chrono::steady_clock::now();
        const bool ok = microsoft ? secondary_translate(L"hello", L"zh-CN", L"en", out, error)
                                  : google_translate(L"hello", L"zh-CN", L"en", out, error);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count();
        return ok ? ms : -1;
    };
    const long long ms_ms = time_one(true);
    const long long g_ms = time_one(false);
    // Google 要快出 40ms 才值得优先，否则走 Edge（少一次绕代理）。
    const bool prefer_google = g_ms >= 0 && (ms_ms < 0 || g_ms + 40 < ms_ms);
    g_google_ok.store(prefer_google);
    debug_log(L"upstream probe: fallback=" +
              (ms_ms < 0 ? std::wstring(L"fail") : std::to_wstring(ms_ms) + L"ms") +
              L" google=" +
              (g_ms < 0 ? std::wstring(L"fail") : std::to_wstring(g_ms) + L"ms") +
              L" -> prefer " + (prefer_google ? L"google" : L"microsoft"));
}

void translate_prewarm(int connections) {
    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(connections));
    for (int i = 0; i < connections; ++i) {
        threads.emplace_back([] { translate_warm_connection(); });
    }
    for (auto& thread : threads) thread.join();
}

void set_translate_provider(TranslateProvider provider) {
    g_provider.store(static_cast<int>(provider));
}

TranslateProvider translate_provider() {
    return static_cast<TranslateProvider>(g_provider.load());
}

TranslateStats translate_stats() {
    TranslateStats stats;
    stats.memory_hits = g_translate_l1_hits.load();
    stats.disk_hits = g_translate_disk_hits.load();
    stats.network = g_translate_network.load();
    stats.skipped = g_translate_skipped.load();
    {
        std::lock_guard lock(g_translate_cache_mutex);
        stats.memory_entries = g_translate_cache.size();
    }
    stats.disk_entries = disk_cache().size();
    return stats;
}

std::vector<ArchiveRow> load_archive(size_t limit, size_t offset) {
    struct ArchiveCache {
        std::mutex mutex;
        bool initialized = false;
        std::filesystem::file_time_type edits_time{};
        std::filesystem::file_time_type deleted_time{};
        uintmax_t edits_size = 0;
        uintmax_t deleted_size = 0;
        std::vector<ArchiveRow> rows;
    };
    static ArchiveCache cache;
    std::lock_guard lock(cache.mutex);
    const auto edits_path = join_path(archive_dir(), L"edits.jsonl");
    const auto deleted_path = join_path(archive_dir(), L"deleted.jsonl");
    const auto stamp = [](const std::wstring& path) {
        std::error_code error;
        const auto time = std::filesystem::last_write_time(std::filesystem::path(path), error);
        const auto safe_time = error ? std::filesystem::file_time_type::min() : time;
        error.clear();
        const auto size = std::filesystem::file_size(std::filesystem::path(path), error);
        return std::pair{safe_time, error ? uintmax_t{0} : size};
    };
    const auto edits_stamp = stamp(edits_path);
    const auto deleted_stamp = stamp(deleted_path);
    if (cache.initialized && cache.edits_time == edits_stamp.first
        && cache.edits_size == edits_stamp.second
        && cache.deleted_time == deleted_stamp.first
        && cache.deleted_size == deleted_stamp.second) {
        if (limit == 0 || offset >= cache.rows.size()) return {};
        const size_t count = std::min(limit, cache.rows.size() - offset);
        return std::vector<ArchiveRow>(cache.rows.begin() + static_cast<std::ptrdiff_t>(offset),
                                       cache.rows.begin() + static_cast<std::ptrdiff_t>(offset + count));
    }

    std::vector<ArchiveRow> rows;
    const auto add_file = [&rows](const std::wstring& path, bool edit) {
        for (const auto& line : read_tail_lines(path, 8192)) {
            if (line.size() < 2 || line.front() != '{') continue;
            ArchiveRow row;
            row.edit = edit;
            row.ts = json_int(line, "ts");
            row.peer = json_int(line, "peer");
            row.msg = static_cast<int>(json_int(line, "msg"));
            if (row.msg <= 0) continue;
            row.date = static_cast<int>(json_int(line, "date"));
            row.from = json_int(line, "from");
            row.ordinal = json_int(line, "ordinal");
            row.durable = json_int(line, "schema") >= 2 && json_int(line, "account") > 0;
            row.source = utf8_to_wide(json_string(line, "source"));
            if (edit) {
                row.old_text = utf8_to_wide(json_string(line, "old"));
                row.new_text = utf8_to_wide(json_string(line, "new"));
            } else {
                row.text = utf8_to_wide(json_string(line, "text"));
            }
            rows.push_back(std::move(row));
        }
    };
    add_file(edits_path, true);
    add_file(deleted_path, false);
    std::sort(rows.begin(), rows.end(), [](const ArchiveRow& a, const ArchiveRow& b) {
        return a.ts > b.ts;
    });
    if (rows.size() > 16384) rows.resize(16384);
    cache.initialized = true;
    cache.edits_time = edits_stamp.first;
    cache.edits_size = edits_stamp.second;
    cache.deleted_time = deleted_stamp.first;
    cache.deleted_size = deleted_stamp.second;
    cache.rows = rows;
    if (limit == 0 || offset >= cache.rows.size()) return {};
    const size_t count = std::min(limit, cache.rows.size() - offset);
    return std::vector<ArchiveRow>(cache.rows.begin() + static_cast<std::ptrdiff_t>(offset),
                                   cache.rows.begin() + static_cast<std::ptrdiff_t>(offset + count));
}

} // namespace tv
