#pragma once

#include "common.h"

namespace tv {

inline std::wstring normalize_translation_language(std::wstring language) {
    language = lower(trim(language));
    std::replace(language.begin(), language.end(), L'_', L'-');
    if (language.empty()) return L"auto";
    if (language == L"zh" || language == L"zh-cn" || language == L"zh-sg" ||
        language == L"zh-hans" || language == L"zh-hans-cn") return L"zh-CN";
    if (language == L"zh-tw" || language == L"zh-hk" || language == L"zh-hant" ||
        language == L"zh-hant-hk" || language == L"zh-hant-tw") return L"zh-TW";
    return language;
}

inline std::string translation_cache_key(const std::wstring& text,
                                         const std::wstring& source,
                                         const std::wstring& target) {
    // v2 also leaves behind responses cached by the old fallback JSON parser.
    return "v2\n" + wide_to_utf8(normalize_translation_language(source)) + "\n" +
           wide_to_utf8(normalize_translation_language(target)) + "\n" + wide_to_utf8(text);
}

inline bool needs_translation(const std::wstring& text, const std::wstring& target,
                             const std::wstring& source) {
    const auto from = normalize_translation_language(source);
    if (from != L"auto" && from == normalize_translation_language(target)) return false;
    for (const wchar_t ch : text) {
        WORD type = 0;
        if (GetStringTypeW(CT_CTYPE1, &ch, 1, &type) && (type & C1_ALPHA)) return true;
    }
    return false;
}

} // namespace tv
