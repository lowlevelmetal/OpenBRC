#include "app/Settings.h"

#include "core/Ini.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/StringUtil.h"

#include <algorithm>
#include <format>

namespace brc::app {
namespace {

constexpr int kMaxSources = 4;

void readSources(const IniFile& ini, std::vector<std::filesystem::path>& out) {
    for (int i = 0; i < kMaxSources; ++i) {
        const std::string key = i == 0 ? "Source" : std::format("Source{}", i + 1);
        const std::string value = ini.getString("Game", key);
        if (!value.empty())
            out.push_back(str::toPath(value));
    }
}

} // namespace

std::filesystem::path Settings::path() { return paths::userConfigDir() / "openbrc.ini"; }

Settings Settings::load() {
    Settings s;
    IniFile ini;
    if (ini.load(path())) {
        readSources(ini, s.gameSources);
        auto& d = s.display;
        if (auto v = render::parseBackendKind(ini.getString("Display", "Renderer", "auto")))
            d.backend = *v;
        d.fullscreen = ini.getBool("Display", "Fullscreen", d.fullscreen);
        d.windowWidth = static_cast<int>(std::clamp<long long>(ini.getInt("Display", "WindowWidth", d.windowWidth), 320, 16384));
        d.windowHeight = static_cast<int>(std::clamp<long long>(ini.getInt("Display", "WindowHeight", d.windowHeight), 240, 16384));
        if (auto v = render::parseAspect(ini.getString("Display", "Aspect", "original")))
            d.aspect = *v;
        if (auto v = render::parseFilter(ini.getString("Display", "Filter", "sharp")))
            d.filter = *v;
        d.vsync = ini.getBool("Display", "VSync", d.vsync);
        s.volume = static_cast<int>(std::clamp<long long>(ini.getInt("Audio", "Volume", s.volume), 0, 100));
        s.pauseInBackground = ini.getBool("Game", "PauseInBackground", s.pauseInBackground);
        s.skipIntroLogos = ini.getBool("Game", "SkipIntroLogos", s.skipIntroLogos);
    }
    if (s.gameSources.empty()) {
        IniFile installer;
        if (installer.load(paths::executableDir() / "openbrc-install.ini"))
            readSources(installer, s.gameSources);
    }
    return s;
}

bool Settings::save() const {
    IniFile ini;
    ini.load(path()); // keep comments and unknown keys
    for (int i = 0; i < kMaxSources; ++i) {
        const std::string key = i == 0 ? "Source" : std::format("Source{}", i + 1);
        if (i < static_cast<int>(gameSources.size()))
            ini.set("Game", key, str::fromPath(gameSources[static_cast<std::size_t>(i)]));
        else
            ini.remove("Game", key);
    }
    ini.setBool("Game", "PauseInBackground", pauseInBackground);
    ini.setBool("Game", "SkipIntroLogos", skipIntroLogos);
    ini.set("Display", "Renderer", render::toString(display.backend));
    ini.setBool("Display", "Fullscreen", display.fullscreen);
    ini.setInt("Display", "WindowWidth", display.windowWidth);
    ini.setInt("Display", "WindowHeight", display.windowHeight);
    ini.set("Display", "Aspect", render::toString(display.aspect));
    ini.set("Display", "Filter", render::toString(display.filter));
    ini.setBool("Display", "VSync", display.vsync);
    ini.setInt("Audio", "Volume", volume);
    if (!ini.save(path())) {
        log::warn("settings: cannot write {}", str::fromPath(path()));
        return false;
    }
    return true;
}

} // namespace brc::app
