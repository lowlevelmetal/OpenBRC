#pragma once

#include "app/Settings.h"
#include "vfs/GameSource.h"

#include <filesystem>
#include <memory>
#include <vector>

struct SDL_Window;

namespace brc::app {

// Finds the original game files, in this order:
//   1. `explicitSources` (from the command line), when given;
//   2. the sources remembered in the settings, unless `forceSetup`;
//   3. a single complete source found by vfs::suggestGameSources();
//   4. the user's choice in native dialogs (disc image, drive or folder),
//      repeated until the game is complete or the user gives up.
// On success the chosen sources are stored in `settings` (the caller saves
// them). Returns nullptr when there is no usable game data; OpenBRC then
// exits, since it never runs without the original game.
std::unique_ptr<vfs::GameData> findGameData(Settings& settings, const std::vector<std::filesystem::path>& explicitSources,
                                            bool forceSetup, SDL_Window* parent);

// --check-source: prints what `path` holds. Exit code 0 when it is usable on
// its own, 1 when it is part of the game (e.g. a partial installation), 2 when
// it is not Barbie Riding Club at all.
int checkSource(const std::filesystem::path& path);

} // namespace brc::app
