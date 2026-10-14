#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace brc::platform {

struct InitOptions {
    bool video = true;
    bool audio = true;
    bool gamepad = false;
};

// Owns SDL's global state: SDL_Init in create(), SDL_Quit in the destructor. Create it on
// the main thread before any window and destroy it after the last one (and after the
// audio device) is gone.
class Sdl {
public:
    static std::unique_ptr<Sdl> create(const InitOptions& options, std::string* error);
    ~Sdl();

    Sdl(const Sdl&) = delete;
    Sdl& operator=(const Sdl&) = delete;

    // The video driver in use ("wayland", "x11", "windows", "cocoa", "offscreen", ...).
    std::string_view videoDriver() const;

    // A thread could not be stopped at shutdown and may still be inside SDL or a driver
    // (e.g. a render thread blocked in a present call). SDL_Quit would pull the rug out
    // from under it, so the destructor then leaves SDL as it is; the process is exiting.
    static void skipQuitOnExit();

private:
    Sdl() = default;
};

} // namespace brc::platform
