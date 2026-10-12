#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

// Parse only digits, checking before multiplication so overflow cannot saturate.
template<class UInt>
bool parse_unsigned(std::wstring_view text, UInt& out, unsigned base = 10) {
    if (text.empty()) return false;
    UInt value = 0;
    for (wchar_t c : text) {
        unsigned digit = c >= L'0' && c <= L'9' ? c - L'0'
            : c >= L'a' && c <= L'f' ? c - L'a' + 10
            : c >= L'A' && c <= L'F' ? c - L'A' + 10 : base;
        if (digit >= base || value > (std::numeric_limits<UInt>::max() - digit) / base)
            return false;
        value = value * base + digit;
    }
    out = value;
    return true;
}

inline bool parse_u32_strict(const std::wstring& text, uint32_t& out) {
    return parse_unsigned(text, out);
}

inline bool parse_hwnd_strict(std::wstring_view text, uint64_t& out) {
    unsigned base = 10;
    if (text.starts_with(L"0x") || text.starts_with(L"0X")) {
        text.remove_prefix(2);
        base = 16;
    }
    return parse_unsigned(text, out, base) && out != 0;
}

inline bool json_space(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

// Locate a member of the outer object, allowing JSON whitespace around ':'.
// Skip strings and nested values so their contents cannot impersonate a key.
inline size_t json_value_position(const std::wstring& json, const std::wstring& key) {
    int depth = 0;
    for (size_t i = 0; i < json.size(); ++i) {
        wchar_t c = json[i];
        if (c == L'{' || c == L'[') { ++depth; continue; }
        if (c == L'}' || c == L']') { --depth; continue; }
        if (c != L'"') continue;
        size_t start = ++i;
        while (i < json.size() && json[i] != L'"') {
            if (json[i] == L'\\') ++i;
            ++i;
        }
        if (i == json.size()) return std::wstring::npos;
        size_t end = i, pos = i + 1;
        while (pos < json.size() && json_space(json[pos])) ++pos;
        if (depth != 1 || json.compare(start, end - start, key) != 0 ||
            pos == json.size() || json[pos] != L':') continue;
        ++pos;
        while (pos < json.size() && json_space(json[pos])) ++pos;
        return pos;
    }
    return std::wstring::npos;
}

inline std::wstring json_token(const std::wstring& json, const std::wstring& key) {
    auto start = json_value_position(json, key);
    if (start == std::wstring::npos) return L"";
    auto end = start;
    while (end < json.size() && !json_space(json[end]) &&
           json[end] != L',' && json[end] != L'}' && json[end] != L']') ++end;
    return json.substr(start, end - start);
}

inline bool json_decode_string(const std::wstring& json, size_t start,
                               std::wstring& out, size_t& end) {
    if (start >= json.size() || json[start] != L'"') return false;
    auto hex = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        if (c >= L'A' && c <= L'F') return c - L'A' + 10;
        return -1;
    };
    size_t i = start + 1;
    while (i < json.size()) {
        wchar_t c = json[i];
        if (c == L'"') { end = i + 1; return true; }
        if (c < 0x20) return false;
        if (c != L'\\') { out += c; ++i; continue; }
        if (i + 1 >= json.size()) return false;
        wchar_t esc = json[i + 1];
        i += 2;
        switch (esc) {
            case L'"': out += L'"'; break;
            case L'\\': out += L'\\'; break;
            case L'/': out += L'/'; break;
            case L'b': out += L'\b'; break;
            case L'f': out += L'\f'; break;
            case L'n': out += L'\n'; break;
            case L'r': out += L'\r'; break;
            case L't': out += L'\t'; break;
            case L'u': {
                if (i + 4 > json.size()) return false;
                uint32_t cp = 0;
                for (size_t k = 0; k < 4; ++k) {
                    int d = hex(json[i + k]);
                    if (d < 0) return false;
                    cp = (cp << 4) | static_cast<uint32_t>(d);
                }
                i += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (i + 6 > json.size() || json[i] != L'\\' || json[i + 1] != L'u')
                        return false;
                    uint32_t low = 0;
                    for (size_t k = 0; k < 4; ++k) {
                        int d = hex(json[i + 2 + k]);
                        if (d < 0) return false;
                        low = (low << 4) | static_cast<uint32_t>(d);
                    }
                    if (low < 0xDC00 || low > 0xDFFF) return false;
                    i += 6;
                    out += static_cast<wchar_t>(cp);
                    out += static_cast<wchar_t>(low);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return false;
                } else {
                    out += static_cast<wchar_t>(cp);
                }
                break;
            }
            default: return false;
        }
    }
    return false;
}
