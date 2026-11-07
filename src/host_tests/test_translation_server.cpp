#include "core.h"
#include "native_translate.h"
#include "overlay.h"
#include "tools.h"

#include <iostream>
#include <stdexcept>

namespace {
std::atomic<int> upstream_calls{0};
std::atomic<int> failures{0};

void check(bool ok, const char* message) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

struct Socket {
    SOCKET value = INVALID_SOCKET;
    ~Socket() { if (value != INVALID_SOCKET) closesocket(value); }
};

std::string encode(std::string_view text) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (const unsigned char ch : text) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9')) {
            result.push_back(ch);
        } else {
            result += '%';
            result += hex[ch >> 4];
            result += hex[ch & 15];
        }
    }
    return result;
}

std::string request(uint16_t port, std::string_view text,
                    std::string_view from = "en", std::string_view to = "zh") {
    Socket socket;
    socket.value = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket.value == INVALID_SOCKET) throw std::runtime_error("socket failed");
    DWORD timeout = 3000;
    setsockopt(socket.value, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (connect(socket.value, reinterpret_cast<sockaddr*>(&address), sizeof(address))) {
        throw std::runtime_error("connect failed");
    }
    const std::string message = "GET /translate?q=" + encode(text) + "&from=" +
        std::string(from) + "&to=" + std::string(to) +
        " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    if (send(socket.value, message.data(), static_cast<int>(message.size()), 0) !=
        static_cast<int>(message.size())) throw std::runtime_error("send failed");
    std::string response;
    char buffer[4096];
    for (;;) {
        const int got = recv(socket.value, buffer, sizeof(buffer), 0);
        if (got <= 0) throw std::runtime_error("incomplete HTTP response");
        response.append(buffer, static_cast<size_t>(got));
        const size_t header = response.find("\r\n\r\n");
        const size_t length = response.find("Content-Length: ");
        if (header != std::string::npos && length != std::string::npos &&
            response.size() >= header + 4 + std::stoull(response.substr(length + 16))) {
            return response;
        }
    }
}

std::string body(const std::string& response) {
    return response.substr(response.find("\r\n\r\n") + 4);
}

std::string read_text(const std::wstring& path) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

size_t count_of(const std::string& text, std::string_view needle) {
    size_t total = 0;
    for (size_t pos = text.find(needle); pos != std::string::npos;
         pos = text.find(needle, pos + needle.size())) {
        ++total;
    }
    return total;
}
} // namespace

// Exercise the real HTTP server with a deterministic in-process upstream.
// No network provider, Telegram process, config file or mouse input is used.
namespace tv {
void translate_probe_upstream() {}
void translate_prewarm(int) {}
TranslateResult translate_text_to(const std::wstring& text, const std::wstring& target,
                                  const std::wstring& source) {
    upstream_calls.fetch_add(1);
    if (text == L"__fail__") return {false, {}, L"test upstream unavailable"};
    return {true, source + L"->" + target + L":" + text, {}};
}
bool ocr_region_lines(HWND, RECT, std::vector<OcrLineHit>&, std::wstring&) { return false; }
} // namespace tv

int main() {
    try {
        tv::TranslationServer server;
        const auto port = static_cast<uint16_t>(54000 + GetCurrentProcessId() % 1000);
        if (!server.start(port)) throw std::runtime_error("server start failed");
        const auto first = request(server.port(), "Hello \"friend\"\nC:\\Temp\t");
        check(first.rfind("HTTP/1.1 200 ", 0) == 0, "successful response status");
        check(body(first) == R"([[["en->zh-CN:Hello \"friend\"\nC:\\Temp\t"]]])",
              "Telegram segmented JSON shape and escaped string");
        const int before = upstream_calls.load();
        const auto second = request(server.port(), "Hello \"friend\"\nC:\\Temp\t", "fr");
        check(upstream_calls.load() == before + 1, "source languages must not share cache entries");
        check(body(second).find("fr->zh-CN:") != std::string::npos, "per-request source language");
        check(body(request(server.port(), "Hello \"friend\"\nC:\\Temp\t")) == body(first),
              "repeat request uses the matching cache entry");
        check(upstream_calls.load() == before + 1, "repeat request cache hit");
        check(body(request(server.port(), "Literal &amp;lt;x&amp;gt;")) ==
              R"([[["en->zh-CN:Literal &lt;x&gt;"]]])", "HTML entities decode exactly once");
        std::vector<std::thread> clients;
        for (int i = 0; i < 24; ++i) {
            clients.emplace_back([&server, i] {
                try {
                    const std::string source = i % 2 ? "fr" : "en";
                    const std::string text = "burst-" + std::to_string(i);
                    check(body(request(server.port(), text, source)) ==
                          "[[[\"" + source + "->zh-CN:" + text + "\"]]]",
                          "concurrent requests retain their own source");
                } catch (const std::exception& error) {
                    check(false, error.what());
                }
            });
        }
        for (auto& client : clients) client.join();
        check(body(request(server.port(), "unknown-source", "auto", "en")) ==
              R"([[["auto->en:unknown-source"]]])", "auto does not inherit another request's source");
        const auto failed = request(server.port(), "__fail__");
        check(failed.rfind("HTTP/1.1 502 ", 0) == 0, "upstream failure remains retryable");
        check(server.failures() == 1, "upstream failure counter");
        const int calls_before_retry = upstream_calls.load();
        check(request(server.port(), "__fail__").rfind("HTTP/1.1 502 ", 0) == 0 &&
              upstream_calls.load() == calls_before_retry + 1, "failure is not cached as a successful original");
        server.stop();

        // translate_engine: the persisted 0|1 contract and the template surgery it
        // drives, on a fake install -- never the real Telegram data folder.
        tv::Settings defaults;
        check(defaults.translate_engine == 0, "engine defaults to Telegram built-in");
        defaults.translate_engine = 1;
        const std::string saved = tv::settings_to_json(defaults);
        check(saved.find("\"translate_engine\": 1") != std::string::npos,
              "engine 1 is persisted as translate_engine: 1");
        check(tv::settings_from_json(saved).translate_engine == 1, "engine 1 round-trips");
        check(tv::settings_from_json(std::string("{\"translate_engine\": 0}")).translate_engine == 0,
              "engine 0 round-trips");
        check(tv::settings_from_json(std::string("{\"translate_engine\": 3}")).translate_engine == 0,
              "an unknown engine value falls back to built-in");
        check(tv::settings_from_json(std::string("{}")).translate_engine == 0,
              "a missing engine key means built-in");

        const std::wstring root = (std::filesystem::current_path() / L"build" / L"tests" /
                                   L"engine" / L"Telegram").wstring();
        const std::wstring tdata = root + L"\\tdata";
        const std::wstring exe = root + L"\\Telegram.exe";
        const std::wstring options = tdata + L"\\experimental_options.json";
        tv::ensure_dir(tdata);
        {
            std::ofstream file(std::filesystem::path(options), std::ios::binary | std::ios::trunc);
            file << R"({"keep-me":1})";
        }
        std::wstring error;
        check(tv::configure_native_translate(exe, 48620, error),
              "engine 1 writes the url template");
        check(tv::native_translate_configured(exe, 48620),
              "the template is visible where Telegram reads it");
        const std::string written = read_text(options);
        check(written.find("\"keep-me\"") != std::string::npos,
              "unrelated experimental options survive the write");
        check(count_of(written, "translate-url-template") == 1,
              "the template is written once, never appended");
        check(tv::configure_native_translate(exe, 48620, error),
              "writing the same template twice succeeds");
        check(count_of(read_text(options), "translate-url-template") == 1,
              "a second write replaces instead of duplicating");
        check(tv::clear_native_translate(exe, error), "engine 0 clears the template");
        check(!tv::native_translate_configured(exe, 48620), "the template is gone after a clear");
        check(read_text(options) == R"({"keep-me":1})",
              "clearing keeps the other options intact");
        {
            std::ofstream file(std::filesystem::path(options), std::ios::binary | std::ios::trunc);
            file << "{}";
        }
        check(tv::configure_native_translate(exe, 48620, error) &&
                  tv::clear_native_translate(exe, error),
              "an empty options object round-trips");
        check(read_text(options) == "{}\n",
              "an emptied options object collapses back to {}");
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "FAIL: " << error.what() << '\n';
    }
    if (failures) return 1;
    std::cout << "translation server: wire format, escaping, source isolation, cache, "
                 "HTML decoding and retryable failures passed\n";
}
