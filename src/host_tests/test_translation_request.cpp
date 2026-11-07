#include "translation_request.h"

#include <cassert>
#include <iostream>

int main() {
    using tv::needs_translation;
    assert(needs_translation(L"Привет мир", L"zh-CN", L"ru"));
    assert(needs_translation(L"مرحبا بالعالم", L"zh-CN", L"ar"));
    assert(needs_translation(L"こんにちは", L"zh-CN", L"ja"));
    assert(needs_translation(L"東京", L"zh-CN", L"ja"));
    assert(!needs_translation(L"你好", L"zh-CN", L"zh"));
    assert(needs_translation(L"你好", L"zh-TW", L"zh-CN"));
    assert(!needs_translation(L"123 45! \U0001F600", L"zh-CN", L"en"));
    assert(tv::normalize_translation_language(L"ZH_hans") == L"zh-CN");
    assert(tv::normalize_translation_language(L"zh_HANT") == L"zh-TW");
    assert(tv::normalize_translation_language(L"") == L"auto");
    assert(tv::translation_cache_key(L"chat", L"en", L"zh") !=
           tv::translation_cache_key(L"chat", L"fr", L"zh"));
    assert(tv::translation_cache_key(L"chat", L"en", L"zh") ==
           tv::translation_cache_key(L"chat", L"EN", L"ZH_hans"));
    assert(tv::translation_cache_key(L"chat", L"auto", L"zh").rfind("v2\n", 0) == 0);
    std::cout << "translation request: non-Latin text, language normalization and cache isolation passed\n";
}
