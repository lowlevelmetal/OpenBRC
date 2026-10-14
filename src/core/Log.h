#pragma once

#include <filesystem>
#include <format>
#include <functional>
#include <string_view>

namespace brc::log {

enum class Level { Trace, Debug, Info, Warn, Error };

void setLevel(Level level);
Level level();
inline bool enabled(Level lvl) { return lvl >= level(); }

// Mirror all output into a file (truncated on open). Pass an empty path to close.
bool setFile(const std::filesystem::path& path);

// Additional sink, e.g. an in-game console. Called with the formatted line.
using Sink = std::function<void(Level, std::string_view)>;
int addSink(Sink sink);
void removeSink(int id);

void write(Level lvl, std::string_view message);

std::string_view levelName(Level lvl);
bool parseLevel(std::string_view text, Level& out);

template <class... Args>
void trace(std::format_string<Args...> fmt, Args&&... args) {
    if (enabled(Level::Trace))
        write(Level::Trace, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    if (enabled(Level::Debug))
        write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    if (enabled(Level::Info))
        write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    if (enabled(Level::Warn))
        write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    if (enabled(Level::Error))
        write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace brc::log
