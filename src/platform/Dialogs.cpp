#include "platform/Dialogs.h"

#include "core/Log.h"
#include "core/StringUtil.h"

#include <SDL3/SDL.h>

#include <mutex>

namespace brc::platform {
namespace {

SDL_MessageBoxFlags messageFlags(MessageKind kind) {
    switch (kind) {
    case MessageKind::Info:
        return SDL_MESSAGEBOX_INFORMATION;
    case MessageKind::Warning:
        return SDL_MESSAGEBOX_WARNING;
    case MessageKind::Error:
        return SDL_MESSAGEBOX_ERROR;
    }
    return SDL_MESSAGEBOX_INFORMATION;
}

// Filled in by SDL's dialog callback, which may run on another thread.
struct DialogState {
    std::mutex mutex;
    bool done = false;
    bool failed = false;
    std::string error;
    std::vector<std::string> paths;
};

void SDLCALL dialogCallback(void* userdata, const char* const* filelist, int /*filter*/) {
    auto* state = static_cast<DialogState*>(userdata);
    std::lock_guard lock(state->mutex);
    if (!filelist) {
        state->failed = true;
        state->error = SDL_GetError();
    } else {
        for (const char* const* p = filelist; *p; ++p)
            state->paths.emplace_back(*p);
    }
    state->done = true;
}

std::optional<std::filesystem::path> runDialog(SDL_FileDialogType type, const FileDialogOptions& options,
                                               std::string* error) {
    if (error)
        error->clear();
    std::vector<SDL_DialogFileFilter> filters;
    if (type == SDL_FILEDIALOG_OPENFILE)
        for (const auto& f : options.filters)
            filters.push_back({f.name.c_str(), f.pattern.c_str()});
    std::string location =
        options.defaultLocation.empty() ? std::string() : str::fromPath(options.defaultLocation);

    // The filters and properties stay alive until the callback has run (we block below).
    SDL_PropertiesID props = SDL_CreateProperties();
    if (!filters.empty()) {
        SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, filters.data());
        SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER,
                              static_cast<Sint64>(filters.size()));
    }
    if (options.parent)
        SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, options.parent);
    if (!location.empty())
        SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, location.c_str());
    if (!options.title.empty())
        SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, options.title.c_str());
    SDL_SetBooleanProperty(props, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, false);

    DialogState state;
    SDL_ShowFileDialogWithProperties(type, dialogCallback, &state, props);
    for (;;) {
        {
            std::lock_guard lock(state.mutex);
            if (state.done)
                break;
        }
        // The XDG portal answers over D-Bus, which SDL services while pumping events.
        if (options.onEvent) {
            SDL_Event event;
            if (SDL_WaitEventTimeout(&event, 10)) {
                options.onEvent(event);
                while (SDL_PollEvent(&event))
                    options.onEvent(event);
            }
        } else {
            SDL_PumpEvents();
            SDL_Delay(10);
        }
    }
    SDL_DestroyProperties(props);

    if (state.failed) {
        log::warn("platform: file dialog failed: {}", state.error);
        if (error)
            *error = state.error.empty() ? std::string("the file dialog could not be shown") : state.error;
        return std::nullopt;
    }
    if (state.paths.empty())
        return std::nullopt; // cancelled
    return str::toPath(state.paths.front());
}

} // namespace

void showMessage(MessageKind kind, std::string_view title, std::string_view text, SDL_Window* parent) {
    std::string t(title), m(text);
    if (!SDL_ShowSimpleMessageBox(messageFlags(kind), t.c_str(), m.c_str(), parent))
        log::warn("platform: cannot show a message box ({}): {}: {}", SDL_GetError(), t, m);
}

bool askYesNo(std::string_view title, std::string_view text, SDL_Window* parent) {
    std::string t(title), m(text);
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes"},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No"},
    };
    SDL_MessageBoxData data{};
    data.flags = SDL_MESSAGEBOX_INFORMATION | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;
    data.window = parent;
    data.title = t.c_str();
    data.message = m.c_str();
    data.numbuttons = static_cast<int>(std::size(buttons));
    data.buttons = buttons;
    int pressed = 0;
    if (!SDL_ShowMessageBox(&data, &pressed)) {
        log::warn("platform: cannot show a message box ({}): {}: {}", SDL_GetError(), t, m);
        return false;
    }
    return pressed == 1;
}

std::optional<std::filesystem::path> pickFile(const FileDialogOptions& options, std::string* error) {
    return runDialog(SDL_FILEDIALOG_OPENFILE, options, error);
}

std::optional<std::filesystem::path> pickFolder(const FileDialogOptions& options, std::string* error) {
    return runDialog(SDL_FILEDIALOG_OPENFOLDER, options, error);
}

} // namespace brc::platform
