#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace tv {

inline std::optional<std::string> read_json_string(std::string_view text, size_t& pos) {
    if (pos >= text.size() || text[pos++] != '"') return std::nullopt;
    std::string out;
    const auto hex_unit = [&text, &pos](uint32_t& value) {
        if (pos + 4 > text.size()) return false;
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char ch = text[pos++];
            value <<= 4;
            if (ch >= '0' && ch <= '9') value |= ch - '0';
            else if (ch >= 'a' && ch <= 'f') value |= ch - 'a' + 10;
            else if (ch >= 'A' && ch <= 'F') value |= ch - 'A' + 10;
            else return false;
        }
        return true;
    };
    while (pos < text.size()) {
        const unsigned char ch = text[pos++];
        if (ch == '"') return out;
        if (ch < 0x20) return std::nullopt;
        if (ch != '\\') { out.push_back(static_cast<char>(ch)); continue; }
        if (pos >= text.size()) return std::nullopt;
        switch (text[pos++]) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
            uint32_t code = 0;
            if (!hex_unit(code)) return std::nullopt;
            if (code >= 0xD800 && code <= 0xDBFF) {
                if (pos + 2 > text.size() || text[pos] != '\\' || text[pos + 1] != 'u') return std::nullopt;
                pos += 2;
                uint32_t low = 0;
                if (!hex_unit(low) || low < 0xDC00 || low > 0xDFFF) return std::nullopt;
                code = 0x10000 + ((code - 0xD800) << 10) + low - 0xDC00;
            } else if (code >= 0xDC00 && code <= 0xDFFF) {
                return std::nullopt;
            }
            if (code <= 0x7F) {
                out += static_cast<char>(code);
            } else if (code <= 0x7FF) {
                out += static_cast<char>(0xC0 | (code >> 6));
                out += static_cast<char>(0x80 | (code & 0x3F));
            } else if (code <= 0xFFFF) {
                out += static_cast<char>(0xE0 | (code >> 12));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (code >> 18));
                out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
            }
            break;
        }
        default: return std::nullopt;
        }
    }
    return std::nullopt;
}

inline std::string json_string_member(std::string_view text, std::string_view key) {
    const auto whitespace = [&text](size_t& pos) {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' ||
               text[pos] == '\r' || text[pos] == '\n')) ++pos;
    };
    for (size_t pos = 0; pos < text.size();) {
        if (text[pos] != '"') { ++pos; continue; }
        const auto name = read_json_string(text, pos);
        if (!name) return {};
        whitespace(pos);
        if (pos >= text.size() || text[pos] != ':' || *name != key) continue;
        ++pos;
        whitespace(pos);
        return read_json_string(text, pos).value_or(std::string());
    }
    return {};
}

} // namespace tv
