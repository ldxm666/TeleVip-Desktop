#include "../news_model.h"
#include "../signed_content.h"
#include "license_signature_fixture.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace tv;
    assert(news::version_parts("2026.11.11.1"));
    assert(!news::version_parts("2026.11.11.1.extra"));
    assert(!news::version_parts("2026.13.10.2"));
    assert(!news::version_parts("2026.11.10.-1"));
    assert(news::official_release("https://github.com/ldxm666/TeleVip-Desktop/releases/tag/v2026.11.12.1"));
    for (const auto* url : {"http://github.com/ldxm666/TeleVip-Desktop/releases/latest", "https://github.com.evil.test/ldxm666/TeleVip-Desktop/releases/latest",
        "https://github.com/ldxm666/TeleVip-Desktop/releases/latest?redirect=evil", "file:///C:/evil.exe", "https://github.com/other/repo/releases/latest"})
        assert(!news::official_release(url));
    const std::string json = R"({"schema":"televip-news-1","revision":"1794000000000","published_at":"1794000000","version":"2026.11.12.1","update_url":"https://github.com/ldxm666/TeleVip-Desktop/releases/tag/v2026.11.12.1","announcement_id":"notice-1","title_zh":"公告","body_en":"Hello\nworld"})";
    const auto m = news::parse(json); assert(m && m->has_update() && m->has_announcement());
    assert(m->body_en == "Hello\nworld");
    assert(!news::parse("{}")); assert(!news::parse(std::string(12001, 'x')));
    auto invalid = json; invalid.replace(invalid.find("notice-1"), 8, "bad id !"); assert(!news::parse(invalid));
    const std::string text = "TeleVip verification fixture v1";
    std::vector<BYTE> payload(text.begin(), text.end()), signature(std::begin(kFixtureSignature), std::end(kFixtureSignature));
    assert(signed_content::verify(payload, signature)); payload[0] ^= 1;
    assert(!signed_content::verify(payload, signature));
    assert(signed_content::decode("%%%invalid%%%").empty());
    assert(signed_content::decode(std::string(24001, 'A')).empty());
    std::cout << "news: versions, official release allowlist, bounded bilingual payload and real CNG tamper checks passed\n";
}
