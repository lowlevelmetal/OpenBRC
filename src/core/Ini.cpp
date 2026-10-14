#include "core/Ini.h"

#include "core/File.h"
#include "core/StringUtil.h"

#include <format>

namespace brc {

bool IniFile::load(const std::filesystem::path& path) {
    auto data = file::readText(path);
    if (!data)
        return false;
    parse(*data);
    return true;
}

bool IniFile::save(const std::filesystem::path& path) const {
    return file::writeAtomic(path, serialize());
}

void IniFile::parse(std::string_view text) {
    m_sections.clear();
    m_sections.push_back(Section{});

    // Skip a UTF-8 BOM (NSIS and Notepad may write one).
    if (text.starts_with("\xEF\xBB\xBF"))
        text.remove_prefix(3);

    for (std::string_view raw : str::split(text, '\n')) {
        if (!raw.empty() && raw.back() == '\r')
            raw.remove_suffix(1);
        const std::string_view line = str::trim(raw);

        if (line.empty()) {
            m_sections.back().lines.push_back({Line::Kind::Blank, {}, {}});
        } else if (line[0] == ';' || line[0] == '#') {
            m_sections.back().lines.push_back({Line::Kind::Comment, std::string(line), {}});
        } else if (line.front() == '[' && line.back() == ']') {
            m_sections.push_back(Section{std::string(str::trim(line.substr(1, line.size() - 2))), {}});
        } else if (const auto eq = line.find('='); eq != std::string_view::npos) {
            std::string key(str::trim(line.substr(0, eq)));
            std::string_view value = str::trim(line.substr(eq + 1));
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                value = value.substr(1, value.size() - 2);
            m_sections.back().lines.push_back({Line::Kind::Pair, std::move(key), std::string(value)});
        } else {
            // Preserve unparseable lines as comments rather than dropping them.
            m_sections.back().lines.push_back({Line::Kind::Comment, std::string(line), {}});
        }
    }

    // Drop the trailing blank produced by a final newline.
    for (auto& s : m_sections)
        while (!s.lines.empty() && s.lines.back().kind == Line::Kind::Blank)
            s.lines.pop_back();
}

std::string IniFile::serialize() const {
    std::string out;
    bool first = true;
    for (const auto& s : m_sections) {
        if (!s.name.empty()) {
            if (!first)
                out += '\n';
            out += std::format("[{}]\n", s.name);
        }
        for (const auto& l : s.lines) {
            switch (l.kind) {
            case Line::Kind::Blank: out += '\n'; break;
            case Line::Kind::Comment: out += l.text + '\n'; break;
            case Line::Kind::Pair: out += std::format("{}={}\n", l.text, l.value); break;
            case Line::Kind::Section: break;
            }
        }
        first = first && s.name.empty() && s.lines.empty();
    }
    return out;
}

IniFile::Section* IniFile::findSection(std::string_view name) {
    for (auto& s : m_sections)
        if (str::iequals(s.name, name))
            return &s;
    return nullptr;
}

const IniFile::Section* IniFile::findSection(std::string_view name) const {
    return const_cast<IniFile*>(this)->findSection(name);
}

IniFile::Section& IniFile::sectionFor(std::string_view name) {
    if (m_sections.empty())
        m_sections.push_back(Section{});
    if (auto* s = findSection(name))
        return *s;
    m_sections.push_back(Section{std::string(name), {}});
    return m_sections.back();
}

std::optional<std::string> IniFile::get(std::string_view section, std::string_view key) const {
    const auto* s = findSection(section);
    if (!s)
        return std::nullopt;
    // Last definition wins, matching the Win32 profile API only loosely but
    // making hand-edited duplicates behave predictably.
    for (auto it = s->lines.rbegin(); it != s->lines.rend(); ++it)
        if (it->kind == Line::Kind::Pair && str::iequals(it->text, key))
            return it->value;
    return std::nullopt;
}

std::string IniFile::getString(std::string_view section, std::string_view key, std::string_view fallback) const {
    auto v = get(section, key);
    return v ? *v : std::string(fallback);
}

long long IniFile::getInt(std::string_view section, std::string_view key, long long fallback) const {
    auto v = get(section, key);
    if (!v)
        return fallback;
    return str::parseInt(*v).value_or(fallback);
}

double IniFile::getDouble(std::string_view section, std::string_view key, double fallback) const {
    auto v = get(section, key);
    if (!v)
        return fallback;
    return str::parseDouble(*v).value_or(fallback);
}

bool IniFile::getBool(std::string_view section, std::string_view key, bool fallback) const {
    auto v = get(section, key);
    if (!v)
        return fallback;
    return str::parseBool(*v).value_or(fallback);
}

void IniFile::set(std::string_view section, std::string_view key, std::string_view value) {
    auto& s = sectionFor(section);
    for (auto it = s.lines.rbegin(); it != s.lines.rend(); ++it) {
        if (it->kind == Line::Kind::Pair && str::iequals(it->text, key)) {
            it->value = std::string(value);
            return;
        }
    }
    s.lines.push_back({Line::Kind::Pair, std::string(key), std::string(value)});
}

void IniFile::setInt(std::string_view section, std::string_view key, long long value) {
    set(section, key, std::to_string(value));
}

void IniFile::setDouble(std::string_view section, std::string_view key, double value) {
    set(section, key, std::format("{}", value));
}

void IniFile::setBool(std::string_view section, std::string_view key, bool value) {
    set(section, key, value ? "true" : "false");
}

bool IniFile::remove(std::string_view section, std::string_view key) {
    auto* s = findSection(section);
    if (!s)
        return false;
    return std::erase_if(s->lines, [&](const Line& l) {
               return l.kind == Line::Kind::Pair && str::iequals(l.text, key);
           }) > 0;
}

bool IniFile::hasSection(std::string_view section) const { return findSection(section) != nullptr; }

std::vector<std::string> IniFile::keys(std::string_view section) const {
    std::vector<std::string> out;
    if (const auto* s = findSection(section))
        for (const auto& l : s->lines)
            if (l.kind == Line::Kind::Pair)
                out.push_back(l.text);
    return out;
}

void IniFile::merge(const IniFile& other) {
    for (const auto& s : other.m_sections)
        for (const auto& l : s.lines)
            if (l.kind == Line::Kind::Pair)
                set(s.name, l.text, l.value);
}

} // namespace brc
