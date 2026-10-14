#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace brc::str {

constexpr char toLower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }
constexpr char toUpper(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }

std::string lower(std::string_view s);
std::string upper(std::string_view s);
bool iequals(std::string_view a, std::string_view b);
bool istartsWith(std::string_view s, std::string_view prefix);
bool iendsWith(std::string_view s, std::string_view suffix);
std::string_view trim(std::string_view s);
std::vector<std::string_view> split(std::string_view s, char sep);

std::optional<long long> parseInt(std::string_view s);
std::optional<double> parseDouble(std::string_view s);
std::optional<bool> parseBool(std::string_view s);

// UTF-8 <-> std::filesystem::path. On Windows, constructing a path from a
// narrow std::string uses the ANSI code page, so always go through these.
std::filesystem::path toPath(std::string_view utf8);
std::string fromPath(const std::filesystem::path& path);

// Canonical form for archive/VFS paths: lowercase, '/' separators, no leading
// or trailing separators, no "." components, ".." resolved where possible.
std::string normalizeVirtualPath(std::string_view path);

} // namespace brc::str
