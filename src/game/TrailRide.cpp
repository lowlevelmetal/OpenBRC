// TrailRide.cpp (0x434cb0–0x43c0bf): the ride itself. The ride loop and its
// input, the trail graph (forks, ends, map jumps), obstacles and jumps, the
// trail movies with their overlays, gait and water sounds, butterflies, and
// the horse/Barbie state machine the scripts drive at stops.
// See docs/engine/nodes.md "TrailNode" and movies.md §4.
#include "game/TrailNode.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "engine/Scripts.h"
#include "engine/SoundManager.h"
#include "game/MiniGameFlags.h"
#include "game/Popups.h"
#include "game/TrailFlags.h"
#include "game/TrailTables.h"
#include "game/Widgets.h"

#include <cmath>
#include <cstdlib>
#include <format>

namespace brc::game {
namespace {

// The statics of the ride loop and its helpers (0x80a18c..0x80a1d8,
// 0x8030a4/0x8030a8): process-wide, like the originals.
struct RideStatics {
    std::uint32_t now = 0;           // [0x80a194]
    std::uint32_t inputTick = 0;     // [0x80a1ac]: steering/speed input
    std::uint32_t jumpTick = 0;      // [0x80a1a4]
    int faceSeen = 0;                // [0x80a190]
    int faceHeld = 0;                // [0x80a1c0]
    std::int16_t lastMouseY = 0;     // [0x80a1b0]
    std::int16_t anchorX = 0;        // [0x80a1bc]
    std::int16_t lastMouseX = 0;     // [0x80a1c8]
    unsigned initBits = 0;           // [0x80a19c]
    bool clampFirst = true;          // [0x8030a8]
    int lastEventFrame = -1;         // [0x8030a4]
    std::uint32_t leftUp = 0;        // [0x80a1cc]
    std::uint32_t rightUp = 0;       // [0x80a1d0]
    std::uint32_t obstacleNow = 0;   // [0x80a1b4]
    std::uint32_t lessonHint = 0;    // [0x80a18c]
    bool lessonHintInit = false;     // [0x80a1a8]
    int checkFrame = 0;              // [0x80a1a0]
    bool checkFrameInit = false;     // [0x80a1b8]
    int slowCount = 0;               // [0x80a1d4]
    int faceLogged = 0;              // [0x80a1d8]
};
RideStatics s_ride;

std::size_t horseIndex(const PlayerRecord& r) { return std::min<std::size_t>(r.horse(), 3); }

void sfx(engine::Engine& e, std::string_view name) {
    e.sounds().playSound(name, -1, false, false, 1, false, 0x70, -1, 0, false);
}

} // namespace

// --- helpers --------------------------------------------------------------------------------------

SmackOverlayMovie* TrailNode::currentBackup() const {
    const int i = m_trail * 3 + m_obstacle;
    return i >= 0 && i < 18 ? m_backups[static_cast<std::size_t>(i)].get() : nullptr;
}

void TrailNode::applyPan(SmackOverlayMovie* m) const {
    // The horizontal pan: the 2x blit's source x and the overlays' shift
    // (centre 0x28 = no shift). Not with the video letterbox.
    if (!m || trailflags::videoLetterbox)
        return;
    m->srcX = m_panX;
    m->scroll = static_cast<std::int16_t>((m_panX - 0x28) * 3);
}

void TrailNode::deleteMovie(std::unique_ptr<SmackOverlayMovie>& m) {
    if (m_current == m.get())
        m_current = nullptr;
    m.reset();
}

void TrailNode::runMissionRideTick() {
    switch (mission()) {
    case 0: m1RideTick(); break;
    case 1: m2RideTick(); break;
    case 2: m3RideTick(); break;
    case 3: m4RideTick(); break;
    case 4: m5RideTick(); break;
    case 5: m6RideTick(); break;
    case 6: m7RideTick(); break;
    case 7: m8RideTick(); break;
    case 8: c1RideTick(); break;
    case 9: c2RideTick(); break;
    case 10: c3RideTick(); break;
    case 12: r2RideTick(); break;
    case 13: r1RideTick(); break;
    case 14: r3RideTick(); break;
    default: break;
    }
}

// --- setup ----------------------------------------------------------------------------------------

void TrailNode::initTrail() {
    // InitTrail (0x434cb0).
    for (auto& p : m_trailPalettes)
        p.reset();
    const auto horse = horseIndex(app().record());
    m_horsePalette = loadPalette(trail::kHorsePalette[horse]);
    auto pal = std::make_unique<std::array<std::uint32_t, 256>>(loadPalette(trail::kTrailPalette[static_cast<std::size_t>(m_trail)]));
    copyUpperPaletteEntries(*pal);
    m_trailPalettes[static_cast<std::size_t>(m_trail)] = std::move(pal);
    m_panX = 0x28;
    m_panY = 0x28;
    m_face = 1;
    for (auto& b : m_backups)
        b.reset();
    loadTrail();
    loadBackupMovies();
    if (m_trail == 2) {
        if (app().record().u32(PlayerRecord::kMissionDone + 6 * 4) != 0) // app+0x270
            m_skip57 = true;
        else
            m_base->gotoFrame(0x3A);
    } else if (m_trail == 4 && app().session().mapRabbit2) {
        app().session().mapRabbit2 = 0;
        m_base->gotoFrame(0x1CC);
    }
    // +0x430: the palette on screen now, the node's own (with DirectDraw a
    // copy read back from the display; otherwise the shared world's).
    m_outsidePalette = engine().screenPalette();
    // The ride starts slowly (8 fps) and speeds up; in debug mode at full rate.
    m_base->fps = app().session().debugMode ? static_cast<std::uint32_t>(trailflags::frameRate) : 8u;
    engine().warpMouse(320, 240);
    if (m_trail == 0)
        m_skip54 = true;
}

void TrailNode::loadTrail() {
    // LoadTrail (0x437e50): the trail movie with its overlays, the back loops
    // when they are not all preloaded, the ambient loop and the music.
    const std::uint32_t t0 = engine().ticks();
    m_enteredAtLoop = m_skip57 = m_skip54 = m_skip100 = false;
    auto& snd = engine().sounds();
    if (m_falls)
        snd.retain("falls", false);
    if (m_stream)
        snd.retain("stream", false);
    m_stream = m_falls = nullptr;
    const auto trailIdx = static_cast<std::size_t>(m_trail);
    const std::string path = smkPath(trail::kTrailMovie[trailIdx * 2 + (trailflags::smallMovies ? 1 : 0)]);
    if (mission() == 0xB)
        c4LoadTrail(0);
    if (!m_trailPalettes[trailIdx]) {
        auto pal = std::make_unique<std::array<std::uint32_t, 256>>(loadPalette(trail::kTrailPalette[trailIdx]));
        copyUpperPaletteEntries(*pal);
        m_trailPalettes[trailIdx] = std::move(pal);
    }
    m_base = std::make_unique<SmackOverlayMovie>(engine(), trailflags::frameRate, true);
    m_base->transparentIndex = 0xFE;
    m_base->usePalette = false;
    m_base->palette = *m_trailPalettes[trailIdx];
    m_base->applyPaletteOnce = true;
    if (!trailflags::videoLetterbox) {
        applyPan(m_base.get());
        m_base->srcY = m_panY;
    }
    m_base->loop = false;
    int headBottom = 480;
    if (trailflags::programmaticLetterbox) {
        m_base->clipW = 600;
        m_base->clipH = 300;
        m_base->centre = true;
        headBottom = 390;
        m_deerDone = true;
        m_butterflies = false;
        trailflags::smallMovies = true; // (after the name was chosen, as in the original)
    }
    if (!m_base->open(path))
        log::warn("*** ERROR *** TrailNode::LoadTrail: {}",
                  trail::kTrailMovie[trailIdx * 2 + (trailflags::smallMovies ? 1 : 0)]);
    m_base->link(this);
    m_base->setPosition(0, 0, 100.0f);
    if (m_trail == 0 && !m_deerDone && !app().session().lowEnd) {
        m_deer = m_base->addOverlay(smkPath("deer.smk"));
        if (m_deer)
            m_deer->y = 0x5A;
    } else {
        m_deer = nullptr;
    }
    m_butterfly = {};
    m_butterflyActive = false;
    if (m_butterflies)
        loadButterflies();
    m_headJump = m_headRight = m_headLeft = m_headStraight = nullptr;
    if (trailflags::horseHeads) {
        const std::size_t h = horseIndex(app().record()) * 2 + (trailflags::programmaticLetterbox ? 1 : 0);
        auto head = [&](const trail::Names8& table, int x) {
            Overlay* o = m_base->addOverlay(headPath(table[h]));
            if (!o) {
                log::warn("*** ERROR *** TrailNode::LoadTrail: {}", table[h]);
                return o;
            }
            o->x = x;
            o->y = headBottom - o->height();
            return o;
        };
        m_headStraight = head(trail::kHeadStraight, 199);
        m_headLeft = head(trail::kHeadLeft, 0x9F);
        m_headRight = head(trail::kHeadRight, 0xD6);
        m_headJump = head(trail::kHeadJump, 0xB9);
    }
    m_insert.reset();
    if (!trailflags::backups) {
        unloadBackupMovies();
        if (m_trail == 4 || m_trail == 5 || m_trail == 0 || m_trail == 3)
            loadBackupMovies();
    }
    m_obstacle = 0;
    m_base->paused = false;
    m_current = m_base.get();
    setHorseFace(m_face, true);
    m_faceCountdown = 0x19;
    m_rideHintPlayed = false;
    m_inObstacle = false;

    if (trailflags::ambient) {
        const int old = m_ambientIndex;
        m_ambientIndex = mission() == 4 ? 0 : trail::kAmbientIndex[trailIdx];
        if (old != m_ambientIndex) {
            if (!m_ambientLoop.empty())
                snd.stopSound(m_ambientLoop);
            m_ambientLoop = trail::kAmbientLoop[static_cast<std::size_t>(m_ambientIndex)];
            snd.playSound(m_ambientLoop, -1, false, false, 0, true, 0x28, -1, 0, false);
        }
    }
    if (!trailflags::music) {
        stopMusic();
    } else {
        const auto& s = app().session();
        const int old = m_musicId;
        if (mission() == 0x11 || (mission() == 0x10 && s.tutorialActive))
            m_musicId = 1;
        else if (!flags::appleSortWon)
            m_musicId = trail::kMusicId[trailIdx];
        else
            m_musicId = 4;
        if (old != m_musicId) {
            static constexpr std::array<std::string_view, 5> kMusic = {"", "nonmissionMusic", "trailMusic",
                                                                       "hiddenMusic", "MysteryMusic"};
            if (m_musicId >= 1 && m_musicId <= 4) {
                if (!s.musicOff) {
                    if (!m_music.empty())
                        stopMusic();
                    m_music = kMusic[static_cast<std::size_t>(m_musicId)];
                    app().playMusic(m_music);
                }
            } else {
                stopMusic();
            }
        }
    }
    log::debug("TrailNode::LoadTrail: {} ms", engine().ticks() - t0);
}

void TrailNode::loadBackupMovies() {
    // LoadBackupMovies (0x438930): the three refusal loops ("<prefix> back0..2")
    // of the current trail, or of every trail that has obstacles (0, 3, 4, 5)
    // when [0x8027a4] is set, each with its reverse horse head.
    const std::size_t h = horseIndex(app().record()) * 2 + (trailflags::programmaticLetterbox ? 1 : 0);
    auto make = [&](int t, int i) {
        const auto idx = static_cast<std::size_t>(t * 3 + i);
        auto m = std::make_unique<SmackOverlayMovie>(engine(), trailflags::frameRate, true);
        m->transparentIndex = m_base ? m_base->transparentIndex : 0xFE;
        m->loop = false;
        const std::string path = smkPath(std::format("{} back{}{}.smk", trail::kPrefix[static_cast<std::size_t>(t)], i,
                                                     trailflags::smallMovies ? "_L" : ""));
        int headBottom = 480;
        if (trailflags::programmaticLetterbox) {
            m->clipW = 600;
            m->clipH = 300;
            m->centre = true;
            headBottom = 390;
        }
        if (!m->open(path))
            log::warn("*** ERROR *** TrailNode::LoadBackupMovies: {}", path);
        m->usePalette = false;
        if (trailflags::horseHeads) {
            Overlay*& head = m_reverseHeads[idx];
            if (i == 0) {
                head = m->addOverlay(headPath(trail::kHeadReverse[h]));
                if (head) {
                    head->x = 0xC4;
                    head->y = headBottom - head->height();
                }
            } else {
                head = m->addSharedOverlay(m_reverseHeads[static_cast<std::size_t>(t * 3)]);
            }
        }
        m->setPosition(0, 0, 100.0f);
        m_backups[idx] = std::move(m);
    };
    if (!trailflags::backups) {
        if (m_trail != 1 && m_trail != 2)
            for (int i = 0; i < 3; ++i)
                make(m_trail, i);
        return;
    }
    // Every prefix up to "trE" (the loop runs over the table's first six).
    for (int t = 0; t < 6; ++t)
        if (t != 1 && t != 2)
            for (int i = 0; i < 3; ++i)
                make(t, i);
}

void TrailNode::unloadBackupMovies() {
    // UnloadBackupMovies (0x4390b0).
    auto drop = [&](std::size_t idx) {
        if (m_current == m_backups[idx].get())
            m_current = nullptr;
        m_backups[idx].reset();
        m_reverseHeads[idx] = nullptr;
    };
    if (!trailflags::backups) {
        for (int i = 2; i >= 0; --i)
            drop(static_cast<std::size_t>(m_trail * 3 + i));
        return;
    }
    for (int t = 0; t < 6; ++t)
        if (t != 1 && t != 2)
            for (int i = 2; i >= 0; --i)
                drop(static_cast<std::size_t>(t * 3 + i));
}

void TrailNode::playInsertMovie(std::string_view name) {
    // PlayInsertMovie (0x439160): "<prefix> <name>" over the trail, with the
    // trail movie's heads shared, made the current movie.
    const auto trailIdx = static_cast<std::size_t>(m_trail);
    m_insert = std::make_unique<SmackOverlayMovie>(engine(), trailflags::frameRate, true);
    m_insert->transparentIndex = m_base ? m_base->transparentIndex : 0xFE;
    m_insert->loop = false;
    const std::string path =
        smkPath(std::format("{} {}{}.smk", trail::kPrefix[trailIdx], name, trailflags::smallMovies ? "_L" : ""));
    if (trailflags::programmaticLetterbox) {
        m_insert->clipW = 600;
        m_insert->clipH = 300;
        m_insert->centre = true;
    }
    if (!m_insert->open(path))
        log::warn("TrailNode::PlayInsertMovie: can't open {}", path);
    m_insert->usePalette = false;
    if (m_trailPalettes[trailIdx])
        m_insert->palette = *m_trailPalettes[trailIdx];
    m_insert->applyPaletteOnce = true;
    m_insertHeadLeft = m_insert->addSharedOverlay(m_headLeft);
    m_insertHeadRight = m_insert->addSharedOverlay(m_headRight);
    m_insertHeadStraight = m_insert->addSharedOverlay(m_headStraight);
    m_insertHeadJump = m_insert->addSharedOverlay(m_headJump);
    if (!trailflags::videoLetterbox) {
        applyPan(m_insert.get());
        m_insert->srcY = m_panY;
    }
    setHorseFace(1, true);
    m_insert->link(this);
    m_insert->setPosition(0, 0, 100.0f);
    m_current = m_insert.get();
}

// --- the ride loop --------------------------------------------------------------------------------

void TrailNode::tick() {
    // SpendTime (0x435980).
    ToolbarNode::tick();
    auto& eng = engine();
    auto& s = app().session();
    auto& rec = app().record();
    if (eng.modal()) {
        m_pauseCopy = true;
        return;
    }
    if (m_pauseCopy) {
        // Back from a pause: the hooves and the ambient loop again.
        m_pauseCopy = false;
        if (m_riding) {
            m_gaitPlaying = false;
            playGaitSound();
        }
        if (trailflags::ambient) {
            m_ambientIndex = mission() == 4 ? 0 : trail::kAmbientIndex[static_cast<std::size_t>(m_trail)];
            if (!m_ambientLoop.empty()) {
                eng.sounds().stopSound(m_ambientLoop);
                m_ambientLoop.clear();
            }
            m_ambientLoop = trail::kAmbientLoop[static_cast<std::size_t>(m_ambientIndex)];
            eng.sounds().playSound(m_ambientLoop, -1, false, false, 0, true, 0x28, -1, 0, false);
        }
    }
    if (s.arriveByMap) {
        s.arriveFromMiniGame = 0;
        s.arriveFromRace = 0;
        s.arriveByMap = 0;
        playGaitSound();
    }
    if (s.arriveFromMiniGame || s.arriveFromRace) {
        horseStop();
        return;
    }
    if (!m_base || !m_current)
        return;

    int curFrame = static_cast<int>(m_current->frameNum());
    int baseFrame = static_cast<int>(m_base->frameNum());
    const auto tf = static_cast<std::uint32_t>(trailFrame());
    s_ride.now = eng.ticks();
    if (!(s_ride.initBits & 1)) {
        s_ride.initBits |= 1;
        s_ride.inputTick = s_ride.now;
    }

    if (!m_riding) {
        // At a stop: the five-minute nag, mission 6's tick and its popup.
        if (s_ride.now - m_lastInput > 300000 && !flags::appleSortWon) {
            playVoice(eng.rand() % 2 == 0 ? "db0516" : "db0598");
            m_lastInput = s_ride.now;
        }
        if (mission() == 5)
            m6RideTick();
        if (!m_modal || !m_modal->finished())
            return;
        m_modal.reset();
        playVoiceNoFlaps("db04060");
        if (!hasBagItem(0x18) && mission() == 5)
            playVoiceAfterPause("db04064");
        return;
    }

    if (m_trail == 0 || m_trail == 3 || m_trail == 5)
        updateWaterSounds();
    // Segment skips.
    if (m_trail == 0) {
        if (tf == 0x36 && m_skip54) {
            m_base->gotoFrame(0xA6);
            m_skip54 = false;
            return;
        }
        if (tf == 100 && m_skip100) {
            m_base->gotoFrame(0xAC);
            m_skip100 = false;
            return;
        }
    } else if (m_trail == 2 && tf == 0x39 && m_skip57) {
        m_base->gotoFrame(0x73);
        m_skip57 = false;
        return;
    }
    // The deer on the apple trail.
    if (m_deer) {
        if (m_deerVisible && tf >= 0xE7) {
            m_deer->enabled = false;
            m_deer->stream->smk.gotoFrame(0);
            m_deerVisible = false;
            m_deerDone = true;
        } else if (!m_deerDone && !m_deerVisible && tf > 0xA6 && tf < 0xB4) {
            log::debug("DEER! {}", tf);
            m_deer->enabled = true;
            m_deerVisible = true;
        }
    }
    // The wishing well's south trail jumps by itself; the cottage loop stops.
    if (m_trail == 7 && tf > 0x8C && tf < 0x96 && !m_jumping)
        horseJump();
    if (m_trail == 1 && tf > 0x50 && tf < 0x59 && !m_jumping) {
        m_base->gotoFrame(0x5F);
        horseStop();
        return;
    }
    // Forks: a head turned the same way for more than five passes.
    if (!(s_ride.initBits & 2)) {
        s_ride.initBits |= 2;
        s_ride.faceSeen = m_face;
    }
    if (m_face == s_ride.faceSeen) {
        ++s_ride.faceHeld;
    } else {
        s_ride.faceHeld = 0;
        s_ride.faceSeen = m_face;
    }
    if (s_ride.faceHeld > 5) {
        if (m_trail == 0) {
            if (tf > 0x2E9 && tf < 0x2EE && m_face == 2) {
                m_base->gotoFrame(0x319);
                m_enteredAtLoop = true;
                return;
            }
        } else if (m_trail == 4) {
            bool fork = true;
            if (tf >= 0x322 && tf <= 0x33E && m_face == 2) {
                m_nextTrail = 2;
                if (rec.u32(PlayerRecord::kMissionDone + 6 * 4) == 0) { // app+0x270
                    m_startFrame = 0x3A;
                } else {
                    m_skip57 = true;
                    m_startFrame = 0;
                }
            } else if (rec.u16(PlayerRecord::kCottageGateOpen) != 0 && baseFrame >= 0x376 && baseFrame <= 0x38C &&
                       m_face == 0) {
                m_nextTrail = 1;
                m_startFrame = 0;
            } else if (rec.u16(PlayerRecord::kWellPathOpen) != 0 && tf >= 0x15F && tf <= 0x17B && m_face == 0) {
                m_nextTrail = 6;
                m_startFrame = 0;
            } else {
                fork = false;
                if (baseFrame < 0x159 || baseFrame < 0x370) // (as coded)
                    s_ride.faceHeld = 0;
            }
            if (fork) {
                switchTrail();
                baseFrame = 0;
                curFrame = 0;
            }
        }
    }
    // The end of the trail.
    if (m_base->done || (m_trail == 0 && tf == 0x315) || (m_trail == 4 && tf == 0x445 && !m_enteredAtLoop)) {
        trailEnded();
        return;
    }
    // Event inserts of the missions.
    if (!m_insert) {
        auto insert = [&](std::string_view name, int resume) {
            playInsertMovie(name);
            m_insert->paused = false;
            m_base->gotoFrame(static_cast<std::uint32_t>(resume));
            m_base->paused = true;
        };
        switch (mission()) {
        case 0:
            if (s.missionTrail == 5 && m_trail == 5 && baseFrame > 0x1C7 && baseFrame < 0x1D1)
                insert("Sparkle", 0x1DE);
            break;
        case 2:
            if (m_trail == 0 && baseFrame > 0x293 && baseFrame < 0x2B3 && !m_hoofPrintsShown[1])
                insert("HoofPrints", 0x2BE);
            else if (m_trail == 4 && baseFrame > 0x274 && baseFrame < 0x292 && !m_hoofPrintsShown[0])
                insert("HoofPrints", 0x29D);
            else if (m_trail == 5 && baseFrame > 0x1CC && baseFrame < 0x1E3 && !m_hoofPrintsShown[2])
                insert("HoofPrints", 0x1E4);
            break;
        case 3:
            if (m_trail == 4 && baseFrame > 0x34A && baseFrame < 0x3A0)
                insert("Cottage", 0x3A1);
            break;
        case 0xC:
            if (m_trail == 3 && baseFrame > 0x32 && baseFrame < 0x6E)
                insert("HorseShoe", 0x79);
            break;
        case 0xD:
            if (m_trail == 0 && baseFrame > 0xEE && baseFrame < 0x104)
                insert("FallenBird", 0x10F);
            break;
        case 0xE:
            if (m_trail == 4 && baseFrame > 0x210 && baseFrame < 0x22E)
                insert("Branches", 0x239);
            break;
        default:
            break;
        }
    }
    // Obstacles.
    if (!m_insert) {
        if (!m_inObstacle) {
            const int f = static_cast<int>(m_base->frameNum());
            for (int i = 0; i < 3; ++i) {
                const auto idx = static_cast<std::size_t>(m_trail * 3 + i);
                if (trail::kObstacleStart[idx] <= f && f < trail::kObstacleEnd[idx]) {
                    m_obstacle = i;
                    m_inObstacle = true;
                    break;
                }
            }
        }
    } else if (m_insert->done) {
        m_base->paused = false;
        m_current = m_base.get();
        m_insert.reset();
    }
    if (m_inObstacle)
        updateObstacle();

    // A jump: the head stays straight, the view bobs every 100 ms.
    const std::uint32_t jumpNow = eng.ticks();
    if (!(s_ride.initBits & 4)) {
        s_ride.initBits |= 4;
        s_ride.jumpTick = jumpNow;
    }
    if (m_jumping) {
        setHorseFace(1, false);
        applyPan(m_current);
        stopGaitSound();
        if (jumpNow - s_ride.jumpTick < 100)
            return;
        s_ride.jumpTick = jumpNow;
        switch (m_jumpPhase) {
        case 0:
            m_panY = static_cast<std::int16_t>(m_panY + 8);
            m_jumpPhase = 1;
            break;
        case 1:
            if (m_panY < 9)
                m_jumpPhase = 2;
            else
                m_panY = static_cast<std::int16_t>(m_panY - 8);
            break;
        case 2:
            if (m_panY < 0x30)
                m_panY = static_cast<std::int16_t>(m_panY + 8);
            else
                m_jumpPhase = 3;
            break;
        case 3:
            m_panY = 0x28;
            m_jumping = false;
            playGaitSound();
            break;
        default:
            break;
        }
        if (!trailflags::videoLetterbox)
            m_current->srcY = m_panY;
        return;
    }

    // Speed and steering by the control mode (app+0x1A8).
    std::uint32_t fps = m_base->fps;
    const std::uint32_t mode = app().controlMode;
    if (mode == 1) {
        // Keyboard: arrows steer and change speed. The original's throttle
        // compares last - now (unsigned) with 100, so it only holds within
        // the same millisecond: the keys act on every pass.
        if (m_keyLeft || m_keyRight || m_keyFaster || m_keySlower) {
            if (s_ride.inputTick - s_ride.now <= 100)
                return;
            s_ride.inputTick = s_ride.now;
            if (m_keyLeft) {
                if (m_panX > 0) {
                    --m_panX;
                    applyPan(m_current);
                }
            } else if (m_keyRight && m_panX < 0x50) {
                ++m_panX;
                applyPan(m_current);
            }
            if (m_keyFaster)
                fps = m_current->speedUp();
            else if (m_keySlower)
                fps = m_current->slowDown();
        }
    } else {
        // Mouse: vertical motion (mode 2) or the held buttons (mode 0) set the
        // speed, and the pointer goes back to the middle line.
        auto [mx, my] = eng.mousePosition();
        auto x = static_cast<std::int16_t>(mx), y = static_cast<std::int16_t>(my);
        if (s_ride.clampFirst) {
            if (y - 100 < 10)
                y = 0x6E;
            else if (y + 100 > 0x1D6)
                y = 0x212;
            if (x - 300 < 10)
                x = 0x136;
            else if (x + 300 > 0x1D6)
                x = 0x14A;
            s_ride.clampFirst = false;
        }
        if (!(s_ride.initBits & 8)) {
            s_ride.initBits |= 8;
            s_ride.lastMouseY = y;
        }
        if ((mode == 2 && std::abs(y - s_ride.lastMouseY) > 100) ||
            (mode == 0 && (!m_leftReleased || !m_rightReleased))) {
            if (mode == 2) {
                fps = y < s_ride.lastMouseY ? m_current->speedUp() : m_current->slowDown();
            } else if (mode == 0) {
                if (s_ride.now - s_ride.inputTick <= 200)
                    return;
                s_ride.inputTick = s_ride.now;
                fps = m_leftReleased ? m_current->slowDown() : m_current->speedUp();
            }
            eng.warpMouse(x, 240);
            s_ride.lastMouseY = 240;
        }
    }
    setGait(fps < 4 ? 0 : fps < 7 ? 1 : fps < 10 ? 2 : fps < 15 ? 3 : 4);

    bool faceDone = false;
    int face = m_face;
    if (app().controlMode == 1) {
        face = m_keyLeft ? 0 : m_keyRight ? 2 : 1;
    } else {
        // Mouse: small horizontal moves nudge the pan; a 300-pixel swing from
        // the anchor turns the head and re-centres the pointer.
        const auto [mx, my] = eng.mousePosition();
        const auto x = static_cast<std::int16_t>(mx);
        if (!(s_ride.initBits & 0x10)) {
            s_ride.initBits |= 0x10;
            s_ride.anchorX = x;
        }
        if (!(s_ride.initBits & 0x20)) {
            s_ride.initBits |= 0x20;
            s_ride.lastMouseX = x;
        }
        if (std::abs(x - s_ride.lastMouseX) > 2) {
            s_ride.lastMouseX = x;
            if (x < s_ride.anchorX) {
                if (m_panX > 0) {
                    --m_panX;
                    applyPan(m_current);
                }
            } else if (m_panX < 0x50) {
                ++m_panX;
                applyPan(m_current);
            }
        }
        if (std::abs(x - s_ride.anchorX) > 300) {
            if (x < s_ride.anchorX)
                face = m_face == 2 ? 1 : 0;
            else
                face = m_face == 0 ? 1 : 2;
            setHorseFace(face, false);
            eng.warpMouse(320, my);
            s_ride.anchorX = s_ride.lastMouseX = 320;
            faceDone = true;
        }
    }
    if (!faceDone)
        setHorseFace(face, false);

    // Once per new frame: comments, butterflies, ride hints, the mission.
    if (s_ride.lastEventFrame == curFrame)
        return;
    s_ride.lastEventFrame = curFrame;
    updatePathCommentary();
    if (m_butterflies) {
        if (!m_butterflyActive) {
            if (curFrame % 75 == 0 && eng.rand() % 3 != 0)
                startButterfly();
        } else {
            updateButterfly();
        }
    }
    if (m_trail == 6 && tf > 0x19 && !m_db03066Played && rec.u32(PlayerRecord::kMissionDone + 5 * 4) != 0) {
        playVoice("db03066");
        m_db03066Played = true;
    } else if (trailflags::dialogue && !m_rideHintPlayed && !s.tutorialActive) {
        if (m_trail == 4 && tf > 0x2DF && tf < 0x334) {
            playVoiceNoFlaps("db9051");
            m_rideHintPlayed = true;
        } else if (m_trail == 0 && tf > 0x2A3 && tf < 0x302 && mission() != 2) {
            playVoiceNoFlaps("db9050");
            m_rideHintPlayed = true;
        }
    }
    runMissionRideTick();
}

void TrailNode::updateWaterSounds() {
    // UpdateWaterSounds (0x436c50): the waterfall on the rainbow trail and the
    // stream by the first obstacle of the apple and turtle trails, faded by
    // frame (AIL_quick_set_volume: volume 0..127, pan 0..127).
    const int f = trailFrame();
    auto& snd = engine().sounds();
    if (m_trail == 3) {
        if (!m_falls) {
            m_falls = snd.getSound("falls", true, false);
            snd.playSound("falls", -1, false, false, 0, false, -1, -1, 0, false);
        }
        if (m_riding && m_falls) {
            double v = f * 1.6933;
            int vol = static_cast<int>(v);
            int pan = static_cast<int>(64.0 + v * 0.5);
            if (f > 0x50) {
                v = (f - 0x50) * 1.6933;
                vol = static_cast<int>(127.0 - (v + v));
                pan = static_cast<int>(127.0 - v);
            }
            vol = std::min(vol, 0x7F);
            pan = std::clamp(pan, 0x40, 0x7F);
            if (f < 0xA0) {
                m_falls->setVolumePan(std::max(vol, 0), pan);
                return;
            }
        }
        snd.stopSound("falls");
        return;
    }
    if (!m_stream) {
        m_stream = snd.getSound("stream", true, false);
        snd.playSound("stream", -1, false, false, 0, false, -1, -1, 0, false);
        if (m_stream)
            m_stream->setVolumePan(0, 0x40);
    }
    if (m_riding && m_stream) {
        const int g = f + (0x28 - trail::kObstacleEnd[static_cast<std::size_t>(m_trail * 3 + (m_trail != 0 ? 1 : 0))]);
        if (g < 0)
            return;
        double v = g * 3.3967;
        int vol = static_cast<int>(v);
        if (g > 0x28) {
            v = (g - 0x28) * 3.3967;
            vol = static_cast<int>(127.0 - (v + v));
        }
        vol = std::min(vol, 0x70);
        if (g < 0xA0) {
            m_stream->setVolumePan(std::max(vol, 0), 0x40);
            return;
        }
    }
    snd.stopSound("stream");
}

int TrailNode::trailFrame() const {
    // GetTrailFrame (0x436e80): the position on the trail, also during an
    // insert or a refusal loop (counted back from where the base resumes).
    if (!m_current || !m_base)
        return 0;
    if (m_current == m_base.get())
        return static_cast<int>(m_base->frameNum());
    if (m_current == m_insert.get())
        return static_cast<int>(m_base->frameNum() + m_insert->frameNum()) - static_cast<int>(m_insert->frames());
    const int idx = m_trail * 3 + m_obstacle;
    if (SmackOverlayMovie* b = currentBackup(); b && m_current == b)
        return trail::kObstacleEnd[static_cast<std::size_t>(idx)] + static_cast<int>(b->frameNum()) -
               static_cast<int>(b->frames());
    return 0;
}

void TrailNode::setHorseFace(int face, bool force) {
    // SetHorseFace (0x436f10): show the head turned left, straight or right.
    if (!m_headStraight || !m_headLeft || !m_headRight)
        return;
    if (!s_ride.checkFrameInit) {
        s_ride.checkFrameInit = true;
        s_ride.checkFrame = m_current ? static_cast<int>(m_current->frameNum()) : 0;
    }
    if (face == m_face && !force) {
        // The developers' check of heads that do not fit the picture (slows
        // the horse down); +0x3DC is never set, so it is dormant.
        if (!m_faceCheck)
            return;
        int f = 0;
        if (m_current == m_base.get())
            f = static_cast<int>(m_base->frameNum());
        else if (m_current == m_insert.get())
            f = static_cast<int>(m_insert->frameNum() + m_base->frameNum()) - static_cast<int>(m_insert->frames());
        else
            return;
        if (f == s_ride.checkFrame)
            return;
        s_ride.checkFrame = f;
        if (m_faceCountdown != 0) {
            --m_faceCountdown;
            return;
        }
        const auto& ranges = trail::kInvalidFace[static_cast<std::size_t>(m_trail)];
        for (std::size_t i = 0; i < 4; ++i) {
            auto in = [&](int fc) {
                const auto& r = ranges[static_cast<std::size_t>(fc)][i];
                return r.lo < f && f < r.hi;
            };
            const bool invalid = m_face == 1 ? (in(0) || in(2)) : in(m_face);
            if (!invalid) {
                s_ride.faceLogged = 0;
                continue;
            }
            log::debug("Invalid Face {} at frame {} at rate {}", m_face, f, m_current->fps);
            if (m_current->fps < 5)
                return;
            const auto max = static_cast<std::uint32_t>(trailflags::frameRate);
            if (m_current->fps > max - 4)
                m_current->slowDown();
            if (m_current->fps > max - 8)
                m_current->slowDown();
            if (s_ride.slowCount == 2) {
                s_ride.slowCount = 0;
                m_current->slowDown();
            } else {
                ++s_ride.slowCount;
            }
            if (m_current->fps > 4 || s_ride.faceLogged != 0)
                return;
            s_ride.faceLogged = 5;
            return;
        }
        return;
    }
    auto set = [&](int fc, bool on) {
        Overlay* base = fc == 0 ? m_headLeft : fc == 1 ? m_headStraight : fc == 2 ? m_headRight : nullptr;
        Overlay* ins = fc == 0 ? m_insertHeadLeft : fc == 1 ? m_insertHeadStraight : fc == 2 ? m_insertHeadRight : nullptr;
        if (!base)
            return;
        base->enabled = on;
        if (m_insert && ins)
            ins->enabled = on;
    };
    set(m_face, false);
    m_face = face;
    set(face, true);
}

void TrailNode::horseJump() {
    // HorseJump (0x437280).
    if (m_jumping)
        return;
    sfx(engine(), "feedback");
    m_jumping = true;
    m_jumpPhase = 0;
    playGaitSound();
}

void TrailNode::updateObstacle() {
    // UpdateObstacle (0x4372f0): jump in the last frames before the obstacle,
    // or the horse refuses: the trail movie goes back, "whinney2", and the
    // back loop plays with the reverse head until the player jumps.
    s_ride.obstacleNow = engine().ticks();
    if (!s_ride.lessonHintInit) {
        s_ride.lessonHintInit = true;
        s_ride.lessonHint = s_ride.obstacleNow;
    }
    const auto idx = static_cast<std::size_t>(m_trail * 3 + m_obstacle);
    const int f = static_cast<int>(m_base->frameNum());
    const int remaining = trail::kObstacleEnd[idx] - f;
    if (f >= trail::kObstacleEnd[idx]) {
        if (m_headJump)
            m_headJump->enabled = false;
        setHorseFace(1, true);
        m_jumping = false;
        m_inObstacle = false;
        playGaitSound();
        return;
    }
    SmackOverlayMovie* back = m_backups[idx].get();
    if (remaining < 5 && !m_jumping && !m_base->paused) {
        m_base->gotoFrame(static_cast<std::uint32_t>(trail::kObstacleRefusal[idx]));
        m_base->paused = true;
        sfx(engine(), "whinney2");
        if (!back)
            return;
        back->link(this);
        if (trailflags::horseHeads && m_reverseHeads[idx])
            m_reverseHeads[idx]->enabled = true;
        back->fps = m_base->fps;
        if (!trailflags::videoLetterbox) {
            applyPan(back);
            back->srcY = m_panY;
        }
        back->paused = false;
        m_current = back;
        return;
    }
    if (!back)
        return;
    if (!back->paused) {
        if (!back->done)
            return;
        if (!m_jumping) {
            if (app().session().tutorialActive && s_ride.obstacleNow - s_ride.lessonHint > 30000) {
                lessonIdleHint();
                s_ride.lessonHint = s_ride.obstacleNow;
            }
            stopGaitSound();
            return;
        }
        // Jumped after the refusal: back to the trail movie.
        back->unlink();
        back->paused = true;
        back->gotoFrame(1);
        back->done = false;
        m_base->paused = false;
        m_current = m_base.get();
        return;
    }
    if (trailflags::horseHeads) {
        if (!m_jumping)
            return;
        if (remaining < 5 && m_headJump && !m_headJump->enabled) {
            m_headStraight->enabled = false;
            m_headLeft->enabled = false;
            m_headRight->enabled = false;
            m_headJump->enabled = true;
        }
    }
    if (!m_jumping || remaining >= 0x19)
        return;
    applyPan(m_base.get());
    if (remaining >= 0xD)
        return;
    stopGaitSound();
    switch (remaining) {
    case 0:
        m_jumping = false;
        playGaitSound();
        break;
    case 1:
    case 2:
    case 0xC:
        m_panY = 0x28;
        break;
    case 3:
    case 0xB:
        m_panY = 0x20;
        break;
    case 4:
    case 0xA:
        m_panY = 0x18;
        break;
    case 5:
    case 9:
        m_panY = 0x10;
        break;
    case 6:
    case 8:
        m_panY = 8;
        break;
    case 7:
        if (app().session().tutorialActive)
            playVoice(engine().rand() % 2 == 0 ? "db0458" : "db0457");
        m_panY = 0;
        break;
    default:
        break;
    }
    if (remaining < 1) {
        m_panY = 0x28;
        m_jumping = false;
        playGaitSound();
    }
    if (!trailflags::videoLetterbox)
        m_base->srcY = m_panY;
}

void TrailNode::trailEnded() {
    // TrailEnded (0x4377f0).
    if (m_trail == 0) {
        if (mission() == 0xB)
            c4LoadTrail(1);
        if (!m_base->done) {
            // The apple trail leads back to the yard.
            engine().requestNode("yard", 16, true);
            return;
        }
    } else if (m_trail == 4) {
        if (m_base->done) {
            m_base->gotoFrame(0x53);
            m_enteredAtLoop = false;
            return;
        }
    } else if (m_trail == 6) {
        horseStop();
        return;
    }
    m_nextTrail = trail::kNextTrail[static_cast<std::size_t>(m_trail)];
    m_startFrame = startFrame();
    switchTrail();
    SmackOverlayMovie::clearWorld();
    if (m_current)
        m_current->spendTime();
}

int TrailNode::startFrame() const {
    // StartFrame (0x4378f0): where the next trail starts (SmackGoto frame).
    if (m_restartSameTrail)
        return 0;
    switch (m_trail) {
    case 0:
        if (m_nextTrail == 4)
            return 0x447;
        break;
    case 1:
        if (m_nextTrail == 4)
            return 0x408;
        break;
    case 2:
        if (m_nextTrail == 0)
            return 0x164;
        break;
    case 3:
        if (m_nextTrail == 0)
            return 0x38;
        break;
    case 4:
        if (m_nextTrail == 2)
            return app().record().u32(PlayerRecord::kMissionDone + 6 * 4) != 0 ? 0 : 0x3A;
        break;
    case 7:
        if (m_nextTrail == 4)
            return 0x1AB;
        break;
    default:
        break;
    }
    return 0;
}

void TrailNode::switchTrail() {
    // SwitchTrail (0x437a40): onto the next trail (a new trail movie at the
    // start frame), or the same one again from the start frame.
    if (m_restartSameTrail) {
        m_restartSameTrail = false;
        deleteMovie(m_insert);
        m_base->fps = 8;
        m_current = m_base.get();
        if (m_trail == 0 && m_startFrame == 0x38)
            m_skip100 = true;
        m_base->gotoFrame(static_cast<std::uint32_t>(m_startFrame));
        return;
    }
    stopGaitSound();
    m_trail = m_nextTrail;
    playGaitSound();
    m_mapLocation = trail::kMapLocation[static_cast<std::size_t>(m_trail)];
    if (m_current) {
        m_current->paused = true;
        m_current->gotoFrame(1);
        m_current->done = false;
    }
    deleteMovie(m_insert);
    const std::uint32_t fps = m_base ? m_base->fps : 8;
    // (The SmackSummary of the old movie goes to the log.)
    std::unique_ptr<SmackOverlayMovie> old = std::move(m_base);
    if (m_current == old.get())
        m_current = nullptr;
    loadTrail();
    old.reset();
    m_base->fps = fps;
    m_current->gotoFrame(static_cast<std::uint32_t>(m_startFrame));
    if (m_trail == 0) {
        if (m_startFrame == 0) {
            m_skip54 = true;
            return;
        }
        if (m_startFrame == 0x38)
            m_skip100 = true;
    } else if (m_trail == 2) {
        if (m_startFrame == 0)
            m_skip57 = true;
    } else if (m_trail == 4 && m_startFrame == 0x447) {
        m_enteredAtLoop = true;
    }
}

// --- sounds and butterflies ---------------------------------------------------------------------

void TrailNode::loadGaitSounds() {
    // LoadGaitSounds (0x4394e0): "<surface><2|8|10|12|15>" per trail and gait.
    for (std::size_t t = 0; t < kTrails; ++t) {
        for (std::size_t g = 0; g < 5; ++g)
            m_gaitSounds[t][g] = std::format("{}{}", trail::kSurfaceName[static_cast<std::size_t>(trail::kSurface[t])],
                                             trail::kGaitSuffix[g]);
        for (std::size_t g = 0; g < 5; ++g)
            engine().sounds().getSound(m_gaitSounds[t][g], true, false);
    }
}

void TrailNode::releaseGaitSounds() {
    // 0x4395f0.
    for (const auto& t : m_gaitSounds)
        for (const auto& n : t)
            engine().sounds().retain(n, false);
}

void TrailNode::setGait(int gait) {
    // SetGait (0x439660).
    if (!m_gaitPlaying) {
        m_gait = gait;
    } else if (m_gait != gait) {
        stopGaitSound();
        m_gait = gait;
        playGaitSound();
    }
}

void TrailNode::playGaitSound() {
    // PlayGaitSound (0x4396b0): the hooves, looped.
    if (m_gaitPlaying)
        return;
    const auto& name = m_gaitSounds[static_cast<std::size_t>(m_trail)][static_cast<std::size_t>(m_gait)];
    engine().sounds().playSound(name, -1, false, false, 0, false, 0x70, -1, 0, false);
    m_gaitPlaying = true;
}

void TrailNode::stopGaitSound() {
    // StopGaitSound (0x439710).
    if (!m_gaitPlaying)
        return;
    engine().sounds().stopSound(m_gaitSounds[static_cast<std::size_t>(m_trail)][static_cast<std::size_t>(m_gait)]);
    m_gaitPlaying = false;
}

void TrailNode::loadButterflies() {
    // LoadButterflies (0x439760): one random species of the trail in three
    // sizes, flying left, or right ("REV").
    if (!m_butterflies)
        return;
    m_butterflyActive = false;
    auto& eng = engine();
    int species = eng.rand() % 3;
    const auto base = static_cast<std::size_t>(m_trail * 3);
    while (trail::kButterflySpecies[base + static_cast<std::size_t>(species)].empty()) {
        if (species == 0)
            return;
        --species;
    }
    m_butterflyDx = eng.rand() % 2 == 0 ? -1 : 1;
    for (std::size_t k = 0; k < 3; ++k) {
        const std::string path = smkPath(std::format(
            "butterflies\\{}{}{}{}.smk", trail::kButterflyPrefix[static_cast<std::size_t>(m_trail)],
            trail::kButterflySpecies[base + static_cast<std::size_t>(species)], trail::kButterflySize[k],
            m_butterflyDx == 1 ? "REV" : ""));
        m_butterfly[k] = m_base->addOverlay(path);
        if (!m_butterfly[k])
            break;
    }
    startButterfly();
}

void TrailNode::startButterfly() {
    // StartButterfly (0x439a00): the small one at the edge it flies from.
    if (!m_butterflies || !m_butterfly[0])
        return;
    m_butterflyStage = 0;
    m_butterfly[0]->enabled = true;
    m_butterflyCount = 0;
    m_butterflyActive = true;
    m_butterfly[0]->x = m_butterflyDx == 1 ? 0 : 0x27B;
    m_butterflyDy = 1;
    m_butterfly[0]->y = (engine().rand() % 3) * 100;
}

void TrailNode::updateButterfly() {
    // UpdateButterfly (0x439aa0): it grows (8, 11, 10 frames per size) and
    // flutters 20 pixels a frame.
    if (!m_butterflies)
        return;
    auto at = [&](int stage) { return m_butterfly[static_cast<std::size_t>(stage)]; };
    auto grow = [&](int from) {
        Overlay* a = at(from);
        Overlay* b = at(from + 1);
        if (a)
            a->enabled = false;
        m_butterflyStage = from + 1;
        if (b) {
            b->enabled = true;
            if (a) {
                b->x = a->x;
                b->y = a->y;
            }
        }
        m_butterflyCount = 0;
    };
    if (m_butterflyStage == 0 && m_butterflyCount == 8)
        grow(0);
    else if (m_butterflyStage == 1 && m_butterflyCount == 0xB)
        grow(1);
    if (m_butterflyStage == 2 && m_butterflyCount == 10) {
        if (at(2))
            at(2)->enabled = false;
        m_butterflyActive = false;
        m_butterflyCount = 0;
        return;
    }
    Overlay* b = at(m_butterflyStage);
    if (!b)
        return;
    if (m_butterflyCount % 5 == 0) {
        if (b->y > 0x15E)
            m_butterflyDy = -1;
        if (b->y < 0x32) {
            m_butterflyDy = 1;
        } else if (b->y < 0xFB) {
            m_butterflyDy = engine().rand() % 2 != 0 ? 1 : -1;
        } else {
            m_butterflyDy = engine().rand() % 3 == 0 ? 1 : -1;
        }
    }
    b->x += m_butterflyDx * 0x14;
    b->y += m_butterflyDy * 0x14;
    b->x = std::clamp(b->x, 0, 0x27B);
    b->y = std::clamp(b->y, 0, 0x1DB);
    ++m_butterflyCount;
}

// --- input ----------------------------------------------------------------------------------------

void TrailNode::onKeyDown(const engine::InputEvent& e) {
    // KeyDown (0x434f60).
    const bool keyboard = app().controlMode == 1;
    if (e.key != 0) {
        switch (e.key) {
        case engine::key::kEscape:
            if (m_riding)
                horseStop();
            showExitDialog(true);
            return;
        case engine::key::kLeft:
            if (keyboard)
                m_keyLeft = true;
            return;
        case engine::key::kRight:
            if (keyboard)
                m_keyRight = true;
            return;
        case engine::key::kUp:
            if (!keyboard)
                return;
            if (m_riding) {
                m_keyFaster = true;
                return;
            }
            // At the rabbit trail's bush or gate: ride through it.
            if (m_mapLocation != 4 || !m_mounted)
                return;
            if (m_decision == 1) {
                if (!m_bush || m_bush->sequence() == 0 || isScriptRunning("RunLeft"))
                    return;
            } else if (m_decision == 4) {
                // (As coded: the gate's first sequence, i.e. closed.)
                if (!m_gate || m_gate->sequence() != 0 || isScriptRunning("RunLeft"))
                    return;
            } else {
                return;
            }
            m_nextTrail = 6;
            stopAllScripts();
            runScript("RunLeft");
            return;
        case engine::key::kDown:
            if (keyboard)
                m_keySlower = true;
            return;
        default:
            ToolbarNode::onKeyDown(e);
            return;
        }
    }
    if (e.ch == 0) {
        ToolbarNode::onKeyDown(e);
        return;
    }
    switch (e.ch) {
    case ' ':
        // Keyboard mode: stop, or ride on from a stop.
        if (!keyboard || m_modal)
            return;
        if (m_pickup) {
            deleteAnim(m_pickup);
            addBagItem(m_pickupItem);
        }
        if (!m_riding) {
            if (!m_mounted) {
                mountAndRide();
                return;
            }
            if (m_trail == 6 && m_decision == 0)
                m_nextTrail = 7;
            if (!isScriptRunning("runOff") && !isScriptRunning("runLeft")) {
                stopAllScripts();
                runScript(m_trail == 5 && m_decision == 1 ? "runLeft" : "runOff");
            }
        } else if (m_current != currentBackup()) {
            horseStop();
        }
        return;
    case '$':
        if (!m_riding) {
            addBagItem(0x18); // the developers' coin
            return;
        }
        ToolbarNode::onKeyDown(e);
        return;
    case 'A':
    case 'a':
        if (!m_mounted && m_barbieSlot == 2) {
            stopAllScripts();
            runScript("FeedApple");
        }
        return;
    case 'P':
        // A developers' page from a friend.
        if (app().record().horse() == 0) {
            if (pager()) {
                pager()->setKeepFriend();
                pager()->pageMessage(1, BarbiePager::kTeresaChat);
            }
        } else if (app().record().horse() == 1) {
            if (pager()) {
                pager()->setKeepFriend();
                pager()->pageMessage(0, BarbiePager::kChristieChat);
            }
        }
        return;
    case 'Y':
    case 'y':
        if (app().session().debugMode)
            app().gotoNode("yard");
        return;
    default:
        ToolbarNode::onKeyDown(e);
        return;
    }
}

void TrailNode::onKeyUp(const engine::InputEvent& e) {
    // KeyUp (0x435400).
    const bool keyboard = app().controlMode == 1;
    if (m_riding && e.key != 0 && keyboard) {
        switch (e.key) {
        case engine::key::kEnter:
            horseJump();
            break;
        case engine::key::kLeft:
            m_keyLeft = false;
            break;
        case engine::key::kRight:
            m_keyRight = false;
            break;
        case engine::key::kUp:
            m_keyFaster = false;
            break;
        case engine::key::kDown:
            m_keySlower = false;
            break;
        default:
            break;
        }
    }
    ToolbarNode::onKeyUp(e);
}

void TrailNode::listen(int message, const engine::MessageData& data) {
    // ListenToMessage (0x435510).
    const std::uint32_t now = engine().ticks(); // [0x80a198]
    const auto* ev = static_cast<const engine::InputEvent*>(data.ptr);
    switch (message) {
    case kMsgEdgeRightAlt:
        // "Continue": ride on from the stop.
        if (m_modal)
            return;
        if (m_pickup) {
            deleteAnim(m_pickup);
            addBagItem(m_pickupItem);
        }
        if (!m_mounted) {
            mountAndRide();
            return;
        }
        if (m_trail == 6 && m_decision == 0)
            m_nextTrail = 7;
        if (!isScriptRunning("runOff")) {
            stopAllScripts();
            runScript(m_trail == 5 && m_decision == 1 ? "runLeft" : "runOff");
        }
        return;
    case -103: // a mouse button went up
        m_lastInput = now;
        if (m_riding && app().controlMode == 0 && ev) {
            if (ev->type == engine::InputEvent::LeftUp) {
                // A double click (two releases within a second) jumps.
                m_leftReleased = true;
                if (now - s_ride.leftUp > 999) {
                    s_ride.leftUp = now;
                    return;
                }
                s_ride.leftUp = 0;
                horseJump();
                return;
            }
            if (ev->type == engine::InputEvent::RightUp) {
                // A right double click stops (not during a refusal loop).
                m_rightReleased = true;
                if (now - s_ride.rightUp > 999) {
                    s_ride.rightUp = now;
                    return;
                }
                s_ride.rightUp = 0;
                if (m_current == currentBackup())
                    return;
                horseStop();
                return;
            }
            ToolbarNode::listen(message, data); // (twice, as coded)
        }
        ToolbarNode::listen(message, data);
        return;
    case -102: // a mouse button went down
        m_lastInput = now;
        if (!m_riding) {
            if (!ev || ev->type != engine::InputEvent::LeftDown) {
                ToolbarNode::listen(message, data);
                return;
            }
            if (m_modal)
                return;
            if (!m_mounted && !isScriptRunning("dismount") && (!cursor() || cursor()->sequence() != 6)) {
                // Dismounted: Barbie walks to where the player clicked.
                const auto [mx, my] = mouseWorldPos();
                m_walkTargetX = static_cast<std::int16_t>(mx);
                m_walkTargetY = static_cast<std::int16_t>(my);
                if (m_walkTargetY > 0x1AD || isScriptRunning("WalkBack"))
                    return;
                walkBarbieToMouse();
                return;
            }
            ToolbarNode::listen(message, data);
            return;
        }
        if (ev && ev->type == engine::InputEvent::LeftDown) {
            if (app().controlMode == 0)
                m_leftReleased = false;
            return;
        }
        if (ev && ev->type == engine::InputEvent::RightDown) {
            if (app().controlMode == 0)
                m_rightReleased = false;
            return;
        }
        ToolbarNode::listen(message, data);
        return;
    case kMsgCamera:
        // A photo: the certificate missions C1-C3 take it over.
        switch (mission()) {
        case 8:
            skipNextPhoto = true;
            c1CameraClick();
            break;
        case 9:
            skipNextPhoto = true;
            c2CameraClick();
            break;
        case 10:
            skipNextPhoto = m_decision == 0;
            c3CameraClick();
            break;
        default:
            break;
        }
        return;
    case engine::msg::kInactivity:
        if (!m_riding && mission() == 5 && m_mapLocation == 6 && m_decision == 0 && m_well &&
            m_well->sequence() == 0 && !engine().anims().find("m6 mouse handle"))
            playVoice("db04058");
        app().idleTimer().stop();
        return;
    default:
        ToolbarNode::listen(message, data);
        return;
    }
}

void TrailNode::gotoNodeById(int id) {
    // GotoNodeById (0x439e20): the map. The tack room leaves the node; the
    // trail locations switch trails inside it.
    auto& s = app().session();
    if (s.activeMission == 0x10) {
        s.activeMission = s.savedMission;
        s.missionTrail = s.savedMissionTrail;
        s.tutorialActive = 0;
    }
    m_startFrame = 0;
    stopAllScripts();
    if (m_stream)
        m_stream->setVolumePan(0, 0x40);
    if (m_falls)
        m_falls->setVolumePan(0, 0x40);
    setHorseState(kHideAll);
    switch (id) {
    case 7:
        clearStopProps();
        app().gotoNode("tackroom");
        return;
    case 0x11:
        m_startFrame = 0x38;
        m_startFramePreset = true;
        m_nextTrail = 0;
        break;
    case 0x12: m_nextTrail = 1; break;
    case 0x13: m_nextTrail = 2; break;
    case 0x14: m_nextTrail = 4; break;
    case 0x15: m_nextTrail = 3; break;
    case 0x16: m_nextTrail = 5; break;
    case 0x17: m_nextTrail = 6; break;
    default: break;
    }
    if (!s.mapRabbit2) {
        if ((m_trail == 1 || m_trail == 6 || m_trail == 7) && m_nextTrail == 4) {
            m_startFrame = 0;
            m_startFramePreset = true;
        } else if (m_nextTrail == 2) {
            if (app().record().u32(PlayerRecord::kMissionDone + 6 * 4) == 0) {
                m_startFrame = 0x3A;
                m_startFramePreset = true;
            } else {
                m_skip57 = true;
            }
        }
    } else {
        if (m_nextTrail == 4) {
            m_startFrame = 0x1CC;
            m_startFramePreset = true;
        }
        s.mapRabbit2 = 0;
    }
    if (m_trail == m_nextTrail) {
        m_restartSameTrail = true;
        if (m_deer && m_deerVisible) {
            m_deer->enabled = false;
            m_deer->stream->smk.gotoFrame(0);
            m_deerVisible = false;
            m_deerDone = true;
        }
    }
    m_jumping = false;
    horseRestart();
}

// --- stops: mounting and walking ------------------------------------------------------------------

void TrailNode::toggleMount() {
    // ToggleMount (0x439ce0): a click on the horse or Barbie.
    if (isScriptRunning("RunOn") || isScriptRunning("mount") || isScriptRunning("dismount"))
        return;
    if (m_mounted) {
        if (isScriptRunning("RunOff")) {
            m_runOnAfterRunOff = true;
            return;
        }
        stopScriptIfRunning("Ambient");
        runScript("dismount");
        m_lastDirection = 2;
        m_barbieSlot = 1;
        m_mounted = false;
        return;
    }
    if (engine().anims().find("barbieWalkOutside")) {
        if (m_barbieSlot != 1) {
            walkBarbieToHorse();
            return;
        }
        if (isScriptRunning("WalkAmb"))
            stopScript("WalkAmb");
        else if (isScriptRunning("WalkLookLeft"))
            stopScript("WalkLookLeft");
        else if (isScriptRunning("WalkBack"))
            stopScript("WalkBack");
    }
    runScript("mount");
    m_mounted = true;
}

void TrailNode::walkBarbieToMouse() {
    // WalkBarbieToMouse (0x43bba0): Barbie walks in 120-pixel cycles towards
    // the click, between the five slots beside the horse.
    if (isScriptRunning("Walk")) {
        if (!isScriptRunning("WalkAmb"))
            return;
        stopScript("Walk");
    }
    if (m_walkDisabled)
        return;
    m_walk = findOrCreateHiddenAnim("barbieWalkOutside");
    if (!m_walk)
        return;
    if (m_lastDirection == 2)
        m_walk->setPosition(-135.0f, 0, 100.0f);
    auto bx = static_cast<std::int16_t>(static_cast<int>(m_walk->x() + 320.0f));
    if (m_lastDirection == 0)
        bx = static_cast<std::int16_t>(bx - 0x37);
    else if (m_lastDirection == 1)
        bx = static_cast<std::int16_t>(bx + 0x2D);
    const std::int16_t tx = m_walkTargetX;
    m_walkCycles = 0;
    if (std::abs(tx - bx) < 10)
        return;
    if (tx < bx) {
        m_walkDirection = 0;
        m_walkTargetX = static_cast<std::int16_t>(tx + 0xB4);
        while (m_walkTargetX < bx) {
            m_walkTargetX = static_cast<std::int16_t>(m_walkTargetX + 0x78);
            ++m_walkCycles;
        }
    } else {
        m_walkDirection = 1;
        m_walkTargetX = static_cast<std::int16_t>(tx - 0xB4);
        while (bx < m_walkTargetX) {
            m_walkTargetX = static_cast<std::int16_t>(m_walkTargetX - 0x78);
            ++m_walkCycles;
        }
    }
    // No further than the end slots.
    const int room = m_walkDirection == 0 ? m_barbieSlot - 1 : 3 - m_barbieSlot;
    if (room < 0)
        return;
    m_walkCycles = std::min(m_walkCycles, room);
    for (auto n : {"WalkAmb", "WalkLookLeft", "WalkBack", "TMoment2", "FeedApple"}) {
        if (isScriptRunning(n)) {
            stopScript(n);
            break;
        }
    }
    runScript("Walk");
    m_barbieSlot += m_walkDirection == 0 ? -(1 + m_walkCycles) : 1 + m_walkCycles;
}

void TrailNode::mountAndRide() {
    // MountAndRide (0x43be80).
    m_rideOffAfterMount = true;
    if (engine().anims().find("barbieWalkOutside")) {
        if (m_barbieSlot != 1) {
            walkBarbieToHorse();
            return;
        }
        if (isScriptRunning("WalkAmb"))
            stopScript("WalkAmb");
        else if (isScriptRunning("WalkLookLeft"))
            stopScript("WalkLookLeft");
        else if (isScriptRunning("WalkBack"))
            stopScript("WalkLookBack"); // (as coded: a script that does not exist)
    }
    runScript("mount");
    m_mounted = true;
}

void TrailNode::walkBarbieToHorse() {
    // WalkBarbieToHorse (0x43bf30): back to the saddle (slot 1), then mount.
    if (isScriptRunning("Walk"))
        return;
    m_walk = findOrCreateHiddenAnim("barbieWalkOutside");
    if (m_walk && m_lastDirection == 2)
        m_walk->setPosition(-135.0f, 0, 100.0f);
    switch (m_barbieSlot) {
    case 0:
        m_walkDirection = 1;
        m_walkCycles = 0;
        break;
    case 2:
        m_walkDirection = 0;
        m_walkCycles = 0;
        break;
    case 3:
        m_walkDirection = 0;
        m_walkCycles = 1;
        break;
    case 4:
        m_walkDirection = 0;
        m_walkCycles = 2;
        break;
    default:
        break;
    }
    for (auto n : {"WalkAmb", "WalkLookLeft", "WalkBack", "TMoment2", "FeedApple"}) {
        if (isScriptRunning(n)) {
            stopScript(n);
            break;
        }
    }
    m_mountAfterWalk = true;
    runScript("Walk");
    m_barbieSlot += m_walkDirection == 0 ? -(1 + m_walkCycles) : 1 + m_walkCycles;
}

// --- the horse state machine --------------------------------------------------------------------

void TrailNode::forceHideAnim(engine::Anim* a) {
    // ForceHideAnim (0x43baa0): hide and pause until both counters are set.
    if (!a)
        return;
    while (a->hideCount() == 0)
        a->hide(true);
    while (a->motionPauseCount() == 0)
        a->pause(true);
}

void TrailNode::forceHideAnimByName(std::string_view name) { forceHideAnim(engine().anims().find(name)); } // 0x43bb10

void TrailNode::forceShowAnim(engine::Anim* a) {
    // ForceShowAnim (0x43bb30).
    if (!a)
        return;
    while (a->hideCount() != 0)
        a->hide(false);
    while (a->motionPauseCount() != 0)
        a->pause(false);
}

void TrailNode::setHorseState(int state) {
    // SetHorseState (0x43b240): which of the horse/Barbie anims may show at a
    // stop (the scripts play them).
    if (m_horseState == state)
        return;
    const auto horse = horseIndex(app().record());
    auto hide = [](std::initializer_list<engine::Anim*> list) {
        for (engine::Anim* a : list)
            forceHideAnim(a);
    };
    switch (m_horseState) {
    case kSubtleAmbient:
        if (state == kGeneralAmbient)
            hide({m_subtleAmbHorse, m_subtleAmbSdl, m_subtleAmbFancy});
        break;
    case kGeneralAmbient:
        if (state == kSubtleAmbient)
            hide({m_generalAmbHorse, m_generalAmbSdl, m_generalAmbFancy});
        break;
    case kWalk:
        if (state == kWalkAmbient)
            hide({m_walk});
        break;
    case kWalkAmbient:
        if (state == kWalk)
            hide({m_barbieAmbients});
        break;
    default:
        hide({m_walk, m_barbieAmbients, m_generalAmbHorse, m_generalAmbSdl, m_generalAmbFancy, m_subtleAmbHorse,
              m_subtleAmbSdl, m_subtleAmbFancy});
        break;
    }
    log::debug("TrailNode::SetHorseState {} -> {}", trail::kHorseStateName[static_cast<std::size_t>(m_horseState)],
               trail::kHorseStateName[static_cast<std::size_t>(std::clamp(state, 0, 13))]);
    m_horseState = state;
    if (state != kMountRun) {
        hide({m_bMountRun});
        if (m_highMemory)
            hide({m_hMountRun, m_fancyMountRun});
    }
    if (state != kAmbient)
        hide({m_ambientBarbie, m_ambientHorse, m_ambientFancy});
    if (state != kTurnLeft) {
        forceHideAnimByName("bTurnLeft");
        forceHideAnimByName(trail::kTurnLeftH[horse]);
        forceHideAnimByName("fTurnLeft");
    }
    if (state != kTurnRight) {
        forceHideAnimByName("bTurnRight");
        forceHideAnimByName(trail::kTurnRightH[horse]);
        forceHideAnimByName("fTurnRight");
    }
    bool excitedDone = false;
    if (state != kLookLeft) {
        forceHideAnimByName("bLookLeft");
        if (state == kExcitedLookLeft) {
            excitedDone = true;
        } else {
            forceHideAnimByName(trail::kLookLeftH[horse]);
            forceHideAnimByName("fLookLeft");
        }
    }
    if (!excitedDone && state != kExcitedLookLeft)
        forceHideAnimByName("bExcitedLook");
    if (state != kTenderMoment)
        hide({m_bTMoment, m_hTMoment, m_sTMoment, m_fTMoment});
    if (state != kFeedApple) {
        forceHideAnimByName("bFeedApple");
        forceHideAnimByName(trail::kEatAppleH[horse]);
    }
    switch (state) {
    case kMountRun:
        hide({m_walk, m_barbieAmbients, m_generalAmbHorse, m_generalAmbSdl, m_generalAmbFancy, m_subtleAmbHorse,
              m_subtleAmbSdl, m_subtleAmbFancy});
        if (!m_highMemory) {
            hide({m_saddleAndReigns});
            forceShowAnim(m_bMountRun);
            forceShowAnim(m_hMountRun);
            forceShowAnim(m_fancyMountRun);
        }
        break;
    case kTurnLeft:
    case kTurnRight:
    case kLookLeft:
    case kExcitedLookLeft:
        if (!m_highMemory)
            hide({m_hMountRun, m_fancyMountRun});
        break;
    case kSubtleAmbient:
        hide({m_generalAmbHorse, m_generalAmbSdl, m_generalAmbFancy});
        break;
    case kGeneralAmbient:
        hide({m_subtleAmbHorse, m_subtleAmbSdl, m_subtleAmbFancy});
        break;
    case kWalk:
        hide({m_barbieAmbients});
        m_walk = findOrCreateHiddenAnim("barbieWalkOutside");
        forceShowAnim(m_walk);
        if (!m_highMemory) {
            m_hMountRun = findOrCreateHiddenAnim(trail::kMountRunH[horse]);
            forceShowAnim(m_hMountRun);
            m_fancyMountRun = m_fancy ? findOrCreateHiddenAnim("fancyMountRun") : nullptr;
            forceShowAnim(m_fancyMountRun);
            m_saddleAndReigns = findOrCreateHiddenAnim("SaddleAndReigns");
            forceShowAnim(m_saddleAndReigns);
            if (m_saddleAndReigns)
                m_saddleAndReigns->setClickId(kHorseClick);
        }
        break;
    case kWalkAmbient:
        hide({m_walk});
        break;
    case kTenderMoment:
    case kFeedApple:
        hide({m_walk, m_barbieAmbients, m_generalAmbHorse, m_generalAmbSdl, m_generalAmbFancy, m_subtleAmbHorse,
              m_subtleAmbSdl, m_subtleAmbFancy});
        if (!m_highMemory)
            hide({m_hMountRun, m_fancyMountRun, m_saddleAndReigns});
        if (state == kTenderMoment) {
            forceShowAnim(m_bTMoment);
            forceShowAnim(m_hTMoment);
            forceShowAnim(m_sTMoment);
            forceShowAnim(m_fTMoment);
        } else if (engine::Anim* a = createAnimAtViewport("bFeedApple"); a && m_bTMoment) {
            a->setPosition(m_bTMoment->x() - 3.0f, m_bTMoment->y(), m_bTMoment->z());
        }
        break;
    case kHideAll:
        hide({m_bMountRun, m_hMountRun, m_fancyMountRun, m_saddleAndReigns, m_walk, m_barbieAmbients,
              m_generalAmbHorse, m_generalAmbSdl, m_generalAmbFancy, m_subtleAmbHorse, m_subtleAmbSdl,
              m_subtleAmbFancy, m_ambientBarbie, m_ambientHorse, m_ambientFancy, m_bTMoment, m_hTMoment, m_sTMoment,
              m_fTMoment});
        break;
    default:
        break;
    }
}

// --- scripts --------------------------------------------------------------------------------------

void TrailNode::scriptMessage(engine::Script& script, std::string_view msg, int counter) {
    // ScriptCommand (0x43a080): the stop scripts set the horse state and get
    // the per-horse anim names; everything also goes to the toolbar.
    const std::string& name = script.name();
    const auto horse = horseIndex(app().record());
    auto is = [&](std::string_view s) { return str::iequals(msg, s); };
    auto fancyVar = [&] {
        if (m_fancy)
            setScriptVar(name, "fancy", 1.0);
    };
    if (is("initAmbient")) {
        fancyVar();
        setScriptVar(name, "hAmbient", trail::kAmbientH[horse]);
        setHorseState(kAmbient);
    } else if (is("initHMountRun")) {
        m_bMountRun = findOrCreateHiddenAnim("bMountRun");
        m_hMountRun = findOrCreateHiddenAnim(trail::kMountRunH[horse]);
        if (m_bMountRun)
            m_bMountRun->setClickId(kHorseClick);
        if (m_hMountRun)
            m_hMountRun->setClickId(kHorseClick);
        setScriptVar(name, "hMountRun", trail::kMountRunH[horse]);
        if (m_fancy) {
            m_fancyMountRun = findOrCreateHiddenAnim("fancyMountRun");
            setScriptVar(name, "fancy", 1.0);
        }
        setHorseState(kMountRun);
    } else if (is("initHLookLeft")) {
        setScriptVar(name, "hLookLeft", trail::kLookLeftH[horse]);
        if (m_fancy) {
            setScriptVar(name, "fancy", 1.0);
            createAnimAtViewport("fLookLeft");
        }
        createAnimAtViewport(trail::kLookLeftH[horse]);
        if (str::iequals(name, "LookLeft")) {
            createAnimAtViewport("bLookLeft");
            setHorseState(kLookLeft);
        } else if (str::iequals(name, "ExcitedLook")) {
            setScriptVar(name, "delay", 500.0);
            createAnimAtViewport("bExcitedLook");
            setHorseState(kExcitedLookLeft);
        }
    } else if (is("initHTurnLeft")) {
        setScriptVar(name, "hTurnLeft", trail::kTurnLeftH[horse]);
        fancyVar();
        setHorseState(kTurnLeft);
    } else if (is("initHTurnRight")) {
        setScriptVar(name, "hTurnRight", trail::kTurnRightH[horse]);
        fancyVar();
        setHorseState(kTurnRight);
    } else if (is("initHorseAmb")) {
        m_generalAmbHorse = findOrCreateHiddenAnim(trail::kGeneralAmbH[horse]);
        m_subtleAmbHorse = findOrCreateHiddenAnim(trail::kSubtleAmbH[horse]);
        m_generalAmbSdl = findOrCreateHiddenAnim("GeneralAmbSdl");
        m_subtleAmbSdl = findOrCreateHiddenAnim("SubtleAmbSdl");
        for (engine::Anim* a : {m_generalAmbHorse, m_subtleAmbHorse, m_generalAmbSdl, m_subtleAmbSdl})
            if (a)
                a->setClickId(kHorseClick);
        if (m_fancy) {
            m_generalAmbFancy = createAnimAtViewport("GeneralAmbFancy");
            m_subtleAmbFancy = createAnimAtViewport("SubtleAmbFancy");
            if (m_generalAmbFancy)
                m_generalAmbFancy->setClickId(kHorseClick);
            if (m_subtleAmbFancy)
                m_subtleAmbFancy->setClickId(kHorseClick);
        }
        setScriptVar(name, "generalHorseAmb", trail::kGeneralAmbH[horse]);
        setScriptVar(name, "subtleHorseAmb", trail::kSubtleAmbH[horse]);
        fancyVar();
        setHorseState(kSubtleAmbient);
    } else if (is("initLookLeft")) {
        setHorseState(kLookLeft);
    } else if (is("initM5Payoff")) {
        setScriptVar(name, "delay", 4000.0);
    } else if (is("initPagerDelay")) {
        setScriptVar(name, "delay", static_cast<double>(m_pagerDelay));
    } else if (is("PagerOKToGo")) {
        setScriptVar(name, "talking", voicePlaying() ? 1.0 : 0.0);
        setScriptVar(name, "busy", m_pagerBusy ? 1.0 : 0.0);
    } else if (is("initTMoment2")) {
        m_bTMoment = createAnimAtViewport("bTMoment2");
        if (m_bTMoment)
            m_bTMoment->setPosition(m_bTMoment->x() - 3.0f, m_bTMoment->y(), m_bTMoment->z());
        m_hTMoment = createAnimAtViewport(trail::kTMomentH[horse]);
        m_sTMoment = createAnimAtViewport("sTMoment2");
        m_fTMoment = m_fancy ? createAnimAtViewport("fTMoment2") : nullptr;
        setScriptVar(name, "hTMoment2", trail::kTMomentH[horse]);
        if (str::iequals(name, "FeedApple"))
            setScriptVar(name, "hEatApple", trail::kEatAppleH[horse]);
        fancyVar();
        setHorseState(kTenderMoment);
    } else if (is("hideHorseForSubtle")) {
        setHorseState(kSubtleAmbient);
    } else if (is("hideHorseForGeneral")) {
        setHorseState(kGeneralAmbient);
    } else if (is("initWalk")) {
        setHorseState(kWalk);
        setScriptVar(name, "cycles", static_cast<double>(m_walkCycles));
        setScriptVar(name, "direction", static_cast<double>(m_walkDirection));
        setScriptVar(name, "lastDirection", static_cast<double>(m_lastDirection));
    } else if (is("initWalkAmb")) {
        setHorseState(kWalkAmbient);
        m_barbieAmbients = findOrCreateHiddenAnim("barbie ambients");
        if (!m_walk || m_lastDirection == 2) {
            if (m_barbieAmbients)
                m_barbieAmbients->setPosition(-135.0f, 0, 195.0f);
        } else if (m_barbieAmbients) {
            const float dx = m_lastDirection != 0 ? 34.0f : -66.0f;
            m_barbieAmbients->setPosition(m_walk->x() + dx, m_walk->y(), m_walk->z());
        }
    } else if (is("moveLeft") || is("moveRight") || is("moveCLeft") || is("moveCRight") || is("resetLeft") ||
               is("resetRight")) {
        const float dx = is("moveLeft") ? -66.0f : is("moveRight") ? 98.0f : is("moveCLeft") ? -132.0f
                         : is("moveCRight") ? 132.0f : is("resetLeft") ? -66.0f : 34.0f;
        if (m_walk)
            m_walk->setPosition(m_walk->x() + dx, m_walk->y(), m_walk->z());
    } else if (is("preload_getnext")) {
        // The queue of anims "preload" creates one by one (never filled by the
        // shipped game: the script gets "").
        if (m_preloadQueue.empty()) {
            setScriptVar("preload", "animName", std::string_view{});
        } else {
            setScriptVar("preload", "animName", m_preloadQueue.front());
            m_preloadQueue.pop_front();
        }
    }
    ToolbarNode::scriptMessage(script, msg, counter);
}

void TrailNode::scriptStarted(const std::string& name) {
    // ScriptStarted (0x43aa40).
    if (str::iequals(name, "mount")) {
        stopScriptIfRunning("HorseAmb");
    } else if (str::iequals(name, "runOff")) {
        setHorseState(kMountRun);
    }
    ToolbarNode::scriptStarted(name);
}

void TrailNode::scriptEnded(engine::Script& script) {
    // ScriptEnded (0x43aab0): the stop scripts chain into each other; riding
    // off ends in HorseRestart.
    const std::string name = script.name();
    auto is = [&](std::string_view s) { return str::iequals(name, s); };
    const auto horse = horseIndex(app().record());
    auto startAmbient = [&] {
        if (!m_highMemory)
            return;
        m_ambientBarbie = findOrCreateHiddenAnim("ambientBarbie");
        m_ambientHorse = findOrCreateHiddenAnim(trail::kAmbientH[horse]);
        if (m_ambientBarbie)
            m_ambientBarbie->setClickId(kHorseClick);
        if (m_ambientHorse)
            m_ambientHorse->setClickId(kHorseClick);
        runScript("Ambient");
    };
    auto walkAmbient = [&] {
        if (!m_highMemory) {
            setHorseState(kWalk);
            return;
        }
        runScript("WalkAmb");
        runScript("HorseAmb");
    };
    if (is("dismount")) {
        loadWalkAnims();
        if (m_highMemory) {
            runScript("WalkAmb");
            runScript("HorseAmb");
        }
    } else if (is("FeedApple")) {
        walkAmbient();
    } else if (is("HorseAmb")) {
        if (m_tenderMomentAfterAmb)
            runScript("TMoment2");
    } else if (is("LookLeft")) {
        startAmbient();
    } else if (is("mount")) {
        if (!m_rideOffAfterMount) {
            startAmbient();
        } else {
            m_rideOffAfterMount = false;
            runScript("RunOff");
        }
    } else if (is("PagerDelay")) {
        choosePagerMessage();
    } else if (is("runOff")) {
        if (m_runOnAfterRunOff) {
            m_runOnAfterRunOff = false;
            runScript("RunOn");
        } else {
            horseRestart();
        }
    } else if (is("RunLeft") || is("RunRight")) {
        setHorseState(kHideAll);
        horseRestart();
    } else if (is("runOn")) {
        // Arrived at a stop: the first rides' hints, the end of the lesson.
        auto& s = app().session();
        auto& rec = app().record();
        const std::uint32_t hints = rec.u32(PlayerRecord::kRideHintCount); // app+0x2EC
        if (!s.tutorialActive) {
            if (hints == 0) {
                playVoiceAfterPause("db9013");
                rec.setU32(PlayerRecord::kRideHintCount, hints + 1);
            } else if (mission() == 0xB) {
                playVoiceAfterPause("db0453");
            }
        } else if (!m_tutorialFinished) {
            if (hints < 2) {
                playVoiceAfterPause("db9013");
                rec.setU32(PlayerRecord::kRideHintCount, hints + 1);
            }
            playVoiceAfterPause("db02001");
        } else {
            s.activeMission = s.savedMission;
            s.missionTrail = s.savedMissionTrail;
            s.tutorialActive = 0;
            m_tutorialFinished = false;
            playVoiceNoFlaps(s.activeMission == 0x11 ? "db9061" : "db9062");
        }
        if (!m_ambientBarbie)
            setHorseState(kMountRun);
        else
            runScript("Ambient");
    } else if (is("TMoment2")) {
        m_tenderMomentAfterAmb = false;
        walkAmbient();
    } else if (is("Walk")) {
        m_lastDirection = m_walkDirection;
        if (m_mountAfterWalk) {
            m_mountAfterWalk = false;
            m_mounted = true;
            runScript("mount");
        } else if (m_barbieSlot == 2) {
            stopScript("WalkAmb");
            stopScript("HorseAmb");
            runScript("TMoment2");
        } else if (m_highMemory) {
            runScript("WalkAmb");
        }
    } else if (is("WalkBack")) {
        app().gotoNode(nodeName(m_exitPropNode));
    }
    switch (mission()) {
    case 0: m1ScriptEnded(name); break;
    case 2: m3ScriptEnded(name); break;
    case 3: m4ScriptEnded(name); break;
    case 4: m5ScriptEnded(name); break;
    case 5: m6ScriptEnded(name); break;
    case 9: c2ScriptEnded(name); break;
    case 14: r3ScriptEnded(name); break;
    default: break;
    }
    ToolbarNode::scriptEnded(script);
}

void TrailNode::scriptPreempted(engine::Script& script) {
    // ScriptPreempted (0x43b000).
    const std::string& name = script.name();
    auto is = [&](std::string_view s) { return str::iequals(name, s); };
    if (is("Ambient")) {
        if (m_ambientPreemptToggle) {
            setHorseState(kMountRun);
        } else {
            m_ambientPreemptToggle = true;
            setHorseState(kHideAll);
        }
    } else if (is("FeedApple")) {
        runScript("HorseAmb");
    } else if (is("LookLeft")) {
        if (!m_highMemory)
            setHorseState(kMountRun);
    } else if (is("Mount")) {
        m_rideOffAfterMount = false;
    } else if (is("TMoment2")) {
        m_tenderMomentAfterAmb = false;
        if (m_highMemory) {
            runScript("WalkAmb");
            runScript("HorseAmb");
        } else {
            setHorseState(kWalk);
        }
    } else if (is("Walk")) {
        m_mountAfterWalk = false;
        m_rideOffAfterMount = false;
    }
    ToolbarNode::scriptPreempted(script);
}

void TrailNode::frameEvent(engine::Anim& anim, int seq, int index) {
    // AnimFrameCue (0x43b180): Barbie's footsteps while she walks.
    if (&anim != m_bMountRun && &anim == m_walk) {
        if (seq == 0 || seq == 3)
            engine().sounds().playSound("barbDirt", -1, false, false, 0, false, 0x70, -1, 0, false);
        else if (seq == 2 || seq == 5)
            engine().sounds().stopSound("barbDirt");
    }
    ToolbarNode::frameEvent(anim, seq, index);
}

} // namespace brc::game
