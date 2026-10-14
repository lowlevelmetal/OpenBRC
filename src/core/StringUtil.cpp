#include "core/StringUtil.h"

#include <algorithm>
#include <charconv>

namespace brc::str {

std::string lower(std::string_view s) {
    std::string out(s);
    std::ranges::transform(out, out.begin(), toLower);
    return out;
}

std::string upper(std::string_view s) {
    std::string out(s);
    std::ranges::transform(out, out.begin(), toUpper);
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::ranges::equal(a, b, [](char x, char y) { return toLower(x) == toLower(y); });
}

bool istartsWith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && iequals(s.substr(0, prefix.size()), prefix);
}

bool iendsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && iequals(s.substr(s.size() - suffix.size()), suffix);
}

std::string_view trim(std::string_view s) {
    constexpr std::string_view ws = " \t\r\n\f\v";
    const auto b = s.find_first_not_of(ws);
    if (b == std::string_view::npos)
        return {};
    const auto e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (true) {
        const auto pos = s.find(sep, start);
        parts.push_back(s.substr(start, pos - start));
        if (pos == std::string_view::npos)
            break;
        start = pos + 1;
    }
    return parts;
}

std::optional<long long> parseInt(std::string_view s) {
    s = trim(s);
    int base = 10;
    bool negative = false;
    if (!s.empty() && (s[0] == '-' || s[0] == '+')) {
        negative = s[0] == '-';
        s.remove_prefix(1);
    }
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s.remove_prefix(2);
    }
    long long value = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value, base);
    if (ec != std::errc{} || ptr != s.data() + s.size() || s.empty())
        return std::nullopt;
    return negative ? -value : value;
}

std::optional<double> parseDouble(std::string_view s) {
    s = trim(s);
    if (!s.empty() && s[0] == '+')
        s.remove_prefix(1);
    double value = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr != s.data() + s.size() || s.empty())
        return std::nullopt;
    return value;
}

std::optional<bool> parseBool(std::string_view s) {
    s = trim(s);
    for (auto t : {"1", "true", "yes", "on"})
        if (iequals(s, t))
            return true;
    for (auto f : {"0", "false", "no", "off"})
        if (iequals(s, f))
            return false;
    return std::nullopt;
}

std::filesystem::path toPath(std::string_view utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string fromPath(const std::filesystem::path& path) {
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::string normalizeVirtualPath(std::string_view path) {
    std::vector<std::string> parts;
    std::string current;
    auto flush = [&] {
        if (current.empty() || current == ".") {
        } else if (current == "..") {
            if (!parts.empty())
                parts.pop_back();
        } else {
            parts.push_back(std::move(current));
        }
        current.clear();
    };
    for (char c : path) {
        if (c == '/' || c == '\\')
            flush();
        else
            current.push_back(toLower(c));
    }
    flush();

    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i)
            out.push_back('/');
        out += parts[i];
    }
    return out;
}

} // namespace brc::str
