// TrailNode.cpp (0x42fda0–0x434caf): the trail node's construction and
// teardown, critters, voices, palettes, stops and their props. The ride itself
// is in TrailRide.cpp. See docs/engine/nodes.md "TrailNode".
#include "game/TrailNode.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "engine/Font.h"
#include "engine/Scripts.h"
#include "engine/SoundManager.h"
#include "game/MiniGameFlags.h"
#include "game/Popups.h"
#include "game/TrailFlags.h"
#include "game/TrailTables.h"
#include "game/Widgets.h"

#include <format>

namespace brc::game {
namespace {

// [0x80268c] the critter at the current stop (kNoCritter = none) and
// [0x80a15c] it has been clicked.
int s_critter = trail::kNoCritter;
bool s_critterClicked = false;

std::size_t horseIndex(const PlayerRecord& r) { return std::min<std::size_t>(r.horse(), 3); }

} // namespace

// --- helpers --------------------------------------------------------------------------------------

int TrailNode::mission() const { return static_cast<int>(app().session().activeMission); }

std::string TrailNode::smkPath(std::string_view name) const {
    // +0x1CC: the CD path (app+0xE4, the Resources folder here) + "smk\".
    return std::format("smk\\{}", name);
}

std::string TrailNode::headPath(std::string_view name) const {
    // +0x250: +0x1CC + "horseheads\".
    return std::format("smk\\horseheads\\{}", name);
}

void TrailNode::deleteAnim(engine::Anim*& anim) {
    // HCNode::DeleteAnim(HC2DAnim*) (0x455180).
    if (anim)
        engine().anims().remove(anim);
    anim = nullptr;
}

void TrailNode::stopScriptIfRunning(std::string_view name) {
    if (isScriptRunning(name))
        stopScript(name);
}

// --- critters (0x42fda0–0x430630) -------------------------------------------------------------------

void TrailNode::spawnCritter() {
    // SpawnCritter (0x42fda0): which critter (if any) sits at this stop, from
    // the mission, the trail and the decision.
    s_critterClicked = false;
    s_critter = trail::kNoCritter;
    m_critter = nullptr;
    m_critterRollover.reset();
    float x = 0, y = 0;
    const int ms = mission();
    if (ms == 2 && (m_trail != 5 || m_decision == 1))
        return;
    auto& eng = engine();
    if (m_trail == 0) {
        if (ms == 0xD)
            return;
        if (m_decision == 5) {
            if (ms == 4)
                return;
            const int r = eng.rand() % 3;
            if (r == 1) {
                s_critter = 10;
            } else if (r == 2) {
                x = 150.0f;
                s_critter = 3;
                y = 102.0f;
            }
        } else if (m_decision == 4) {
            // The current movie's frame (base or insert; anything else reads
            // an unset value in the original, which fails the window test).
            std::int64_t f = -1;
            if (m_current == m_base.get())
                f = m_base->frameNum();
            else if (m_current == m_insert.get())
                f = m_insert->frameNum();
            if (f < 0 || f < 0x1CC || f > 0x208)
                return;
            x = 150.0f;
            s_critter = 3;
            y = 105.0f;
        } else if (m_decision == 2) {
            return;
        }
    } else if (m_trail == 4) {
        if (ms == 0xE && m_decision != 0)
            return;
        if (m_decision == 0) {
            if (eng.rand() % 3 == 1) {
                s_critter = 3;
                x = eng.rand() % 2 == 0 ? 120.0f : 150.0f;
                y = 110.0f;
            } else {
                x = -244.0f;
                s_critter = 0xE;
                y = -53.0f;
            }
        } else {
            if (m_decision == 1)
                return;
            if (m_decision == 5) {
                if (eng.rand() % 3 == 1) {
                    if (ms == 9)
                        return;
                    const int r = eng.rand();
                    x = -150.0f;
                    y = -6.0f;
                    s_critter = r % 2 != 0 ? 0xE : 0xC;
                } else {
                    x = 130.0f;
                    s_critter = 3;
                    y = 100.0f;
                }
            }
        }
    } else {
        if (m_trail != 5)
            return;
        if (m_decision == 5) {
            if (eng.rand() % 2 == 0) {
                x = 200.0f;
                y = 0;
                s_critter = 7;
            } else {
                s_critter = 1;
            }
        } else if (m_decision == 0 && (eng.rand() % 2 == 0 || eng.rand() % 2 != 0)) {
            x = 280.0f;
            y = 0;
            s_critter = 7;
        } else {
            s_critter = 1;
        }
    }
    if (s_critter == trail::kNoCritter)
        return;
    m_critter = createAnimAtViewport(trail::kCritterAnim[static_cast<std::size_t>(s_critter)]);
    if (!m_critter)
        return;
    m_critter->setClickId(kCritterClick);
    if (s_critter != 10)
        m_critterRollover = std::make_unique<ACRollover>(*this, m_critter, 7);
    // A coordinate that truncates to 0 keeps the anim's own.
    if (static_cast<int>(x) == 0)
        x = m_critter->x();
    if (static_cast<int>(y) == 0)
        y = m_critter->y();
    m_critter->setPosition(x, y, m_critter->z());
    if (s_critter == 1) {
        if (eng.rand() % 2 != 0)
            m_critter->setSpeed(-2.0f, 0, 0);
        else
            m_critter->setSequence(1, true);
    }
}

void TrailNode::removeCritter() {
    // 0x430250.
    if (!m_critter)
        return;
    deleteAnim(m_critter);
    m_critterRollover.reset();
    s_critter = trail::kNoCritter;
}

void TrailNode::critterClicked() {
    // CritterClicked (0x4302a0): once per stop, a sound and the critter's
    // escape (sequence 1, or a move).
    if (s_critterClicked || !m_critter)
        return;
    auto& snd = engine().sounds();
    auto sfx = [&](std::string_view name) { snd.playSound(name, -1, false, false, 1, false, 0x70, -1, 0, false); };
    switch (s_critter) {
    case 1:
        sfx("turtle");
        m_critter->setSpeed(0, 0, 0);
        if (m_critter->sequence() != 0) {
            m_critter->setSequence(0, true);
            m_critter->setSpeed(-2.0f, 0, 0);
            return;
        }
        m_critter->setSequence(1, true);
        return;
    case 2:
        m_critter->setSpeed(230.0f, 0, 0);
        sfx("newrabit");
        break;
    case 3:
        sfx("newrabit");
        break;
    case 4:
    case 5:
        sfx("sparrow");
        m_critter->setSpeed(-155.0f, -105.0f, 0);
        break;
    case 6:
    case 7:
        sfx("turtle");
        if (m_critter->sequence() != 0) {
            m_critter->setSequence(0, true);
            return;
        }
        m_critter->setSequence(1, true);
        return;
    case 8:
        sfx("frog");
        break;
    case 9:
        sfx("turtle");
        if (engine().rand() % 2 != 0 && !voicePlaying())
            playVoiceAfterPause("db03068");
        m_critter->setSpeed(-20.0f, 0, 0);
        break;
    case 10:
        s_critterClicked = true;
        return;
    case 0xC:
    case 0xE:
        sfx("sparrow");
        break;
    default:
        break;
    }
    s_critterClicked = true;
    m_critter->setSequence(1, true);
}

void TrailNode::sayCritterComment() {
    // SayCritterComment (0x430580).
    if (s_critter == trail::kNoCritter)
        return;
    const int ms = mission();
    if (ms == 2 && !(m_trail == 5 && m_decision != 1))
        return;
    if (ms == 0 && m_trail == 5 && m_decision == 1)
        return;
    const int r = engine().rand() % 9;
    if (r == 0) {
        playVoiceAfterPause("db03068");
    } else if (r == 1) {
        playVoiceAfterPause("db3067");
    } else {
        const auto v = trail::kCritterVoice[static_cast<std::size_t>(s_critter)];
        if (!v.empty())
            playVoiceAfterPause(v);
    }
}

// --- voices (0x430630–0x430ad0) -----------------------------------------------------------------

void TrailNode::loadMissionVoices() {
    // 0x430630.
    if (!trailflags::dialogue)
        return;
    preloadPathVoices();
    switch (mission()) {
    case 0: m1LoadSounds(); break;
    case 1: m2LoadSounds(); break;
    case 2: m3LoadSounds(); break;
    case 3: m4LoadSounds(); break;
    case 4: m5LoadSounds(); break;
    case 5: m6LoadSounds(); break;
    case 6: m7LoadSounds(); break;
    case 7: m8LoadSounds(); break;
    case 8: c1LoadSounds(); break;
    case 9: c2LoadSounds(); break;
    case 10: c3LoadSounds(); break;
    case 11: c4LoadSounds(); break;
    case 12: r2LoadSounds(); break;
    case 13: r1LoadSounds(); break;
    case 14: r3LoadSounds(); break;
    default: break;
    }
}

void TrailNode::releaseMissionVoices() {
    // 0x430730.
    if (!trailflags::dialogue)
        return;
    releasePathVoices();
    switch (mission()) {
    case 0: m1ReleaseSounds(); break;
    case 1: m2ReleaseSounds(); break;
    case 2: m3ReleaseSounds(); break;
    case 3: m4ReleaseSounds(); break;
    case 4: m5ReleaseSounds(); break;
    case 5: m6ReleaseSounds(); break;
    case 6: m7ReleaseSounds(); break;
    case 7: m8ReleaseSounds(); break;
    case 8: c1ReleaseSounds(); break;
    case 9: c2ReleaseSounds(); break;
    case 10: c3ReleaseSounds(); break;
    case 11: c4ReleaseSounds(); break;
    case 12: r2ReleaseSounds(); break;
    case 13: r1ReleaseSounds(); break;
    case 14: r3ReleaseSounds(); break;
    default: break;
    }
}

namespace {
constexpr std::array<std::string_view, 6> kPathVoicesA = {"db04115", "db9050", "db9051", "db04032", "db04040", "db04033"};
constexpr std::array<std::string_view, 8> kPathVoicesB = {"db04037", "db04152", "cow2",    "db04041",
                                                          "db0600",  "db0613",  "db04039", "db04042"};
} // namespace

void TrailNode::preloadPathVoices() {
    // 0x430830: GetSound(keep) of the ride comments.
    auto& snd = engine().sounds();
    for (auto v : kPathVoicesA)
        snd.getSound(v, true, false);
    snd.getSound(horseNameSound(), true, false);
    if (app().record().u32(PlayerRecord::kMissionDone + 5 * 4) != 0) // app+0x26C
        snd.getSound("db03066", true, false);
    for (auto v : kPathVoicesB)
        snd.getSound(v, true, false);
}

void TrailNode::releasePathVoices() {
    // 0x430990.
    auto& snd = engine().sounds();
    for (auto v : kPathVoicesA)
        snd.retain(v, false);
    snd.retain(horseNameSound(), false);
    if (app().record().u32(PlayerRecord::kMissionDone + 5 * 4) != 0)
        snd.retain("db03066", false);
    for (auto v : kPathVoicesB)
        snd.retain(v, false);
}

void TrailNode::updatePathCommentary() {
    // UpdatePathCommentary (0x430ad0): Barbie's remarks along the trails, once
    // each per map location (+0x650), by trail frame.
    if (!trailflags::dialogue)
        return;
    const auto f = static_cast<std::uint32_t>(trailFrame());
    auto& s = app().session();
    if (s.tutorialActive) {
        lessonRideTick();
        return;
    }
    auto& snd = engine().sounds();
    if (m_trail == static_cast<int>(s.missionTrail)) {
        if (mission() != 4 || f % 40 != 0)
            return;
        const int r = engine().rand() % 4;
        if (r == 0)
            snd.playSound("thunder", -1, false, false, 1, false, 0x70, -1, 0, false);
        else if (r == 1)
            snd.playSound("thunder2", -1, false, false, 1, false, 0x70, -1, 0, false);
        return;
    }
    if (f == 100 && s.missionTrail != 7 && mission() != 0xF && mission() != 5) {
        playVoiceNoFlaps("db04115");
        return;
    }
    if (mission() == 9 && !voicePlaying()) {
        bool hit = false;
        if (m_mapLocation == 2)
            hit = f == 0x45;
        else if (m_mapLocation == 3)
            hit = f == 0x19;
        else
            return;
        if (hit)
            playVoiceNoFlaps("db0516");
        return;
    }
    if (mission() == 2 || mission() == 9 || voicePlaying())
        return;
    int& count = m_commentCount[static_cast<std::size_t>(m_mapLocation)];
    const auto& win = trail::kDecision[static_cast<std::size_t>(m_trail)];
    switch (m_mapLocation) {
    case 0:
        if (count < 1) {
            if (f >= 0x128 && f <= 299) {
                playVoice("db04152");
                ++count;
            }
        } else if (count > 1) {
            if (count < 3) {
                if (f > win[4].lo + 25 && f < win[4].hi) {
                    playVoice("db04041");
                    ++count;
                }
            } else if (count == 3 && mission() != 8 && f >= 0x227 && f <= 599) {
                playVoice("db0600");
                ++count;
            }
        } else if (f >= 0x132 && f <= 0x135) {
            snd.playSound("cow2", -1, false, false, 1, false, 0x70, -1, 0, false);
            ++count;
        }
        return;
    case 1:
    case 6:
        if (count != 0 || f < (m_mapLocation == 1 ? 0x1Fu : 0x33u))
            return;
        if (f < (m_mapLocation == 1 ? 0x32u : 0x3Cu)) {
            playVoice("db04040");
            ++count;
        }
        return;
    case 2:
        if (count == 0) {
            if (win[1].lo < f && f < win[1].hi) {
                playVoice("db0613");
                ++count;
            }
        } else if (count == 1 && f > 200) {
            playVoice("db04039");
            ++count;
        }
        return;
    case 3:
        if (count == 0 && f > 0xCD) {
            playVoice("db04032");
            ++count;
        }
        return;
    case 4:
        if (count == 0) {
            if (f > 0xD2 && f < 0xDC) {
                playVoice("db04037");
                ++count;
            }
        } else if (count == 1 && f > 500 && f < 0x1FE) {
            sayHorseName();
            playVoice("db04033");
            ++count;
        }
        return;
    case 5:
        if (count == 0) {
            if (f > 0x96) {
                sayHorseName();
                playVoice("db04042");
                ++count;
            }
        } else if (count == 1 && f > 300) {
            std::string_view v = "db04032";
            if (engine().rand() % 2 == 0) {
                sayHorseName();
                v = "db04033";
            }
            playVoice(v);
            ++count;
        }
        return;
    default:
        return;
    }
}

// --- mission resources and palettes (0x431050–0x431420) -------------------------------------------

namespace {
std::string_view missionOmt(int mission) {
    switch (mission) {
    case 0: return "mission1.omt";
    case 2: return "mission3.omt";
    case 4: return "mission5.omt";
    case 5: return "mission6.omt";
    case 8: return "cert1.omt";
    default: return {};
    }
}
} // namespace

void TrailNode::openMissionResource() {
    // 0x431050.
    m_missionResource = mission();
    if (auto f = missionOmt(m_missionResource); !f.empty())
        engine().resources().open(f);
}

void TrailNode::closeMissionResource() {
    // 0x4310f0.
    if (auto f = missionOmt(m_missionResource); !f.empty())
        engine().resources().close(f);
}

void TrailNode::setOutsidePalette() {
    // SetOutsidePalette (0x431180): the node's own palette for the stop scene
    // (screen palette with DirectDraw, after WaitForVerticalBlank and a redraw
    // request; the shared Smacker world's palette otherwise).
    engine().setScreenPalette(m_outsidePalette);
}

std::array<std::uint32_t, 256> TrailNode::loadPalette(std::string_view name) {
    // LoadPalette (0x431200): a 768-byte RGB 'DATA' chunk (the .act files in
    // trails.omt); anything else gives a white palette.
    std::array<std::uint8_t, 0x300> raw;
    raw.fill(0xFF);
    auto& res = engine().resources();
    if (auto found = res.find(formats::FourCC("DATA"), name)) {
        const auto bytes = res.read(found);
        if (bytes.size() == raw.size())
            for (std::size_t i = 0; i < raw.size(); ++i)
                raw[i] = static_cast<std::uint8_t>(bytes[i]);
    }
    std::array<std::uint32_t, 256> pal{};
    for (std::size_t i = 0; i < 256; ++i)
        pal[i] = std::uint32_t{raw[3 * i]} << 16 | std::uint32_t{raw[3 * i + 1]} << 8 | raw[3 * i + 2];
    return pal;
}

void TrailNode::copyUpperPaletteEntries(std::array<std::uint32_t, 256>& pal) {
    // CopyUpperPaletteEntries (0x431380): the horse heads' colours, entries
    // 0xC0..0xFF of the horse palette (only 0xFE..0xFF after the yard's '?').
    for (std::size_t i = trailflags::narrowSpritePalette ? 0xFE : 0xC0; i < 0x100; ++i)
        pal[i] = m_horsePalette[i];
}

// --- construction ---------------------------------------------------------------------------------

TrailNode::TrailNode(Godiva& app, const std::string& name, int trailId) : ToolbarNode(app, name) {
    // TrailNode::TrailNode (0x431420).
    auto& rec = app.record();
    auto& s = app.session();
    m_pauseCopy = engine().modal(); // [0x80a438]
    // ([0x80a40c] = 1 only times anim creation in the log.)
    setCursor("cursorOut");
    edgeRightAlt = true; // +0xC8: the right edge means "continue"
    if (cursor())
        cursor()->hide(true);
    setSkin(kOut);
    for (auto [anim, id] : std::initializer_list<std::pair<std::string_view, int>>{
             {"tbQuitOut", kQuit}, {"tbPagerOut", kPager}, {"tbCameraOut", kCamera}, {"tbMapOut", kMap},
             {"tbBagOut", kBag}, {"tbHelpOut", kHelp}, {"tbHomeOut", kHome}})
        if (RolloverButton* b = addToolbarButton(anim, id))
            b->setEnabled(false);

    // The presentation switches (movies.md §4.1), unless the yard's debug
    // mode set them by hand.
    if (!s.debugMode) {
        trailflags::horseHeads = true;
        const bool low = s.lowEnd != 0;
        trailflags::music = !low;
        trailflags::ambient = true;
        trailflags::dialogue = true;
        trailflags::backups = true;
        trailflags::butterflies = !low;
        trailflags::smallMovies = low;
        trailflags::programmaticLetterbox = low;
        trailflags::videoLetterbox = low;
        trailflags::frameRate = low ? 12 : 14;
    }
    if (trailflags::videoLetterbox)
        trailflags::programmaticLetterbox = true;

    auto& res = engine().resources();
    if (!res.isOpen("trails.omt"))
        res.open("trails.omt");
    if (!res.isOpen("trlAnims.omt"))
        res.open("trlAnims.omt");
    engine().anims().preloadCache("trlAnims.omt.cache");
    if (rec.horse() == PlayerRecord::kNoHorse)
        rec.setU32(PlayerRecord::kHorse, 0);
    if (rec.u32(PlayerRecord::kSaddle) == 2)
        rec.setU32(PlayerRecord::kSaddle, 0);
    m_fancy = rec.u32(PlayerRecord::kSaddle) == 1;
    const auto horse = horseIndex(rec);
    if (!res.isOpen(trail::kTrailOmt[horse]))
        res.open(trail::kTrailOmt[horse]);
    engine().anims().preloadCache(std::format("{}.cache", trail::kTrailOmt[horse]));

    m_missionResource = 0x11;
    openMissionResource();
    if (rec.u16(PlayerRecord::kFirstRideDone) == 0) {
        // The first ride is the riding lesson: the mission is put aside and
        // restored when the lesson ends (or the node is left).
        s.savedMission = s.activeMission;
        s.savedMissionTrail = s.missionTrail;
        s.activeMission = 0x10;
        s.missionTrail = 4;
        s.tutorialActive = 1;
        rec.setU16(PlayerRecord::kFirstRideDone, 1);
        app.saveGame();
        lessonLoadSounds();
    }
    m_pagerBusy = false;
    loadMissionVoices();
    // The world's background becomes an empty one (0x45cd30 with vtable
    // 0x7f11a0): the trail movie covers the screen.
    setBackground(nullptr);
    m_mountAnimsPending = true;
    m_butterflies = trailflags::butterflies;
    // GlobalMemoryStatus: more than 18,000,000 bytes of RAM allow the ambient,
    // walk and tender-moment anims. Always true on a machine that runs this.
    m_highMemory = true;
    m_field474 = true;
    m_tenderMomentAfterAmb = false;
    m_ambientPreemptToggle = true;
    m_trail = m_nextTrail = trailId;
    m_restartSameTrail = false;
    m_mapLocation = trail::kMapLocation[static_cast<std::size_t>(m_trail)];
    m_startFrame = 0;
    m_startFramePreset = false;
    m_leftReleased = m_rightReleased = true;
    m_keySlower = m_keyFaster = m_keyLeft = m_keyRight = false;
    m_faceCheck = false;
    m_inObstacle = false;
    m_jumping = false;
    m_walkDisabled = false;
    m_decisionAnim = nullptr;
    m_tutorialFinished = false;
    m_gaitPlaying = false;
    setGait(0);
    m_stream = m_falls = nullptr;
    m_skipStopComment = false;
    m_rideHintPlayed = false;
    m_commentCount = {};
    m_riding = true;
    m_rideOffAfterMount = m_mountAfterWalk = m_runOnAfterRunOff = false;
    m_pagerDelay = 0;
    m_hoofPrintsShown = {};
    m_field74C = {};
    m_db03066Played = false;
    m_dropInsertOnRestart = false;
    m_deerVisible = m_deerDone = false;
    m_musicId = 0;
    m_ambientIndex = 5;
    m_ambientLoop.clear();
    // (+0x70C = app+0x314 and the other mission fields belong to the mission code.)
    for (std::size_t i = 1; i < trail::kAmbientLoop.size(); ++i)
        engine().sounds().getSound(trail::kAmbientLoop[i], true, false);

    initTrail();
    loadGaitSounds();
    engine().sounds().getSound("whinney", true, false);

    // The mount anims exist from the start, hidden (their pointers are only
    // kept from the first stop on, LoadMountAnims).
    m_walk = m_barbieAmbients = m_bMountRun = nullptr;
    createHiddenAnim("bMountRun");
    m_hMountRun = nullptr;
    createHiddenAnim(trail::kMountRunH[horse]);
    m_fancyMountRun = nullptr;
    if (m_fancy)
        createHiddenAnim("fancyMountRun");
    m_ambientFancy = m_ambientHorse = m_ambientBarbie = nullptr;
    m_subtleAmbHorse = m_generalAmbHorse = m_subtleAmbFancy = nullptr;
    m_subtleAmbSdl = m_generalAmbSdl = m_generalAmbFancy = m_saddleAndReigns = nullptr;
    if (m_highMemory) {
        createHiddenAnim("ambientBarbie");
        createHiddenAnim(trail::kAmbientH[horse]);
        if (m_fancy)
            createHiddenAnim("ambientFancy");
    }
    m_fTMoment = m_sTMoment = m_hTMoment = m_bTMoment = nullptr;
    m_horseState = kNoState;
    setHorseState(kHideAll);

    listenToMouseButtons();                    // [0x80a5a0]+0xC
    engine().keyListeners().addListener(this); // [0x80a5a0]+0x18
    app.idleTimer().stop();
    runScript("preload");
}

void TrailNode::start() {
    // Start (0x431f80): the hooves, unless the node opens on a stop.
    const auto& s = app().session();
    if (!s.arriveFromMiniGame && !s.arriveFromRace)
        playGaitSound();
}

TrailNode::~TrailNode() {
    // ~TrailNode (0x431fa0).
    // (Renderer back to dirty rectangles, present mode 1.)
    auto& snd = engine().sounds();
    auto& s = app().session();
    snd.retain("falls", false);
    snd.retain("stream", false);
    // Whether Barbie left on horseback (the yard and stable read it).
    s.wasRiding = m_riding ? 1 : (m_mounted ? 1 : 0);
    clearStopProps();
    if (mission() == 0xB)
        c4UnloadTrail();
    m_riding = false;
    if (m_current) {
        m_current->paused = true;
        m_current->gotoFrame(1);
        m_current->done = false;
    }
    unloadBackupMovies();
    m_current = nullptr;
    m_insert.reset();
    // (SmackSummary of the trail movie, discarded.)
    m_base.reset();
    stopGaitSound();
    m_ambientLoop.clear(); // +0x4B8 = 0 (still playing; stopped below only if set)
    for (std::size_t i = 1; i < trail::kAmbientLoop.size(); ++i)
        snd.retain(trail::kAmbientLoop[i], false);
    releaseGaitSounds();
    releaseMissionVoices();
    lessonReleaseSounds();
    if (m_decisionAnim)
        deleteAnim(m_decisionAnim);
    if (!s.keepTrailResources) {
        auto& anims = engine().anims();
        for (auto n : {"ambientFancy", "fancyMountRun", "SaddleAndReigns", "ambientBarbie", "bMountRun", "ambientPal",
                       "PalMountRun", "ambientWht", "WhtMountRun", "ambientPnt", "PntMountRun", "ambientDpl",
                       "DplMountRun"})
            anims.remove(n);
        snd.retain("whinney", false);
        auto& res = engine().resources();
        res.close("trails.omt");
        res.close("trlAnims.omt");
        res.close(trail::kTrailOmt[horseIndex(app().record())]);
    }
    closeMissionResource();
    // (The palettes are freed with the node. The ambient loop name was
    // cleared above, so its stop here never happens: the node switch stops it.)
    if (s.activeMission == 0x10) {
        s.activeMission = s.savedMission;
        s.missionTrail = s.savedMissionTrail;
    }
    s.tutorialActive = 0;
    m_preloadQueue.clear();
    engine().mouseListeners().removeListener(this);
    engine().keyListeners().removeListener(this);
}

// --- input ------------------------------------------------------------------------------------------

void TrailNode::onPress(const engine::Pick& pick) {
    // MouseDown (0x432600): ignored while riding; otherwise by the clicked
    // anim's id.
    if (m_riding)
        return;
    const int id = pick.element ? pick.element->clickId() : 0;
    auto& rec = app().record();
    auto& snd = engine().sounds();
    auto sfx = [&](std::string_view name) { snd.playSound(name, -1, false, false, 1, false, 0x70, -1, 0, false); };
    if (id > kHelpClickId) {
        if (id == 0x36B9)
            m6MouseDown(pick);
        else
            ToolbarNode::onPress(pick);
        return;
    }
    if (id == kHelpClickId) {
        playVoice("db02001");
        return;
    }
    switch (id) {
    case kHorseClick:
        toggleMount();
        return;
    case kExitPropClick:
        if (mission() == 4)
            m5MouseDown(pick);
        else if (mission() == 3 || (rec.u16(PlayerRecord::kCottageGateOpen) != 0 && m_trail == 1))
            m4MouseDown(pick);
        else if (mission() == 7)
            m8MouseDown(pick);
        if (!m_mounted && m_highMemory) {
            if (isScriptRunning("dismount"))
                runScript("HorseAmb");
            runScript("WalkBack");
            return;
        }
        app().gotoNode(nodeName(m_exitPropNode));
        return;
    case kRoadSignClick:
        if (m_trail == 0) {
            if (!isScriptRunning("runRight")) {
                m_nextTrail = 4;
                stopAllScripts();
                runScript("RunRight");
            }
        } else if (m_trail == 4) {
            if (pick.element == m_signB) {
                if (!isScriptRunning("runRight")) {
                    stopAllScripts();
                    m_nextTrail = 2;
                    runScript("RunRight");
                }
            } else if (!m_mounted) {
                mountAndRide();
            } else if (!isScriptRunning("runOff")) {
                stopAllScripts();
                runScript("runOff");
            }
        }
        return;
    case kMailboxClick:
        if (mission() == 0) {
            m1MouseDown(pick);
            return;
        }
        if (!m_mailbox)
            return;
        if (m_mailbox->sequence() == 0) {
            m_mailbox->setSequence(1, true);
            sfx("mailOpen");
        } else {
            m_mailbox->setSequence(0, true);
            sfx("mailClos");
        }
        return;
    case kPickupClick:
        if (m_pickup) {
            m_pickup->setSequence(0, true);
            m_pickup->hide(true);
            m_pickup = nullptr;
        }
        if (mission() == 0)
            m1MouseDown(pick);
        if (mission() == 5)
            m6MouseDown(pick);
        if (mission() == 8)
            c1MouseDown(pick);
        if (mission() == 9)
            c2MouseDown(pick);
        addBagItem(m_pickupItem);
        return;
    case kCertificateClick:
        playVoice("db0197");
        deleteAnim(m_certificate);
        if (m_certificateFont) {
            m_certificateText.reset();
            m_certificateFont.reset();
            m_pagerDelay = 8000;
            if (!checkAllMissionsDone())
                runScript("PagerDelay");
            if (mission() == 0xB) {
                playVoiceAfterPause("db0198");
                app().session().activeMission = 0x11;
            }
        }
        return;
    case kCritterClick:
        critterClicked();
        return;
    case kOwlClick:
        if (mission() == 0)
            m1MouseDown(pick);
        if (m_owl)
            m_owl->setSequence(1, true);
        m_owlRollover.reset();
        sfx("owl");
        sfx("owlfly");
        return;
    case kCowClick:
        sfx("cow2");
        return;
    case 0x2719:
    case 0x271A:
        m1MouseDown(pick);
        return;
    case 0x271B:
        m2MouseDown(pick);
        return;
    case 0x271C:
    case 0x271D:
        m4MouseDown(pick);
        return;
    case 0x271E:
        rec.setU32(PlayerRecord::kFinaleState, 2);
        app().gotoNode("finalpayoff");
        ToolbarNode::onPress(pick);
        return;
    case kBushClick:
        if (rec.u16(PlayerRecord::kWellPathOpen) != 0 && mission() != 5) {
            if (!m_bush)
                return;
            if (m_bush->sequence() == 0) {
                m_bush->setSequence(1, true);
                return;
            }
            if (m_mounted) {
                if (!isScriptRunning("RunLeft")) {
                    m_nextTrail = 6;
                    stopAllScripts();
                    runScript("RunLeft");
                }
                return;
            }
            m_bush->setSequence(0, true);
            return;
        }
        [[fallthrough]];
    case 0x2720:
    case 0x2721:
    case 0x2722:
        m6MouseDown(pick);
        return;
    case 0x2723:
        m7MouseDown(pick);
        return;
    case 0x2724:
        c2MouseDown(pick);
        ToolbarNode::onPress(pick);
        return;
    case 0x2725:
        c3MouseDown(pick);
        ToolbarNode::onPress(pick);
        return;
    case 0x2726:
        r2MouseDown(pick);
        ToolbarNode::onPress(pick);
        return;
    case 0x2727:
    case 0x2728:
        r1MouseDown(pick);
        return;
    case 0x2729:
        r3MouseDown(pick);
        return;
    default:
        ToolbarNode::onPress(pick);
        return;
    }
}

// --- stops (0x432c90–0x434caf) ------------------------------------------------------------------------

void TrailNode::horseRestart() {
    // HorseRestart (0x432c90): leave a stop, on the same trail or onto the
    // one chosen there.
    log::debug("TrailNode::HorseRestart()");
    stopVoice();
    if (saddlebag())
        saddlebag()->stopBlink();
    if (pager())
        pager()->stopBlink();
    closePopup();
    if (exitDialogShowing())
        closeExitDialog();
    clearStopProps();
    if (m_decisionAnim)
        deleteAnim(m_decisionAnim);
    showToolbar(false, false);
    if (m_trail == m_nextTrail && !m_restartSameTrail) {
        m_current->applyPaletteOnce = true;
    } else {
        if (!m_startFramePreset)
            m_startFrame = startFrame();
        else
            m_startFramePreset = false;
        switchTrail();
        m_current->paused = false;
        SmackOverlayMovie::clearWorld();
        m_current->spendTime();
    }
    if (m_dropInsertOnRestart) {
        m_dropInsertOnRestart = false;
        m_current = m_base.get();
        m_insert.reset();
    }
    // The developers' leak checks: eleven HCRolloverButtons (the toolbar's
    // seven and the four of the exit dialog's owner) and no ACRollover.
    if (Rollover::liveCount() != 0xB)
        log::debug("*** ERROR *** TrailNode::HorseRestart -- you have {} rollovers", Rollover::liveCount());
    if (ACRollover::liveCount() != 0)
        log::debug("*** ERROR *** TrailNode::HorseRestart -- you have {} AC rollovers", ACRollover::liveCount());
    m_current->paused = false;
    m_riding = true;
    // (WaitForVerticalBlank, redraw request.)
    if (m_base && m_base->palette)
        engine().setScreenPalette(*m_base->palette);
    m_current->fps = 4;
    setGait(0);
    playGaitSound();
    // (Renderer to full redraws, present mode 0.)
}

void TrailNode::horseStop() {
    // HorseStop (0x432f30): stop at the current frame, find the decision and
    // set the stop up.
    if (!m_riding)
        return;
    m_wellRollover.reset(); // +0x768 = 0
    stopVoice();
    // (Renderer to dirty rectangles, present mode 1.)
    if (m_mountAnimsPending)
        loadMountAnims();
    m_mountAnimsPending = false;
    m_lastInput = engine().ticks();
    m_riding = false;
    m_current->paused = true;
    stopGaitSound();

    auto& s = app().session();
    auto& rec = app().record();
    bool normalStop = true;
    auto prop = [&](std::string_view name, int clickId, std::unique_ptr<ACRollover>* rollover, int cursorSeq = 7) {
        engine::Anim* a = createAnimAtViewport(name);
        if (a) {
            if (clickId)
                a->setClickId(clickId);
            if (rollover)
                *rollover = std::make_unique<ACRollover>(*this, a, cursorSeq);
        }
        return a;
    };
    auto openRequestedPopup = [&] {
        // A popup the player asked for in the mini-game (app+0x1AC).
        if (app().popupRequest == 1 || app().popupRequest == 3 || app().popupRequest == 4) {
            openPopup(static_cast<int>(app().popupRequest));
            app().popupRequest = 0;
        }
    };

    if (!s.arriveFromMiniGame) {
        if (!s.arriveFromRace) {
            const auto f = static_cast<std::uint32_t>(trailFrame());
            m_decision = 5;
            const auto& win = trail::kDecision[static_cast<std::size_t>(m_trail)];
            for (int i = 0; i < 5; ++i) {
                if (win[static_cast<std::size_t>(i)].lo > f || f > win[static_cast<std::size_t>(i)].hi)
                    continue;
                if (m_trail == 3) {
                    if (i == 0 && mission() == 0xC && f > 0x5A)
                        break;
                    if (i == 0 && mission() != 7) {
                        m_decision = 0;
                        showDecisionAnim("decision1");
                        break;
                    }
                }
                // Overlapping windows each show their anim (the earlier one stays).
                m_decision = i;
                showDecisionAnim(std::format("decision{}", i));
            }
            switch (m_trail) {
            case 0:
                if (m_decision == 0) {
                    m_signA = prop("aat rr sign", kRoadSignClick, &m_signARollover);
                } else if (m_decision == 2) {
                    m_owl = prop("owl", kOwlClick, mission() == 9 ? nullptr : &m_owlRollover);
                    if (mission() == 9)
                        m_owlRollover.reset();
                } else if (m_decision == 3) {
                    createAnimAtViewport("cow cud");
                    m_cow = prop("sandees cow", kCowClick, &m_cowRollover);
                    engine().sounds().playSound("cowchew", -1, false, false, 0, false, 0x70, -1, 0, false);
                }
                break;
            case 1:
                if (m_decision == 5)
                    createAnimAtViewport("cottageGrass");
                break;
            case 2: {
                std::string_view name;
                if (m_decision == 0) {
                    if (f < 0x3A || f > 99) {
                        name = "bell grass";
                    } else {
                        m_decision = 5;
                        deleteDecisionAnim();
                        name = "obt ggrass0";
                    }
                } else if (m_decision == 1) {
                    name = "bridge rail";
                } else if (m_decision == 2) {
                    name = "obt dgrass2";
                } else if (m_decision == 5) {
                    name = "obt ggrass0";
                }
                if (!name.empty())
                    createAnimAtViewport(name);
                break;
            }
            case 4:
                if (m_decision == 0) {
                    m_mailbox = prop("mailbox", kMailboxClick, &m_mailboxRollover);
                } else if (m_decision == 1) {
                    m_bush = createAnimAtViewport("m6 bush");
                    if (m_bush && rec.u16(PlayerRecord::kWellPathOpen) != 0 &&
                        (mission() != 5 || s.missionTrail != 4)) {
                        m_bush->setSequence(2, true);
                        m_bush->setClickId(kBushClick);
                        m_bushRollover = std::make_unique<ACRollover>(*this, m_bush, 8);
                    }
                } else if (m_decision == 3) {
                    m_signA = prop("rrt rr sign", kRoadSignClick, &m_signARollover);
                    m_signB = prop("rrt ob sign", kRoadSignClick, &m_signBRollover);
                } else if (m_decision == 4) {
                    m_bush = createAnimAtViewport("m4 bush");
                    if (m_bush && rec.u16(PlayerRecord::kCottageGateOpen) != 0) {
                        m_bush->setSequence(1, true);
                        m_gate = prop("m4 gate", 0x271D, &m_gateRollover, 8);
                    }
                } else if (m_decision == 5 && f > 0x3F0) {
                    m_decision = 2;
                    showDecisionAnim("decision2");
                }
                break;
            case 6:
            case 7:
                if (m_decision == 0) {
                    m_well = createAnimAtViewport("wishing well");
                    if (m_well) {
                        if (hasBagItem(0x17))
                            m_well->setSequence(3, true);
                        m_well->setClickId(kWellClick);
                        m_wellRollover = std::make_unique<ACRollover>(*this, m_well, 7);
                    }
                } else if (m_decision == 5) {
                    createAnimAtViewport("cottageGrass");
                }
                break;
            default:
                break;
            }
        } else {
            // Back from a race: the stop by the start line.
            s.arriveFromRace = 0;
            if (m_trail == 4) {
                const bool finished = s.raceFinished != 0;
                m_base->gotoFrame(finished ? 0x1CC : 100);
                m_decision = finished ? 2 : 0;
                showDecisionAnim(finished ? "decision2" : "decision0");
            } else if (m_trail == 5) {
                const bool finished = s.raceFinished != 0;
                m_base->gotoFrame(finished ? 0x1D2 : 0x5A);
                m_decision = finished ? 1 : 0;
                showDecisionAnim(finished ? "decision1" : "decision0");
            }
        }
    } else {
        // Back from a mini-game: at its entrance.
        s.arriveFromMiniGame = 0;
        normalStop = false;
        if (mission() == 4 && m_trail == 0) {
            m_field74C = {true, true, true, true};
            m_base->gotoFrame(0x264);
            m_decision = 1;
            m_jumping = true; // +0x3EC (as coded)
        } else if (m_trail == 1) {
            m_base->gotoFrame(0x5F);
            m_decision = 0;
            showDecisionAnim("decision0");
            m_pagerDelay = 7000;
            if (rec.u32(PlayerRecord::kMissionDone + 3 * 4) != 0 && !checkAllMissionsDone()) // app+0x264
                runScript("PagerDelay");
        } else if (m_trail == 2) {
            if (mission() == 1 || trailflags::bridgeArrival) {
                m_base->gotoFrame(0x96);
                m_decision = 1;
                showDecisionAnim("decision1");
                createAnimAtViewport("bridge rail");
                openRequestedPopup();
            } else if (mission() == 6) {
                m_base->gotoFrame(0x73);
                m_decision = 0;
                showDecisionAnim("decision0");
                if (!checkAllMissionsDone())
                    choosePagerMessage();
            }
        } else if (m_trail == 3) {
            m_base->gotoFrame(0x41);
            m_decision = 0;
            showDecisionAnim("decision0");
            if (!checkAllMissionsDone())
                choosePagerMessage();
            openRequestedPopup();
        }
    }

    trailflags::bridgeArrival = false;
    if (m_decision == 5)
        showDecisionAnim("generic0");
    showToolbar(true, false);
    if (!m_insert && !flags::appleSortWon)
        spawnCritter();
    runScript(normalStop ? "runOn" : (m_highMemory ? "Ambient" : "LookLeft"));
    m_stopMission = mission();
    m2SetupStop();
    m4SetupStop();
    switch (mission()) {
    case 0: m1SetupStop(); break;
    case 2: m3SetupStop(); break;
    case 4: m5SetupStop(); break;
    case 5: m6SetupStop(); break;
    case 6: m7SetupStop(); break;
    case 7: m8SetupStop(); break;
    case 8: c1SetupStop(); break;
    case 9: c2SetupStop(); break;
    case 10: c3SetupStop(); break;
    case 11: c4SetupStop(); break;
    case 12: r2SetupStop(); break;
    case 13: r1SetupStop(); break;
    case 14: r3SetupStop(); break;
    default: break;
    }
    if (!voicePlaying() && !delayedVoicePending()) {
        sayStopComment();
        sayCritterComment();
    }
    m_mounted = true;
    edgeRightAlt = true;
    setOutsidePalette();
    updateMapLocation();
}

void TrailNode::updateMapLocation() {
    // UpdateMapLocation (0x433c10): the map's "you are here" spot.
    const auto f = static_cast<std::uint32_t>(trailFrame());
    const auto& w = trail::kDecision[static_cast<std::size_t>(m_trail)];
    auto& map = app().map();
    switch (m_mapLocation) {
    case 0:
        map.setYouAreHere(f < w[2].lo ? 5 : f < w[4].lo ? 6 : f < w[4].hi ? 7 : f >= w[1].hi ? 9 : 8);
        break;
    case 1:
        map.setYouAreHere(0xF);
        break;
    case 2:
        map.setYouAreHere(f < w[1].lo ? 0x13 : f >= w[1].hi ? 0x15 : 0x14);
        break;
    case 3:
        if (f < w[0].hi)
            map.setYouAreHere(0x10);
        else if (f < w[1].hi)
            map.setYouAreHere(0x12);
        break;
    case 4:
        map.setYouAreHere(f < w[0].lo ? 10 : f < w[1].hi ? 11 : f < w[2].hi ? 13 : f > 0x446 ? 11 : 14);
        break;
    case 5:
        map.setYouAreHere(f < w[0].lo ? 0 : f < w[0].hi ? 1 : f < w[1].lo ? 2 : f >= w[1].hi ? 4 : 3);
        break;
    case 6:
        map.setYouAreHere(0xC);
        break;
    default:
        break;
    }
}

void TrailNode::showDecisionAnim(std::string_view name) {
    // ShowDecisionAnim (0x433eb0): "<prefix> <name>" with the prefix's third
    // letter replaced by 't' ("aat decision0", "rrt generic0", ...).
    std::string anim = std::format("{} {}", trail::kPrefix[static_cast<std::size_t>(m_trail)], name);
    anim[2] = 't';
    m_decisionAnim = createAnimAtViewport(anim);
    if (!m_decisionAnim)
        log::debug("{} anim not found", anim);
}

void TrailNode::deleteDecisionAnim() {
    // 0x433fd0.
    if (m_decisionAnim)
        deleteAnim(m_decisionAnim);
}

void TrailNode::clearStopProps() {
    // ClearStopProps (0x434000): the props of one stop (the first group found
    // wins), then the certificate, the item to pick up, the critter and the
    // missions' cleanup.
    auto& anims = engine().anims();
    auto has = [&](std::string_view n) { return anims.find(n) != nullptr; };
    if (has("cow cud")) {
        anims.remove("cow cud");
        anims.remove("sandees cow");
        m_cow = nullptr;
        m_cowRollover.reset();
        engine().sounds().stopSound("cowchew");
    } else if (has("aat rr sign")) {
        anims.remove("aat rr sign");
        m_signA = nullptr;
        m_signARollover.reset();
    } else if (has("owl")) {
        anims.remove("owl");
        m_owl = nullptr;
        m_owlRollover.reset();
    } else if (has("m4 bush")) {
        anims.remove("m4 bush");
        anims.remove("m4 gate");
        anims.remove("m4 sparkle");
        m_bush = m_gate = nullptr;
        m_gateRollover.reset();
        m_bushRollover.reset();
    } else if (has("cottageGrass")) {
        anims.remove("cottageGrass");
    } else if (has("bell grass")) {
        anims.remove("bell grass");
    } else if (has("bridge rail")) {
        anims.remove("bridge rail");
    } else if (has("obt dgrass2")) {
        anims.remove("obt dgrass2");
    } else if (has("obt ggrass0")) {
        anims.remove("obt ggrass0");
    } else if (has("M7 bell")) {
        m_bellRollover.reset();
        anims.remove("m7 bell");
        anims.remove("M7 nobell");
        m_bellAnim = nullptr;
    } else if (has("mailbox")) {
        m_mailboxRollover.reset();
        anims.remove("mailbox");
        m_mailbox = nullptr;
    } else if (has("rrt rr sign")) {
        anims.remove("rrt rr sign");
        m_signARollover.reset();
        anims.remove("rrt ob sign");
        m_signBRollover.reset();
        m_signA = m_signB = nullptr;
    } else if (has("m6 bush")) {
        if (m_bush)
            m_bush->hide(true);
        anims.remove("m6 bush");
        m_bush = nullptr;
        m_bushRollover.reset();
    } else if (has("wishing well")) {
        if (cursor() && cursor() == anims.find("m6 coin cursor")) {
            setCursor("cursorOut");
            edgeRightAlt = true;
        }
        m_wellRollover.reset();
        anims.remove("wishing well");
        anims.remove("m6 coin toss");
        anims.remove("m6 coin cursor");
        m_well = nullptr;
    }
    if (m_certificate)
        deleteAnim(m_certificate);
    if (m_certificateFont) {
        m_certificateText.reset();
        m_certificateFont.reset();
    }
    if (m_pickup) {
        if (m_pickupItem == 0xF)
            engine().sounds().stopSound("musicbox");
        deleteAnim(m_pickup);
        addBagItem(m_pickupItem);
    }
    removeCritter();
    m2CleanupStop();
    m4CleanupStop();
    switch (m_stopMission) {
    case 0: m1CleanupStop(); break;
    case 2: m3CleanupStop(); break;
    case 4: m5CleanupStop(); break;
    case 5: m6CleanupStop(); break;
    case 6: m7CleanupStop(); break;
    case 7: m8CleanupStop(); break;
    case 8: c1CleanupStop(); break;
    case 9: c2CleanupStop(); break;
    case 10: c3CleanupStop(); break;
    case 12: r2CleanupStop(); break;
    case 13: r1CleanupStop(); break;
    case 14: r3CleanupStop(); break;
    default: break;
    }
}

void TrailNode::sayStopComment() {
    // SayStopComment (0x4344c0).
    if (app().session().tutorialActive)
        return;
    if (m_skipStopComment) {
        m_skipStopComment = false;
        return;
    }
    if (m_decision == 5)
        return;
    if (m_trail == 0) {
        if (m_decision == 4 || m_decision == 1) {
            playVoiceAfterPause("db0599");
            return;
        }
        if (m_decision == 3) {
            delayVoice("db04153", 1500);
            engine().sounds().playSound("cow2", -1, false, false, 1, false, 0x70, -1, 0, false);
            return;
        }
    }
    if (m_trail == 1 && m_decision == 0 && app().record().u16(PlayerRecord::kQuiltViewed) != 0) {
        delayVoice("db0090", 1000);
        return;
    }
    if (m_trail == 4 && m_decision == 0) {
        playVoiceAfterPause("db0009");
        return;
    }
    if (m_trail == 3 && m_decision == 0) {
        playVoice("db0623");
        return;
    }
    const int r = engine().rand() % 3;
    if (r == 0) {
        playVoiceAfterPause("db0519");
        return;
    }
    if (r == 1 && mission() != 0x11) {
        playVoiceAfterPause("db0018");
        return;
    }
    playVoiceAfterPause("db0516");
}

void TrailNode::showCertificateName() {
    // ShowCertificateName (0x434610, the same as the yard's 0x43e930): the
    // player's name on the certificate, font 18, centred at y 270, at most 300
    // pixels wide, just above the certificate.
    if (!m_certificate)
        return;
    m_certificateFont = engine::Font::bureau18(engine().resources(), palette());
    if (!m_certificateFont)
        return;
    m_certificateText = std::make_unique<engine::TextElement>(m_certificateFont);
    m_certificateText->link(this);
    m_certificateText->setText(app().record().name());
    const engine::Image8* img = m_certificateText->image();
    const int w = std::min(img ? img->width : 0, 300);
    m_certificateText->setPosition(static_cast<float>(320 - w / 2), 270.0f, m_certificate->z() + 3.0f);
}

void TrailNode::createHiddenAnim(std::string_view name) {
    // 0x434930.
    if (engine::Anim* a = createAnimAtViewport(name))
        a->hide(true);
}

engine::Anim* TrailNode::findOrCreateHiddenAnim(std::string_view name) {
    // 0x434950.
    engine::Anim* a = engine().anims().find(name);
    if (!a) {
        a = createAnimAtViewport(name);
        if (a)
            a->hide(true);
    }
    return a;
}

void TrailNode::loadMountAnims() {
    // LoadMountAnims (0x434990), at the first stop.
    stopScript("preload");
    const auto horse = horseIndex(app().record());
    m_bMountRun = createAnimAtViewport("bMountRun");
    m_hMountRun = createAnimAtViewport(trail::kMountRunH[horse]);
    if (m_hMountRun)
        m_hMountRun->setSequence(0, true);
    if (m_bMountRun)
        m_bMountRun->setClickId(kHorseClick);
    if (m_hMountRun)
        m_hMountRun->setClickId(kHorseClick);
    m_fancyMountRun = m_fancy ? createAnimAtViewport("fancyMountRun") : nullptr;
    if (!m_highMemory) {
        m_ambientFancy = m_ambientHorse = m_ambientBarbie = nullptr;
    } else {
        m_ambientBarbie = createAnimAtViewport("ambientBarbie");
        m_ambientHorse = createAnimAtViewport(trail::kAmbientH[horse]);
        if (m_ambientBarbie)
            m_ambientBarbie->setClickId(kHorseClick);
        if (m_ambientHorse)
            m_ambientHorse->setClickId(kHorseClick);
        m_ambientFancy = m_fancy ? createAnimAtViewport("ambientFancy") : nullptr;
        if (m_ambientFancy)
            m_ambientFancy->setClickId(kHorseClick);
    }
    setHorseState(kNoState);
    setHorseState(kHideAll);
}

void TrailNode::loadWalkAnims() {
    // LoadWalkAnims (0x434ad0), after dismounting.
    const auto horse = horseIndex(app().record());
    m_walk = createAnimAtViewport("barbieWalkOutside");
    forceHideAnim(m_walk);
    if (!m_highMemory) {
        m_barbieAmbients = nullptr;
        m_saddleAndReigns = createAnimAtViewport("SaddleAndReigns");
        if (m_saddleAndReigns)
            m_saddleAndReigns->setClickId(kHorseClick);
        forceHideAnim(m_saddleAndReigns);
    } else {
        m_barbieAmbients = createAnimAtViewport("barbie ambients");
        if (!m_fancy) {
            m_subtleAmbFancy = m_generalAmbFancy = nullptr;
        } else {
            m_generalAmbFancy = createAnimAtViewport("GeneralAmbFancy");
            m_subtleAmbFancy = createAnimAtViewport("SubtleAmbFancy");
            if (m_generalAmbFancy)
                m_generalAmbFancy->setClickId(kHorseClick);
            if (m_subtleAmbFancy)
                m_subtleAmbFancy->setClickId(kHorseClick);
        }
    }
    if (m_highMemory) {
        m_generalAmbHorse = createAnimAtViewport(trail::kGeneralAmbH[horse]);
        m_subtleAmbHorse = createAnimAtViewport(trail::kSubtleAmbH[horse]);
        m_generalAmbSdl = createAnimAtViewport("GeneralAmbSdl");
        m_subtleAmbSdl = createAnimAtViewport("SubtleAmbSdl");
        for (engine::Anim* a : {m_generalAmbHorse, m_subtleAmbHorse, m_generalAmbSdl, m_subtleAmbSdl})
            if (a)
                a->setClickId(kHorseClick);
        return;
    }
    m_subtleAmbHorse = m_generalAmbHorse = m_subtleAmbSdl = m_generalAmbSdl = nullptr;
    m_subtleAmbFancy = m_generalAmbFancy = nullptr;
}

} // namespace brc::game
