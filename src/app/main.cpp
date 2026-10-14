// openbrc: the OpenBRC executable.
#include "app/Autoplay.h"
#include "app/CommandLine.h"
#include "app/GameDataSetup.h"
#include "app/Settings.h"
#include "audio/Device.h"
#include "audio/Mixer.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Version.h"
#include "game/Godiva.h"
#include "platform/Clock.h"
#include "platform/Dialogs.h"
#include "platform/Mouse.h"
#include "platform/Sdl.h"
#include "render/Presenter.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <format>
#include <print>

namespace brc::app {
namespace {

// The engine's view of the platform.
class SdlHost final : public engine::Host {
public:
    SdlHost(render::Presenter& presenter, platform::Mouse& mouse) : m_presenter(presenter), m_mouse(mouse) {}

    std::uint32_t ticks() override { return static_cast<std::uint32_t>(m_clock.milliseconds()); }

    void present(const engine::Image8& screen, const std::array<std::uint32_t, 256>& palette) override {
        m_presenter.submit({screen.pixels.data(), screen.width, screen.height, screen.width, &palette});
    }

    std::pair<int, int> mousePosition() override { return m_pointer ? *m_pointer : m_mouse.position(); }
    // Autoplay drives a virtual pointer instead of the real one.
    void setPointerOverride(std::pair<int, int>* p) { m_pointer = p; }
    bool leftButtonDown() override { return (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) != 0; }
    void warpMouse(int x, int y) override {
        if (m_pointer)
            *m_pointer = {x, y};
        else
            m_mouse.warp(x, y);
    }

    void keepAlive() override {
        // Inside the fades: keep the window responsive, but leave input queued
        // (the engine drops it after the switch, as the original did).
        SDL_PumpEvents();
        m_presenter.pump();
        platform::sleep(std::chrono::microseconds(500));
    }

private:
    render::Presenter& m_presenter;
    platform::Mouse& m_mouse;
    platform::Clock m_clock;
    std::pair<int, int>* m_pointer = nullptr;
};

std::uint16_t modifiers() {
    const SDL_Keymod m = SDL_GetModState();
    std::uint16_t out = 0;
    if (m & SDL_KMOD_SHIFT)
        out |= engine::key::kShift;
    if (m & SDL_KMOD_CTRL)
        out |= engine::key::kCtrl;
    if (m & SDL_KMOD_ALT)
        out |= engine::key::kAlt;
    return out;
}

// Virtual key -> the engine's key code (0x45e0d0); 0 for every other key.
int keyCode(SDL_Keycode k) {
    switch (k) {
    case SDLK_ESCAPE: return engine::key::kEscape;
    case SDLK_BACKSPACE: return engine::key::kBackspace;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return engine::key::kEnter;
    case SDLK_DELETE: return engine::key::kDelete;
    case SDLK_LEFT: return engine::key::kLeft;
    case SDLK_RIGHT: return engine::key::kRight;
    case SDLK_UP: return engine::key::kUp;
    case SDLK_DOWN: return engine::key::kDown;
    case SDLK_TAB: return engine::key::kTab;
    default: return 0;
    }
}

int run(int argc, char** argv) {
    std::string error;
    auto cl = CommandLine::parse(argc, argv, &error);
    if (!cl) {
        std::println(stderr, "openbrc: {}", error);
        return 2;
    }
    if (cl->help) {
        std::print("{}", CommandLine::usage());
        return 0;
    }
    if (cl->version) {
        std::println("OpenBRC {}", kProjectVersion);
        return 0;
    }
    log::setLevel(cl->verbose ? log::Level::Debug : log::Level::Info);
    if (cl->checkSource)
        return checkSource(*cl->checkSource);
    log::setFile(cl->logFile ? *cl->logFile : paths::userDataDir() / "openbrc.log");
    log::info("OpenBRC {}", kProjectVersion);

    auto sdl = platform::Sdl::create({}, &error);
    if (!sdl) {
        platform::showMessage(platform::MessageKind::Error, "OpenBRC", error);
        return 1;
    }

    Settings settings = Settings::load();
    if (cl->renderer)
        settings.display.backend = *cl->renderer;
    if (cl->fullscreen)
        settings.display.fullscreen = *cl->fullscreen;
    if (cl->skipLogos)
        settings.skipIntroLogos = true;

    auto presenter = render::Presenter::create("OpenBRC", settings.display, &error);
    if (!presenter) {
        platform::showMessage(platform::MessageKind::Error, "OpenBRC",
                              std::format("Cannot open the game window (no Vulkan or OpenGL renderer): {}", error));
        return 1;
    }
    log::info("renderer: {} on {}", presenter->backendName(), presenter->deviceName());
    presenter->setSettingsChangedCallback([&](const render::DisplaySettings& d) {
        settings.display = d;
        settings.save();
    });

    // The original game is required: find it or ask for it.
    auto data = findGameData(settings, cl->sources, cl->setup, presenter->window());
    if (!data) {
        log::info("no game data; exiting");
        return 1;
    }
    settings.save();
    for (const auto& s : data->sources)
        log::info("game data: {}", s.describe());

    audio::Mixer mixer;
    mixer.setUserVolume(static_cast<float>(settings.volume) / 100.0f);
    auto output = audio::openOutput(mixer);

    platform::Mouse mouse(*presenter);
    SdlHost host(*presenter, mouse);
    game::Godiva app(data->vfs, host, &mixer);
    std::string startNode = cl->startNode.value_or("");
    if (startNode.empty() && settings.skipIntroLogos)
        startNode = "preferences";
    app.start(startNode);
    mouse.showCursor(false); // the game draws its own cursor

    std::optional<Autoplay> autoplay;
    std::pair<int, int> autoPointer{320, 240};
    if (cl->autoplay) {
        autoplay = Autoplay::parse(*cl->autoplay, paths::userDataDir() / "autoplay", &error);
        if (!autoplay) {
            log::error("{}", error);
            return 2;
        }
        host.setPointerOverride(&autoPointer);
    }
    SDL_StartTextInput(presenter->window());

    bool running = true;
    while (running && !app.quitRequested()) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            presenter->handleEvent(ev);
            int gx = 0, gy = 0;
            const bool isMouse = mouse.handleEvent(ev, &gx, &gy);
            switch (ev.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                // WM_CLOSE: quit at once, without saving (application.md §3.5).
                app.quit();
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                if (settings.pauseInBackground)
                    app.setActive(false);
                mouse.showCursor(true);
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                app.setActive(true);
                mouse.showCursor(false);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                if (!isMouse || (ev.button.button != SDL_BUTTON_LEFT && ev.button.button != SDL_BUTTON_RIGHT))
                    break;
                const bool left = ev.button.button == SDL_BUTTON_LEFT;
                const bool down = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                engine::InputEvent e;
                e.type = left ? (down ? engine::InputEvent::LeftDown : engine::InputEvent::LeftUp)
                              : (down ? engine::InputEvent::RightDown : engine::InputEvent::RightUp);
                e.modifiers = modifiers();
                e.x = gx;
                e.y = gy;
                app.postInput(e);
                break;
            }
            case SDL_EVENT_KEY_DOWN: {
                const SDL_Keycode k = ev.key.key;
                const bool alt = (ev.key.mod & SDL_KMOD_ALT) != 0;
                if ((k == SDLK_RETURN && alt) || k == SDLK_F11) {
                    presenter->toggleFullscreen();
                    break;
                }
                if (alt && k == SDLK_F4) {
                    app.quit();
                    break;
                }
                if (k == SDLK_F10) {
                    presenter->cycleAspect();
                    break;
                }
                if (k == SDLK_F9) {
                    presenter->cycleFilter();
                    break;
                }
                if (k == SDLK_F8) {
                    presenter->toggleVsync();
                    break;
                }
                if (k == SDLK_F12) {
                    const auto dir = paths::userDataDir() / "Screenshots";
                    std::error_code ec;
                    std::filesystem::create_directories(dir, ec);
                    const auto name = std::format("openbrc-{}.png", std::chrono::system_clock::now().time_since_epoch().count());
                    presenter->requestScreenshot(dir / name);
                    presenter->toast("Screenshot saved");
                    break;
                }
                const auto [mx, my] = mouse.position();
                engine::InputEvent e;
                e.type = engine::InputEvent::KeyDown;
                e.modifiers = modifiers();
                e.key = keyCode(k);
                e.x = mx;
                e.y = my;
                app.postInput(e); // WM_KEYDOWN
                // WM_CHAR for the keys that produce control characters (text arrives below).
                std::uint8_t ch = 0;
                if (k == SDLK_RETURN || k == SDLK_KP_ENTER)
                    ch = '\r';
                else if (k == SDLK_ESCAPE)
                    ch = 27;
                else if (k == SDLK_TAB)
                    ch = '\t';
                if (ch) {
                    e.key = 0;
                    e.ch = ch;
                    app.postInput(e);
                }
                break;
            }
            case SDL_EVENT_KEY_UP: {
                const auto [mx, my] = mouse.position();
                engine::InputEvent e;
                e.type = engine::InputEvent::KeyUp;
                e.modifiers = modifiers();
                e.key = keyCode(ev.key.key);
                e.x = mx;
                e.y = my;
                app.postInput(e);
                break;
            }
            case SDL_EVENT_TEXT_INPUT: {
                // Typed characters (WM_CHAR), as Windows-1252 bytes where possible.
                const auto [mx, my] = mouse.position();
                for (const char* p = ev.text.text; *p;) {
                    std::uint32_t cp = static_cast<unsigned char>(*p);
                    int len = 1;
                    if (cp >= 0xC0 && cp < 0xE0 && p[1]) {
                        cp = ((cp & 0x1F) << 6) | (static_cast<unsigned char>(p[1]) & 0x3F);
                        len = 2;
                    } else if (cp >= 0xE0) {
                        len = cp >= 0xF0 ? 4 : 3;
                        cp = '?';
                    }
                    p += len;
                    if (cp > 0xFF)
                        continue;
                    engine::InputEvent e;
                    e.type = engine::InputEvent::KeyDown;
                    e.modifiers = modifiers();
                    e.ch = static_cast<std::uint8_t>(cp);
                    e.x = mx;
                    e.y = my;
                    app.postInput(e);
                }
                break;
            }
            default:
                break;
            }
        }
        if (!presenter->pump()) {
            platform::showMessage(platform::MessageKind::Error, "OpenBRC", "The graphics renderer stopped working.");
            running = false;
            break;
        }
        if (autoplay && !autoplay->step(app, host.ticks(), autoPointer))
            app.quit();
        app.runPass();
        // The original spins at full speed; a short sleep keeps the CPU cool
        // while still running about a thousand passes a second.
        platform::sleep(std::chrono::milliseconds(1));
    }
    settings.display = presenter->settings();
    settings.save();
    SDL_StopTextInput(presenter->window());
    mouse.showCursor(true);
    return 0;
}

} // namespace
} // namespace brc::app

int main(int argc, char** argv) {
    try {
        return brc::app::run(argc, argv);
    } catch (const std::exception& e) {
        brc::log::error("fatal: {}", e.what());
        std::println(stderr, "openbrc: fatal: {}", e.what());
        return 1;
    }
}
