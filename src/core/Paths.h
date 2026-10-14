#pragma once

#include <filesystem>

namespace brc::paths {

// Directory containing the running executable.
std::filesystem::path executableDir();

// Per-user configuration directory (created on demand).
//   Windows: %APPDATA%\OpenBRC
//   macOS:   ~/Library/Application Support/OpenBRC
//   Linux:   $XDG_CONFIG_HOME/openbrc (default ~/.config/openbrc)
std::filesystem::path userConfigDir();

// Per-user data directory for saves, photos, printouts and logs (created on demand).
//   Windows: %LOCALAPPDATA%\OpenBRC
//   macOS:   ~/Library/Application Support/OpenBRC
//   Linux:   $XDG_DATA_HOME/openbrc (default ~/.local/share/openbrc)
std::filesystem::path userDataDir();

// When "portable.txt" exists next to the executable, config and data live
// next to the executable instead (USB-stick installs, development builds).
bool isPortable();

} // namespace brc::paths
