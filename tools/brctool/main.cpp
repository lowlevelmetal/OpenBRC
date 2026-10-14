// brctool: inspect and convert the original Barbie Riding Club data.
#include "Command.h"

#include "core/Log.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <cstdlib>
#include <print>

namespace brc::tool {

std::vector<Command>& registry() {
    static std::vector<Command> commands;
    return commands;
}

const vfs::Vfs& Context::data() {
    if (m_data)
        return m_data->vfs;
    std::vector<std::string> args = sourceArgs;
    if (args.empty()) {
        if (const char* env = std::getenv("OPENBRC_GAME_DATA"); env && *env) {
#ifdef _WIN32
            for (auto p : str::split(env, ';'))
#else
            for (auto p : str::split(env, ':'))
#endif
                if (!p.empty())
                    args.emplace_back(p);
        }
    }
    if (args.empty()) {
        std::println(stderr, "brctool: no game data; pass --source <disc image|disc|install> or set OPENBRC_GAME_DATA");
        std::exit(2);
    }
    std::vector<vfs::GameSource> sources;
    for (const auto& a : args) {
        std::string error;
        auto s = vfs::probeGameSource(str::toPath(a), &error);
        if (!s) {
            std::println(stderr, "brctool: {}", error);
            std::exit(2);
        }
        sources.push_back(std::move(*s));
    }
    m_data = std::make_unique<vfs::GameData>(vfs::assembleGameData(std::move(sources)));
    if (!m_data->missing.empty())
        std::println(stderr, "brctool: warning: {} game files missing (first: {})", m_data->missing.size(),
                     m_data->missing.front());
    return m_data->vfs;
}

} // namespace brc::tool

int main(int argc, char** argv) {
    using namespace brc;
    log::setLevel(log::Level::Warn);
    tool::Context ctx;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--source" && i + 1 < argc)
            ctx.sourceArgs.emplace_back(argv[++i]);
        else if (a == "-v")
            log::setLevel(log::Level::Debug);
        else
            args.emplace_back(a);
    }
    auto& cmds = tool::registry();
    std::ranges::sort(cmds, {}, &tool::Command::name);
    if (args.empty() || args[0] == "help" || args[0] == "--help") {
        std::println("usage: brctool [--source <disc image|disc|install>]... [-v] <command> [args]\n");
        for (const auto& c : cmds)
            std::println("  {:<10} {}\n  {:<10}   {}", c.name, c.usage, "", c.help);
        return args.empty() ? 1 : 0;
    }
    for (const auto& c : cmds)
        if (c.name == args[0])
            return c.run(ctx, std::span(args).subspan(1));
    std::println(stderr, "brctool: unknown command '{}' (try brctool help)", args[0]);
    return 1;
}
