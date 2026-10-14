#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Window;
union SDL_Event;

namespace brc::platform {

// ---- message boxes ---------------------------------------------------------------------
// Blocking; usable before SDL video is initialised and without a parent window.

enum class MessageKind { Info, Warning, Error };

void showMessage(MessageKind kind, std::string_view title, std::string_view text,
                 SDL_Window* parent = nullptr);

// Yes / No question; true for Yes. Escape and closing the box answer No.
bool askYesNo(std::string_view title, std::string_view text, SDL_Window* parent = nullptr);

// ---- native file and folder pickers --------------------------------------------------
// Wrappers around SDL_ShowOpenFileDialog / SDL_ShowOpenFolderDialog, which are
// asynchronous: these block the calling (main) thread, pumping SDL events until the user
// has chosen. On Linux they need xdg-desktop-portal or zenity.

struct FileFilter {
    std::string name;    // "Disc images"
    std::string pattern; // extensions without dots separated by ';' ("iso;bin;cue;img"), or "*"
};

struct FileDialogOptions {
    std::string title;
    std::filesystem::path defaultLocation; // folder (or file) to start in; empty: system default
    std::vector<FileFilter> filters;       // file dialogs only
    SDL_Window* parent = nullptr;          // makes the dialog modal for this window
    // Receives the events that arrive while the dialog is open (e.g. forwarded to
    // render::Presenter::handleEvent). Without it the events stay queued for the caller.
    std::function<void(const SDL_Event&)> onEvent;
};

// The chosen file / folder, or nullopt when the user cancelled or the dialog failed; in
// the latter case *error says why (it is cleared otherwise).
std::optional<std::filesystem::path> pickFile(const FileDialogOptions& options, std::string* error = nullptr);
std::optional<std::filesystem::path> pickFolder(const FileDialogOptions& options,
                                                std::string* error = nullptr);

} // namespace brc::platform
