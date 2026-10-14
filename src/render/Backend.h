#pragma once

// Interface between the presentation front end (Presenter.cpp) and the GPU backends
// (VulkanBackend.cpp, OpenGLBackend.cpp). Not used outside src/render/.

#include "render/DisplaySettings.h"

#include <SDL3/SDL_video.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace brc::render {

// The finished output image read back for a screenshot: RGBA8, top row first, tightly
// packed.
struct Capture {
    std::vector<std::uint8_t> rgba;
    int width = 0;
    int height = 0;
    bool ok = false;
};

// Everything a backend needs to draw one presentation frame. The pointed-to data stays
// valid and unchanged for the duration of the draw call.
struct FrameData {
    // Source frame: 8-bit indices (tightly packed, srcW bytes per row) and 256 palette
    // entries of 0x00RRGGBB (B, G, R, X bytes in memory on little-endian hosts).
    // frameSeq changes whenever the content changes; 0 means nothing was submitted yet.
    const std::uint8_t* index = nullptr;
    const std::uint32_t* palette = nullptr;
    int srcW = 640;
    int srcH = 480;
    std::uint64_t frameSeq = 0;

    // Output: requested drawable size in pixels (a backend may use the surface's actual
    // size instead), scaling and filtering.
    int outW = 0;
    int outH = 0;
    Aspect aspect = Aspect::Original;
    Filter filter = Filter::Sharp;
    bool vsync = true;

    // Toast overlay: coverage bitmap (kOverlayTexW pitch), changed when overlaySeq
    // changes. overlayAlpha == 0 means no overlay this frame.
    const std::uint8_t* overlay = nullptr;
    int overlayW = 0;
    int overlayH = 0;
    std::uint64_t overlaySeq = 0;
    float overlayAlpha = 0.0f;

    // When non-null, read the finished output back into it.
    Capture* capture = nullptr;
};

enum class DrawResult {
    Ok,      // presented
    Skipped, // nothing presented (minimised, swapchain out of date, ...); retry soon
    Lost,    // the backend is unusable (device lost); the front end recreates it
};

// Shader modes beyond the filters (params.x in present_common.glsl).
inline constexpr float kModeOverlay = 4.0f;

class Backend {
public:
    virtual ~Backend() = default;

    virtual BackendKind kind() const = 0;
    virtual std::string_view name() const = 0;       // "Vulkan", "OpenGL"
    virtual std::string_view deviceName() const = 0; // GPU / renderer string
    virtual SDL_WindowFlags windowFlags() const = 0; // SDL_WINDOW_VULKAN / SDL_WINDOW_OPENGL

    // Main thread, before the window is created (OpenGL context attributes).
    virtual void prepareWindow() {}
    // Main thread: create every GPU object for this window; logs the device on success.
    // The destructor (main thread, after the render thread has finished) frees them.
    virtual bool init(SDL_Window* window, int outW, int outH, bool vsync) = 0;

    // Render thread: bind to the thread before the first draw / release after the last.
    virtual void attach() {}
    virtual DrawResult draw(const FrameData& frame) = 0;
    virtual void detach() {}
};

// nullptr when the backend was not compiled in.
std::unique_ptr<Backend> createVulkanBackend();
std::unique_ptr<Backend> createOpenGLBackend();

} // namespace brc::render
