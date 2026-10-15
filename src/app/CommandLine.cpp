#include "app/CommandLine.h"

#include "core/StringUtil.h"

#include <format>
#include <string_view>

namespace brc::app {

std::optional<CommandLine> CommandLine::parse(int argc, char** argv, std::string* error) {
    CommandLine cl;
    auto fail = [&](std::string msg) -> std::optional<CommandLine> {
        if (error)
            *error = std::move(msg);
        return std::nullopt;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto value = [&]() -> std::optional<std::string_view> {
            if (i + 1 >= argc)
                return std::nullopt;
            return std::string_view(argv[++i]);
        };
        if (a == "--source" || a == "--iso") {
            auto v = value();
            if (!v)
                return fail(std::format("{} needs a path", a));
            cl.sources.push_back(str::toPath(*v));
        } else if (a == "--check-source") {
            auto v = value();
            if (!v)
                return fail("--check-source needs a path");
            cl.checkSource = str::toPath(*v);
        } else if (a == "--setup") {
            cl.setup = true;
        } else if (a == "--renderer") {
            auto v = value();
            auto kind = v ? render::parseBackendKind(*v) : std::nullopt;
            if (!kind)
                return fail("--renderer must be auto, vulkan or opengl");
            cl.renderer = kind;
        } else if (a == "--fullscreen") {
            cl.fullscreen = true;
        } else if (a == "--windowed") {
            cl.fullscreen = false;
        } else if (a == "--node") {
            auto v = value();
            if (!v)
                return fail("--node needs a scene name");
            cl.startNode = std::string(*v);
        } else if (a == "--skip-logos") {
            cl.skipLogos = true;
        } else if (a == "--log") {
            auto v = value();
            if (!v)
                return fail("--log needs a file name");
            cl.logFile = str::toPath(*v);
        } else if (a == "--autoplay") {
            auto v = value();
            if (!v)
                return fail("--autoplay needs a script");
            cl.autoplay = std::string(*v);
        } else if (a == "--slot") {
            auto v = value();
            auto n = v ? str::parseInt(*v) : std::nullopt;
            if (!n || *n < 0 || *n > 9)
                return fail("--slot needs a save slot 0..9");
            cl.slot = static_cast<int>(*n);
        } else if (a == "--dev-state") {
            auto v = value();
            if (!v)
                return fail("--dev-state needs key=value pairs");
            cl.devState = std::string(*v);
        } else if (a == "-v" || a == "--verbose") {
            cl.verbose = true;
        } else if (a == "-h" || a == "--help" || a == "/?") {
            cl.help = true;
        } else if (a == "--version") {
            cl.version = true;
        } else if (a.starts_with("-psn_")) {
            // macOS Finder adds a process serial number argument on old systems.
        } else {
            return fail(std::format("unknown option '{}' (try --help)", a));
        }
    }
    return cl;
}

std::string CommandLine::usage() {
    return "usage: openbrc [options]\n"
           "\n"
           "  --source <path>        the original game: a disc image (.iso, .cue, .bin, .img),\n"
           "                         the disc's drive or folder, or an installation; repeat to\n"
           "                         combine an installation with the disc. Remembered.\n"
           "  --check-source <path>  report whether <path> holds the game, then exit\n"
           "                         (exit code 0 when it is usable on its own)\n"
           "  --setup                choose the game disc again\n"
           "  --renderer <name>      auto (default), vulkan or opengl\n"
           "  --fullscreen, --windowed\n"
           "  --skip-logos           start at the options screen\n"
           "  --node <scene>         start in a scene (debugging)\n"
           "  --slot <n>             load saved game <n> first (debugging)\n"
           "  --dev-state <k=v,...>  set player/session fields: horse, saddle, course, control,\n"
           "                         mission, trail, firstride, finale, debug, coin, wellpath, gate,\n"
           "                         trailcert, m1..m8, c1..c4, b1..b3\n"
           "  --log <file>           also write the log to <file>\n"
           "  -v, --verbose          more log output\n"
           "  --version, --help\n";
}

} // namespace brc::app
