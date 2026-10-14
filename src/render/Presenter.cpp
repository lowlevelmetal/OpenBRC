// Presentation front end: window ownership, the game -> render thread frame hand-off,
// scaling geometry and coordinate mapping, the display options and the toast overlay.
// The GPU work lives in VulkanBackend.cpp and OpenGLBackend.cpp.
//
// Threads: the main thread owns the SDL window and its events. submit() copies each
// finished frame into one of three slots (triple buffering: the game writes "back", the
// newest finished frame waits in "ready", the render thread reads "front"), so neither
// thread ever waits for the other beyond a few index swaps under the mutex. The render
// thread wakes when there is something new to show (frame, resize, option, toast
// animation, screenshot) and presents it; with vsync the present blocks on the display,
// otherwise presents are capped at the display's refresh rate.
//
// Ported from the BarbieRecomp runtime (runtime/render/render.c, the maintainer's own
// code).

#include "render/Presenter.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "platform/Sdl.h"
#include "render/Backend.h"
#include "render/Geometry.h"
#include "render/Overlay.h"
#include "render/Screenshot.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace brc::render {

namespace {

constexpr std::uint64_t kToastHoldNs = 1'500'000'000;
constexpr std::uint64_t kToastFadeNs = 400'000'000;
constexpr int kMinWindowW = 160;
constexpr int kMinWindowH = 120;

std::uint32_t packSize(int w, int h) {
    return static_cast<std::uint32_t>(std::clamp(w, 1, 0xffff)) << 16 |
           static_cast<std::uint32_t>(std::clamp(h, 1, 0xffff));
}

struct Slot {
    std::vector<std::uint8_t> index; // tightly packed, w bytes per row
    int w = 0;
    int h = 0;
    std::array<std::uint32_t, 256> palette{};
    std::uint64_t seq = 0;
};

} // namespace

struct Presenter::Shared {
    std::mutex mutex;
    std::condition_variable cv;     // wakes the render thread
    std::condition_variable doneCv; // render thread -> main thread: it has finished
    bool threadDone = false;
    bool stop = false;
    std::atomic<bool> failed{false}; // the backend reported a lost device

    // Frame mailbox. `back` is touched by the submitting thread only, `front` by the
    // render thread only; `ready` changes hands under the mutex.
    std::array<Slot, 3> slots;
    int back = 0;
    int ready = 1;
    int front = 2;
    bool readyNew = false;
    std::uint64_t seq = 0;

    // Output and options, published by the main thread.
    int outW = 0;
    int outH = 0;
    Aspect aspect = Aspect::Original;
    Filter filter = Filter::Sharp;
    bool vsync = true;
    float refreshHz = 60.0f;
    bool minimized = false;
    bool redraw = false;

    OverlayBitmap toast;
    std::uint64_t toastSeq = 0;
    std::uint64_t toastStartNs = 0;

    std::filesystem::path screenshotPath;
    bool screenshotPending = false;

    // Caller holds the mutex.
    void wake() {
        redraw = true;
        cv.notify_all();
    }

    void setToast(const OverlayBitmap& bmp) {
        toast = bmp;
        ++toastSeq;
        toastStartNs = SDL_GetTicksNS();
        wake();
    }

    float toastAlpha(std::uint64_t now) const {
        if (!toastStartNs)
            return 0.0f;
        std::uint64_t t = now - toastStartNs;
        if (t < kToastHoldNs)
            return 1.0f;
        if (t >= kToastHoldNs + kToastFadeNs)
            return 0.0f;
        return 1.0f - static_cast<float>(t - kToastHoldNs) / static_cast<float>(kToastFadeNs);
    }
};

// The render thread. Holds its own reference to the shared state, so that a thread stuck
// in the driver at shutdown can be abandoned safely.
void Presenter::renderLoop(std::shared_ptr<Shared> shared, Backend* backend) {
    using namespace std::chrono_literals;
    Shared& s = *shared;
    OverlayBitmap overlay;
    std::uint64_t overlaySeq = 0;
    std::uint64_t lastPresent = 0;
    bool toastDrawn = false;
    // Debug aid: OPENBRC_RENDER_FAIL_AFTER=<n> reports a lost device after n presents, to
    // exercise the recovery in pump().
    static const long long failAfter = [] {
        const char* v = std::getenv("OPENBRC_RENDER_FAIL_AFTER");
        return v && *v ? std::max(std::atoll(v), 1LL) : 0LL;
    }();
    long long presents = 0;

    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);
    backend->attach();
    std::unique_lock lock(s.mutex);
    while (!s.stop) {
        std::uint64_t now = SDL_GetTicksNS();
        float alpha = s.toastAlpha(now);
        bool shotDue = s.screenshotPending;
        if (s.minimized || !(s.redraw || alpha > 0.0f || toastDrawn || shotDue)) {
            s.cv.wait_for(lock, 250ms);
            continue;
        }
        if (s.readyNew) {
            std::swap(s.front, s.ready);
            s.readyNew = false;
        }
        s.redraw = false;
        if (s.toastSeq != overlaySeq) {
            overlay = s.toast;
            overlaySeq = s.toastSeq;
        }
        const Slot& fs =
            s.slots[static_cast<std::size_t>(s.front)]; // only this thread touches the front slot
        Capture capture;
        std::filesystem::path shotPath = shotDue ? s.screenshotPath : std::filesystem::path();
        FrameData f;
        f.index = fs.seq ? fs.index.data() : nullptr;
        f.palette = fs.palette.data();
        f.srcW = fs.seq ? fs.w : 640;
        f.srcH = fs.seq ? fs.h : 480;
        f.frameSeq = fs.seq;
        f.outW = s.outW;
        f.outH = s.outH;
        f.aspect = s.aspect;
        f.filter = s.filter;
        f.vsync = s.vsync;
        f.overlay = overlay.pixels.data();
        f.overlayW = overlay.width;
        f.overlayH = overlay.height;
        f.overlaySeq = overlaySeq;
        f.overlayAlpha = alpha;
        f.capture = shotDue ? &capture : nullptr;
        auto period = static_cast<std::uint64_t>(1e9 / (s.refreshHz > 1.0f ? s.refreshHz : 60.0f));
        lock.unlock();

        DrawResult r = backend->draw(f);
        if (failAfter && r == DrawResult::Ok && ++presents >= failAfter) {
            log::warn("render: OPENBRC_RENDER_FAIL_AFTER: simulating a lost device");
            r = DrawResult::Lost;
        }

        // Save outside the lock; encoding a large image takes a moment.
        std::string shotMessage;
        if (shotDue && r == DrawResult::Ok) {
            std::string error;
            if (saveCapture(capture, shotPath, &error)) {
                log::info("render: screenshot saved to {} ({}x{})", str::fromPath(shotPath), capture.width,
                          capture.height);
                shotMessage = "Screenshot saved";
            } else {
                log::error("render: screenshot {}: {}", str::fromPath(shotPath), error);
                shotMessage = "Screenshot failed";
            }
        }
        OverlayBitmap shotToast = shotMessage.empty() ? OverlayBitmap{} : rasterizeToast(shotMessage);
        now = SDL_GetTicksNS();
        lock.lock();
        if (!shotMessage.empty()) {
            if (s.screenshotPath == shotPath)
                s.screenshotPending = false;
            s.setToast(shotToast);
        }
        if (r == DrawResult::Lost) {
            s.failed = true;
            break;
        }
        if (r == DrawResult::Skipped) {
            // E.g. the swapchain went out of date or the window has no area: retry soon.
            s.redraw = true;
            s.cv.wait_for(lock, 10ms);
            continue;
        }
        toastDrawn = alpha > 0.0f;
        // Without vsync, cap at the refresh rate; with vsync the present already blocks,
        // the half-period cap only guards against drivers that ignore it.
        std::uint64_t minGap = f.vsync ? period / 2 : period;
        std::uint64_t next = lastPresent + minGap;
        if (lastPresent && now < next && !s.stop) {
            lock.unlock();
            SDL_DelayPrecise(next - now);
            lock.lock();
            now = next;
        }
        lastPresent = now;
    }
    lock.unlock();
    backend->detach();
    lock.lock();
    s.threadDone = true;
    s.doneCv.notify_all();
}

namespace {

// nullptr when that backend is not compiled in.
std::unique_ptr<Backend> createBackend(BackendKind kind) {
#ifdef OPENBRC_HAVE_VULKAN
    if (kind == BackendKind::Vulkan)
        return createVulkanBackend();
#endif
#ifdef OPENBRC_HAVE_OPENGL
    if (kind == BackendKind::OpenGL)
        return createOpenGLBackend();
#endif
    return nullptr;
}

bool compiledIn(BackendKind kind) {
#ifdef OPENBRC_HAVE_VULKAN
    if (kind == BackendKind::Vulkan)
        return true;
#endif
#ifdef OPENBRC_HAVE_OPENGL
    if (kind == BackendKind::OpenGL)
        return true;
#endif
    return false;
}

std::string_view backendLabel(BackendKind kind) {
    return kind == BackendKind::Vulkan ? "Vulkan" : "OpenGL";
}

} // namespace

Presenter::Presenter() : m_shared(std::make_shared<Shared>()), m_sourceSize(packSize(640, 480)) {}

Presenter::~Presenter() {
    shutdown();
}

std::unique_ptr<Presenter> Presenter::create(const std::string& title, const DisplaySettings& settings,
                                             std::string* error) {
    std::unique_ptr<Presenter> p(new Presenter());
    p->m_settings = settings;
    p->m_settings.windowWidth = std::max(settings.windowWidth, kMinWindowW);
    p->m_settings.windowHeight = std::max(settings.windowHeight, kMinWindowH);
    {
        Shared& s = *p->m_shared;
        std::lock_guard lock(s.mutex);
        s.aspect = settings.aspect;
        s.filter = settings.filter;
        s.vsync = settings.vsync;
    }

    // The preferred backend, then the other one as a fallback.
    const BackendKind first =
        settings.backend == BackendKind::OpenGL ? BackendKind::OpenGL : BackendKind::Vulkan;
    const BackendKind second = first == BackendKind::Vulkan ? BackendKind::OpenGL : BackendKind::Vulkan;
    std::vector<BackendKind> order;
    for (BackendKind kind : {first, second}) {
        if (compiledIn(kind))
            order.push_back(kind);
        else if (kind == settings.backend)
            log::warn("render: this build has no {} renderer", backendLabel(kind));
    }
    for (std::size_t i = 0; i < order.size(); ++i) {
        auto backend = createBackend(order[i]);
        if (backend &&
            p->openWindow(std::move(backend), title, p->m_settings.windowWidth, p->m_settings.windowHeight))
            break;
        if (i + 1 < order.size())
            log::warn("render: {} is not available; falling back to {}", backendLabel(order[i]),
                      backendLabel(order[i + 1]));
    }
    if (!p->m_window) {
        if (error)
            *error = "no usable renderer: neither Vulkan nor OpenGL could be initialised (see the log)";
        return nullptr;
    }
    p->startThread();
    return p;
}

bool Presenter::openWindow(std::unique_ptr<Backend> backend, const std::string& title, int width,
                           int height) {
    backend->prepareWindow();
    SDL_WindowFlags flags =
        backend->windowFlags() | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    if (m_settings.fullscreen)
        flags |= SDL_WINDOW_FULLSCREEN;
    SDL_Window* window = SDL_CreateWindow(title.c_str(), width, height, flags);
    if (!window) {
        log::warn("render: {}: cannot create the window: {}", backend->name(), SDL_GetError());
        return false;
    }
    SDL_SetWindowFullscreenMode(window, nullptr); // fullscreen = borderless at the desktop mode
    SDL_SetWindowMinimumSize(window, kMinWindowW, kMinWindowH);
    int pw = 0;
    int ph = 0;
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    if (!backend->init(window, pw, ph, m_settings.vsync)) {
        backend.reset(); // before the window it renders to
        SDL_DestroyWindow(window);
        return false;
    }
    m_window = window;
    m_backend = std::move(backend);
    SDL_ShowWindow(window);
    publishOutput();
    log::info("render: presenting with {} on {}", m_backend->name(), m_backend->deviceName());
    return true;
}

void Presenter::startThread() {
    Shared& s = *m_shared;
    {
        std::lock_guard lock(s.mutex);
        s.stop = false;
        s.threadDone = false;
        s.failed = false;
        s.redraw = true;
    }
    m_startedNs = SDL_GetTicksNS();
    m_thread = std::thread(renderLoop, m_shared, m_backend.get());
}

// Returns false if the thread is stuck (e.g. a driver blocking in present forever).
bool Presenter::stopThread() {
    using namespace std::chrono_literals;
    if (!m_thread.joinable())
        return true;
    Shared& s = *m_shared;
    bool done = false;
    {
        std::unique_lock lock(s.mutex);
        s.stop = true;
        s.cv.notify_all();
        done = s.doneCv.wait_for(lock, 2s, [&] { return s.threadDone; });
    }
    if (!done) {
        log::error("render: the render thread did not stop; skipping GPU teardown");
        m_thread.detach();
        return false;
    }
    m_thread.join();
    return true;
}

// The render thread is stuck but still uses the backend and the window: abandon both
// (and SDL) to it rather than freeing them under its feet.
void Presenter::abandonRenderer() {
    (void)m_backend.release();
    m_window = nullptr;
    m_dead = true;
    platform::Sdl::skipQuitOnExit();
}

bool Presenter::shutdown() {
    if (!m_window)
        return !m_dead;
    if (!stopThread()) {
        abandonRenderer();
        return false;
    }
    m_backend.reset();
    SDL_DestroyWindow(m_window);
    m_window = nullptr;
    return true;
}

bool Presenter::pump() {
    if (m_dead)
        return false;
    if (!m_thread.joinable() || !m_shared->failed.load())
        return true;
    log::warn("render: {}: the GPU device was lost; restarting the renderer", m_backend->name());
    if (!stopThread()) {
        abandonRenderer();
        return false;
    }
    BackendKind kind = m_backend->kind();
    m_backend.reset();
    // Losses in quick succession mean the backend does not work here; one now and then
    // (driver update, GPU reset) is just recovered from.
    if (SDL_GetTicksNS() - m_startedNs > 60'000'000'000ull)
        m_failures = 0;
    ++m_failures;
    if (m_failures < 3) {
        int pw = 0;
        int ph = 0;
        SDL_GetWindowSizeInPixels(m_window, &pw, &ph);
        auto backend = createBackend(kind);
        if (backend && backend->init(m_window, pw, ph, m_settings.vsync)) {
            m_backend = std::move(backend);
            publishOutput();
            startThread();
            return true;
        }
    }
    auto fallback = kind == BackendKind::Vulkan ? createBackend(BackendKind::OpenGL) : nullptr;
    if (!fallback) {
        log::error("render: the {} renderer stopped working", backendLabel(kind));
        m_dead = true;
        return false;
    }
    // Vulkan keeps failing: switch to OpenGL, which needs a window of its own.
    std::string title = SDL_GetWindowTitle(m_window);
    SDL_DestroyWindow(m_window);
    m_window = nullptr;
    m_failures = 0;
    if (!openWindow(std::move(fallback), title, m_settings.windowWidth, m_settings.windowHeight)) {
        log::error("render: the renderer stopped working and OpenGL is not available");
        m_dead = true;
        return false;
    }
    startThread();
    return true;
}

// ---------------------------------------------------------------------------- window

void Presenter::publishOutput() {
    if (!m_window)
        return;
    int pw = 0;
    int ph = 0;
    SDL_GetWindowSizeInPixels(m_window, &pw, &ph);
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(m_window));
    Shared& s = *m_shared;
    std::lock_guard lock(s.mutex);
    s.outW = pw;
    s.outH = ph;
    if (mode && mode->refresh_rate > 1.0f && mode->refresh_rate != s.refreshHz) {
        s.refreshHz = mode->refresh_rate;
        log::info("render: display {}x{} at {:.2f} Hz", mode->w, mode->h, mode->refresh_rate);
    }
    s.wake();
}

void Presenter::publishSettings() {
    Shared& s = *m_shared;
    std::lock_guard lock(s.mutex);
    s.aspect = m_settings.aspect;
    s.filter = m_settings.filter;
    s.vsync = m_settings.vsync;
    s.wake();
}

void Presenter::notifySettingsChanged() {
    if (m_settingsChanged)
        m_settingsChanged(m_settings);
}

void Presenter::setSettingsChangedCallback(std::function<void(const DisplaySettings&)> callback) {
    m_settingsChanged = std::move(callback);
}

void Presenter::handleEvent(const SDL_Event& event) {
    if (!m_window)
        return;
    if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST &&
        event.window.windowID != SDL_GetWindowID(m_window))
        return;
    Shared& s = *m_shared;
    switch (event.type) {
    case SDL_EVENT_WINDOW_RESIZED: {
        SDL_WindowFlags flags = SDL_GetWindowFlags(m_window);
        if (!(flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED))) {
            m_settings.windowWidth = std::max(event.window.data1, kMinWindowW);
            m_settings.windowHeight = std::max(event.window.data2, kMinWindowH);
        }
        publishOutput();
        break;
    }
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
        publishOutput();
        break;
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: {
        bool fullscreen = event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
        publishOutput();
        if (fullscreen != m_settings.fullscreen) {
            m_settings.fullscreen = fullscreen;
            notifySettingsChanged();
        }
        break;
    }
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_HIDDEN: {
        std::lock_guard lock(s.mutex);
        s.minimized = true;
        break;
    }
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_SHOWN: {
        {
            std::lock_guard lock(s.mutex);
            s.minimized = false;
        }
        publishOutput();
        break;
    }
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_FOCUS_GAINED: {
        // Visible for sure, also where no RESTORED event follows a minimise (Wayland).
        std::lock_guard lock(s.mutex);
        s.minimized = false;
        s.wake();
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------------------- frames

void Presenter::submit(const IndexedFrame& frame) {
    if (!frame.pixels || !frame.palette || frame.width <= 0 || frame.height <= 0 || frame.pitch < frame.width)
        return;
    Shared& s = *m_shared;
    const int slotIndex = s.back; // written only by this function
    Slot& slot = s.slots[static_cast<std::size_t>(slotIndex)];
    const auto w = static_cast<std::size_t>(frame.width);
    const auto h = static_cast<std::size_t>(frame.height);
    slot.index.resize(w * h);
    if (static_cast<std::size_t>(frame.pitch) == w) {
        std::memcpy(slot.index.data(), frame.pixels, w * h);
    } else {
        for (std::size_t y = 0; y < h; ++y)
            std::memcpy(slot.index.data() + y * w, frame.pixels + y * static_cast<std::size_t>(frame.pitch),
                        w);
    }
    slot.palette = *frame.palette;
    slot.w = frame.width;
    slot.h = frame.height;
    {
        std::lock_guard lock(s.mutex);
        slot.seq = ++s.seq;
        s.back = s.ready;
        s.ready = slotIndex;
        s.readyNew = true;
        s.wake();
    }
    m_sourceSize.store(packSize(frame.width, frame.height), std::memory_order_relaxed);
}

void Presenter::setSourceSize(int width, int height) {
    if (width > 0 && height > 0)
        m_sourceSize.store(packSize(width, height), std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------- coordinates

std::pair<int, int> Presenter::gameSize() const {
    std::uint32_t packed = m_sourceSize.load(std::memory_order_relaxed);
    return {static_cast<int>(packed >> 16), static_cast<int>(packed & 0xffff)};
}

namespace {

ViewMapping currentMapping(SDL_Window* window, std::pair<int, int> source, Aspect aspect) {
    ViewMapping m;
    if (window) {
        SDL_GetWindowSize(window, &m.windowW, &m.windowH);
        SDL_GetWindowSizeInPixels(window, &m.pixelW, &m.pixelH);
    }
    m.srcW = source.first;
    m.srcH = source.second;
    m.aspect = aspect;
    return m;
}

} // namespace

std::pair<int, int> Presenter::windowToGame(float wx, float wy) const {
    return render::windowToGame(currentMapping(m_window, gameSize(), m_settings.aspect), wx, wy);
}

std::pair<float, float> Presenter::gameToWindow(int gx, int gy) const {
    return render::gameToWindow(currentMapping(m_window, gameSize(), m_settings.aspect), gx, gy);
}

// ---------------------------------------------------------------------------- options + toast

void Presenter::toast(std::string_view text) {
    OverlayBitmap bmp = rasterizeToast(text);
    log::info("render: {}", text);
    Shared& s = *m_shared;
    std::lock_guard lock(s.mutex);
    s.setToast(bmp);
}

void Presenter::setAspect(Aspect aspect) {
    if (aspect == m_settings.aspect)
        return;
    m_settings.aspect = aspect;
    publishSettings();
    notifySettingsChanged();
}

void Presenter::setFilter(Filter filter) {
    if (filter == m_settings.filter)
        return;
    m_settings.filter = filter;
    publishSettings();
    notifySettingsChanged();
}

void Presenter::setVsync(bool vsync) {
    if (vsync == m_settings.vsync)
        return;
    m_settings.vsync = vsync;
    publishSettings();
    notifySettingsChanged();
}

void Presenter::setFullscreen(bool fullscreen) {
    if (!m_window)
        return;
    bool current = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (fullscreen != current) {
        SDL_SetWindowFullscreenMode(m_window, nullptr);
        if (!SDL_SetWindowFullscreen(m_window, fullscreen)) {
            log::warn("render: cannot {} fullscreen: {}", fullscreen ? "enter" : "leave", SDL_GetError());
            return;
        }
    }
    if (fullscreen != m_settings.fullscreen) {
        m_settings.fullscreen = fullscreen;
        notifySettingsChanged();
    }
}

void Presenter::toggleFullscreen() {
    if (m_window)
        setFullscreen((SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) == 0);
}

void Presenter::cycleAspect() {
    setAspect(nextAspect(m_settings.aspect));
    toast(std::string("Aspect: ") + std::string(displayName(m_settings.aspect)));
}

void Presenter::cycleFilter() {
    setFilter(nextFilter(m_settings.filter));
    toast(std::string("Filter: ") + std::string(displayName(m_settings.filter)));
}

void Presenter::toggleVsync() {
    setVsync(!m_settings.vsync);
    toast(m_settings.vsync ? "VSync: on" : "VSync: off");
}

void Presenter::requestScreenshot(std::filesystem::path path) {
    Shared& s = *m_shared;
    std::lock_guard lock(s.mutex);
    s.screenshotPath = std::move(path);
    s.screenshotPending = true;
    s.wake();
}

bool Presenter::screenshotPending() const {
    Shared& s = *m_shared;
    std::lock_guard lock(s.mutex);
    return s.screenshotPending;
}

BackendKind Presenter::backendKind() const {
    return m_backend ? m_backend->kind() : BackendKind::Auto;
}

std::string_view Presenter::backendName() const {
    return m_backend ? m_backend->name() : std::string_view();
}

std::string_view Presenter::deviceName() const {
    return m_backend ? m_backend->deviceName() : std::string_view();
}

} // namespace brc::render
