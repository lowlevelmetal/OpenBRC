#include "app/GameDataSetup.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "platform/Dialogs.h"
#include "vfs/DiscManifest.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <format>
#include <print>

namespace brc::app {
namespace {

std::unique_ptr<vfs::GameData> assemble(const std::vector<std::filesystem::path>& paths, std::string* problem) {
    std::vector<vfs::GameSource> sources;
    for (const auto& p : paths) {
        std::string error;
        auto s = vfs::probeGameSource(p, &error);
        if (!s) {
            if (problem)
                *problem = error;
            return nullptr;
        }
        sources.push_back(std::move(*s));
    }
    auto data = std::make_unique<vfs::GameData>(vfs::assembleGameData(std::move(sources)));
    if (!data->usable()) {
        if (problem)
            *problem = data->sources.empty()
                           ? std::string("no game data")
                           : std::format("{} of the game's {} data files are missing (for example {})",
                                         data->missing.size(), vfs::kDiscManifest.size(), data->missing.front());
        return nullptr;
    }
    return data;
}

enum class Choice { Image, Folder, Quit };

Choice askForSource(SDL_Window* parent, const std::string& reason) {
    const std::string text = std::format(
        "OpenBRC needs your original Barbie Riding Club CD-ROM to play.\n\n"
        "Choose the disc's drive, a disc image of it (.iso, .cue, .bin or .img), or a folder with an "
        "installation of the original game.{}{}",
        reason.empty() ? "" : "\n\n", reason);
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Disc image..."},
        {0, 1, "Drive or folder..."},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 2, "Quit"},
    };
    SDL_MessageBoxData box{};
    box.flags = SDL_MESSAGEBOX_INFORMATION;
    box.window = parent;
    box.title = "Where is your game disc?";
    box.message = text.c_str();
    box.numbuttons = 3;
    box.buttons = buttons;
    int pressed = 2;
    if (!SDL_ShowMessageBox(&box, &pressed))
        return Choice::Quit;
    return pressed == 0 ? Choice::Image : pressed == 1 ? Choice::Folder : Choice::Quit;
}

std::filesystem::path defaultLocation() {
    if (const char* home = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS))
        return str::toPath(home);
    return {};
}

} // namespace

std::unique_ptr<vfs::GameData> findGameData(Settings& settings, const std::vector<std::filesystem::path>& explicitSources,
                                            bool forceSetup, SDL_Window* parent) {
    std::string problem;
    if (!explicitSources.empty()) {
        if (auto data = assemble(explicitSources, &problem)) {
            settings.gameSources = explicitSources;
            return data;
        }
        log::error("game data: {}", problem);
    } else if (!forceSetup && !settings.gameSources.empty()) {
        if (auto data = assemble(settings.gameSources, &problem))
            return data;
        log::warn("game data: the remembered location is no longer usable: {}", problem);
    }

    if (!forceSetup && explicitSources.empty()) {
        for (const auto& candidate : vfs::suggestGameSources()) {
            if (auto data = assemble({candidate}, nullptr)) {
                log::info("game data: found {}", str::fromPath(candidate));
                settings.gameSources = {candidate};
                return data;
            }
        }
    }

    // Ask. A partial installation is kept and combined with the next choice.
    std::vector<std::filesystem::path> chosen;
    for (;;) {
        const Choice c = askForSource(parent, problem);
        if (c == Choice::Quit)
            return nullptr;
        platform::FileDialogOptions opts;
        opts.parent = parent;
        opts.defaultLocation = defaultLocation();
        std::optional<std::filesystem::path> path;
        std::string dialogError;
        if (c == Choice::Image) {
            opts.title = "Choose your Barbie Riding Club disc image";
            opts.filters = {{"Disc images", "iso;cue;bin;img"}, {"All files", "*"}};
            path = platform::pickFile(opts, &dialogError);
        } else {
            opts.title = "Choose the Barbie Riding Club disc drive or installation folder";
            path = platform::pickFolder(opts, &dialogError);
        }
        if (!path) {
            if (!dialogError.empty())
                problem = "The file chooser failed: " + dialogError;
            continue;
        }
        std::string error;
        auto source = vfs::probeGameSource(*path, &error);
        if (!source) {
            problem = error;
            continue;
        }
        if (std::ranges::find(chosen, *path) == chosen.end())
            chosen.push_back(*path);
        if (auto data = assemble(chosen, &problem)) {
            settings.gameSources = chosen;
            return data;
        }
        if (source->kind == vfs::GameSource::Kind::InstallDirectory)
            problem = "That installation holds only part of the game. Please also choose the game disc (or "
                      "its image); the two are used together, as the original game did.";
    }
}

int checkSource(const std::filesystem::path& path) {
    std::string error;
    auto s = vfs::probeGameSource(path, &error);
    if (!s) {
        std::println("{}", error);
        return 2;
    }
    std::println("{}", s->describe());
    std::println("data files: {}/{}", s->provided, vfs::kDiscManifest.size());
    std::println("executable: {}", !s->exeFound ? "not present" : s->exeKnown ? "v1.0" : "unknown build");
    if (!s->complete()) {
        std::println("incomplete: {} files missing (first: {}); combine it with the game disc", s->missing.size(),
                     s->missing.front());
        return 1;
    }
    std::println("usable");
    return 0;
}

} // namespace brc::app
