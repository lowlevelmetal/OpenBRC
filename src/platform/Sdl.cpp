#include "platform/Sdl.h"

#include "core/Log.h"
#include "core/Version.h"

#include <SDL3/SDL.h>

// Our own main() is the entry point on every desktop platform; tell SDL so.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

#include <atomic>

namespace brc::platform {
namespace {

std::atomic<bool> g_skipQuit{false};

} // namespace

std::unique_ptr<Sdl> Sdl::create(const InitOptions& options, std::string* error) {
    SDL_SetMainReady();
    SDL_SetAppMetadata(kProjectName, kProjectVersion, "io.github.lowlevelmetal.OpenBRC");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_TYPE_STRING, "game");

    SDL_InitFlags flags = SDL_INIT_EVENTS;
    if (options.video)
        flags |= SDL_INIT_VIDEO;
    if (options.audio)
        flags |= SDL_INIT_AUDIO;
    if (options.gamepad)
        flags |= SDL_INIT_GAMEPAD;
    if (!SDL_Init(flags)) {
        if (error)
            *error = std::string("SDL_Init failed: ") + SDL_GetError();
        return nullptr;
    }
    int version = SDL_GetVersion();
    const char* driver = options.video ? SDL_GetCurrentVideoDriver() : nullptr;
    log::info("platform: SDL {}.{}.{}, video driver {}", SDL_VERSIONNUM_MAJOR(version),
              SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version), driver ? driver : "(none)");
    return std::unique_ptr<Sdl>(new Sdl());
}

Sdl::~Sdl() {
    if (g_skipQuit.load()) {
        log::warn("platform: leaving SDL initialised (a thread is still inside it)");
        return;
    }
    SDL_Quit();
}

std::string_view Sdl::videoDriver() const {
    const char* driver = SDL_GetCurrentVideoDriver();
    return driver ? driver : "";
}

void Sdl::skipQuitOnExit() {
    g_skipQuit.store(true);
}

} // namespace brc::platform
