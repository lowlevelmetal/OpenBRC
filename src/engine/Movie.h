#pragma once

#include "audio/Mixer.h"
#include "engine/Element.h"
#include "engine/Task.h"
#include "formats/Smacker.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace brc::engine {

class Engine;
class Node;

// What HCMovie and the Godiva Smacker players share: a Smacker stream decoded
// into an 8-bit picture that is an element of a node's world, with the
// stream's audio played through a mixer stream. See docs/engine/movies.md.
class MovieBase : public Element, public Task {
public:
    ~MovieBase() override;

    bool opened() const { return m_smk != nullptr; }
    bool paused() const { return m_paused; }
    void setPaused(bool p);
    bool done() const { return m_done; }
    bool loop() const { return m_loop; }
    void setLoop(bool l) { m_loop = l; }
    std::uint32_t frame() const { return m_smk ? m_smk->currentFrame() : 0; }
    std::uint32_t frameCount() const { return m_smk ? m_smk->frameCount() : 0; }
    int width() const { return m_world.width; }
    int height() const { return m_world.height; }
    const std::string& name() const { return m_name; }

    const Image8* image() const override { return m_smk ? &m_world : nullptr; }

protected:
    explicit MovieBase(Engine& engine);
    // Opens "Resources/<path>" (case-insensitive, '\' or '/'), decodes into a
    // world of the movie's size. `colorKey`: index of magenta in the screen
    // palette becomes transparent (RGB(255,0,255) key).
    bool openFile(std::string_view path, bool colorKey, std::string_view what);
    // Decodes the current frame (and queues its audio); returns false at a decode error.
    bool decodeCurrent();
    // Installs the stream's palette as the screen palette when the movie is
    // full-screen (the node palette already matches the smaller movies).
    void applyPalette(bool fullScreenOnly);
    // Time-based pacing (SmackWait): true when the next frame is due.
    bool frameDue() const;
    void scheduleNext();
    void restartTiming();

    Engine& m_engine;
    std::string m_name;
    std::unique_ptr<formats::Smacker> m_smk;
    Image8 m_world;
    bool m_paused = true;
    bool m_loop = false;
    bool m_done = false;
    double m_frameMs = 100;        // the file's rate, or a forced one
    std::uint32_t m_nextDue = 0;
    std::uint32_t m_pausedAt = 0;
    audio::VoiceId m_voice = audio::VoiceId::None;
};

// HCMovie (HCMovie.cpp, 0x453240): paced by the file's frame rate (SmackWait),
// tells the current node MovieDone(basename) after the last frame, which stays
// on screen.
class HcMovie final : public MovieBase {
public:
    HcMovie(Engine& engine, Node& owner);

    // Open (0x4534e0). `name` relative to the Resources folder, e.g. "smk\Intro.smk".
    // Shows the first frame immediately.
    bool open(std::string_view name, bool preload = false, bool doubleSize = false, bool directToScreen = false,
              bool noSkip = true, int extraBuffer = 0, bool colorKey = true);
    void advance();           // 0x453a20
    void spendTime() override; // 0x453490
};

// The Godiva Smacker player (0x41a300): a forced frame rate (SmackFrameRate),
// looping by default, all instances pacing from one shared timestamp.
class SmackMovie : public MovieBase {
public:
    SmackMovie(Engine& engine, int fps, bool usePalette);

    // Open (0x41a700): `path` as the callers build it (CD path + "smk\…").
    bool open(std::string_view path, bool preload = false);
    void gotoFrame(std::uint32_t frame1Based); // 0x41a610: SmackGoto, done = 0
    void advance();                             // 0x41a7e0
    void spendTime() override;                  // 0x41a640
    int fps() const { return m_fps; }
    void setAccelerate(bool a) { m_accelerate = a; }
    void setDecelerate(bool d) { m_decelerate = d; }
    void stepDown();                            // 0x41a990
    void stepUp();                              // 0x41a9c0

private:
    int m_fps;
    int m_maxFps;
    bool m_usePalette;
    bool m_accelerate = false;
    bool m_decelerate = false;
};

} // namespace brc::engine
