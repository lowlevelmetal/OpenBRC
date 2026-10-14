#pragma once

// Presentation of the game's 8-bit screen: owns the game window and shows the frames the
// game produces, scaled to any window size, aspect ratio, pixel density and refresh rate.
//
// The game hands over finished frames (palette indices + palette) with submit(), which
// only copies them into a triple buffer. A dedicated render thread resolves the palette
// on the GPU, scales the image to the window (aspect / filter modes) and presents it in
// step with the display, using Vulkan or, as a fallback, OpenGL. Everything here except
// submit() is for the main thread, which owns the window and its events.

#include "platform/GameView.h"
#include "render/DisplaySettings.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

struct SDL_Window;
union SDL_Event;

namespace brc::render {

class Backend;

// One frame of the game screen.
struct IndexedFrame {
    const std::uint8_t* pixels = nullptr; // palette indices, `pitch` bytes per row
    int width = 0;
    int height = 0;
    int pitch = 0;
    const std::array<std::uint32_t, 256>* palette = nullptr; // 0x00RRGGBB
};

class Presenter final : public platform::GameView {
public:
    // Opens the window (hidden until the GPU is ready, fullscreen when the settings say
    // so) with the preferred backend, falling back to OpenGL, and starts the render
    // thread. SDL video must be initialised. Returns nullptr with *error set when neither
    // backend works.
    static std::unique_ptr<Presenter> create(const std::string& title, const DisplaySettings& settings,
                                             std::string* error);
    ~Presenter() override;

    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;

    // ---- platform::GameView
    SDL_Window* window() const override { return m_window; }
    std::pair<int, int> gameSize() const override;
    std::pair<int, int> windowToGame(float wx, float wy) const override;
    std::pair<float, float> gameToWindow(int gx, int gy) const override;

    // ---- frames (any one thread, usually the game thread)
    // Queues a frame for presentation. Copies the pixels and palette and returns; never
    // waits for the GPU or the display. When frames come faster than the display, the
    // newest one wins.
    void submit(const IndexedFrame& frame);
    // Source size announced by a display mode change, before the next submit.
    void setSourceSize(int width, int height);

    // ---- main thread
    // Feed every SDL event here (resize, display change, expose, minimise, fullscreen...).
    void handleEvent(const SDL_Event& event);
    // Periodic work: recovers from a lost GPU device by restarting the backend, or
    // switching to OpenGL in a new window (window() then changes). Returns false when no
    // renderer works any more; the application should report it and quit.
    bool pump();

    // Display options, each acknowledged with a toast.
    void toggleFullscreen();
    void cycleAspect();
    void cycleFilter();
    void toggleVsync();
    // The same without a toast (settings menus, applying saved settings).
    void setFullscreen(bool fullscreen);
    void setAspect(Aspect aspect);
    void setFilter(Filter filter);
    void setVsync(bool vsync);

    // A short message in the top-left corner that fades out after two seconds. UTF-8;
    // characters outside ASCII show as '?'.
    void toast(std::string_view text);

    // Saves the next presented output image (what is on screen, at the drawable's
    // resolution) as PNG, or BMP when the extension is ".bmp". Asynchronous; the result
    // is logged and toasted.
    void requestScreenshot(std::filesystem::path path);
    bool screenshotPending() const;

    // Current options, for persisting. Includes the windowed size (updated as the user
    // resizes the window) and the fullscreen state (also when changed by the system).
    const DisplaySettings& settings() const { return m_settings; }
    // Called on the main thread after fullscreen / aspect / filter / vsync change.
    void setSettingsChangedCallback(std::function<void(const DisplaySettings&)> callback);

    BackendKind backendKind() const; // Vulkan or OpenGL, the one in use
    std::string_view backendName() const;
    std::string_view deviceName() const;

    // Stops the render thread and frees the GPU objects and the window (the destructor
    // does this too). Returns false if the render thread is stuck in the driver; its
    // resources are then abandoned and SDL is left initialised (platform::Sdl).
    bool shutdown();

private:
    struct Shared; // state shared with the render thread (Presenter.cpp)

    Presenter();
    static void renderLoop(std::shared_ptr<Shared> shared, Backend* backend);

    bool openWindow(std::unique_ptr<Backend> backend, const std::string& title, int width, int height);
    void startThread();
    bool stopThread();
    void abandonRenderer();
    void publishOutput();
    void publishSettings();
    void notifySettingsChanged();

    SDL_Window* m_window = nullptr;
    std::unique_ptr<Backend> m_backend;
    std::shared_ptr<Shared> m_shared;
    std::thread m_thread;
    DisplaySettings m_settings;
    std::function<void(const DisplaySettings&)> m_settingsChanged;
    std::atomic<std::uint32_t> m_sourceSize; // width << 16 | height, read by the mapping
    int m_failures = 0;                      // device losses in a row (see pump())
    std::uint64_t m_startedNs = 0;           // when the render thread last started
    bool m_dead = false;
};

} // namespace brc::render
