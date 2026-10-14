// rendertest: opens the game window and shows an animated 640x480 indexed test pattern
// with a cycling palette, to exercise the presentation backends.
//
//   rendertest [--backend auto|vulkan|opengl] [--fullscreen] [--frames N]
//              [--screenshot FILE] [--verify] [--aspect original|stretch|integer]
//              [--filter sharp|nearest|smooth|crt] [--no-vsync] [--size WxH] [--rate HZ]
//              [--exercise] [--verbose] [--pick-file | --pick-folder]
//
// Keys: Alt+Enter fullscreen, F9 vsync, F10 aspect, F11 filter, F12 screenshot,
// R relative mouse, C cursor, W warp to the centre, Escape quit. The crosshair follows
// the mouse in game coordinates.
//
// --verify shows a static pattern of solid 16x16 blocks before the screenshot is taken,
// reads the screenshot back and checks the colour at the centre of every block against
// the palette (exit code 3 on a mismatch). An explicit --backend that could not be used
// exits with code 2. --exercise changes every display option, the window size, the
// fullscreen state and the source size while running (swapchain / resource recreation,
// e.g. under OPENBRC_VK_VALIDATION=1). --pick-file / --pick-folder open the native
// file / folder picker over the window, print the choice and exit.

#include "core/Log.h"
#include "core/StringUtil.h"
#include "platform/Clock.h"
#include "platform/Dialogs.h"
#include "platform/Mouse.h"
#include "platform/Sdl.h"
#include "render/Geometry.h"
#include "render/Presenter.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <print>
#include <string>
#include <vector>

namespace {

using namespace brc;

constexpr int kW = 640;
constexpr int kH = 480;
constexpr int kBlock = 16;

struct Options {
    render::DisplaySettings display;
    int frames = 0; // 0: run until closed
    std::string screenshot;
    bool verify = false;
    bool exercise = false;
    bool verbose = false;
    int pick = 0; // 1 file, 2 folder
    double rate = 60.0;
    bool filterGiven = false;
};

[[noreturn]] void usage(int code) {
    std::println(
        code ? stderr : stdout,
        "usage: rendertest [--backend auto|vulkan|opengl] [--fullscreen] [--frames N]\n"
        "                  [--screenshot FILE] [--verify] [--aspect original|stretch|integer]\n"
        "                  [--filter sharp|nearest|smooth|crt] [--no-vsync] [--size WxH] [--rate HZ]\n"
        "                  [--exercise] [--verbose] [--pick-file | --pick-folder]");
    std::exit(code);
}

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        auto value = [&]() -> std::string_view {
            if (i + 1 >= argc)
                usage(1);
            return argv[++i];
        };
        if (a == "--backend") {
            auto k = render::parseBackendKind(value());
            if (!k)
                usage(1);
            o.display.backend = *k;
        } else if (a == "--fullscreen") {
            o.display.fullscreen = true;
        } else if (a == "--frames") {
            o.frames = static_cast<int>(str::parseInt(value()).value_or(0));
        } else if (a == "--screenshot") {
            o.screenshot = std::string(value());
        } else if (a == "--verify") {
            o.verify = true;
        } else if (a == "--exercise") {
            o.exercise = true;
        } else if (a == "--verbose" || a == "-v") {
            o.verbose = true;
        } else if (a == "--pick-file") {
            o.pick = 1;
        } else if (a == "--pick-folder") {
            o.pick = 2;
        } else if (a == "--aspect") {
            auto v = render::parseAspect(value());
            if (!v)
                usage(1);
            o.display.aspect = *v;
        } else if (a == "--filter") {
            auto v = render::parseFilter(value());
            if (!v)
                usage(1);
            o.display.filter = *v;
            o.filterGiven = true;
        } else if (a == "--no-vsync") {
            o.display.vsync = false;
        } else if (a == "--size") {
            auto parts = str::split(value(), 'x');
            if (parts.size() != 2)
                usage(1);
            o.display.windowWidth = static_cast<int>(str::parseInt(parts[0]).value_or(1280));
            o.display.windowHeight = static_cast<int>(str::parseInt(parts[1]).value_or(960));
        } else if (a == "--rate") {
            o.rate = std::max(1.0, str::parseDouble(value()).value_or(60.0));
        } else if (a == "--help" || a == "-h") {
            usage(0);
        } else {
            std::println(stderr, "rendertest: unknown option {}", a);
            usage(1);
        }
    }
    if (o.verify && o.screenshot.empty())
        o.screenshot = "rendertest-verify.bmp";
    if (o.verify && o.frames <= 0)
        o.frames = 30;
    if (o.exercise && o.frames <= 0)
        o.frames = 240;
    return o;
}

std::uint32_t hsv(double h, double s, double v) {
    h = h - std::floor(h);
    double r = 0, g = 0, b = 0;
    int i = static_cast<int>(h * 6.0);
    double f = h * 6.0 - i;
    double p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    switch (i % 6) {
    case 0:
        r = v, g = t, b = p;
        break;
    case 1:
        r = q, g = v, b = p;
        break;
    case 2:
        r = p, g = v, b = t;
        break;
    case 3:
        r = p, g = q, b = v;
        break;
    case 4:
        r = t, g = p, b = v;
        break;
    default:
        r = v, g = p, b = q;
        break;
    }
    auto c = [](double x) { return static_cast<std::uint32_t>(std::clamp(x * 255.0 + 0.5, 0.0, 255.0)); };
    return c(r) << 16 | c(g) << 8 | c(b);
}

// Indices 0..223 cycle through the hues; 224..255 are fixed.
enum : std::uint8_t { kBlack = 224, kWhite, kGrey, kRed, kGreen, kBlue, kYellow, kCrosshair };

void makePalette(std::array<std::uint32_t, 256>& pal, double phase) {
    for (int i = 0; i < 224; ++i)
        pal[static_cast<std::size_t>(i)] = hsv(i / 224.0 + phase, 0.75, 0.95);
    pal[kBlack] = 0x000000;
    pal[kWhite] = 0xffffff;
    pal[kGrey] = 0x808080;
    pal[kRed] = 0xff0000;
    pal[kGreen] = 0x00ff00;
    pal[kBlue] = 0x0000ff;
    pal[kYellow] = 0xffff00;
    pal[kCrosshair] = 0xff00ff;
    for (int i = kCrosshair + 1; i < 256; ++i)
        pal[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(i) * 0x010101u;
}

void drawPattern(std::vector<std::uint8_t>& px, int frame, int mouseX, int mouseY) {
    auto put = [&](int x, int y, std::uint8_t c) {
        if (x >= 0 && x < kW && y >= 0 && y < kH)
            px[static_cast<std::size_t>(y * kW + x)] = c;
    };
    // Top: hue gradient (animated through the palette). Bottom: checkerboard.
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            std::uint8_t c = y < kH / 2                            ? static_cast<std::uint8_t>(x * 224 / kW)
                             : (((x / kBlock) ^ (y / kBlock)) & 1) ? std::uint8_t{kWhite}
                                                                   : std::uint8_t{kGrey};
            put(x, y, c);
        }
    // One-pixel grid every 32 pixels: shows the filter's sharpness.
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; x += 32)
            put(x, y, kBlack);
    for (int y = 0; y < kH; y += 32)
        for (int x = 0; x < kW; ++x)
            put(x, y, kBlack);
    // A frame around the edge, so the scaling can be checked at a glance.
    for (int x = 0; x < kW; ++x) {
        put(x, 0, kRed);
        put(x, kH - 1, kRed);
    }
    for (int y = 0; y < kH; ++y) {
        put(0, y, kRed);
        put(kW - 1, y, kRed);
    }
    // A bouncing box.
    int bx = std::abs((frame * 3) % (2 * (kW - 64)) - (kW - 64));
    int by = std::abs((frame * 2) % (2 * (kH - 64)) - (kH - 64));
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            put(bx + x, by + y, (x < 2 || y < 2 || x > 61 || y > 61) ? kYellow : kBlue);
    // Crosshair at the mouse position.
    for (int d = -8; d <= 8; ++d) {
        put(mouseX + d, mouseY, kCrosshair);
        put(mouseX, mouseY + d, kCrosshair);
    }
}

std::uint8_t verifyIndex(int bx, int by) {
    return static_cast<std::uint8_t>((bx * 7 + by * 13) % 224);
}

void drawVerifyPattern(std::vector<std::uint8_t>& px) {
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x)
            px[static_cast<std::size_t>(y * kW + x)] = verifyIndex(x / kBlock, y / kBlock);
}

SDL_Surface* loadImage(const std::string& path) {
    if (str::iendsWith(path, ".bmp"))
        return SDL_LoadBMP(path.c_str());
#if SDL_VERSION_ATLEAST(3, 4, 0)
    return SDL_LoadPNG(path.c_str());
#else
    SDL_SetError("this SDL cannot read PNG; use a .bmp screenshot for --verify");
    return nullptr;
#endif
}

// Checks the colour at the centre of every 16x16 block of the verify pattern.
bool verifyScreenshot(const std::string& path, const std::array<std::uint32_t, 256>& pal,
                      render::Aspect aspect, render::Filter filter) {
    SDL_Surface* loaded = loadImage(path);
    if (!loaded) {
        std::println(stderr, "rendertest: cannot read {}: {}", path, SDL_GetError());
        return false;
    }
    SDL_Surface* img = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (!img)
        return false;
    render::Rect view = render::computeView(img->w, img->h, kW, kH, aspect);
    render::Rect toast = render::overlayRect(img->w, img->h, 256, 16); // keep clear of the toast
    const int tolerance = filter == render::Filter::Crt ? 80 : 3;
    int checked = 0, bad = 0;
    for (int by = 0; by < kH / kBlock; ++by)
        for (int bx = 0; bx < kW / kBlock; ++bx) {
            double gx = bx * kBlock + kBlock / 2.0, gy = by * kBlock + kBlock / 2.0;
            int x = view.x + static_cast<int>(gx * view.w / kW);
            int y = view.y + static_cast<int>(gy * view.h / kH);
            if (x >= toast.x && x < toast.x + toast.w + 16 && y >= toast.y && y < toast.y + toast.h + 16)
                continue;
            const auto* p = static_cast<const std::uint8_t*>(img->pixels) + y * img->pitch + x * 4;
            std::uint32_t want = pal[verifyIndex(bx, by)];
            int dr = std::abs(p[0] - static_cast<int>(want >> 16 & 0xff));
            int dg = std::abs(p[1] - static_cast<int>(want >> 8 & 0xff));
            int db = std::abs(p[2] - static_cast<int>(want & 0xff));
            ++checked;
            if (std::max({dr, dg, db}) > tolerance) {
                if (bad < 5)
                    std::println(stderr,
                                 "rendertest: block ({},{}) at ({},{}): got {:02x}{:02x}{:02x}, want {:06x}",
                                 bx, by, x, y, p[0], p[1], p[2], want);
                ++bad;
            }
        }
    // Outside the view (the bars) must be black.
    int barChecks = 0, barBad = 0;
    if (view.x > 2) {
        for (int y = img->h / 4; y < img->h * 3 / 4; y += 7) {
            const auto* p = static_cast<const std::uint8_t*>(img->pixels) + y * img->pitch + (view.x / 2) * 4;
            ++barChecks;
            barBad += (p[0] | p[1] | p[2]) > 2;
        }
    }
    SDL_DestroySurface(img);
    std::println("rendertest: verify {}: {} of {} blocks match{}", path, checked - bad, checked,
                 barChecks ? std::format(", {} of {} bar samples black", barChecks - barBad, barChecks) : "");
    return checked > 0 && bad == 0 && barBad == 0;
}

// One step of --exercise: something changes every few frames.
void exerciseStep(render::Presenter& p, int frame) {
    switch (frame % 120) {
    case 10:
        p.cycleFilter();
        break;
    case 20:
        p.cycleAspect();
        break;
    case 30:
        p.toggleVsync();
        break;
    case 40:
        SDL_SetWindowSize(p.window(), 900, 400);
        break; // wide: pillarbox
    case 50:
        SDL_SetWindowSize(p.window(), 500, 800);
        break; // tall: letterbox
    case 60:
        p.toggleFullscreen();
        break;
    case 80:
        p.toggleFullscreen();
        break;
    case 90:
        p.cycleAspect();
        break;
    case 100:
        p.toggleVsync();
        break;
    case 110:
        SDL_SetWindowSize(p.window(), 1280, 960);
        break;
    default:
        break;
    }
}

} // namespace

int main(int argc, char** argv) {
    Options opt = parseArgs(argc, argv);
    if (opt.verify && !opt.filterGiven)
        opt.display.filter = render::Filter::Nearest;
    log::setLevel(opt.verbose ? log::Level::Debug : log::Level::Info);

    std::string error;
    auto sdl = platform::Sdl::create({.video = true, .audio = false}, &error);
    if (!sdl) {
        std::println(stderr, "rendertest: {}", error);
        return 1;
    }
    auto presenter = render::Presenter::create("OpenBRC render test", opt.display, &error);
    if (!presenter) {
        std::println(stderr, "rendertest: {}", error);
        return 1;
    }
    std::println("rendertest: {} on {}, video driver {}", presenter->backendName(), presenter->deviceName(),
                 sdl->videoDriver());
    if (opt.display.backend != render::BackendKind::Auto && presenter->backendKind() != opt.display.backend) {
        std::println(stderr, "rendertest: requested {} but got {}", render::toString(opt.display.backend),
                     presenter->backendName());
        return 2;
    }
    if (!opt.verify)
        presenter->toast(std::format("{} - {}", presenter->backendName(), presenter->deviceName()));

    if (opt.pick) {
        platform::FileDialogOptions dialog;
        dialog.title = opt.pick == 1 ? "Choose a disc image" : "Choose the game folder";
        dialog.parent = presenter->window();
        dialog.filters = {{"Disc images", "iso;bin;cue;img"}, {"All files", "*"}};
        dialog.onEvent = [&](const SDL_Event& e) { presenter->handleEvent(e); };
        auto chosen =
            opt.pick == 1 ? platform::pickFile(dialog, &error) : platform::pickFolder(dialog, &error);
        if (chosen)
            std::println("rendertest: chose {}", str::fromPath(*chosen));
        else if (!error.empty())
            std::println(stderr, "rendertest: dialog failed: {}", error);
        else
            std::println("rendertest: cancelled");
        return chosen ? 0 : error.empty() ? 4 : 1;
    }

    platform::Mouse mouse(*presenter);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kW * kH));
    std::array<std::uint32_t, 256> palette{};
    const auto period = std::chrono::nanoseconds(static_cast<std::int64_t>(1e9 / opt.rate));
    platform::Clock clock;
    auto nextFrame = clock.elapsed();
    int frame = 0;
    int shots = 0;
    bool running = true;
    bool shotRequested = false;
    std::uint64_t shotDeadlineMs = 0;
    int exitCode = 0;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            presenter->handleEvent(ev);
            mouse.handleEvent(ev);
            if (ev.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                switch (ev.key.key) {
                case SDLK_ESCAPE:
                    running = false;
                    break;
                case SDLK_RETURN:
                    if (ev.key.mod & SDL_KMOD_ALT)
                        presenter->toggleFullscreen();
                    break;
                case SDLK_F9:
                    presenter->toggleVsync();
                    break;
                case SDLK_F10:
                    presenter->cycleAspect();
                    break;
                case SDLK_F11:
                    presenter->cycleFilter();
                    break;
                case SDLK_F12:
                    presenter->requestScreenshot(std::format("rendertest-{}.png", ++shots));
                    break;
                case SDLK_R:
                    mouse.setRelativeMode(!mouse.relativeMode());
                    break;
                case SDLK_C:
                    mouse.showCursor(!mouse.cursorVisible());
                    break;
                case SDLK_W:
                    mouse.warp(kW / 2, kH / 2);
                    break;
                default:
                    break;
                }
            }
        }
        if (!presenter->pump()) {
            std::println(stderr, "rendertest: the renderer stopped working");
            return 1;
        }

        bool frozen = opt.verify && opt.frames > 0 && frame >= opt.frames / 2;
        if (frozen) {
            makePalette(palette, 0.0);
            drawVerifyPattern(pixels);
        } else {
            makePalette(palette, frame / 240.0);
            auto [mx, my] = mouse.position();
            drawPattern(pixels, frame, mx, my);
        }
        if (opt.exercise) {
            exerciseStep(*presenter, frame);
            if (frame % 120 == 102) {
                mouse.warp(100, 50);
            } else if (frame % 120 == 106) {
                // Where the system pointer actually is, mapped back to game pixels (the
                // compositor may refuse to warp an unfocused window, e.g. on Wayland).
                float wx = 0, wy = 0;
                SDL_GetMouseState(&wx, &wy);
                auto [gx, gy] = presenter->windowToGame(wx, wy);
                auto [mx, my] = mouse.position();
                std::println(
                    "rendertest: warped to (100,50): game position ({},{}), system pointer at game ({},{})",
                    mx, my, gx, gy);
            }
            // Frames 65..74 of every 120 come at 320x240 (every other row and the left half
            // of each), which recreates the source-sized GPU resources twice.
            if (frame % 120 >= 65 && frame % 120 < 75)
                presenter->submit({pixels.data(), kW / 2, kH / 2, kW * 2, &palette});
            else
                presenter->submit({pixels.data(), kW, kH, kW, &palette});
        } else {
            presenter->submit({pixels.data(), kW, kH, kW, &palette});
        }
        ++frame;

        if (opt.frames > 0 && frame >= opt.frames) {
            if (opt.screenshot.empty()) {
                running = false;
            } else if (!shotRequested) {
                presenter->requestScreenshot(str::toPath(opt.screenshot));
                shotRequested = true;
                shotDeadlineMs = clock.milliseconds() + 5000;
            } else if (!presenter->screenshotPending()) {
                running = false;
                if (opt.verify && !verifyScreenshot(opt.screenshot, palette, presenter->settings().aspect,
                                                    presenter->settings().filter))
                    exitCode = 3;
            } else if (clock.milliseconds() > shotDeadlineMs) {
                std::println(stderr, "rendertest: the screenshot was not taken in time");
                running = false;
                exitCode = 1;
            }
        }

        nextFrame += period;
        auto now = clock.elapsed();
        if (nextFrame > now)
            platform::sleepPrecise(nextFrame - now);
        else
            nextFrame = now;
    }
    const auto& s = presenter->settings();
    std::println("rendertest: {} frames; settings: {} {}x{} aspect={} filter={} vsync={}", frame,
                 s.fullscreen ? "fullscreen" : "windowed", s.windowWidth, s.windowHeight,
                 render::toString(s.aspect), render::toString(s.filter), s.vsync ? "on" : "off");
    presenter.reset();
    return exitCode;
}
