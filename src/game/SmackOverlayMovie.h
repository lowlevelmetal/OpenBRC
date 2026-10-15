#pragma once

#include "engine/Element.h"
#include "engine/Task.h"
#include "formats/Smacker.h"

#include <array>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace brc::engine {
class Engine;
}

namespace brc::game {

// HCSmackerOverlayElement (0x41ab50, vtable 0x7f1aa0, 0x130 bytes): the trail
// rides' Smacker player. A Godiva Smacker element (HCSmackerElement 0x41a300,
// embedded at +0x14) whose frames go to the one 640x480 world all these
// elements share ([0x809f50]), shown doubled (2x) through a clip window, with
// a list of overlay streams (horse heads, deer, butterflies) composited over
// it with a transparent index. See docs/engine/movies.md §3.
class SmackOverlayMovie final : public engine::Element, public engine::Task {
public:
    // A Smacker stream with its own decode buffer (the radmalloc'ed buffers of
    // 0x41adb0 / 0x41ae20) and the frame clock SmackWait keeps for it.
    struct Stream {
        explicit Stream(formats::Smacker s) : smk(std::move(s)) {}
        formats::Smacker smk;
        double periodMs = 100;      // the rate forced by SmackFrameRate at open time
        std::uint32_t nextDue = 0;  // when SmackWait lets the next frame through
        bool timed = false;         // a frame has been shown
    };

    // An overlay list node (0x1C bytes: Smack*, buffer, u16 enabled, next,
    // u16 owned, x, y). Shared overlays (AddSharedOverlay) point at another
    // element's stream and buffer.
    struct Overlay {
        std::shared_ptr<Stream> stream; // +0x00 / +0x04
        bool enabled = false;           // +0x08: drawn and advanced
        bool owned = true;              // +0x10: 0 for shared overlays
        int x = 0, y = 0;               // +0x14 / +0x18: screen position (1x)
        int width() const { return static_cast<int>(stream->smk.width()); }
        int height() const { return static_cast<int>(stream->smk.height()); }
    };

    // 0x41ab50 (fps, double): fps is the SmackFrameRate forced on every stream
    // and the starting/maximum rate; double shows the movie at 2x.
    SmackOverlayMovie(engine::Engine& engine, int fps, bool doubleSize);
    ~SmackOverlayMovie() override; // 0x41ac60

    // HCSmackerOverlayElement::Open (0x41adb0): `path` as the trail builds it
    // ("smk\AppleW.smk", relative to the Resources folder).
    bool open(std::string_view path, bool preload = false);
    Overlay* addOverlay(std::string_view path);      // 0x41ae20 (vt+0x04)
    Overlay* addSharedOverlay(const Overlay* other); // 0x41af10 (vt+0x10)
    void rewind();                                   // 0x41afb0 (vt+0x08)
    void advance();                                  // 0x41afe0 (vt+0x00)
    void spendTime() override;                       // 0x41acb0 (vt+0x0C)
    void applyPalette();                             // 0x41b5e0

    // HCSmackerElement methods (on the +0x14 sub-object).
    void gotoFrame(std::uint32_t frame); // 0x41a610: SmackGoto (1-based) when frame < Frames, done = 0
    std::uint32_t slowDown();            // 0x41a990: fps - 1, at least 2
    std::uint32_t speedUp();             // 0x41a9c0: fps + 1, at most the constructor's fps

    // The shared world filled with index 0 (world canvas vt+0x24 with a zero
    // colour, as TrailEnded 0x4377f0 and HorseRestart 0x432c90 do).
    static void clearWorld();

    bool opened() const { return m_main != nullptr; }
    std::uint32_t frameNum() const { return m_main ? m_main->smk.currentFrame() : 0; } // Smack +0x374
    std::uint32_t frames() const { return m_main ? m_main->smk.frameCount() : 0; }      // Smack +0x0C
    const std::string& path() const { return m_path; }

    const engine::Image8* image() const override;

    // --- fields the trail code sets (offsets in the 0x130-byte object) ------------
    std::uint8_t transparentIndex = 0;                  // +0x0C
    std::array<std::uint8_t, 3> keyRgb{255, 255, 255};  // +0x0D..+0x0F
    std::uint8_t paletteLow = 0, paletteHigh = 0xFF;    // +0x10 / +0x11
    std::int16_t scroll = 0;                            // +0x12: horizontal shift of the overlays
    bool paused = true;                                 // +0xFE
    bool loop = true;                                   // +0x100
    bool done = false;                                  // +0x102
    std::uint32_t fps;                                  // +0x104
    std::uint32_t maxFps;                               // +0x108
    bool accelerate = false;                            // +0x10C
    bool decelerate = false;                            // +0x10E
    bool usePalette = true;                             // +0x110
    std::int16_t srcX = 0, srcY = 0;                    // +0x11C / +0x11E: source origin of the 2x blit
    std::uint16_t clipW = 640, clipH = 480;             // +0x120 / +0x122
    bool centre = false;                                // +0x124: centre the clip window (letterbox)
    bool applyPaletteOnce = false;                      // +0x12A: apply +0x12C after the next frame
    std::optional<std::array<std::uint32_t, 256>> palette; // +0x12C (0x00RRGGBB)

private:
    std::shared_ptr<Stream> openStream(std::string_view path) const;
    bool smackWait(const Stream& s) const; // SmackWait: nonzero while it is too early
    void frameShown(Stream& s) const;
    static void seek(Stream& s, std::uint32_t frame);

    engine::Engine& m_engine;
    std::string m_path;                  // +0xF8 (+0xE4 of the element)
    std::shared_ptr<Stream> m_main;      // +0xF0: Smack*; +0x04: decode buffer
    std::list<Overlay> m_overlays;       // +0x08
    bool m_doubleSize;                   // +0x128
    std::shared_ptr<engine::Image8> m_world; // [0x809f50], reference counted by [0x809f58]
};

} // namespace brc::game
