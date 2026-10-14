#include "core/Log.h"

#include "core/StringUtil.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <vector>

namespace brc::log {
namespace {

struct State {
    std::mutex mutex;
    std::ofstream file;
    std::vector<std::pair<int, Sink>> sinks;
    int nextSinkId = 1;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

State& state() {
    static State s;
    return s;
}

#ifdef NDEBUG
std::atomic<Level> g_level{Level::Info};
#else
std::atomic<Level> g_level{Level::Debug};
#endif

} // namespace

void setLevel(Level lvl) { g_level.store(lvl, std::memory_order_relaxed); }
Level level() { return g_level.load(std::memory_order_relaxed); }

bool setFile(const std::filesystem::path& path) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    if (s.file.is_open())
        s.file.close();
    if (path.empty())
        return true;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    s.file.open(path, std::ios::out | std::ios::trunc);
    return s.file.is_open();
}

int addSink(Sink sink) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    int id = s.nextSinkId++;
    s.sinks.emplace_back(id, std::move(sink));
    return id;
}

void removeSink(int id) {
    auto& s = state();
    std::lock_guard lock(s.mutex);
    std::erase_if(s.sinks, [id](const auto& p) { return p.first == id; });
}

std::string_view levelName(Level lvl) {
    switch (lvl) {
    case Level::Trace: return "trace";
    case Level::Debug: return "debug";
    case Level::Info: return "info";
    case Level::Warn: return "warn";
    case Level::Error: return "error";
    }
    return "?";
}

bool parseLevel(std::string_view text, Level& out) {
    for (Level lvl : {Level::Trace, Level::Debug, Level::Info, Level::Warn, Level::Error}) {
        if (str::iequals(text, levelName(lvl))) {
            out = lvl;
            return true;
        }
    }
    return false;
}

void write(Level lvl, std::string_view message) {
    auto& s = state();
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - s.start).count();
    std::string line = std::format("[{:9.3f}] {:5} {}", elapsed, levelName(lvl), message);

    std::lock_guard lock(s.mutex);
    std::FILE* stream = lvl >= Level::Warn ? stderr : stdout;
    std::fwrite(line.data(), 1, line.size(), stream);
    std::fputc('\n', stream);
    if (lvl >= Level::Warn)
        std::fflush(stream);
    if (s.file.is_open()) {
        s.file << line << '\n';
        if (lvl >= Level::Warn)
            s.file.flush();
    }
    for (auto& [id, sink] : s.sinks)
        sink(lvl, line);
}

} // namespace brc::log
