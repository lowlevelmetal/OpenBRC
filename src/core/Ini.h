#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace brc {

// Minimal INI document that preserves section/key order and comments so files
// edited by users (or written by the Windows installer via NSIS WriteINIStr)
// round-trip cleanly. Section and key lookups are case-insensitive.
class IniFile {
public:
    bool load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;
    void parse(std::string_view text);
    std::string serialize() const;

    std::optional<std::string> get(std::string_view section, std::string_view key) const;
    std::string getString(std::string_view section, std::string_view key, std::string_view fallback = {}) const;
    long long getInt(std::string_view section, std::string_view key, long long fallback) const;
    double getDouble(std::string_view section, std::string_view key, double fallback) const;
    bool getBool(std::string_view section, std::string_view key, bool fallback) const;

    void set(std::string_view section, std::string_view key, std::string_view value);
    void setInt(std::string_view section, std::string_view key, long long value);
    void setDouble(std::string_view section, std::string_view key, double value);
    void setBool(std::string_view section, std::string_view key, bool value);
    bool remove(std::string_view section, std::string_view key);

    bool hasSection(std::string_view section) const;
    std::vector<std::string> keys(std::string_view section) const;

    // Copies every key from `other` into this document, overriding values.
    void merge(const IniFile& other);

private:
    struct Line {
        enum class Kind { Blank, Comment, Section, Pair } kind;
        std::string text;  // raw text for Blank/Comment, name for Section, key for Pair
        std::string value; // Pair only
    };
    struct Section {
        std::string name;
        std::vector<Line> lines;
    };

    Section* findSection(std::string_view name);
    const Section* findSection(std::string_view name) const;
    Section& sectionFor(std::string_view name);

    std::vector<Section> m_sections; // m_sections[0] is the unnamed preamble
};

} // namespace brc
