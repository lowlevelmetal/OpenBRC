#pragma once

#include "vfs/Vfs.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace brc::vfs {

// Where the original Barbie Riding Club files come from. OpenBRC never runs
// without one; the user points the installer or first-run setup at any of:
//   * a disc image (.iso, .bin/.cue, .img) or a raw optical device (/dev/sr0)
//   * the disc itself (its drive or mount point), or a copy of its contents
//     (has "Barbie Riding Club.exe" and a Resources folder)
//   * an existing installation of the original game (has Program/ and
//     Resources/ folders, as the 1998 installer created them)
//
// The original game searches its install folder ("HD Path") before the disc
// ("CD Path") for every resource. An installation usually holds only part of
// the data, so it is combined with the disc; a complete one works alone.
struct GameSource {
    enum class Kind { DiscImage, DiscDirectory, InstallDirectory };

    Kind kind = Kind::DiscDirectory;
    std::filesystem::path path;
    std::shared_ptr<const FileSystem> fs; // virtual paths: "resources/common.omt", ...

    std::string volumeId;   // "BRCV1_0" for the disc, empty otherwise
    bool exeFound = false;  // the original executable is present
    bool exeKnown = false;  // ... and is the v1.0 build OpenBRC was made from
    std::size_t provided = 0; // how many manifest files this source holds
    std::vector<std::string> missing; // manifest files not present (or wrong size)

    bool complete() const { return missing.empty(); }
    std::string describe() const;
    static std::string_view kindName(Kind kind);
};

// Identifies what `path` is. Returns std::nullopt (and sets `error`) if it is
// not a Barbie Riding Club disc, image or installation at all.
std::optional<GameSource> probeGameSource(const std::filesystem::path& path, std::string* error = nullptr);

// The game data assembled from one or more sources.
struct GameData {
    Vfs vfs;
    std::vector<GameSource> sources;
    std::vector<std::string> missing; // manifest files no source provides

    bool usable() const { return !sources.empty() && missing.empty(); }
};

// Mounts `sources` the way the original searches them: installations before
// discs, otherwise in the order given. Fills `missing` with every required
// file that none of them provides.
GameData assembleGameData(std::vector<GameSource> sources);

// Candidate locations for the setup screen: inserted discs, disc images in
// the usual download folders and installations of the original game.
std::vector<std::filesystem::path> suggestGameSources();

} // namespace brc::vfs
