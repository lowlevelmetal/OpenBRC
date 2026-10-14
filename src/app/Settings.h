#pragma once

#include "render/DisplaySettings.h"

#include <filesystem>
#include <string>
#include <vector>

namespace brc::app {

// Everything OpenBRC remembers between runs. Stored in <config dir>/openbrc.ini;
// the Windows installer's choice of game data (openbrc-install.ini next to the
// executable) is used when the user has not chosen one yet.
struct Settings {
    // Where the original game is: disc images, discs or installations. The
    // first usable combination wins (see vfs::assembleGameData).
    std::vector<std::filesystem::path> gameSources;

    render::DisplaySettings display;

    int volume = 100;               // 0..100, on top of the game's own volume
    bool pauseInBackground = true;  // the original stops when another program is active
    bool skipIntroLogos = false;    // jump straight to the options screen

    static std::filesystem::path path();
    static Settings load();
    bool save() const;
};

} // namespace brc::app
