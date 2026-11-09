#include "license.h"
#include "license_public_key.h"
#include "license_lifecycle.h"
#include "json_text.h"
#include "core.h"
#include "render.h"
#include "motion.h"
#include "native_button.h"
#include <wincrypt.h>
#include <sddl.h>
#include <mutex>
#include <sstream>
#include <condition_variable>

#pragma comment(lib, "crypt32.lib")

namespace tv::license {
namespace {
constexpr wchar_t kHost[] = L"47-109-49-93.sslip.io";
constexpr INTERNET_PORT kPort = 21116;
constexpr wchar_t kPrefix[] = L"/televip/api/client/";
constexpr UINT kCompleted = WM_APP + 173;
std::mutex mutex;
std::condition_variable wake;
std::thread heartbeat;
std::atomic<bool> stop{false};
bool refresh_requested = false;
std::atomic<ULONGLONG> deadline{0};
std::wstring verified_path, message = L"尚未验证卡密";
std::wstring recovery_card;
std::string device, session_token, expected_hash;
HANDLE locked_hook = INVALID_HANDLE_VALUE;
bool persist_card = true;
#ifdef TV_LICENSE_TESTING
std::atomic<bool> test_offline{false};
#endif

struct Internet {
    HINTERNET value = nullptr;
    explicit Internet(HINTERNET v) : value(v) {}
    ~Internet() { if (value) WinHttpCloseHandle(value); }
    Internet(const Internet&) = delete;
    Internet& operator=(const Internet&) = delete;
};

std::vector<BYTE> digest(const void* input, size_t size) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    std::vector<BYTE> result(32);
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    const auto status = BCryptHash(algorithm, nullptr, 0, (PUCHAR)input, static_cast<ULONG>(size), result.data(), 32);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) return {};
    return result;
}

std::string hex(const std::vector<BYTE>& bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string text;
    for (BYTE byte : bytes) { text += digits[byte >> 4]; text += digits[byte & 15]; }
    return text;
}

std::string nonce() {
    std::vector<BYTE> bytes(32);
    if (BCryptGenRandom(nullptr, bytes.data(), 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return {};
    return hex(bytes);
}

std::vector<BYTE> unbase64(const std::string& input) {
    DWORD count = 0;
    if (!CryptStringToBinaryA(input.c_str(), static_cast<DWORD>(input.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
        nullptr, &count, nullptr, nullptr) || count > 16384) return {};
    std::vector<BYTE> result(count);
    if (!CryptStringToBinaryA(input.c_str(), static_cast<DWORD>(input.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
        result.data(), &count, nullptr, nullptr)) return {};
    result.resize(count);
    return result;
}

std::wstring directory() {
    const auto path = join_path(get_local_appdata(), L"TeleVipNative\\Cloud");
    ensure_dir(path);
    return path;
}

std::string machine_id() {
    wchar_t guid[256]{};
    DWORD bytes = sizeof(guid);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid",
        RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, guid, &bytes) != ERROR_SUCCESS) return {};
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> data(size);
    const BOOL ok = GetTokenInformation(token, TokenUser, data.data(), size, &size);
    CloseHandle(token);
    if (!ok) return {};
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid, &sid)) return {};
    const auto raw = wide_to_utf8(std::wstring(L"TeleVipNative/device/v1:") + guid + L":" + sid);
    LocalFree(sid);
    return hex(digest(raw.data(), raw.size()));
}

void save_card(const std::wstring& card) {
    const auto plain = wide_to_utf8(card);
    DATA_BLOB input{static_cast<DWORD>(plain.size()), (BYTE*)plain.data()}, output{};
    if (!CryptProtectData(&input, L"TeleVip Native card", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return;
    std::ofstream file(join_path(directory(), L"card.dpapi"), std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
}

std::wstring saved_card() {
    std::ifstream file(join_path(directory(), L"card.dpapi"), std::ios::binary);
    std::vector<BYTE> data((std::istreambuf_iterator<char>(file)), {});
    if (data.empty() || data.size() > 8192) return {};
    DATA_BLOB input{static_cast<DWORD>(data.size()), data.data()}, output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return {};
    const auto value = utf8_to_wide(std::string((char*)output.pbData, output.cbData));
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return value;
}

bool post(const wchar_t* route, const std::string& body, const std::string& bearer,
          std::vector<BYTE>& output, std::wstring& error, size_t maximum = 16384,
          RequestFailure* failure = nullptr) {
    if (failure) *failure = {};
#ifdef TV_LICENSE_TESTING
    if (test_offline) { error = L"Isolated test: temporary network outage"; return false; }
#endif
    Internet internet(WinHttpOpen(L"TeleVipNative/Cloud-1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!internet.value) { error = L"无法初始化安全连接"; return false; }
    WinHttpSetTimeouts(internet.value, 8000, 8000, 15000, 15000);
    Internet connection(WinHttpConnect(internet.value, kHost, kPort, 0));
    if (!connection.value) { error = L"无法连接授权服务器"; return false; }
    const std::wstring path = std::wstring(kPrefix) + route;
    Internet request(WinHttpOpenRequest(connection.value, L"POST", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.value) { error = L"无法创建验证请求"; return false; }
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
    std::wstring headers = L"Content-Type: application/json\r\nCache-Control: no-store\r\n";
    if (!bearer.empty()) headers += L"Authorization: Bearer " + utf8_to_wide(bearer) + L"\r\n";
    if (!WinHttpSendRequest(request.value, headers.c_str(), static_cast<DWORD>(headers.size()),
        (LPVOID)body.data(), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) {
        error = L"服务器安全连接失败（" + std::to_wstring(GetLastError()) + L"）";
        if (failure) failure->winhttp = GetLastError();
        return false;
    }
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
        if (failure) failure->winhttp = GetLastError();
        error = L"无法读取授权服务器状态";
        return false;
    }
    if (failure) {
        failure->http = status;
        wchar_t retry[64]{};
        DWORD retry_size = sizeof(retry);
        if (status == 429 && WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CUSTOM, L"Retry-After",
            retry, &retry_size, WINHTTP_NO_HEADER_INDEX)) {
            try {
                const auto text = trim(retry);
                if (!text.empty() && text.find_first_not_of(L"0123456789") == std::wstring::npos)
                    failure->retry_after = static_cast<unsigned>(std::min<unsigned long>(std::stoul(text), 86400));
            } catch (const std::exception&) { /* Missing/invalid advice uses the bounded backoff. */ }
        }
    }
    output.clear();
    for (;;) {
        BYTE buffer[8192]; DWORD received = 0;
        if (!WinHttpReadData(request.value, buffer, sizeof(buffer), &received)) { error = L"下载中断，请重试"; return false; }
        if (!received) break;
        if (output.size() + received > maximum) { error = L"服务器响应超出允许大小"; return false; }
        output.insert(output.end(), buffer, buffer + received);
    }
    if (status != 200) {
        const std::string response(output.begin(), output.end());
        const auto explanation = json_string_member(response, "message");
        if (failure) failure->kind = classify_http(_wcsicmp(route, L"refresh") == 0, status,
            json_string_member(response, "reason"));
        error = explanation.empty() ? L"验证失败（HTTP " + std::to_wstring(status) + L"）" : utf8_to_wide(explanation);
        return false;
    }
    if (failure) failure->kind = FailureKind::None;
    return true;
}

bool signature_valid(const std::vector<BYTE>& payload, const std::vector<BYTE>& signature) {
    if (signature.size() != 64 || payload.empty()) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) < 0) return false;
    std::vector<BYTE> blob(sizeof(BCRYPT_ECCKEY_BLOB) + sizeof(kPublicXY));
    const BCRYPT_ECCKEY_BLOB header{BCRYPT_ECDSA_PUBLIC_P256_MAGIC, 32};
    memcpy(blob.data(), &header, sizeof(header));
    memcpy(blob.data() + sizeof(header), kPublicXY, sizeof(kPublicXY));
    bool ok = false;
    if (BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key, blob.data(), static_cast<ULONG>(blob.size()), 0) >= 0) {
        const auto hashed = digest(payload.data(), payload.size());
        ok = !hashed.empty() && BCryptVerifySignature(key, nullptr, (PUCHAR)hashed.data(), 32,
            (PUCHAR)signature.data(), 64, 0) >= 0;
        BCryptDestroyKey(key);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}

bool is_hex(const std::string& s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool authorize(const std::wstring& card, bool refresh, std::wstring& error, RequestFailure* failure = nullptr) {
    const auto challenge = nonce();
    if (device.empty() || challenge.empty()) { error = L"无法生成设备校验信息"; return false; }
    std::string key = wide_to_utf8(trim(card));
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (!refresh && (key.size() < 12 || key.size() > 64 || !std::all_of(key.begin(), key.end(), [](unsigned char c) { return std::isalnum(c) && c < 128; }))) {
        error = L"请输入有效卡密（12–64 位字母或数字）"; return false;
    }
    std::string bearer;
    { std::lock_guard<std::mutex> lock(mutex); bearer = session_token; }
    const std::string body = "{\"hook_revision\":\"20261110.1\",\"device_id\":\"" + device + "\",\"nonce\":\"" + challenge + "\"" +
        (refresh ? "}" : ",\"card_key\":\"" + key + "\"}");
    std::vector<BYTE> response;
    if (!post(refresh ? L"refresh" : L"activate", body, refresh ? bearer : "", response, error, 16384, failure)) return false;
    if (failure) failure->kind = FailureKind::InvalidResponse;
    const std::string json(response.begin(), response.end());
    const auto payload = unbase64(json_string_member(json, "payload"));
    if (!signature_valid(payload, unbase64(json_string_member(json, "signature")))) { error = L"服务器签名校验失败"; return false; }
    std::vector<std::string> fields;
    std::istringstream stream(std::string(payload.begin(), payload.end()));
    for (std::string line; std::getline(stream, line);) fields.push_back(line);
    if (fields.size() != 8 || fields[0] != "TELEVIP-HOOK-1" || fields[1] != device || fields[2] != challenge ||
        !is_hex(fields[4]) || !is_hex(fields[6]) || !is_hex(fields[7])) { error = L"授权响应与本次请求不匹配"; return false; }
    uint64_t expires = 0, bytes = 0;
    try { expires = std::stoull(fields[3]); bytes = std::stoull(fields[5]); } catch (...) { error = L"授权响应格式无效"; return false; }
    const auto current = now_ms() / 1000;
    if (expires <= current || expires > current + 330 || bytes < 512 || bytes > 16 * 1024 * 1024) { error = L"授权已到期或本机时间不准确"; return false; }
    const auto lease_end = GetTickCount64() + (expires - current) * 1000;
    bool have_module = false;
    { std::lock_guard<std::mutex> lock(mutex); have_module = locked_hook != INVALID_HANDLE_VALUE && !verified_path.empty(); }
    if (!refresh && !have_module) {
        std::vector<BYTE> dll;
        if (!post(L"hook", "{\"device_id\":\"" + device + "\"}", fields[6], dll, error, static_cast<size_t>(bytes), failure)) return false;
        if (failure) failure->kind = FailureKind::InvalidResponse;
        if (dll.size() != bytes || hex(digest(dll.data(), dll.size())) != fields[4] || dll[0] != 'M' || dll[1] != 'Z') {
            error = L"Hook 文件完整性校验失败"; return false;
        }
        const auto path = join_path(directory(), L"hook_" + utf8_to_wide(nonce().substr(0, 16)) + L".dll");
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) { error = L"无法写入授权模块"; return false; }
        DWORD written = 0;
        const BOOL ok = WriteFile(file, dll.data(), static_cast<DWORD>(dll.size()), &written, nullptr);
        FlushFileBuffers(file); CloseHandle(file);
        if (!ok || written != dll.size()) { DeleteFileW(path.c_str()); error = L"授权模块写入失败"; return false; }
        HANDLE held = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (held == INVALID_HANDLE_VALUE) { DeleteFileW(path.c_str()); error = L"无法锁定授权模块"; return false; }
        { std::lock_guard<std::mutex> lock(mutex); locked_hook = held; verified_path = path; expected_hash = fields[4]; }
    } else {
        std::lock_guard<std::mutex> lock(mutex);
        if (expected_hash != fields[4]) {
            if (failure) failure->kind = FailureKind::RestartRequired;
            error = L"授权模块已更新，请重新启动客户端"; return false;
        }
    }
    if (!refresh && persist_card) save_card(utf8_to_wide(key));
    { std::lock_guard<std::mutex> lock(mutex);
        if (!refresh) recovery_card = utf8_to_wide(key);
        session_token = fields[7]; message = L"云端验证成功 · 已绑定当前设备";
    }
    deadline = lease_end;
    if (failure) failure->kind = FailureKind::None;
    return true;
}

void trace_renewal(const char* event, const RequestFailure& failure = {}, unsigned delay = 0) {
    const auto path = join_path(directory(), L"renewal.log");
    std::error_code ignored;
    const auto bytes = std::filesystem::file_size(path, ignored);
    if (!ignored && bytes > 256 * 1024) {
        const auto previous = join_path(directory(), L"renewal.previous.log");
        std::filesystem::remove(previous, ignored);
        std::filesystem::rename(path, previous, ignored);
    }
    std::ofstream output(path, std::ios::binary | std::ios::app);
    output << now_ms() << " " << event << " kind=" << static_cast<unsigned>(failure.kind)
        << " http=" << failure.http << " winhttp=" << failure.winhttp << " retry_seconds=" << delay << "\n";
}

bool renew(std::wstring& error, RequestFailure& failure) {
    if (authorize({}, true, error, &failure)) return true;
    if (failure.kind != FailureKind::SessionLost) return false;
    // An expired short token is not an expired card. Prove the original card again.
    std::wstring card;
    { std::lock_guard<std::mutex> lock(mutex); card = recovery_card; }
    if (card.empty()) card = saved_card();
    if (card.empty()) {
        failure.kind = FailureKind::RestartRequired;
        error = L"无法读取已保存卡密，请重新启动并授权";
        return false;
    }
    trace_renewal("session_reauthenticate", failure);
    const bool ok = authorize(card, false, error, &failure);
    SecureZeroMemory(card.data(), card.size() * sizeof(wchar_t));
    return ok;
}

void start_heartbeat() {
    { std::lock_guard<std::mutex> lock(mutex); stop = false; refresh_requested = false; }
    heartbeat = std::thread([] {
        RenewalPolicy policy;
        unsigned delay = 60;
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait_for(lock, std::chrono::seconds(delay), [] { return stop.load() || refresh_requested; });
                if (stop) return;
                refresh_requested = false;
            }
            std::wstring error;
            RequestFailure failure;
            if (renew(error, failure)) {
                policy.succeeded(); delay = 60;
                trace_renewal("renewed", failure);
                continue;
            }
            const auto decision = policy.failed(failure.kind, failure.retry_after);
            // Only an authenticated, explicit card rejection revokes the current signed lease.
            if (failure.kind == FailureKind::Rejected) deadline = 0;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (failure.kind == FailureKind::Rejected)
                    message = L"卡密已到期、被停用或设备不匹配，请重新授权";
                else if (failure.kind == FailureKind::RestartRequired) message = error;
                else message = authorized() ? L"授权连接暂时中断，正在自动重试"
                    : L"授权服务器暂不可达，功能暂停；连接恢复后将自动恢复原开关";
            }
            trace_renewal("renewal_failed", failure, decision.delay_seconds);
            if (decision.terminal) return;
            // Keep retrying after lease expiry so a sleeping/offline client can recover itself.
            delay = decision.delay_seconds;
        }
    });
}

struct ActivationDialog {
    HWND window = nullptr, edit = nullptr, status = nullptr, button = nullptr;
    HWND group = nullptr, label = nullptr;
    HWND chrome[3]{};
    HFONT font = nullptr;
    HBRUSH card_brush = nullptr, input_brush = nullptr;
    Renderer renderer;
    Palette palette;
    float scale = 1;
    float content_x = 0, content_y = 52;
    bool reduce_motion = false;
    motion::Spring hover[5], press[5], focus;
    motion::Tween scene, status_fade;
    ULONGLONG last_frame = 0;
    std::thread worker;
    std::wstring error;
    bool busy = false, success = false, failed = false, closing = false, presented = false;
};

constexpr UINT_PTR kActivationAnimation = 173;

void layout_activation(ActivationDialog* dialog);

void activation_status(ActivationDialog* dialog) {
    dialog->status_fade.snap(dialog->reduce_motion ? 1.0f : 0.0f);
    dialog->status_fade.target = 1;
    dialog->last_frame = GetTickCount64();
    SetTimer(dialog->window, kActivationAnimation, 16, nullptr);
}

void animate_activation(ActivationDialog* dialog) {
    dialog->last_frame = GetTickCount64();
    SetTimer(dialog->window, kActivationAnimation, 16, nullptr);
}

LRESULT CALLBACK activation_control(HWND hwnd, UINT msg, WPARAM w, LPARAM l,
    UINT_PTR, DWORD_PTR data) {
    auto* dialog = reinterpret_cast<ActivationDialog*>(data);
    const int index = hwnd == dialog->button ? 0 : hwnd == dialog->group ? 1 :
        hwnd == dialog->chrome[0] ? 2 : hwnd == dialog->chrome[1] ? 3 : 4;
    const bool button = hwnd != dialog->edit;
    switch (msg) {
    case WM_ERASEBKGND: if (button) return 1; break;
    case WM_SETCURSOR:
        if (button) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; } break;
    case WM_MOUSEMOVE:
        if (button && dialog->hover[index].target != 1) {
            dialog->hover[index].target = 1;
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&track);
            animate_activation(dialog);
        }
        break;
    case WM_MOUSELEAVE:
        if (button) { dialog->hover[index].target = 0; animate_activation(dialog); }
        break;
    case WM_LBUTTONDOWN:
        if (button) { dialog->press[index].target = 1; animate_activation(dialog); }
        break;
    case WM_LBUTTONUP: case WM_CAPTURECHANGED:
        if (button) { dialog->press[index].target = 0; animate_activation(dialog); }
        break;
    case WM_SETFOCUS: case WM_KILLFOCUS:
        if (!button) { dialog->focus.target = msg == WM_SETFOCUS ? 1.0f : 0.0f; animate_activation(dialog); }
        InvalidateRect(dialog->window, nullptr, FALSE);
        break;
    case WM_NCDESTROY: RemoveWindowSubclass(hwnd, activation_control, 1); break;
    }
    return DefSubclassProc(hwnd, msg, w, l);
}

void layout_activation(ActivationDialog* dialog) {
    const float old_scale = dialog->scale;
    dialog->scale = GetDpiForWindow(dialog->window) / 96.0f;
    const float scale = dialog->scale;
    if (!dialog->font || old_scale != scale) {
        if (dialog->font) DeleteObject(dialog->font);
        dialog->font = CreateFontW(-int(15 * scale), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    }
    RECT rect{}; GetClientRect(dialog->window, &rect);
    dialog->content_x = std::max(0.0f, (rect.right / scale - 560) * .5f);
    dialog->content_y = 52 + std::max(0.0f, (rect.bottom / scale - 592) * .5f);
    const float offset = dialog->reduce_motion ? 0 : 16 * (1 - dialog->scene.value);
    auto place = [&](HWND child, int x, int y, int width, int height) {
        const float status_y = child == dialog->status && !dialog->reduce_motion ? 4 * (1 - dialog->status_fade.value) : 0;
        SetWindowPos(child, nullptr, int((x + dialog->content_x) * scale), int((y + dialog->content_y + offset + status_y) * scale),
            int(width * scale), int(height * scale), SWP_NOZORDER | SWP_NOACTIVATE);
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(dialog->font), FALSE);
        const float opacity = dialog->scene.value * (child == dialog->status ? dialog->status_fade.value : 1);
        SetLayeredWindowAttributes(child, 0, BYTE(std::lround(clampf(opacity, 0, 1) * 255)), LWA_ALPHA);
    };
    place(dialog->label, 48, 128, 464, 24);
    place(dialog->edit, 62, 177, 436, 26);
    place(dialog->button, 48, 260, 464, 44);
    place(dialog->status, 48, 324, 464, 54);
    place(dialog->group, 28, 420, 160, 40);
    for (int i = 0; i < 3; ++i) {
        SetWindowPos(dialog->chrome[i], nullptr, int((8 + 20 * i) * scale), int(14 * scale), int(24 * scale), int(24 * scale), SWP_NOZORDER | SWP_NOACTIVATE);
        SetLayeredWindowAttributes(dialog->chrome[i], 0, 255, LWA_ALPHA);
    }
    dialog->renderer.resize(rect.right, rect.bottom, 96 * scale);
    InvalidateRect(dialog->window, nullptr, FALSE);
}

void paint_activation(ActivationDialog* dialog) {
    auto& r = dialog->renderer;
    const auto& p = dialog->palette;
    r.begin(p.window);
    RECT client{}; GetClientRect(dialog->window, &client);
    const float width = client.right / dialog->scale;
    r.fill_rect({0, 0, width, 52}, p.titlebar);
    r.stroke_line(0, 51.5f, width, 51.5f, solid_over(p.border_soft, p.titlebar));
    TextStyle chrome; chrome.size = 12.5f; chrome.weight = FontWeight::SemiBold;
    chrome.align = TextAlign::Center; chrome.valign = TextVAlign::Middle;
    r.text(L"TeleVip · 云端授权", {90, 0, width - 90, 52}, chrome, p.text_secondary);
    r.push_visual(dialog->scene.value, dialog->content_x,
        dialog->content_y + (dialog->reduce_motion ? 0 : 16 * (1 - dialog->scene.value)));
    r.image(102, {28, 28, 80, 80});
    TextStyle title; title.size = 25; title.weight = FontWeight::SemiBold;
    r.text(L"TeleVip 云端授权", {98, 26, 532, 60}, title, p.text);
    TextStyle body; body.size = 13; body.wrap = true;
    r.text(L"验证卡密，安全连接你的专属功能。", {98, 65, 532, 89}, body, p.text_secondary);
    r.fill_round({28, 106, 532, 398}, 14, p.card);
    r.stroke_round({28, 106, 532, 398}, 14, solid_over(p.border, p.card));
    r.fill_round({48, 164, 512, 212}, 8, solid_over(p.input, p.card));
    const float focused = dialog->reduce_motion ? dialog->focus.target : dialog->focus.value;
    r.stroke_round({48, 164, 512, 212}, 8, mix_color(solid_over(p.border, p.card), p.accent, focused), 1 + .5f * focused);
    r.icon(Icon::Lock, {49, 224, 63, 238}, p.text_tertiary);
    r.text(L"绑定当前 Windows 设备 · 本地凭据由 DPAPI 保护", {70, 222, 512, 246}, body, p.text_secondary);
    r.stroke_line(28, 478, 532, 478, solid_over(p.divider, p.window));
    r.text(L"交流反馈与版本动态\nt.me/MapAdkillerr", {208, 421, 532, 463}, body, p.text_secondary);
    TextStyle note = body; note.size = 12;
    r.text(L"验证成功后下载并校验 Hook 模块。请妥善保管卡密，勿转发他人。", {28, 494, 532, 530}, note, p.text_tertiary);
    r.pop_visual(); r.end();
}

void begin_activation(ActivationDialog* dialog) {
    if (dialog->busy || dialog->closing) return;
    wchar_t text[128]{};
    GetWindowTextW(dialog->edit, text, 128);
    if (trim(text).empty()) {
        dialog->failed = true;
        dialog->error = L"请先输入卡密，再点击验证并继续。";
        SetWindowTextW(dialog->status, dialog->error.c_str()); SetFocus(dialog->edit);
        InvalidateRect(dialog->status, nullptr, FALSE);
        activation_status(dialog);
        return;
    }
    dialog->busy = true;
    dialog->failed = false;
    dialog->error.clear();
    EnableWindow(dialog->button, FALSE);
    EnableWindow(dialog->edit, FALSE);
    SetWindowTextW(dialog->button, L"正在验证…");
    SetWindowTextW(dialog->status, L"正在安全验证并下载授权模块…");
    activation_status(dialog);
    if (dialog->worker.joinable()) dialog->worker.join();
    dialog->worker = std::thread([dialog, card = std::wstring(text)] {
        dialog->success = authorize(card, false, dialog->error);
        PostMessageW(dialog->window, kCompleted, 0, 0);
    });
}

LRESULT CALLBACK activation_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    auto* dialog = reinterpret_cast<ActivationDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        dialog = static_cast<ActivationDialog*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        dialog->window = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dialog));
    }
    if (!dialog) return DefWindowProcW(hwnd, msg, w, l);
    switch (msg) {
    case WM_CREATE: {
        const Settings settings = load_settings();
        bool dark = settings.dark;
        wchar_t value[8]{};
        if (GetEnvironmentVariableW(L"TV_DARK", value, 8)) dark = _wtoi(value) != 0;
        dialog->palette = make_palette(dark, settings.accent);
        const Color card = dialog->palette.card;
        dialog->card_brush = CreateSolidBrush(RGB(BYTE(card.r * 255), BYTE(card.g * 255), BYTE(card.b * 255)));
        const Color input = solid_over(dialog->palette.input, card);
        dialog->input_brush = CreateSolidBrush(RGB(BYTE(input.r * 255), BYTE(input.g * 255), BYTE(input.b * 255)));
        BOOL animations = TRUE; SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
        dialog->reduce_motion = !animations || (GetEnvironmentVariableW(L"TV_REDUCED_MOTION", value, 8) && _wtoi(value) != 0);
        BOOL dark_caption = dark; DwmSetWindowAttribute(hwnd, 20, &dark_caption, sizeof(dark_caption));
        const int corner = 2; DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));
        dialog->scene.snap(dialog->reduce_motion ? 1.0f : 0.0f); dialog->scene.target = 1;
        dialog->status_fade.snap(1);
        if (!dialog->renderer.init(hwnd)) return -1;
        dialog->renderer.load_image(102, RT_RCDATA);
        auto child = [&](const wchar_t* type, const wchar_t* text, DWORD style, int id) {
            HWND control = CreateWindowExW(WS_EX_LAYERED, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1,
                hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            SetLayeredWindowAttributes(control, 0, 0, LWA_ALPHA);
            return control;
        };
        dialog->label = child(L"STATIC", L"授权卡密 (&K)", 0, 3003);
        dialog->edit = child(L"EDIT", saved_card().c_str(), WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD, 3001);
        SendMessageW(dialog->edit, EM_SETLIMITTEXT, 64, 0);
        dialog->button = child(L"BUTTON", L"验证并继续", WS_TABSTOP | BS_OWNERDRAW, IDOK);
        dialog->status = child(L"STATIC", L"准备就绪\n验证成功后自动进入主界面。", 0, 3004);
        dialog->group = child(L"BUTTON", L"TG 群组  ↗", WS_TABSTOP | BS_OWNERDRAW, 3002);
        dialog->chrome[0] = child(L"BUTTON", L"关闭", WS_TABSTOP | BS_OWNERDRAW, 3005);
        dialog->chrome[1] = child(L"BUTTON", L"最小化", WS_TABSTOP | BS_OWNERDRAW, 3006);
        dialog->chrome[2] = child(L"BUTTON", L"最大化或还原", WS_TABSTOP | BS_OWNERDRAW, 3007);
        for (HWND control : {dialog->edit, dialog->button, dialog->group, dialog->chrome[0], dialog->chrome[1], dialog->chrome[2]})
            SetWindowSubclass(control, activation_control, 1, reinterpret_cast<DWORD_PTR>(dialog));
        layout_activation(dialog);
        SetFocus(dialog->edit);
        animate_activation(dialog);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_NCCALCSIZE:
        if (w && IsZoomed(hwnd)) {
            MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
            reinterpret_cast<NCCALCSIZE_PARAMS*>(l)->rgrc[0] = monitor.rcWork;
        }
        return 0;
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        RECT window{}; GetWindowRect(hwnd, &window);
        if (!IsZoomed(hwnd)) {
            const int edge = int(6 * dialog->scale);
            const bool left = point.x < window.left + edge, right = point.x >= window.right - edge;
            const bool top = point.y < window.top + edge, bottom = point.y >= window.bottom - edge;
            if (top && left) return HTTOPLEFT; if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT; if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT; if (right) return HTRIGHT; if (top) return HTTOP; if (bottom) return HTBOTTOM;
        }
        ScreenToClient(hwnd, &point);
        if (point.y < int(52 * dialog->scale) && point.x > int(76 * dialog->scale)) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_NCLBUTTONDBLCLK:
        if (w == HTCAPTION) { ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE); return 0; } break;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(l);
        info->ptMinTrackSize = {LONG(560 * dialog->scale), LONG(592 * dialog->scale)};
        MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
        info->ptMaxPosition = {monitor.rcWork.left - monitor.rcMonitor.left, monitor.rcWork.top - monitor.rcMonitor.top};
        info->ptMaxSize = {monitor.rcWork.right - monitor.rcWork.left, monitor.rcWork.bottom - monitor.rcWork.top};
        return 0;
    }
    case WM_SIZE: if (dialog->edit) layout_activation(dialog); return 0;
    case WM_DPICHANGED: {
        const auto* rect = reinterpret_cast<RECT*>(l);
        SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        layout_activation(dialog); return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{}; BeginPaint(hwnd, &paint); paint_activation(dialog); EndPaint(hwnd, &paint);
        if (!dialog->presented) { dialog->presented = true; dialog->last_frame = GetTickCount64(); }
        return 0;
    }
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: {
        const HDC dc = reinterpret_cast<HDC>(w);
        Color color = reinterpret_cast<HWND>(l) == dialog->status ?
            (dialog->failed ? dialog->palette.danger : dialog->palette.text_secondary) : dialog->palette.text;
        SetTextColor(dc, RGB(BYTE(color.r * 255), BYTE(color.g * 255), BYTE(color.b * 255)));
        const bool edit = reinterpret_cast<HWND>(l) == dialog->edit;
        const Color card = edit ? solid_over(dialog->palette.input, dialog->palette.card) : dialog->palette.card;
        SetBkColor(dc, RGB(BYTE(card.r * 255), BYTE(card.g * 255), BYTE(card.b * 255)));
        return reinterpret_cast<LRESULT>(edit ? dialog->input_brush : dialog->card_brush);
    }
    case WM_DRAWITEM: {
        const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(l);
        const int i = item.CtlID == IDOK ? 0 : item.CtlID == 3002 ? 1 : int(item.CtlID) - 3003;
        if (i < 0 || i >= 5) return FALSE;
        wchar_t label[80]{}; GetWindowTextW(item.hwndItem, label, 80);
        const float hover = dialog->reduce_motion ? dialog->hover[i].target : dialog->hover[i].value;
        const float press = (item.itemState & ODS_SELECTED) ? 1 : dialog->reduce_motion ? dialog->press[i].target : dialog->press[i].value;
        if (i >= 2) return paint_native_traffic(item, dialog->palette, dialog->scale, hover, press, i - 2);
        return paint_native_button(item, dialog->palette, dialog->font, label, dialog->scale, hover, press,
            i == 0, i == 0 ? dialog->palette.card : dialog->palette.window, i != 0);
    }
    case WM_TIMER: {
        if (w != kActivationAnimation) break;
        const ULONGLONG now = GetTickCount64();
        float remaining = std::min(.25f, (now - dialog->last_frame) / 1000.0f); dialog->last_frame = now;
        bool scene_changed = false, focus_changed = false;
        bool button_changed[5]{};
        // Preserve wall-clock duration even when the first frame warms font caches.
        while (remaining > 0) {
            const float dt = std::min(.05f, remaining); remaining -= dt;
            scene_changed |= dialog->scene.update(dt) | dialog->status_fade.update(dt);
            if (dialog->reduce_motion) dialog->focus.snap(dialog->focus.target);
            else focus_changed |= dialog->focus.update(dt, motion::kHover);
            for (int i = 0; i < 5; ++i) {
                if (dialog->reduce_motion) { dialog->hover[i].snap(dialog->hover[i].target); dialog->press[i].snap(dialog->press[i].target); button_changed[i] = true; }
                else { button_changed[i] |= dialog->hover[i].update(dt, motion::kHover); button_changed[i] |= dialog->press[i].update(dt, motion::kPress); }
            }
        }
        bool active = dialog->scene.value != dialog->scene.target || dialog->status_fade.value != dialog->status_fade.target || dialog->focus.value != dialog->focus.target;
        for (int i = 0; i < 5; ++i) {
            active |= dialog->hover[i].value != dialog->hover[i].target || dialog->press[i].value != dialog->press[i].target;
            if (button_changed[i]) InvalidateRect(i == 0 ? dialog->button : i == 1 ? dialog->group : dialog->chrome[i - 2], nullptr, FALSE);
        }
        if (scene_changed) layout_activation(dialog);
        if (scene_changed || focus_changed) InvalidateRect(hwnd, nullptr, FALSE);
        if (dialog->closing && dialog->scene.value == 0) { DestroyWindow(hwnd); return 0; }
        if (!active) KillTimer(hwnd, kActivationAnimation);
        return 0;
    }
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS;
    case WM_COMMAND:
        if (LOWORD(w) == IDOK) begin_activation(dialog);
        if (LOWORD(w) == 3002) ShellExecuteW(hwnd, L"open", L"https://t.me/MapAdkillerr", nullptr, nullptr, SW_SHOWNORMAL);
        if (LOWORD(w) == 3005) SendMessageW(hwnd, WM_CLOSE, 0, 0);
        if (LOWORD(w) == 3006) ShowWindow(hwnd, SW_MINIMIZE);
        if (LOWORD(w) == 3007) ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
        if (LOWORD(w) == IDCANCEL) SendMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
    case kCompleted:
        dialog->busy = false;
        if (dialog->worker.joinable()) dialog->worker.join();
        dialog->failed = !dialog->success;
        if (dialog->success) SendMessageW(hwnd, WM_CLOSE, 0, 0);
        else {
            SetWindowTextW(dialog->status, dialog->error.c_str());
            SetWindowTextW(dialog->button, L"验证并继续");
            activation_status(dialog);
            EnableWindow(dialog->button, TRUE); EnableWindow(dialog->edit, TRUE); SetFocus(dialog->edit);
        }
        return 0;
    case WM_CLOSE:
        if (!dialog->busy && !dialog->closing) {
            dialog->closing = true;
            if (dialog->reduce_motion) DestroyWindow(hwnd);
            else { dialog->scene.target = 0; animate_activation(dialog); }
        }
        return 0;
    case WM_DESTROY: KillTimer(hwnd, kActivationAnimation); return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
} // namespace

bool authorized() { return deadline.load() > GetTickCount64(); }

std::wstring hook_path() {
    if (!authorized()) return {};
    std::lock_guard<std::mutex> lock(mutex);
    return verified_path;
}

std::wstring status_text() {
    std::lock_guard<std::mutex> lock(mutex);
    if (!authorized() && !session_token.empty() && message == L"云端验证成功 · 已绑定当前设备")
        return L"短会话正在续期，连接恢复后将自动恢复原开关";
    return message;
}

void request_refresh() {
    { std::lock_guard<std::mutex> lock(mutex); refresh_requested = true; }
    wake.notify_all();
}

void shutdown() {
    { std::lock_guard<std::mutex> lock(mutex); stop = true; }
    wake.notify_all();
    if (heartbeat.joinable()) heartbeat.join();
    deadline = 0;
    std::lock_guard<std::mutex> lock(mutex);
    if (locked_hook != INVALID_HANDLE_VALUE) { CloseHandle(locked_hook); locked_hook = INVALID_HANDLE_VALUE; }
    if (!verified_path.empty()) DeleteFileW(verified_path.c_str());
    verified_path.clear(); session_token.clear();
    if (!recovery_card.empty()) SecureZeroMemory(recovery_card.data(), recovery_card.size() * sizeof(wchar_t));
    recovery_card.clear();
}

bool startup(HINSTANCE instance, int argc, wchar_t** argv, bool& probe) {
    probe = argc >= 2 && _wcsicmp(argv[1], L"--license-check") == 0;
    device = machine_id();
    if (probe) {
        persist_card = false;
        if (argc != 4) return false;
        std::ifstream input(argv[2], std::ios::binary);
        std::string raw((std::istreambuf_iterator<char>(input)), {});
        std::wstring error;
        bool ok = authorize(utf8_to_wide(raw), false, error);
        if (ok) ok = authorize({}, true, error);
        std::ofstream output(argv[3], std::ios::binary | std::ios::trunc);
        output << (ok ? "PASS: TLS, machine binding, signed nonce, SHA256, authorized download, signed lease refresh\n" : "FAIL: " + wide_to_utf8(error) + "\n");
        shutdown();
        return ok;
    }
    ActivationDialog dialog;
    WNDCLASSW type{};
    type.lpfnWndProc = activation_proc; type.hInstance = instance;
    type.lpszClassName = L"TeleVipCloudActivation"; type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&type);
    const float scale = GetDpiForSystem() / 96.0f;
    RECT bounds{0, 0, int(560 * scale), int(592 * scale)};
    constexpr DWORD style = WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN;
    const HWND window = CreateWindowExW(0, type.lpszClassName, L"TeleVip · 云端卡密验证",
        style, CW_USEDEFAULT, CW_USEDEFAULT,
        bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, instance, &dialog);
    if (!window) return false;
    ShowWindow(window, SW_SHOWNORMAL);
    MSG msg{};
    while (IsWindow(window) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    if (dialog.worker.joinable()) dialog.worker.join();
    if (dialog.font) DeleteObject(dialog.font);
    if (dialog.card_brush) DeleteObject(dialog.card_brush);
    if (dialog.input_brush) DeleteObject(dialog.input_brush);
    if (!dialog.success) return false;
    start_heartbeat();
    return true;
}
} // namespace tv::license
