// The horse race (race.cpp 0x422d00–0x4269df, class Race, ctor 0x422d00):
// Barbie against Christie (the rabbit run, app+0x308 == 1) or Teresa (the
// beach, app+0x308 == 2) over 20 screens. Steering by the mouse buttons, the
// keyboard or (not settable in the preferences) mouse motion; jumps over a
// stream or rocks; a rubber-band rival; win/loss bookkeeping and a save when
// the node ends. See docs/engine/nodes.md "Race".
//
// The original attaches the rider, the fancy tack and Barbie-at-the-start as
// child elements of the horse, re-placed every SpendTime so that their
// absolute position is the horse's (position − frame offset, then + the
// parent's frame offset). This port's elements are flat: they get the horse's
// position and speed, and a depth just above it (children draw right after
// their parent, display.md §3.3).
#include "core/Log.h"
#include "core/StringUtil.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "engine/Scripts.h"
#include "engine/SoundManager.h"
#include "game/Popups.h"
#include "game/ToolbarNode.h"
#include "game/Widgets.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>

namespace brc::game {
namespace {

// Click id of the start flag (anim +0x11c).
constexpr int kStartFlagClick = 10000;

// Per horse (record horse: 0 palomino, 1 white, 2 paint, 3 dapple).
constexpr std::array<std::string_view, 4> kHorseAnims = {"race palomino", "race white", "racePnt",
                                                         "race dapple"}; // 0x8014c0
constexpr std::array<std::string_view, 4> kHorseOmt = {"racePal.omt", "raceWht.omt", "racePnt.omt",
                                                       "raceDpl.omt"};
constexpr std::array<std::string_view, 4> kStartSounds = {"racepalo", "racewhit", "racepaint",
                                                          "racedap"}; // 0x801590
// The rivals (+0x200: 0 Christie, 1 Teresa), 0x8014d0.
constexpr std::array<std::string_view, 2> kRivals = {"raceChristie", "raceTeresa"};
// The countdown lines (0x8014d8) and how long after each the start scripts run (0x7f31f0).
constexpr std::array<std::string_view, 3> kCountdown = {"db0486", "db0487", "db0488"};
constexpr std::array<std::uint32_t, 3> kStartDelays = {0, 700, 1200};

// Per course (+0x1FC: 0 beach, 1 rabbit run).
constexpr std::array<std::string_view, 2> kAmbient = {"xtrr001", "xrrt001"}; // 0x8015a0
struct CourseTiles {
    std::string_view start, finish, generic0, generic1, generic2;
};
constexpr std::array<CourseTiles, 2> kTiles = {
    {{"arch", "turtle rock", "generic0 race", "generic1 race", ""},
     {"rrr mailbox", "rrr old oak", "rrr 2", "rrr 0", "rrr 1"}}};                 // 0x8015a8
constexpr std::array<int, 2> kGenericTiles = {2, 3};                              // 0x7f3218
constexpr std::array<std::string_view, 2> kRocks = {"rocks", "rrr rocks"};        // 0x8015d0
constexpr std::array<std::string_view, 2> kStreams = {"stream", "rrr logs"};      // 0x8015d8
constexpr std::array<double, 2> kJumpSeq = {4, 5};                                // 0x7f3220: stream, rocks
constexpr std::array<std::string_view, 2> kGallop08 = {"sand08", "grass08"};      // 0x801530
constexpr std::array<std::string_view, 2> kGallop10 = {"sand10", "grass10"};      // 0x801538
constexpr std::array<std::string_view, 2> kGallop12 = {"sand12", "grass12"};      // 0x801540
constexpr std::array<std::string_view, 2> kGallop15 = {"sand15", "grass15"};      // 0x801548
constexpr std::array<std::string_view, 2> kRivalGallop = {"csand10", "cgrass10"}; // 0x801558

// Volumes: the effect globals 0x7f1abc / 0x7f1ac8 (never written: 0x70) and
// the rival's, gCompetitorVolume 0x80a110 = 0x70 − 0x20 (CRT init 0x422cc0).
constexpr int kEffectVolume = 0x70;
constexpr int kRivalVolume = 0x70 - 0x20;

constexpr float kTileWidth = 640.0f;
constexpr float kFinishLine = 11810.0f; // 0x7f3200
constexpr float kChildZ = 0.1f;         // flat-model depth of the horse's children above it

// Globals of race.cpp that outlive a race (statics in the original).
float s_rivalSpeed = 150.0f;            // 0x8015e0, the AI's competitor speed
bool s_aiInit = false;                  // 0x80a118 bit 0
std::uint32_t s_aiSameSince = 0;        // 0x80a11c
bool s_tiredInit = false;               // 0x80a080 bit 0
std::uint32_t s_tiredSince = 0;         // 0x80a114
bool s_tired = false;                   // 0x80a134
bool s_tiredTiming = false;             // 0x80a138
bool s_stepInit = false;                // 0x80a120 bit 0
std::uint32_t s_stepLast = 0;           // 0x80a124
std::uint32_t s_lastInput = 0;          // 0x80a074: tick of the last ListenToMessage
std::uint32_t s_lastLeftUp = 0;         // 0x80a128: for the double click
int s_countdownLine = 0;                // 0x80a12c
std::int16_t s_pointerY = 240;          // 0x80a078: y the pointer was put back to
engine::Anim* s_lastObstacle = nullptr; // 0x80a130: compared only

// __ftol: truncation toward zero.
int ftol(float v) {
    return static_cast<int>(v);
}

class Race final : public ToolbarNode {
public:
    Race(Godiva& app, const std::string& name) : ToolbarNode(app, name) {
        // 0x422d00. (A backdrop object, the renderer's full-redraw mode and
        // the screen device's present mode are display details.)
        m_soundsOn = true; // +0x280
        setSkin(kOut);
        setCursor("cursorOut");
        edgeRightAlt = true; // +0xC8: a right-edge click leaves (0x32CC)
        addToolbarButton("tbQuitOut", kQuit);
        addToolbarButton("tbPagerOut", kPager);
        addToolbarButton("tbCameraOut", kCamera);
        addToolbarButton("tbMapOut", kMap);
        addToolbarButton("tbBagOut", kBag);
        addToolbarButton("tbHelpOut", kHelp);
        addToolbarButton("tbHomeOut", kHome);
        // (+0xFC = 1 is never read.)

        PlayerRecord& rec = app.record();
        if (rec.u32(PlayerRecord::kHorse) == PlayerRecord::kNoHorse) // 4: a random horse, kept
            rec.setU32(PlayerRecord::kHorse, static_cast<std::uint32_t>(engine().rand() & 3));
        if (rec.u32(PlayerRecord::kSaddle) == 2)
            rec.setU32(PlayerRecord::kSaddle, 0);
        m_fancy = rec.u32(PlayerRecord::kSaddle) == 1; // +0x240

        listenToMouseButtons();                    // [0x80a5a0]+0xC
        engine().keyListeners().addListener(this); // [0x80a5a0]+0x18

        Session& session = app.session();
        if (session.raceCourse == 1) {
            m_course = 1;
        } else if (session.raceCourse == 2) {
            m_course = 0;
        } else {
            log::error("Race::Race()  - raceChallenge not set!");
            m_course = 1;
        }
        m_rivalIndex = m_course == 0 ? 1 : 0;
        if (m_soundsOn) {
            releaseAmbient();
            playAmbientLoop(kAmbient[static_cast<std::size_t>(m_course)], 0x28);
        }
        m_firstTick = true; // +0x234

        buildCourse();

        m_rival = createAnimAtViewport(kRivals[static_cast<std::size_t>(m_rivalIndex)]);
        if (m_rival) {
            m_rival->setPosition(-100.0f, 0.0f, 100.0f);
            m_rival->pause(true);
        }
        const std::uint32_t horse = rec.horse();
        if (horse > 3) {
            log::error("Race::Race()  - Unsupported horse type!"); // the original stops building here
            return;
        }
        engine().resources().open(kHorseOmt[horse]);
        m_horse = createAnimAtViewport(kHorseAnims[horse]);
        if (!m_horse)
            return;
        m_horse->setPosition(-100.0f, 0.0f, 200.0f);
        m_horse->pause(true);
        // Children: absolute = the horse's position plus the frame offset it
        // had, minus that offset again, i.e. (0, 0) for the rider here.
        m_rider = createAnimAtViewport("raceBarbie");
        if (m_rider) {
            m_rider->addHeaderFlags(formats::anim_flags::kKeepDepth);
            m_rider->setPosition(0.0f, 0.0f, m_horse->z() + kChildZ);
            m_rider->pause(true);
            m_rider->hide(true);
        }
        if (m_fancy) {
            m_fancyAnim = createAnimAtViewport("raceFancy");
            if (m_fancyAnim) {
                m_fancyAnim->addHeaderFlags(formats::anim_flags::kKeepDepth);
                m_fancyAnim->pause(true);
                m_fancyAnim->hide(true);
                m_fancyAnim->setPosition(m_horse->x() + m_horse->imageOffsetX(),
                                         m_horse->y() + m_horse->imageOffsetY(), m_horse->z() + 2 * kChildZ);
            }
            m_hFancy = createAnimAtViewport("hRaceFancy");
            if (m_hFancy) {
                m_hFancy->pause(true);
                m_hFancy->setSequence(static_cast<int>(horse), true);
                m_hFancy->setPosition(m_hFancy->x() - 100.0f, 0.0f, m_hFancy->z());
            }
        }
        m_startBarbie = createAnimAtViewport("startBarbie");
        if (m_startBarbie) {
            m_startBarbie->addHeaderFlags(formats::anim_flags::kKeepDepth);
            m_startBarbie->setPosition(m_horse->x(), m_horse->y(), m_horse->z() + kChildZ);
            m_startBarbie->setSequence(static_cast<int>(horse), true);
            m_startBarbie->pause(true);
        }
        m_winner = nullptr;
        m_startFlag = createAnimAtViewport("start flag");
        if (m_startFlag) {
            m_startFlag->setClickId(kStartFlagClick);
            m_flagRollover = std::make_unique<ACRollover>(*this, m_startFlag, 7);
        }
        if (engine::Anim* endFlag = createAnimAtViewport("end flag"))
            endFlag->setPosition(12160.0f, 0.0f, endFlag->z());

        // Keep the race sounds loaded (released by the destructor; "stingera" is not).
        auto& sounds = engine().sounds();
        const auto c = static_cast<std::size_t>(m_course);
        for (std::string_view s : {std::string_view("trotup"), kGallop08[c], kGallop10[c], kGallop12[c],
                                   kGallop15[c], std::string_view("stops"), std::string_view("ctrotup")})
            sounds.getSound(s, true, false);
        m_rivalGallopSound = sounds.getSound(kRivalGallop[c], true, false); // +0x230
        for (std::string_view s : {"cstops", "stingera", "stamps", "blow", "whinney1"})
            sounds.getSound(s, true, false);

        chooseLevels(rec);
        placeObstacles();
        m_obstacleIndex = 0;
        m_intro = true; // +0x1CA
        app.map().setYouAreHere(m_course == 1 ? 10 : 1);
        runDelay(3000);
    }

    ~Race() override {
        // 0x423cc0.
        m_flagRollover.reset();
        setMode(2);
        auto& sounds = engine().sounds();
        if (!m_gallop.empty())
            sounds.stopSound(m_gallop);
        const auto c = static_cast<std::size_t>(m_course);
        for (std::string_view s : {std::string_view("trotup"), kGallop08[c], kGallop10[c], kGallop12[c],
                                   kGallop15[c], std::string_view("stops"), std::string_view("ctrotup"),
                                   kRivalGallop[c], std::string_view("cstops"), std::string_view("stamps"),
                                   std::string_view("blow"), std::string_view("whinney1")})
            sounds.retain(s, false);
        // (+0xFC = 0 is never read.)
        Session& session = app().session();
        PlayerRecord& rec = app().record();
        if (m_winner && m_winner == m_horse) {
            session.lastRaceResult = 1;
            rec.setU32(PlayerRecord::kRaceWins, rec.u32(PlayerRecord::kRaceWins) + 1);
            log::info("Barbie Won!");
        } else if (m_winner && m_winner == m_rival) {
            session.lastRaceResult = 2;
            rec.setU32(PlayerRecord::kRaceLosses, rec.u32(PlayerRecord::kRaceLosses) + 1);
            log::info("Barbie came in second!");
        } else {
            log::error("No winner!");
        }
        app().saveGame();
        const std::uint32_t horse = rec.horse();
        if (horse <= 3)
            engine().resources().close(kHorseOmt[horse]);
        else
            log::error("Race::~Race()  - Unsupported horse type!");
        // (The renderer's dirty-rectangle mode and the copy present mode come back: display only.)
        stopMusic();
        releaseAmbient();
    }

    // --- SpendTime (0x424390, this = node+0xC) -------------------------------------------------

    void tick() override {
        ToolbarNode::tick();              // 0x41cba0: pointer and cursor, then the world
        if (engine().modal() || !m_horse) // [0x80a438]
            return;
        followHorse();
        if (!m_started)
            return;
        if (m_finished) {
            if (!m_commentPlayed && m_now - m_last > 3000) {
                showToolbar(true, true);
                moveRollovers(cameraX(), cameraY());
                if (engine::Anim* c = cursor())
                    while (!c->visible())
                        c->hide(false);
                playResultComment();
                m_commentPlayed = true;
                return;
            }
            m_now = engine().ticks();
            return;
        }

        const int camX = ftol(cameraX());
        if (m_soundsOn && m_rivalSound == kRivalGallop[static_cast<std::size_t>(m_course)] &&
            m_rivalGallopSound && m_rival) {
            // The rival's hooves pan with its distance and fade out beyond a screen
            // (AIL_quick_set_volume).
            const int d = ftol(m_rival->x()) - camX;
            const int ad = std::abs(d);
            const int volume =
                ad > 0x1e0 ? ftol(static_cast<float>(480.0 / ad)) * kRivalVolume : kRivalVolume;
            const int pan = std::clamp(d / 5 + 0x40, 0, 0x7f);
            m_rivalGallopSound->setVolumePan(volume, pan);
        }
        std::int16_t speed = m_speed;
        if (m_refusing) {
            updateObstacles(speed);
            return;
        }
        if (camX < 11520 || m_horse->sequence() == 4 || m_horse->sequence() == 5)
            speed = updateObstacles(speed);
        if (speed != m_speed) {
            checkHorseTired(m_speed);
            m_speed = speed;
        }
        if (m_firstTick) {
            s_pointerY = 240;
            m_firstTick = false;
            m_now = m_last = engine().ticks();
            return;
        }
        if (!m_running) {
            countdown();
            return;
        }

        m_now = engine().ticks();
        if (!m_winner && m_rival) {
            if (m_horse->x() > kFinishLine && m_rival->x() <= m_horse->x()) {
                m_winner = m_horse;
                engine().sounds().playSound("stingera", -1, false, false, 1, false, kEffectVolume, -1, 0,
                                            false);
                log::info("Barbie has crossed the finish line first");
            }
            if (m_rival->x() > kFinishLine && m_horse->x() < m_rival->x()) {
                m_winner = m_rival;
                log::info("Friend has crossed the finish line first");
            }
        }
        updateCompetitorAI(m_speed);
        if (camX > 11520) {
            finishStretch(camX);
            return;
        }
        if (m_course == 0 && m_gulls < 3 && engine().rand() % 10000 == 0) {
            if ((m_now & 1) && m_soundsOn)
                engine().sounds().playSound("gulls", -1, false, false, 1, false, kEffectVolume, -1, 0, false);
            ++m_gulls;
        }
        steer();
    }

    // --- input (0x424000, 0x424080, 0x424100, 0x4242f0) ------------------------------------

    void listen(int message, const engine::MessageData& data) override {
        // ListenToMessage (0x424100).
        s_lastInput = engine().ticks();
        const auto* e = static_cast<const engine::InputEvent*>(data.ptr);
        if (message == kMsgEdgeRightAlt) {
            Session& session = app().session();
            session.arriveFromRace = 1;
            session.raceFinished = m_finished ? 1 : 0;
            gotoNode(m_course == 1 ? "rabbitE" : "beachE");
            return;
        }
        if (message == -104) { // key down: the KeyDown handler again (after the application's call)
            if (e)
                onKeyDown(*e);
            return;
        }
        const bool steering = m_started && !m_finished && !m_intro && app().controlMode == 0;
        if (message == -103 && e && steering) { // mouse button up
            if (e->type == engine::InputEvent::LeftUp) {
                m_accelReleased = true;
                if (s_lastInput - s_lastLeftUp < 400) {
                    m_jumpRequest = true; // double click
                    s_lastLeftUp = 0;
                    return;
                }
                s_lastLeftUp = s_lastInput;
                return;
            }
            if (e->type == engine::InputEvent::RightUp) {
                m_brakeReleased = true;
                return;
            }
        } else if (message == -102 && e && steering) { // mouse button down
            if (e->type == engine::InputEvent::LeftDown) {
                m_accelReleased = false;
                return;
            }
            if (e->type == engine::InputEvent::RightDown) {
                m_brakeReleased = false;
                return;
            }
            ToolbarNode::listen(message, data); // (twice in the original)
        }
        ToolbarNode::listen(message, data);
    }

    void onKeyDown(const engine::InputEvent& e) override {
        // KeyDown (0x424000).
        switch (e.key) {
        case 0:
            break;
        case engine::key::kEscape:
            if (m_started && !m_finished)
                return;
            showExitDialog(true);
            return;
        case engine::key::kUp:
            if (app().controlMode == 1)
                m_accelReleased = false;
            return;
        case engine::key::kDown:
            if (app().controlMode == 1)
                m_brakeReleased = false;
            return;
        default:
            break;
        }
        ToolbarNode::onKeyDown(e);
    }

    void onKeyUp(const engine::InputEvent& e) override {
        // KeyUp (0x424080).
        if (app().controlMode == 1 && m_started && !m_finished && !m_intro && e.key != 0) {
            if (e.key == engine::key::kEnter) {
                m_jumpRequest = true;
                return;
            }
            if (e.key == engine::key::kUp) {
                m_accelReleased = true;
                return;
            }
            if (e.key == engine::key::kDown) {
                m_brakeReleased = true;
                return;
            }
        }
        ToolbarNode::onKeyUp(e);
    }

    void onPress(const engine::Pick& pick) override {
        // MouseDown (0x4242f0).
        const int id = pick.element ? pick.element->clickId() : 0;
        if (id == kStartFlagClick) {
            if (!m_started) {
                m_started = true;
                if (app().controlMode == 0)
                    m_accelReleased = false; // the button that clicked the flag is held
                m_flagRollover.reset();
            }
            return;
        }
        if (id != kHelpClickId) {
            ToolbarNode::onPress(pick);
            return;
        }
        if (m_finished)
            playVoiceNoFlaps("db02001");
        else
            playVoice("db04089");
    }

    // --- scripts and anims (0x426140, 0x4263e0, 0x4266b0) ------------------------------------

    void scriptMessage(engine::Script& script, std::string_view message, int counter) override {
        // ScriptCommand (0x426140).
        const std::string& name = script.name();
        const auto horse = static_cast<std::size_t>(std::min<std::uint32_t>(app().record().horse(), 3));
        if (str::iequals(message, "initRace")) {
            setScriptVar(name, "horse", kHorseAnims[horse]);
            if (m_fancy)
                setScriptVar(name, "fancy", 1.0);
            if (str::iequals(name, "raceStart")) {
                setScriptVar(name, "startSound", kStartSounds[horse]);
                setScriptVar(name, "startSeq", static_cast<double>(horse));
            } else if (str::iequals(name, "raceJump")) {
                setScriptVar(name, "jumpseq", kJumpSeq[static_cast<std::size_t>(m_obstacleType)]);
            }
        } else if (str::iequals(message, "initCRace")) {
            setScriptVar(name, "competitor", kRivals[static_cast<std::size_t>(m_rivalIndex)]);
            if (str::iequals(name, "raceCJump"))
                setScriptVar(name, "jumpseq", kJumpSeq[static_cast<std::size_t>(m_obstacleType)]);
        } else if (str::iequals(message, "startUp")) {
            // Barbie has mounted: the rider replaces Barbie standing at the start.
            if (m_startBarbie) {
                m_startBarbie->hide(true);
                m_startBarbie->unlink();
                engine().anims().remove(m_startBarbie);
                m_startBarbie = nullptr;
            }
            if (m_hFancy)
                m_hFancy->hide(true);
            if (m_rider && m_horse)
                m_rider->setPosition(m_horse->x(), m_horse->y(), m_horse->z() + kChildZ);
            if (m_fancy && m_fancyAnim && m_rider)
                m_fancyAnim->setPosition(m_rider->x(), m_rider->y(), m_fancyAnim->z());
            if (m_gallop.empty()) {
                m_gallop = "trotup";
                engine().sounds().playSound(m_gallop, -1, false, false, 0, true, kEffectVolume, -1, 0, false);
            }
        }
        ToolbarNode::scriptMessage(script, message, counter);
    }

    void scriptEnded(engine::Script& script) override {
        // ScriptEnded (0x4263e0).
        const std::string& name = script.name();
        if (str::iequals(name, "delay")) {
            if (m_flagRollover) // the flag has not been clicked yet
                playVoice("db04089");
        } else if (str::iequals(name, "raceStart")) {
            m_intro = false;
            runScript("raceRun");
            for (engine::Anim* a : {m_horse, m_rider, m_fancy ? m_fancyAnim : nullptr})
                if (a)
                    a->setTimeMode(false); // one frame per world update from now on
            setMode(0);
            m_gait = 10;
            setInterval(100);
            m_speed = 50;
            setBarbieSpeed(50.0);
        } else if (str::iequals(name, "raceJump")) {
            runScript("raceRun");
            m_jumpRequest = false;
        } else if (str::iequals(name, "raceCJump")) {
            runScript("raceCRun");
            m_rivalJumping = false;
        } else if (str::iequals(name, "raceAbortGo")) {
            runScript("raceRun");
            m_speed = 300;
            setBarbieSpeed(300.0);
            m_refusing = false;
            m_jumpRequest = false;
        } else if (str::iequals(name, "raceCStart")) {
            runScript("raceCRun");
            if (m_rival)
                m_rival->setTimeMode(false);
            setMode(0);
            m_gait = 10;
            setInterval(100);
            // A random start: 220, 185 or 150 px/s (GetTickCount() % 3).
            static constexpr std::array<float, 3> kRivalStart = {220.0f, 185.0f, 150.0f};
            if (m_rival)
                m_rival->setSpeed(kRivalStart[engine().ticks() % 3], 0.0f, 0.0f);
            const std::string_view gallop = kRivalGallop[static_cast<std::size_t>(m_course)];
            if (m_rivalSound != gallop) {
                if (!m_rivalSound.empty())
                    engine().sounds().stopSound(m_rivalSound);
                m_rivalSound = gallop;
                if (m_soundsOn)
                    engine().sounds().playSound(gallop, -1, false, false, 0, true, kRivalVolume, -1, 0,
                                                false);
            }
        }
        ToolbarNode::scriptEnded(script);
    }

    void frameEvent(engine::Anim& anim, int seq, int index) override {
        // AnimFrameCue (0x4266b0).
        auto& sounds = engine().sounds();
        if (&anim == m_rider) {
            if (seq == 1) {
                if (index == 3) { // walking off
                    if (m_horse && m_horse->frameIndex() == 3)
                        sounds.playSound("whinney1", -1, false, false, 1, false, kEffectVolume, -1, 0, false);
                    if (m_speed != 25) {
                        m_speed = 25;
                        setBarbieSpeed(25.0);
                    }
                } else if (index == 5 && m_speed != 50) {
                    m_speed = 50;
                    setBarbieSpeed(50.0);
                }
            } else if (seq == 6) { // refusal
                if (index == 5) {
                    stopGallop();
                    sounds.playSound("skid", -1, false, false, 1, false, kEffectVolume, -1, 0, false);
                } else if (index == 9) {
                    m_speed = 0;
                    setBarbieSpeed(0.0);
                }
            } else if (seq == 7 && index == 17) { // the jump after a refusal
                sounds.playSound("newjump", -1, false, false, 1, false, kEffectVolume, -1, 0, false);
                m_speed = 300;
                setBarbieSpeed(300.0);
            }
        } else if (&anim == m_rival && seq == 1) {
            if (index == 5) {
                if (m_rivalSound.empty()) {
                    m_rivalSound = "ctrotup";
                    if (m_soundsOn)
                        sounds.playSound(m_rivalSound, -1, false, false, 0, true, kRivalVolume, -1, 0, false);
                }
                m_rival->setSpeed(50.0f, 0.0f, 0.0f);
            } else if (index == 10) {
                m_rival->setSpeed(100.0f, 0.0f, 0.0f);
            }
        }
        ToolbarNode::frameEvent(anim, seq, index);
    }

private:
    // --- construction ----------------------------------------------------------------------------

    void buildCourse() {
        // The background: a 20 × 1 layer of 640 × 480 tiles (0x477ad0, installed
        // with SetCanvas 0x45cd30): the start, the generic tiles, the finish.
        const CourseTiles& t = kTiles[static_cast<std::size_t>(m_course)];
        auto& res = engine().resources();
        auto image = [&](std::string_view name) -> std::shared_ptr<const engine::Image8> {
            return name.empty() ? nullptr : res.image(name, palette());
        };
        std::array<std::shared_ptr<const engine::Image8>, 20> tiles;
        tiles[0] = image(t.start);
        const auto g0 = image(t.generic0), g1 = image(t.generic1);
        if (kGenericTiles[static_cast<std::size_t>(m_course)] == 2) {
            for (std::size_t i = 1; i < 19; ++i)
                tiles[i] = i % 2 != 0 ? g0 : g1;
        } else {
            const auto g2 = image(t.generic2);
            for (std::size_t i = 1; i < 19; ++i)
                tiles[i] = i % 3 == 0 ? g0 : i % 3 == 1 ? g1 : g2;
        }
        tiles[19] = image(t.finish);
        auto layer = std::make_shared<engine::Image8>(20 * 640, 480, 0);
        for (std::size_t i = 0; i < tiles.size(); ++i)
            if (tiles[i])
                engine::blit(*layer, *tiles[i], static_cast<int>(i) * 640, 0, layer->bounds(), true);
            else
                log::warn("race: missing course tile {}", i);
        setBackground(std::move(layer));
    }

    void chooseLevels(const PlayerRecord& rec) {
        // The rival's speed level (rec+0x88) and the obstacle level (rec+0x90),
        // promoted for this race from the race history (rec+0x38, 5 × 0x14).
        m_speedLevel = static_cast<int>(rec.u32(0x88));
        m_obstacleLevel = static_cast<int>(rec.u32(0x90));
        int sameLevel = 0, flagged = 0, zeroLast = 0;
        for (std::size_t i = 0; i < 5; ++i) {
            const std::size_t e = PlayerRecord::kRaceHistory + 0x14 * i;
            if (rec.u16(e + 4) != 0)
                ++flagged;
            if (static_cast<int>(rec.u32(e)) == m_speedLevel)
                ++sameLevel;
            if (rec.u32(e + 0x10) == 0)
                ++zeroLast;
        }
        if (sameLevel > 2 && flagged > 3 && (m_speedLevel == 0 || m_speedLevel == 1))
            ++m_speedLevel;
        static constexpr std::array<std::array<int, 3>, 3> kRivalSpeeds = {
            {{100, 170, 250}, {150, 220, 300}, {200, 270, 350}}};
        if (m_speedLevel >= 0 && m_speedLevel <= 2) {
            const auto& v = kRivalSpeeds[static_cast<std::size_t>(m_speedLevel)];
            m_minSpeed = v[0], m_midSpeed = v[1], m_maxSpeed = v[2];
            log::info("Race speed level = low"); // (the same text for every level)
        }
        if (sameLevel > 2 && zeroLast > 3 && (m_obstacleLevel == 0 || m_obstacleLevel == 1))
            ++m_obstacleLevel;
    }

    void placeObstacles() {
        // Obstacle positions, multiples of 640 (+0x208), then the first one.
        auto rnd = [&] { return engine().rand(); };
        auto& x = m_obstacleX;
        switch (m_obstacleLevel) {
        case 0:
            if (rnd() % 2 == 0) {
                m_obstacleCount = 2;
                x[0] = static_cast<float>((rnd() & 3) + 3) * kTileWidth;
                x[1] = static_cast<float>((rnd() & 3) + 11) * kTileWidth;
            } else {
                m_obstacleCount = 1;
                x[0] = static_cast<float>(rnd() % 6 + 6) * kTileWidth;
            }
            break;
        case 1:
            x[0] = static_cast<float>(rnd() % 3 + 2) * kTileWidth;
            x[1] = static_cast<float>((rnd() % 3 + 3) * 640) + x[0];
            x[2] = static_cast<float>((rnd() % 3 + 3) * 640) + x[1];
            if (x[2] <= 7680.0f) {
                m_obstacleCount = 4;
                x[3] = static_cast<float>((rnd() % 3 + 3) * 640) + x[2];
            } else {
                m_obstacleCount = 3;
            }
            break;
        case 2:
            x[0] = static_cast<float>(rnd() % 3 + 2) * kTileWidth;
            for (std::size_t i = 1; i < 5; ++i)
                x[i] = static_cast<float>((rnd() % 3 + 1) * 640) + x[i - 1];
            if (rnd() % 2 == 0) {
                m_obstacleCount = 6;
                x[5] = static_cast<float>((rnd() % 3 + 1) * 640) + x[4];
            } else {
                m_obstacleCount = 5;
            }
            break;
        default:
            break;
        }
        const auto c = static_cast<std::size_t>(m_course);
        if (rnd() % 2 == 0) {
            m_rocks = createAnimAtViewport(kRocks[c]);
            if (m_rocks)
                m_rocks->setPosition(x[0], 0.0f, 50.0f);
            m_obstacleType = 1;
            m_stream = nullptr;
        } else {
            m_stream = createAnimAtViewport(kStreams[c]);
            if (m_stream)
                m_stream->setPosition(x[0], 0.0f, 50.0f);
            m_obstacleType = 0;
            m_rocks = nullptr;
        }
    }

    // --- the race loop's parts ------------------------------------------------------------------

    void followHorse() {
        // Barbie at the start (or the rider) back on the horse, the fancy tack on the rider.
        engine::Anim* child = m_startBarbie ? m_startBarbie : m_rider;
        if (child)
            child->setPosition(m_horse->x(), m_horse->y(), m_horse->z() + kChildZ);
        if (m_fancy && m_fancyAnim && m_rider)
            m_fancyAnim->setPosition(m_rider->x(), m_rider->y(), m_horse->z() + 2 * kChildZ);
    }

    void countdown() {
        // From the click on the flag to the start: a line, the start scripts
        // after its delay, then (3.3 s, 4.3 s for the white horse) the music.
        if (m_now == m_last) {
            s_countdownLine = engine().rand() % 3;
            playVoice(kCountdown[static_cast<std::size_t>(s_countdownLine)]);
            if (engine::Anim* c = cursor(); c && c->visible())
                c->hide(true);
            showToolbar(false, true);
            m_launched = false;
        }
        if (!m_launched && m_now - m_last > kStartDelays[static_cast<std::size_t>(s_countdownLine)]) {
            runScript("raceStart");
            runScript("raceCStart");
            m_launched = true;
        }
        const std::uint32_t elapsed = m_now - m_last;
        if (elapsed <= 3300) {
            m_now = engine().ticks();
            return;
        }
        if (app().record().horse() == 1 && elapsed < 4300) {
            m_now = engine().ticks();
            return;
        }
        stopMusic();
        if (m_soundsOn && !app().session().musicOff) {
            if (!m_music.empty())
                stopMusic();
            m_music = "raceMusic"; // +0x174
            app().playMusic(m_music);
        }
        m_running = true;
        s_pointerY = 240;
        engine().warpMouse(320, 240);
    }

    void finishStretch(int camX) {
        // Past x 11520: no more steering. The horses slow down and stop after
        // the finish line; when both stand still the race is over.
        auto& sounds = engine().sounds();
        if (camX > 12010) {
            if (m_horse->sequence() == 2 && m_horse->frameIndex() > 4) {
                m_horse->setTimeMode(true);
                if (m_rider)
                    m_rider->setTimeMode(true);
                m_horse->setSequence(3, true);
                if (m_rider)
                    m_rider->setSequence(3, true);
                if (m_fancy && m_fancyAnim) {
                    m_fancyAnim->setTimeMode(true);
                    m_fancyAnim->setSequence(3, true);
                }
                stopGallop();
                sounds.playSound("stops", -1, false, false, 1, false, kEffectVolume, -1, 0, false);
                m_speed = static_cast<std::int16_t>(12160 - camX);
                if (m_rival && m_horse->x() - 320.0f < m_rival->x() && m_rival->sequence() == 2) {
                    m_rival->setSpeed(static_cast<float>(m_speed), 0.0f, 0.0f);
                    m_rival->setSequence(3, true);
                    stopRivalGallop();
                    if (m_soundsOn)
                        sounds.playSound("cstops", -1, false, false, 1, false, kRivalVolume, -1, 0, false);
                    log::info("stopping competitor - because horse is stopping");
                    return;
                }
                if (m_rival)
                    m_rival->setTimeMode(true);
                setMode(2);
            }
            if (m_horse->sequence() == 3) {
                const int f = m_horse->frameIndex();
                if (f >= 8) {
                    if (f == 8)
                        sounds.playSound("blow", -1, false, false, 1, false, kEffectVolume, -1, 0, false);
                    if (m_speed != 0) {
                        m_speed = 0;
                        setBarbieSpeed(0.0);
                    }
                } else if (f >= 4) {
                    if (m_speed > 70) {
                        m_speed = 70;
                        setCameraSpeed(70.0f, 0.0f);
                        setHorseVelocity(m_speed + 100);
                    }
                } else {
                    m_speed = static_cast<std::int16_t>(12210 - camX);
                    setCameraSpeed(static_cast<float>(m_speed), 0.0f);
                    setHorseVelocity(m_speed + 50);
                }
            }
        }
        if (camX > 12035 && !m_finished) {
            if (camX > 12110 && m_speed >= 70) {
                m_speed = 25;
                setBarbieSpeed(25.0);
            } else if (camX > 12150 && m_speed >= 25) {
                m_speed = 1;
                setBarbieSpeed(1.0);
            }
            if (m_horse->speedX() * 1000.0f == 0.0f && (!m_rival || m_rival->speedX() * 1000.0f == 0.0f)) {
                m_finished = true;
                m_last = m_now;
                if (engine::Anim* c = cursor(); c && !c->visible()) {
                    app().map().setYouAreHere(m_course == 1 ? 13 : 3);
                    stopMusic();
                }
            }
            return;
        }
        if (m_speed == 0)
            return;
        setBarbieSpeed(m_speed);
        if (m_rival && m_rival->speedX() * 1000.0f != 0.0f)
            m_rival->setSpeed(static_cast<float>(m_speed), 0.0f, 0.0f);
        if (m_gallop.empty())
            return;
        const std::string_view gallop = gallopFor(m_speed);
        if (gallop == m_gallop)
            return;
        sounds.stopSound(m_gallop);
        m_gallop = gallop;
        sounds.playSound(m_gallop, -1, false, false, 0, false, kEffectVolume, -1, 0, false);
    }

    void steer() {
        // The speed from the controls: accelerate +10 (every 200 ms with the
        // buttons or keys) up to 300, brake −10 down to 150, and without input
        // drift towards 150 every 2 s; 300 for 10 s tires the horse (260).
        const auto [mx, my] = engine().mousePosition();
        const auto y = static_cast<std::int16_t>(my);
        const std::uint32_t mode = app().controlMode;
        enum { None, Accelerate, Brake } action = None;
        if (mode == 2) {
            if (std::abs(y - s_pointerY) > 50)
                action = y < s_pointerY ? Accelerate : Brake;
        } else if (!m_accelReleased) {
            action = Accelerate;
        } else if (!m_brakeReleased) {
            action = Brake;
        }
        bool changed = true;
        if (action == Accelerate) {
            if (mode != 2) {
                if (m_now - m_last <= 200)
                    return;
                m_last = m_now;
            }
            if (m_speed < 300)
                m_speed = static_cast<std::int16_t>(m_speed + 10);
            else if (checkHorseTired(m_speed))
                m_speed = 260;
        } else if (action == Brake) {
            if (mode != 2) {
                if (m_now - m_last <= 200)
                    return;
                m_last = m_now;
            }
            if (m_speed > 150)
                m_speed = static_cast<std::int16_t>(m_speed - 10);
        } else if (checkHorseTired(m_speed)) {
            m_speed = 260;
        } else if (speedStepTimer()) {
            m_speed = static_cast<std::int16_t>(m_speed > 150 ? m_speed - 10 : m_speed + 10);
        } else {
            changed = false;
        }
        if (!changed)
            return;
        // Gait: the sound and the world's frame interval (one anim frame per update).
        int gait = 15, interval = 66;
        if (m_speed < 200)
            gait = 8, interval = 125;
        else if (m_speed < 250)
            gait = 10, interval = 100;
        else if (m_speed < 300)
            gait = 12, interval = 83;
        if (m_gait != gait) {
            m_gait = gait;
            setInterval(static_cast<std::uint32_t>(interval));
        }
        const std::string_view gallop = gallopFor(m_speed);
        if (gallop != m_gallop) {
            if (!m_gallop.empty())
                engine().sounds().stopSound(m_gallop);
            m_gallop = gallop;
            engine().sounds().playSound(m_gallop, -1, false, false, 0, true, kEffectVolume, -1, 0, false);
        }
        setBarbieSpeed(m_speed);
        engine().warpMouse(mx, 240); // SetCursorPos (0x422ce0)
        s_pointerY = 240;
    }

    std::string_view gallopFor(int speed) const {
        const auto c = static_cast<std::size_t>(m_course);
        if (speed < 200)
            return kGallop08[c];
        if (speed < 250)
            return kGallop10[c];
        if (speed < 300)
            return kGallop12[c];
        return kGallop15[c];
    }

    std::int16_t updateObstacles(std::int16_t speed) {
        // UpdateObstacles (0x425240): move the obstacle anims along, take a
        // jump at the right moment, or refuse when no jump was asked for.
        if (m_obstacleIndex < m_obstacleCount - 1 &&
            m_obstacleX[static_cast<std::size_t>(m_obstacleIndex)] - (-320.0f) < cameraX()) {
            ++m_obstacleIndex;
            const float x = m_obstacleX[static_cast<std::size_t>(m_obstacleIndex)];
            const auto c = static_cast<std::size_t>(m_course);
            if (!m_stream || (m_rocks && s_lastObstacle == m_rocks)) {
                m_stream = createAnimAtViewport(kStreams[c]);
                if (m_stream)
                    m_stream->setPosition(x, 0.0f, 50.0f);
                s_lastObstacle = m_stream;
                m_obstacleType = 0;
            } else if (!m_rocks || s_lastObstacle == m_stream) {
                m_rocks = createAnimAtViewport(kRocks[c]);
                if (m_rocks)
                    m_rocks->setPosition(x, 0.0f, 50.0f);
                s_lastObstacle = m_rocks;
                m_obstacleType = 1;
            }
        }
        const float obstacle = m_obstacleX[static_cast<std::size_t>(m_obstacleIndex)];
        const float takeOff = obstacle - 200.0f;
        const float hx = m_horse->x();
        // The rival jumps by itself when it is less than 200 px before the obstacle.
        auto rivalJump = [&] {
            if (m_rival && (m_rivalJumping || (takeOff < m_rival->x() && m_rival->x() < obstacle)))
                tryStartJump(m_rival);
        };
        if (m_jumpRequest) {
            if (!m_refusing) {
                if (obstacle - 700.0f < hx && hx < takeOff)
                    return speed; // too early: the request waits
                const bool jumped = tryStartJump(m_horse);
                rivalJump();
                if (!jumped)
                    return speed;
                log::info("Jump started at {:6.0f}", hx);
                setBarbieSpeed(220.0);
                if (m_gait == 10)
                    return 220;
                m_gait = 10;
                setInterval(100);
                if (!m_gallop.empty())
                    engine().sounds().stopSound(m_gallop);
                m_gallop = kGallop10[static_cast<std::size_t>(m_course)];
                engine().sounds().playSound(m_gallop, -1, false, false, 0, true, kEffectVolume, -1, 0, false);
                return 220;
            }
        } else if (!m_refusing) {
            if (obstacle - 100.0f < hx && hx < obstacle) {
                // No jump asked for: the horse refuses and backs up.
                m_refusing = true;
                runScript("raceAbort");
                m_gait = 12;
                setInterval(83);
                const auto back = static_cast<std::int16_t>(m_course != 1 ? -15 : -30);
                setBarbieSpeed(back);
                rivalJump();
                return back;
            }
            rivalJump();
            return speed;
        }
        // Refusing: a jump request starts again once the refusal is over.
        if (!isScriptRunning("raceAbort") && !isScriptRunning("raceAbortGo") && m_jumpRequest)
            runScript("raceAbortGo");
        rivalJump();
        return speed;
    }

    bool tryStartJump(engine::Anim* who) {
        // TryStartJump (0x425f00).
        if (who == m_horse && !isScriptRunning("raceJump")) {
            runScript("raceJump");
            if (m_gait != 10) {
                m_gait = 10;
                setInterval(100);
            }
            return true;
        }
        if (who == m_rival && !isScriptRunning("raceCJump")) {
            runScript("raceCJump");
            m_rivalJumping = true;
            return true;
        }
        return false;
    }

    void updateCompetitorAI(std::int16_t speed) {
        // UpdateCompetitorAI (0x4256c0): keep the rival close to Barbie. Its
        // speed (s_rivalSpeed) persists from race to race.
        if (!m_rival)
            return;
        const std::uint32_t now = engine().ticks();
        if (!s_aiInit) {
            s_aiInit = true;
            s_aiSameSince = now;
        }
        engine::Anim* r = m_rival;
        const float rx = r->x(), hx = m_horse->x();
        auto& sounds = engine().sounds();
        auto stopAtFinish = [&](const char* why) {
            s_rivalSpeed = 0.0f;
            r->setSpeed(0.0f, 0.0f, 0.0f);
            r->setSequence(3, true);
            stopRivalGallop();
            if (m_soundsOn)
                sounds.playSound("cstops", -1, false, false, 1, false, kRivalVolume, -1, 0, false);
            log::info("{}", why);
        };
        auto set = [&] { r->setSpeed(s_rivalSpeed, 0.0f, 0.0f); };
        if (s_rivalSpeed != 0.0f && rx > 12160.0f && hx < 11520.0f && r->sequence() == 2) {
            // Far ahead of Barbie: wait at the finish.
            s_rivalSpeed = 0.0f;
            r->setSpeed(0.0f, 0.0f, 0.0f);
            r->setSequence(3, true);
            r->setTimeMode(false);
            r->setPosition(12160.0f, 0.0f, 100.0f);
            stopRivalGallop();
            if (m_soundsOn)
                sounds.playSound("cstops", -1, false, false, 1, false, kRivalVolume, -1, 0, false);
            log::info("stopping competitor");
            return;
        }
        if (rx > 12480.0f) {
            stopAtFinish("stopping competitor - off race course");
            r->setPosition(12160.0f, 0.0f, 100.0f);
            return;
        }
        if (rx > 11900.0f && r->sequence() == 2) {
            // Past the finish: slow to a stop.
            const auto v = static_cast<std::int16_t>(ftol(12160.0f - cameraX() - (-50.0f)));
            s_rivalSpeed = static_cast<float>(v);
            if (static_cast<float>(v) > static_cast<float>(speed))
                s_rivalSpeed = static_cast<float>(speed);
            set();
            r->setSequence(3, true);
            r->setTimeMode(true);
            stopRivalGallop();
            if (m_soundsOn)
                sounds.playSound("cstops", -1, false, false, 1, false, kRivalVolume, -1, 0, false);
            return;
        }
        if (r->sequence() == 3) {
            const int f = r->frameIndex();
            if (f >= 8) {
                s_rivalSpeed = 0.0f;
                r->setSpeed(0.0f, 0.0f, 0.0f);
                return;
            }
            if (f < 4)
                return;
            s_rivalSpeed = static_cast<float>(speed);
            set();
            return;
        }
        if (rx > 4480.0f && rx < 6400.0f && rx < hx + 100.0f) {
            if (s_rivalSpeed <= static_cast<float>(m_maxSpeed + 10) &&
                s_rivalSpeed <= static_cast<float>(speed + 10)) {
                log::debug("Giving Barbie a run for her money");
                s_rivalSpeed += 10.0f;
            }
            set();
            return;
        }
        if (rx > 9600.0f && speed > 220 && hx - 100.0f < rx) {
            if (!(static_cast<float>(speed - 10) > s_rivalSpeed)) {
                log::debug("Let Barbie win");
                if (!(static_cast<float>(m_minSpeed) > s_rivalSpeed))
                    s_rivalSpeed -= 10.0f;
            }
            set();
            return;
        }
        if (static_cast<float>(speed) == s_rivalSpeed) {
            if (now - s_aiSameSince > 1500) {
                log::debug("Too long at same speed as Barbie");
                s_aiSameSince = now;
                if (speed <= 160 || hx > rx || (now & 1) == 0)
                    s_rivalSpeed += 10.0f;
                else
                    s_rivalSpeed -= 10.0f;
                set();
                return;
            }
        } else {
            s_aiSameSince = now;
        }
        if (hx - 100.0f > rx && !(static_cast<float>(m_maxSpeed) < s_rivalSpeed)) {
            // Behind: catch up (+20 when more than 250 px behind).
            if (!(hx - 250.0f <= rx)) {
                s_rivalSpeed += 10.0f;
                s_rivalSpeed += 10.0f;
                set();
            } else if (!(static_cast<float>(speed + 10) < s_rivalSpeed)) {
                s_rivalSpeed += 10.0f;
                set();
            }
        }
        if (hx + 100.0f < rx && static_cast<float>(m_minSpeed) < s_rivalSpeed && speed > 150 &&
            !(static_cast<float>(speed + 10) > s_rivalSpeed)) {
            // Ahead: ease off.
            s_rivalSpeed -= 10.0f;
            set();
        }
    }

    bool checkHorseTired(std::int16_t speed) {
        // CheckHorseTired (0x425da0): 10 s at 300 → tired for 5 s.
        const std::uint32_t now = engine().ticks();
        if (!s_tiredInit) {
            s_tiredInit = true;
            s_tiredSince = now;
        }
        if (!s_tired) {
            if (speed == 300) {
                if (!s_tiredTiming) {
                    s_tiredSince = now;
                    s_tiredTiming = true;
                    return false;
                }
                if (now - s_tiredSince > 10000) {
                    s_tired = true;
                    log::info("Horse tired");
                    s_tiredTiming = false;
                    s_tiredSince = now;
                    return true;
                }
            } else if (s_tiredTiming) {
                s_tiredTiming = false;
            }
        } else if (now - s_tiredSince > 5000) {
            s_tired = false;
            log::info("No longer tired");
            return false;
        }
        return s_tired;
    }

    bool speedStepTimer() {
        // SpeedStepTimer (0x425eb0): true every 2 s.
        const std::uint32_t now = engine().ticks();
        if (!s_stepInit) {
            s_stepInit = true;
            s_stepLast = now;
        }
        if (now - s_stepLast > 2000) {
            s_stepLast = now;
            return true;
        }
        return false;
    }

    void playResultComment() {
        // PlayResultComment (0x425f90), then the "what next" line 5 s later.
        const int r1 = engine().rand();
        const int r2 = engine().rand();
        const bool even = r1 % 2 == 0;
        std::string_view line;
        switch (r2 % 6) {
        case 0:
            if (m_winner == m_horse)
                line = r1 % 3 == 0 ? "db0491" : r1 % 3 == 1 ? "db0454" : "db0453";
            else
                line = "db0489";
            break;
        case 1:
            sayHorseNameThen("db0492"); // 0x801500 / 0x8014fc: the same line
            delayVoice("db9059", 5000);
            return;
        case 2:
            line = "db0492";
            break;
        case 3:
            if (m_rivalIndex == 0)
                line = even ? "db0501" : "db0498";
            else
                line = even ? "db0511" : "db0508";
            break;
        case 4:
            line = "db0490";
            break;
        default:
            if (m_rivalIndex == 0)
                line = even ? "dc0499" : "dc0497";
            else
                line = even ? "dt0507" : "dt0509";
            break;
        }
        playVoice(line);
        delayVoice("db9059", 5000);
    }

    // --- helpers ---------------------------------------------------------------------------

    void setBarbieSpeed(double v) {
        // 0x426940: the view and the horse move together.
        setCameraSpeed(static_cast<float>(v), 0.0f);
        setHorseVelocity(v);
    }

    void setHorseVelocity(double v) {
        // 0x426980 (the horse only; its children ride along).
        const auto f = static_cast<float>(v);
        for (engine::Anim* a : {m_horse, m_rider, m_startBarbie, m_fancy ? m_fancyAnim : nullptr})
            if (a)
                a->setSpeed(f, 0.0f, 0.0f);
    }

    void setMode(int mode) {
        // Node vt+0x14 (0x45cd70); like vt+0x18 it also stops the world clock.
        setUpdateMode(mode);
        restartClock();
    }

    void setInterval(std::uint32_t ms) {
        // Node vt+0x18 (0x45cdb0): the next frame comes a full interval later.
        setFrameInterval(ms);
        restartClock();
    }

    void stopGallop() {
        if (!m_gallop.empty())
            engine().sounds().stopSound(m_gallop);
        m_gallop = {};
    }

    void stopRivalGallop() {
        if (!m_rivalSound.empty())
            engine().sounds().stopSound(m_rivalSound);
        m_rivalSound = {};
    }

    // Input (+0x1C0..+0x1CA).
    bool m_accelReleased = true; // +0x1C0
    bool m_brakeReleased = true; // +0x1C4
    bool m_jumpRequest = false;  // +0x1C8
    bool m_intro = true;         // +0x1CA: until "raceStart" ends
    // Anims (+0x1CC..+0x1F0).
    engine::Anim* m_horse = nullptr;                    // +0x1CC
    engine::Anim* m_rider = nullptr;                    // +0x1D0
    engine::Anim* m_fancyAnim = nullptr;                // +0x1D4
    engine::Anim* m_hFancy = nullptr;                   // +0x1D8
    engine::Anim* m_startBarbie = nullptr;              // +0x1DC
    engine::Anim* m_rival = nullptr;                    // +0x1E0
    engine::Anim* m_winner = nullptr;                   // +0x1E4
    engine::Anim* m_stream = nullptr;                   // +0x1E8
    engine::Anim* m_rocks = nullptr;                    // +0x1EC
    engine::Anim* m_startFlag = nullptr;                // +0x1F0
    std::unique_ptr<ACRollover> m_flagRollover;         // +0x1F8
    int m_course = 1;                                   // +0x1FC: 0 beach, 1 rabbit run
    int m_rivalIndex = 0;                               // +0x200
    int m_obstacleType = 0;                             // +0x204: 0 stream, 1 rocks
    std::array<float, 6> m_obstacleX{};                 // +0x208
    int m_obstacleCount = 0;                            // +0x220
    int m_obstacleIndex = 0;                            // +0x224
    std::string_view m_gallop;                          // +0x228: Barbie's hooves
    std::string_view m_rivalSound;                      // +0x22C: the rival's hooves
    engine::Sound* m_rivalGallopSound = nullptr;        // +0x230
    bool m_firstTick = true;                            // +0x234
    bool m_started = false;                             // +0x236: the flag was clicked
    bool m_finished = false;                            // +0x238: both horses stopped
    bool m_running = false;                             // +0x23A
    bool m_launched = false;                            // +0x23C: start scripts run
    bool m_commentPlayed = false;                       // +0x23E
    bool m_fancy = false;                               // +0x240
    std::uint32_t m_now = 0, m_last = 0;                // +0x244 / +0x248
    std::int16_t m_speed = 0;                           // +0x24C: Barbie's speed, px/s
    int m_gait = 0;                                     // +0x254: 8, 10, 12, 15
    bool m_rivalJumping = false;                        // +0x258
    bool m_refusing = false;                            // +0x25A
    int m_gulls = 0;                                    // +0x25C
    int m_speedLevel = 0;                               // +0x260
    int m_obstacleLevel = 0;                            // +0x268
    int m_minSpeed = 0, m_midSpeed = 0, m_maxSpeed = 0; // +0x274..+0x27C
    bool m_soundsOn = true;                             // +0x280
    std::string m_music;                                // +0x174
};

BRC_REGISTER_NODE("race",
                  [](Godiva& app, const std::string& name) { return std::make_unique<Race>(app, name); });

} // namespace
} // namespace brc::game
