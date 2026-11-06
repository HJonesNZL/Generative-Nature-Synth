#pragma once
#include <string>
#include <string_view>

namespace atlas::detail {
inline std::string quoteJson(std::string_view value) {
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const auto byte : value) {
        const auto c = static_cast<unsigned char>(byte);
        if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 32) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
        else result += static_cast<char>(c);
    }
    return result + "\"";
}
}
