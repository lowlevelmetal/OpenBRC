#pragma once

#include "render/DisplaySettings.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace brc::app {

struct CommandLine {
    std::vector<std::filesystem::path> sources;     // --source (repeatable)
    std::optional<std::filesystem::path> checkSource; // --check-source: report and exit
    bool setup = false;                             // --setup: choose the game data again
    std::optional<render::BackendKind> renderer;    // --renderer
    std::optional<bool> fullscreen;                 // --fullscreen / --windowed
    std::optional<std::string> startNode;           // --node <scene> (debugging)
    bool skipLogos = false;                         // --skip-logos
    bool verbose = false;                           // -v
    std::optional<std::filesystem::path> logFile;   // --log <file>
    std::optional<std::string> autoplay;            // --autoplay "<script>" (testing)
    bool help = false;
    bool version = false;

    // Returns nullopt (and sets `error`) for unknown options or missing values.
    static std::optional<CommandLine> parse(int argc, char** argv, std::string* error);
    static std::string usage();
};

} // namespace brc::app
