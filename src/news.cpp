#include "news.h"
#include "signed_content.h"
#include <random>

namespace tv::news {
namespace {
struct Internet {
    HINTERNET value;
    ~Internet() { if (value) WinHttpCloseHandle(value); }
};
std::filesystem::path state_path() {
    wchar_t path[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path))) return {};
    return std::filesystem::path(path) / L"TeleVipDesktop" / L"news-read.txt";
}
}
Client::~Client() {
    { std::lock_guard lock(mutex_); stop_ = true; }
    wake_.notify_one(); if (worker_.joinable()) worker_.join();
}
void Client::start() {
    if (worker_.joinable()) return;
    const auto path = state_path();
    std::ifstream file(path, std::ios::binary);
    std::getline(file, acknowledged_);
    if (acknowledged_.size() != 13 || acknowledged_.find_first_not_of("0123456789") != std::string::npos) acknowledged_.clear();
    worker_ = std::thread([this] { run(); });
}
void Client::request() {
    { std::lock_guard lock(mutex_); requested_ = true; }
    wake_.notify_one();
}
std::optional<Result> Client::poll() {
    std::lock_guard lock(mutex_); auto result = std::move(completed_); completed_.reset(); return result;
}
void Client::acknowledge(const std::string& revision) {
    if (revision.size() != 13 || revision.find_first_not_of("0123456789") != std::string::npos) return;
    { std::lock_guard lock(mutex_); acknowledged_ = revision; }
    std::error_code ec; const auto path = state_path(); if (path.empty()) return;
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto temporary = path.wstring() + L".tmp";
    { std::ofstream file(temporary, std::ios::binary | std::ios::trunc); file << revision; if (!file) return; }
    MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH);
}
bool Client::fetch(Model& result) {
    Internet session{WinHttpOpen(L"TeleVip-Desktop/2026.11.11.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) return false;
    WinHttpSetTimeouts(session.value, 3000, 3000, 4000, 4000);
    Internet connection{WinHttpConnect(session.value, L"47-109-49-93.sslip.io", 21116, 0)};
    if (!connection.value) return false;
    Internet request{WinHttpOpenRequest(connection.value, L"GET", L"/televip/api/client/news", nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request.value) return false;
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    const std::wstring headers = etag_.empty() ? L"" : L"If-None-Match: " + utf8_to_wide(etag_) + L"\r\n";
    if (!WinHttpSendRequest(request.value, headers.c_str(), static_cast<DWORD>(headers.size()), nullptr, 0, 0, 0)
        || !WinHttpReceiveResponse(request.value, nullptr)) return false;
    DWORD status = 0, count = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &count, WINHTTP_NO_HEADER_INDEX)) return false;
    if (status == 304 && current_) { result = *current_; return true; }
    if (status != 200) return false;
    std::string body;
    for (;;) {
        char buffer[4096]; DWORD received = 0;
        if (!WinHttpReadData(request.value, buffer, sizeof(buffer), &received)) return false;
        if (!received) break;
        if (body.size() + received > 24000) return false;
        body.append(buffer, received);
    }
    const auto payload = signed_content::decode(json_string_member(body, "payload"));
    const auto signature = signed_content::decode(json_string_member(body, "signature"));
    if (!signed_content::verify(payload, signature)) return false;
    auto parsed = parse(std::string(payload.begin(), payload.end()));
    std::string acknowledged;
    { std::lock_guard lock(mutex_); acknowledged = acknowledged_; }
    if (!parsed || (!acknowledged.empty() && parsed->revision < acknowledged)
        || (current_ && parsed->revision < current_->revision)) return false;
    // Only a validated response may advance the conditional-request cache.
    wchar_t tag[128]{}; count = sizeof(tag);
    if (WinHttpQueryHeaders(request.value, WINHTTP_QUERY_ETAG, WINHTTP_HEADER_NAME_BY_INDEX, tag, &count, WINHTTP_NO_HEADER_INDEX))
        etag_ = wide_to_utf8(tag);
    else etag_.clear();
    current_ = *parsed; result = std::move(*parsed); return true;
}
void Client::run() {
    std::mt19937 random(GetTickCount() ^ GetCurrentProcessId());
    unsigned failures = 0;
    auto delay = std::chrono::seconds(1 + random() % 10);
    for (;;) {
        bool manual;
        { std::unique_lock lock(mutex_);
          wake_.wait_for(lock, delay, [this] { return stop_ || requested_; });
          if (stop_) return; manual = requested_; requested_ = false; }
        Model model; const bool ok = fetch(model);
        { std::lock_guard lock(mutex_); if (stop_) return;
          completed_ = Result{ok ? std::optional<Model>(std::move(model)) : std::nullopt, ok, manual}; }
        failures = ok ? 0 : std::min(5u, failures + 1);
        delay = std::chrono::seconds((ok ? 300 : std::min(1800u, 60u << failures)) + random() % 31);
    }
}
}
