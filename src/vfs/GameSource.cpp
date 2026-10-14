#include "vfs/GameSource.h"

#include "core/Log.h"
#include "core/Sha256.h"
#include "core/StringUtil.h"
#include "vfs/DirectoryFs.h"
#include "vfs/DiscManifest.h"
#include "vfs/IsoImage.h"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

namespace brc::vfs {
namespace {

void setError(std::string* error, std::string msg) {
    if (error)
        *error = std::move(msg);
}

// Case-insensitive lookup of a direct child of `dir`.
std::optional<std::filesystem::path> findChild(const std::filesystem::path& dir, std::string_view name) {
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (str::iequals(str::fromPath(it->path().filename()), name))
            return it->path();
    return std::nullopt;
}

std::string exePath(GameSource::Kind kind) {
    const std::string exe = str::lower(kGameExeName);
    return kind == GameSource::Kind::InstallDirectory ? "program/" + exe : exe;
}

// Checks the executable and counts which manifest files the source holds.
void inspect(GameSource& s) {
    if (auto exe = s.fs->open(exePath(s.kind))) {
        s.exeFound = true;
        const auto bytes = exe->readAll();
        s.exeKnown = Sha256::toHex(Sha256::hash(bytes)) == kGameExeSha256;
    }
    s.provided = 0;
    s.missing.clear();
    for (const auto& e : kDiscManifest) {
        auto f = s.fs->open(e.path);
        if (f && f->size() == e.size)
            ++s.provided;
        else
            s.missing.emplace_back(e.path);
    }
}

// A folder is recognised by the original's layout; returns the root to index.
std::optional<std::pair<GameSource::Kind, std::filesystem::path>> classifyDirectory(std::filesystem::path dir) {
    // Accept the Resources or Program sub-folder too, and step up to the root.
    const std::string leaf = str::lower(str::fromPath(dir.filename()));
    if ((leaf == "resources" || leaf == "program") && dir.has_parent_path() &&
        findChild(dir.parent_path(), "Resources"))
        dir = dir.parent_path();
    const auto resources = findChild(dir, "Resources");
    if (!resources)
        return std::nullopt;
    const bool hasCommon = findChild(*resources, "common.omt").has_value() ||
                           findChild(*resources, "commonhd.omt").has_value();
    if (const auto program = findChild(dir, "Program"); program && findChild(*program, kGameExeName))
        return std::pair{GameSource::Kind::InstallDirectory, dir};
    if (findChild(dir, kGameExeName))
        return std::pair{GameSource::Kind::DiscDirectory, dir};
    if (hasCommon)
        return std::pair{GameSource::Kind::DiscDirectory, dir}; // a copy of the disc's data
    return std::nullopt;
}

} // namespace

std::string_view GameSource::kindName(Kind kind) {
    switch (kind) {
    case Kind::DiscImage:
        return "disc image";
    case Kind::DiscDirectory:
        return "disc";
    case Kind::InstallDirectory:
        return "installation";
    }
    return "?";
}

std::string GameSource::describe() const {
    std::string s = std::format("{} {}", kindName(kind), str::fromPath(path));
    if (!volumeId.empty())
        s += std::format(" (volume {})", volumeId);
    return s;
}

std::optional<GameSource> probeGameSource(const std::filesystem::path& path, std::string* error) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        setError(error, std::format("'{}' does not exist", str::fromPath(path)));
        return std::nullopt;
    }

    GameSource s;
    s.path = path;
    if (std::filesystem::is_directory(path, ec)) {
        const auto cls = classifyDirectory(path);
        if (!cls) {
            setError(error, std::format("'{}' is not a Barbie Riding Club disc or installation (no Resources "
                                        "folder with the game data)",
                                        str::fromPath(path)));
            return std::nullopt;
        }
        s.kind = cls->first;
        s.path = cls->second;
        s.fs = std::make_shared<DirectoryFs>(s.path, 4);
    } else {
        std::string isoError;
        auto iso = IsoImage::open(path, &isoError);
        if (!iso) {
            setError(error, std::format("'{}' is not a readable disc image ({})", str::fromPath(path), isoError));
            return std::nullopt;
        }
        s.kind = GameSource::Kind::DiscImage;
        s.volumeId = iso->volumeId();
        s.fs = iso;
        if (!iso->exists("resources/common.omt")) {
            setError(error, std::format("'{}' is a disc image, but not of Barbie Riding Club (volume '{}')",
                                        str::fromPath(path), s.volumeId));
            return std::nullopt;
        }
    }

    inspect(s);
    if (s.provided == 0) {
        setError(error, std::format("'{}' holds none of the Barbie Riding Club data files", str::fromPath(path)));
        return std::nullopt;
    }
    log::info("game source: {} — {}/{} data files{}{}", s.describe(), s.provided, kDiscManifest.size(),
              s.exeFound ? "" : ", no game executable",
              s.exeFound && !s.exeKnown ? ", executable is not the v1.0 build" : "");
    return s;
}

GameData assembleGameData(std::vector<GameSource> sources) {
    GameData data;
    // Installations ("HD Path") are searched before discs ("CD Path"), as the
    // original does for every resource (HCApplication DB search, 0x44d510).
    std::ranges::stable_sort(sources, [](const GameSource& a, const GameSource& b) {
        auto rank = [](const GameSource& s) { return s.kind == GameSource::Kind::InstallDirectory ? 0 : 1; };
        return rank(a) < rank(b);
    });
    int priority = static_cast<int>(sources.size());
    for (auto& s : sources)
        data.vfs.mount(s.fs, priority--);
    for (const auto& e : kDiscManifest) {
        const bool have = std::ranges::any_of(sources, [&](const GameSource& s) {
            return std::ranges::find(s.missing, e.path) == s.missing.end();
        });
        if (!have)
            data.missing.emplace_back(e.path);
    }
    data.sources = std::move(sources);
    return data;
}

std::vector<std::filesystem::path> suggestGameSources() {
    std::vector<std::filesystem::path> candidates;
    std::error_code ec;
    auto consider = [&](const std::filesystem::path& p) {
        if (std::ranges::find(candidates, p) != candidates.end())
            return;
        if (probeGameSource(p))
            candidates.push_back(p);
    };

#ifdef _WIN32
    const DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(drives & (1u << i)))
            continue;
        const wchar_t root[] = {static_cast<wchar_t>(L'A' + i), L':', L'\\', 0};
        if (GetDriveTypeW(root) == DRIVE_CDROM)
            consider(root);
    }
    // The original installer records its folders under
    // HKLM\Software\Mattel\Barbie (R) Riding Club\1.0 (32-bit view).
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Mattel\\Barbie ® Riding Club\\1.0", 0,
                      KEY_READ | KEY_WOW64_32KEY, &key) == ERROR_SUCCESS) {
        for (const wchar_t* value : {L"HD Path", L"CD Path"}) {
            wchar_t buf[1024] = {};
            DWORD size = sizeof(buf) - sizeof(wchar_t);
            DWORD type = 0;
            if (RegQueryValueExW(key, value, nullptr, &type, reinterpret_cast<BYTE*>(buf), &size) ==
                    ERROR_SUCCESS &&
                type == REG_SZ) {
                std::filesystem::path p(buf);
                consider(p);
                if (p.has_parent_path())
                    consider(p.parent_path());
            }
        }
        RegCloseKey(key);
    }
    for (const char* var : {"ProgramFiles", "ProgramFiles(x86)"}) {
        if (const char* pf = std::getenv(var)) {
            consider(std::filesystem::path(pf) / "Mattel Media" / "Barbie Riding Club");
            consider(std::filesystem::path(pf) / "Barbie Riding Club");
        }
    }
    if (const char* profile = std::getenv("USERPROFILE")) {
        for (std::filesystem::directory_iterator it(std::filesystem::path(profile) / "Downloads", ec), end;
             !ec && it != end; it.increment(ec)) {
            const std::string name = str::lower(str::fromPath(it->path().filename()));
            if ((name.ends_with(".iso") || name.ends_with(".cue")) &&
                (name.find("brc") != std::string::npos || name.find("barbie") != std::string::npos))
                consider(it->path());
        }
    }
#else
    std::vector<std::filesystem::path> mountRoots = {"/media", "/mnt", "/Volumes"};
    if (const char* user = std::getenv("USER")) {
        mountRoots.push_back(std::filesystem::path("/run/media") / user);
        mountRoots.push_back(std::filesystem::path("/media") / user);
    }
    for (const auto& root : mountRoots)
        for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_directory(ec))
                consider(it->path());
    if (const char* home = std::getenv("HOME")) {
        const std::filesystem::path h(home);
        for (const char* sub : {".wine/drive_c/Program Files/Mattel Media/Barbie Riding Club",
                                ".wine/drive_c/Program Files (x86)/Mattel Media/Barbie Riding Club"})
            consider(h / sub);
        for (const char* dir : {"Downloads", "Games", "Desktop", "."}) {
            for (std::filesystem::directory_iterator it(h / dir, ec), end; !ec && it != end; it.increment(ec)) {
                const std::string name = str::lower(str::fromPath(it->path().filename()));
                if ((name.ends_with(".iso") || name.ends_with(".cue") || name.ends_with(".img")) &&
                    (name.find("brc") != std::string::npos || name.find("barbie") != std::string::npos))
                    consider(it->path());
            }
        }
    }
#if !defined(__APPLE__)
    for (const char* dev : {"/dev/sr0", "/dev/sr1", "/dev/cdrom"})
        if (std::filesystem::exists(dev, ec))
            consider(dev);
#endif
#endif
    return candidates;
}

} // namespace brc::vfs
