// HCSmackerOverlayElement (0x41aa30–0x41b68f) on the Godiva Smacker element
// (HCSmackerElement, 0x41a300–0x41aa2f). See docs/engine/movies.md §3.
#include "game/SmackOverlayMovie.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "engine/Engine.h"
#include "engine/Resources.h"

#include <algorithm>
#include <cmath>

namespace brc::game {
namespace {

// [0x809f50] / [0x809f58]: the 640x480 world every HCSmackerElement draws
// into, created by the first element and freed with the last.
std::weak_ptr<engine::Image8>& sharedWorld() {
    static std::weak_ptr<engine::Image8> world;
    return world;
}

// [0x809f68]: when any overlay element last advanced (0x41acb0).
std::uint32_t s_lastAdvance = 0;

// The rate of the last SmackFrameRate call: AddOverlay (0x41ae20) opens its
// streams with SMACKFRAMERATE (0x80) without calling SmackFrameRate itself.
int s_smackFrameRate = 0;

std::string resourceFile(std::string_view path) {
    std::string p(path);
    std::ranges::replace(p, '\\', '/');
    return engine::resourcePath(p);
}

// SmackBlit / SmackBlitTrans (BarbieRecomp api_usage §6.6): copy a w x h
// rectangle of an 8-bit buffer; `twice` writes every source pixel as 2x2 with
// the destination origin in half-resolution units; `trans` >= 0 skips that
// index. Everything is clipped to both buffers.
void smackBlit(engine::Image8& dst, int dx, int dy, std::span<const std::uint8_t> src, int srcPitch, int srcRows,
               int sx, int sy, int w, int h, bool twice, int trans) {
    const int scale = twice ? 2 : 1;
    for (int j = 0; j < h; ++j) {
        const int srow = sy + j;
        if (srow < 0 || srow >= srcRows)
            continue;
        for (int i = 0; i < w; ++i) {
            const int scol = sx + i;
            if (scol < 0 || scol >= srcPitch)
                continue;
            const std::uint8_t v = src[static_cast<std::size_t>(srow) * static_cast<std::size_t>(srcPitch) +
                                       static_cast<std::size_t>(scol)];
            if (v == trans)
                continue;
            for (int yy = 0; yy < scale; ++yy) {
                const int y = (dy + j) * scale + yy;
                if (y < 0 || y >= dst.height)
                    continue;
                for (int xx = 0; xx < scale; ++xx) {
                    const int x = (dx + i) * scale + xx;
                    if (x >= 0 && x < dst.width)
                        dst.at(x, y) = v;
                }
            }
        }
    }
}

} // namespace

SmackOverlayMovie::SmackOverlayMovie(engine::Engine& engine, int fps_, bool doubleSize)
    : Task(engine.tasks()), fps(static_cast<std::uint32_t>(fps_)), maxFps(static_cast<std::uint32_t>(fps_)),
      m_engine(engine), m_doubleSize(doubleSize) {
    // 0x41ab50: the overlay list (0x41aa30: key RGB 255,255,255, palette range
    // 0..255), then the element (0x41a300) with use-palette 1, loop 1, paused 1,
    // clip 640x480. The HSMACKBLIT ([0x809f60], flags 1, or 3 when double) has
    // no state worth keeping here.
    m_world = sharedWorld().lock();
    if (!m_world) {
        m_world = std::make_shared<engine::Image8>(640, 480, 0);
        sharedWorld() = m_world;
    }
}

SmackOverlayMovie::~SmackOverlayMovie() {
    // 0x41ac60: ~HCSmackerElement (SmackSummary, SmackClose, the last one frees
    // the world), then ~HCSmackerOverlayList (closes the owned overlays).
    unlink();
}

const engine::Image8* SmackOverlayMovie::image() const { return m_main ? m_world.get() : nullptr; }

std::shared_ptr<SmackOverlayMovie::Stream> SmackOverlayMovie::openStream(std::string_view path) const {
    auto file = m_engine.resources().openFile(resourceFile(path));
    if (!file) {
        log::warn("SmackOverlayMovie: can't open {}", path);
        return nullptr;
    }
    auto smk = formats::Smacker::open(file);
    if (!smk) {
        log::warn("SmackOverlayMovie: {}: {}", path, smk.error());
        return nullptr;
    }
    auto s = std::make_shared<Stream>(std::move(*smk));
    s->smk.useOwnBuffer();
    // The streams are opened with all audio tracks (0xFE000), but the trail
    // files are silent.
    s->smk.setAudioTracks(0);
    s->periodMs = s_smackFrameRate > 0 ? 1000.0 / s_smackFrameRate
                                       : static_cast<double>(s->smk.frameDurationUs()) / 1000.0;
    return s;
}

bool SmackOverlayMovie::open(std::string_view path, bool) {
    // HCSmackerOverlayElement::Open (0x41adb0) = HCSmackerElement::Open
    // (0x41a700): SmackFrameRate(fps) when fps != 0, SmackOpen, decode target
    // the shared world at (0, 0) pitch 640. With double (or, later, overlays)
    // the main movie decodes into its own buffer (+0x04) instead; here every
    // stream has its own buffer and the 1x case copies it into the world.
    m_path = path;
    if (fps != 0)
        s_smackFrameRate = static_cast<int>(fps);
    m_main = openStream(path);
    return m_main != nullptr;
}

SmackOverlayMovie::Overlay* SmackOverlayMovie::addOverlay(std::string_view path) {
    // 0x41ae20: opens the stream (0xFE200, +0x80 when fps != 0), decodes into
    // its own buffer and appends it disabled at (0, 0).
    auto s = openStream(path);
    if (!s)
        return nullptr;
    if (fps == 0)
        s->periodMs = static_cast<double>(s->smk.frameDurationUs()) / 1000.0;
    Overlay& o = m_overlays.emplace_back();
    o.stream = std::move(s);
    o.enabled = false;
    o.owned = true;
    o.x = o.y = 0;
    return &o;
}

SmackOverlayMovie::Overlay* SmackOverlayMovie::addSharedOverlay(const Overlay* other) {
    // 0x41af10: a disabled copy of another element's overlay (same stream and
    // buffer, same position, not owned).
    if (!other)
        return nullptr;
    Overlay& o = m_overlays.emplace_back();
    o.stream = other->stream;
    o.enabled = false;
    o.owned = false;
    o.x = other->x;
    o.y = other->y;
    return &o;
}

void SmackOverlayMovie::rewind() {
    // 0x41afb0: paused, main movie to frame 1, done = 0, then the overlays via
    // 0x41ab20, which never advances its list pointer and so hangs when an
    // overlay exists (it has no caller); here it rewinds each overlay once.
    paused = true;
    gotoFrame(1);
    done = false;
    for (auto& o : m_overlays)
        seek(*o.stream, 0);
}

void SmackOverlayMovie::gotoFrame(std::uint32_t frame) {
    // 0x41a610: SmackGoto is 1-based (FrameNum = n ? n - 1 : 0).
    if (!m_main || frame >= m_main->smk.frameCount())
        return;
    seek(*m_main, frame ? frame - 1 : 0);
    done = false;
}

void SmackOverlayMovie::seek(Stream& s, std::uint32_t frame) { s.smk.gotoFrame(frame); }

std::uint32_t SmackOverlayMovie::slowDown() {
    // 0x41a990 (unsigned: 0 - 1 also becomes 2).
    if (--fps < 2)
        fps = 2;
    return fps;
}

std::uint32_t SmackOverlayMovie::speedUp() {
    // 0x41a9c0.
    if (++fps > maxFps)
        fps = maxFps;
    return fps;
}

bool SmackOverlayMovie::smackWait(const Stream& s) const {
    return s.timed && static_cast<std::int32_t>(m_engine.ticks() - s.nextDue) < 0;
}

void SmackOverlayMovie::frameShown(Stream& s) const {
    // SmackWait's clock: frames are due one period apart; a stream that fell
    // far behind starts over instead of racing to catch up.
    const std::uint32_t now = m_engine.ticks();
    const auto period = static_cast<std::uint32_t>(std::lround(s.periodMs));
    if (!s.timed || static_cast<std::int32_t>(now - s.nextDue) > 250)
        s.nextDue = now + period;
    else
        s.nextDue += period;
    s.timed = true;
}

void SmackOverlayMovie::spendTime() {
    // 0x41acb0: like HCSmackerElement::SpendTime (0x41a640) with its own shared
    // timestamp, and every enabled overlay must be ready too.
    if (!m_main || paused || fps == 0)
        return;
    if (static_cast<std::uint32_t>(m_engine.ticks() - s_lastAdvance) <= 1000 / fps)
        return;
    bool ready = true;
    for (const auto& o : m_overlays) {
        if (o.enabled && smackWait(*o.stream)) {
            ready = false;
            break;
        }
    }
    if (smackWait(*m_main) || !ready)
        return;
    s_lastAdvance = m_engine.ticks();
    advance();
    if (accelerate) {
        if (++fps > 24)
            fps = 24;
    } else if (decelerate) {
        if (--fps == 1)
            fps = 2;
    }
}

void SmackOverlayMovie::applyPalette() {
    // 0x41b5e0: with DirectDraw (always here) the screen palette, after
    // WaitForVerticalBlank; otherwise the world's.
    if (palette)
        m_engine.setScreenPalette(*palette);
}

void SmackOverlayMovie::clearWorld() {
    if (auto w = sharedWorld().lock())
        w->fill(0);
}

void SmackOverlayMovie::advance() {
    // HCSmackerOverlayElement::AdvanceFrame (0x41afe0).
    if (!m_main || !(loop || !done))
        return;
    formats::Smacker& smk = m_main->smk;

    // 1. The movie's palette: entries low..high from the main stream, the rest
    //    from the first overlay; an entry equal to the key colour becomes the
    //    transparent index. The trail rides turn this off (use-palette 0).
    if (smk.paletteChanged() && usePalette) {
        std::array<std::uint32_t, 256> rgb{};
        auto take = [&](const std::array<std::uint8_t, 768>& p, int i) {
            const std::uint8_t r = p[static_cast<std::size_t>(3 * i)], g = p[static_cast<std::size_t>(3 * i + 1)],
                               b = p[static_cast<std::size_t>(3 * i + 2)];
            if (r == keyRgb[0] && g == keyRgb[1] && b == keyRgb[2])
                transparentIndex = static_cast<std::uint8_t>(i);
            rgb[static_cast<std::size_t>(i)] = std::uint32_t{r} << 16 | std::uint32_t{g} << 8 | b;
        };
        for (int i = paletteLow; i <= paletteHigh; ++i)
            take(smk.palette(), i);
        // (The original reads a null stream here when there is no overlay and
        // high < 255; no caller does that.)
        if (((!m_overlays.empty() && paletteLow != 0) || paletteHigh != 0) && !m_overlays.empty())
            for (int i = paletteHigh + 1; i < 256; ++i)
                take(m_overlays.front().stream->smk.palette(), i);
        palette = rgb;
        applyPalette();
    }

    // 2. SmackDoFrame for the main stream and each enabled overlay.
    if (auto r = smk.decodeFrame(); !r)
        log::warn("SmackOverlayMovie {}: {}", m_path, r.error());
    frameShown(*m_main);
    markChanged();
    for (auto& o : m_overlays) {
        if (!o.enabled)
            continue;
        if (auto r = o.stream->smk.decodeFrame(); !r)
            log::warn("SmackOverlayMovie {}: overlay: {}", m_path, r.error());
        frameShown(*o.stream);
    }

    // 3. The letterbox origin.
    int offX = 0, offY = 0;
    if (centre) {
        offX = (640 - clipW) / 2;
        offY = (480 - clipH) / 2;
    }

    const int movieW = static_cast<int>(smk.width()), movieH = static_cast<int>(smk.height());
    const auto mainPixels = smk.frameBuffer();
    if (m_overlays.empty() && !m_doubleSize) {
        // Decoded straight into the world at (0, 0).
        smackBlit(*m_world, 0, 0, mainPixels, movieW, movieH, 0, 0, movieW, movieH, false, -1);
    } else {
        // 4. The main buffer into the world: 1x whole at (0, 0), or 2x from
        //    (srcX, srcY) through the clip window.
        if (!m_doubleSize)
            smackBlit(*m_world, 0, 0, mainPixels, movieW, movieH, 0, 0, movieW, movieH, false, -1);
        else
            smackBlit(*m_world, offX >> 1, offY >> 1, mainPixels, movieW, movieH, srcX, srcY, clipW >> 1,
                      clipH >> 1, true, -1);

        // 5. The enabled overlays, 1x with the transparent index.
        for (const auto& o : m_overlays) {
            if (!o.enabled)
                continue;
            const auto pixels = o.stream->smk.frameBuffer();
            const int ow = o.width(), oh = o.height();
            if (ow == 640) {
                // Full-width overlays only move with the scroll.
                if (scroll < 1)
                    smackBlit(*m_world, 0, 0, pixels, 640, oh, -scroll, 0, scroll + 640, oh, false, transparentIndex);
                else
                    smackBlit(*m_world, scroll, 0, pixels, 640, oh, 0, 0, 640 - scroll, oh, false, transparentIndex);
                continue;
            }
            // At (x + scroll, y), clipped to the screen and the letterbox. The
            // arithmetic is the original's, unsigned: a destination left of or
            // above the screen is not drawn, and the letterbox clip on the
            // left/top widens instead of shifting the source (harmless with
            // the trail's values).
            std::int64_t dx = std::int64_t{scroll} + o.x, dy = o.y;
            int sx = 0, sy = 0;
            if (o.x < 0) {
                sx = -o.x;
                dx = 0;
            }
            if (o.y < 0) {
                sy = -o.y;
                dy = 0;
            }
            if (dx < 0 || dy < 0)
                continue;
            std::int64_t w = ow, h = oh;
            if (w + dx > 640)
                w = 640 - dx;
            if (h + dy > 480)
                h = 480 - dy;
            if (dx < offX) {
                w += offX - dx;
                dx = offX;
            }
            if (dy < offY) {
                h += offY - dy;
                dy = offY;
            }
            if (w + dx > offX + clipW)
                w = offX + clipW - dx;
            if (h + dy > offY + clipH)
                h = offY + clipH - dy;
            if (dx < 640 && dy < 480)
                smackBlit(*m_world, static_cast<int>(dx), static_cast<int>(dy), pixels, ow, oh, sx, sy,
                          static_cast<int>(w), static_cast<int>(h), false, transparentIndex);
        }
    }

    // 6. Next frames: the main movie loops or ends; overlays always loop.
    if (smk.currentFrame() + 1 >= smk.frameCount()) {
        if (loop)
            seek(*m_main, 0);
        else
            done = true;
    } else {
        smk.nextFrame();
    }
    for (auto& o : m_overlays) {
        if (!o.enabled)
            continue;
        auto& os = o.stream->smk;
        if (os.currentFrame() + 1 >= os.frameCount())
            seek(*o.stream, 0);
        else
            os.nextFrame();
    }

    // 7. The palette the owner set, once.
    if (applyPaletteOnce) {
        applyPalette();
        applyPaletteOnce = false;
    }
}

} // namespace brc::game
