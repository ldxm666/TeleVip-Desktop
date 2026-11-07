#pragma once

#include "common.h"

namespace tv {

enum class Lang {
    Chinese,
    English,
};

class I18n {
public:
    void set_lang(Lang lang);
    Lang lang() const { return lang_; }
    bool is_zh() const { return lang_ == Lang::Chinese; }
    const std::wstring& get(const std::wstring& key) const;
    std::wstring t(const std::wstring& key) const;
    std::wstring t(const std::wstring& key,
                   const std::unordered_map<std::wstring, std::wstring>& values) const;
    const wchar_t* lang_label() const { return is_zh() ? L"中文" : L"English"; }

private:
    Lang lang_ = Lang::Chinese;
};

} // namespace tv
