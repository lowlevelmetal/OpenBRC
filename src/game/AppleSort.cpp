// The apple-sorting mini-game (AppleSort.cpp, class AppleSort, ctor 0x4015e0,
// mission M5): a red or green apple drops through ten rows of pegs; eight
// flippers steer it; red apples belong in the right bucket, green ones in the
// left, and three of each win. See docs/engine/nodes.md "AppleSort".
#include "core/Log.h"
#include "core/StringUtil.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "engine/Scripts.h"
#include "engine/SoundManager.h"
#include "game/MiniGameFlags.h"
#include "game/ToolbarNode.h"
#include "game/Widgets.h"

#include <array>
#include <cstdlib>
#include <memory>
#include <string>

namespace brc::game {
namespace {

// Click ids (anim +0x11c).
constexpr int kBucketsClick = 0x2719;
constexpr int kFlipperClick = 10000; // + flipper number 1..8

// Mission M5's completion counter (app+0x268).
constexpr std::size_t kAppleSortDone = PlayerRecord::kMissionDone + 0x10;

// The speeds (units per second) and their limits, set by the static
// initialisers at 0x401400..0x401500: per win, and the floor below which
// misses no longer slow the apple.
constexpr float kSxPerWin = 4.8f, kSyPerWin = 2.0f, kSfPerWin = 5.2f;  // 0x809940 / 0x809944 / 0x80994c
constexpr float kSxMin = 43.2f, kSyMin = 18.0f, kSfMin = 46.8f;       // 0x809948 / 0x809938 / 0x80993c

// ForceHideAnim / ForceShowAnim (COMDAT copies at 0x401520 / 0x401580).
void forceHide(engine::Anim* a) {
    while (a->hideCount() == 0)
        a->hide(true);
    while (a->motionPauseCount() == 0)
        a->pause(true);
}

void forceShow(engine::Anim* a) {
    while (a->hideCount() != 0)
        a->hide(false);
    while (a->motionPauseCount() != 0)
        a->pause(false);
}

// Shows an anim hidden any number of times (hide count set to 1, then shown).
void unhide(engine::Anim* a) {
    if (a->hideCount() != 0) {
        a->setHideCount(1);
        a->hide(false);
    }
}

// SnapToLane (0x4034f0): when the apple's x is inside a lane's window, snaps
// it to the lane's centre. Lane 2 (the drop lane) also catches an apple still
// rolling along the top (y == 0) once it has passed the window's near edge.
bool snapToLane(engine::Anim* a, int lane) {
    const float x = a->x();
    float to = 0;
    switch (lane) {
    case 0:
        if (!(-122.0f < x && x < -116.0f))
            return false;
        to = -119.0f;
        break;
    case 1:
        if (!(-55.0f < x && x < -50.0f))
            return false;
        to = -53.0f;
        break;
    case 2: {
        const double vx = static_cast<double>(a->speedX()) * 1000.0;
        if (!((15.0f < x && x < 21.0f) || (a->y() == 0.0f && vx > 0.0 && x > 15.0f) ||
              (a->y() == 0.0f && vx < 0.0 && x < 21.0f)))
            return false;
        to = 18.0f;
        break;
    }
    case 3:
        if (!(81.0f < x && x < 86.0f))
            return false;
        to = 84.0f;
        break;
    case 4:
        if (!(148.0f < x && x < 153.0f))
            return false;
        to = 151.0f;
        break;
    default:
        return false;
    }
    a->setPosition(to, a->y(), a->z());
    return true;
}

// IsAtRow (0x403710) / SnapToRow (0x403930): the peg rows' y windows and
// centres.
constexpr std::array<float, 10> kRowTop = {40, 67, 96, 126, 154, 183, 212, 239, 268, 298};
constexpr std::array<float, 10> kRowBottom = {50, 77, 106, 136, 164, 193, 222, 249, 278, 308};
constexpr std::array<float, 10> kRowY = {45, 72, 101, 131, 159, 188, 217, 244, 273, 303};

bool isAtRow(engine::Anim* a, int row) {
    const float y = a->y();
    return kRowTop[static_cast<std::size_t>(row)] < y && y < kRowBottom[static_cast<std::size_t>(row)];
}

void snapToRow(engine::Anim* a, int row) { a->setPosition(a->x(), kRowY[static_cast<std::size_t>(row)], a->z()); }

class AppleSort final : public ToolbarNode {
public:
    AppleSort(Godiva& app, const std::string& name) : ToolbarNode(app, name) {
        // 0x4015e0.
        engine().anims().preloadCache("applesort.omt.cache");
        listenToMouseButtons();                    // [0x80a5a0]+0xC
        engine().keyListeners().addListener(this); // [0x80a5a0]+0x18
        Session& session = app.session();
        if (session.activeMission != 4) {
            session.activeMission = 4;
            session.missionTrail = 0;
        }
        setCursor("cursorOut");
        edgeScroll = true;      // +0xC4: either edge leaves (message 13000)
        miniGame = true;        // +0xCE
        miniGameReturn = 0x11;  // +0xD0: appleW
        setSkin(kOut);
        addToolbarButton("tbQuitOut", kQuit);
        addToolbarButton("tbPagerOut", kPager);
        addToolbarButton("tbCameraOut", kCamera);
        addToolbarButton("tbMapOut", kMap);
        addToolbarButton("tbBagOut", kBag);
        addToolbarButton("tbHelpOut", kHelp);
        addToolbarButton("tbHomeOut", kHome);
        if (!ambientLoop().empty())
            releaseAmbient();
        playAmbientLoop("wind", 0x28);

        engine::Anim* buckets = createAnimAtViewport("apple buckets");
        buckets->setClickId(kBucketsClick);
        m_bucketsRollover = std::make_unique<ACRollover>(*this, buckets, 7);
        m_red = createAnimAtViewport("red apple2");
        m_red->setPosition(36, 0, 75);
        m_green = createAnimAtViewport("green apple");
        m_redBounce = createAnimAtViewport("red bounce");
        forceHide(m_redBounce);
        m_greenBounce = createAnimAtViewport("green bounce");
        forceHide(m_greenBounce);
        m_egg = nullptr;
        for (int i = 1; i <= 8; ++i) {
            engine::Anim* f = createAnimAtViewport("flipper" + std::to_string(i));
            f->setClickId(kFlipperClick + i);
            m_flippers[static_cast<std::size_t>(i - 1)] = f;
            m_flipperRollovers[static_cast<std::size_t>(i - 1)] = std::make_unique<ACRollover>(*this, f, 7);
        }
        m_inPlay = 0;
        // The bucket apples, three per colour, stacked and hidden until sorted:
        // green ones ("green appleA".."C") at x -119, red ones ("red appleA"..)
        // at x 151; offsets 0 / -10 / +10 at y 321 / 319 / 317.
        constexpr std::array<float, 2> kBucketX = {-119.0f, 151.0f};
        for (std::size_t colour = 0; colour < 2; ++colour) {
            m_sorted[colour] = 0;
            for (std::size_t i = 0; i < 3; ++i) {
                const std::string instance =
                    std::string(colour == 0 ? "green apple" : "red apple") + static_cast<char>('A' + i);
                engine::Anim* a = createAnimAtViewport(colour == 0 ? "green apple" : "red apple2", instance);
                m_bucket[colour][i] = a;
                static constexpr std::array<float, 3> kDx = {0.0f, -10.0f, 10.0f};
                static constexpr std::array<float, 3> kY = {321.0f, 319.0f, 317.0f};
                a->setPosition(kBucketX[colour] + kDx[i], kY[i], a->z());
                a->hide(true);
            }
        }
        m_apple = nullptr;
        m_started = false;
        m_won = false;
        const auto wins = static_cast<float>(static_cast<int>(app.record().u32(kAppleSortDone)));
        m_sx = kSxPerWin * wins + 72.0f;
        m_sy = kSyPerWin * wins + 30.0f;
        m_streak = 0;
        m_sf = kSfPerWin * wins + 78.0f;
        // The ambient timer (app+0x19C) starts again from zero.
        app.idleTimer().stop();
        app.idleTimer().start();
        if (app.record().u32(kAppleSortDone) == 0) {
            playVoice("pause");
            playVoiceNoFlaps("db0108");
            playVoiceNoFlaps("pause");
            playVoiceNoFlaps("pause");
            playVoiceNoFlaps("db03015");
        } else {
            delayVoice("db0108", 750);
        }
        m_hints = true;
        m_flipperUsed = false;
        m_leaf = createAnimAtViewport("leaf");
        engine().resources().open("GameBarb.omt");
        runScript("GameAmb");
        m_typed.fill(0);
        m_typedCount = 0;
    }

    ~AppleSort() override {
        // 0x401bd0.
        releaseAmbient();
        stopScript("GameAmb");
        app().idleTimer().stop();
        miniGame = false;
        m_bucketsRollover.reset();
        for (auto& r : m_flipperRollovers)
            r.reset();
        if (m_won) {
            PlayerRecord& rec = app().record();
            rec.setU32(kAppleSortDone, rec.u32(kAppleSortDone) + 1);
            app().saveGame();
            flags::appleSortWon = true;
        }
        releaseAmbient();
        engine().resources().close("GameBarb.omt");
        auto& anims = engine().anims();
        for (const char* n : {"red apple2", "apple buckets", "green apple", "red apple", "flipper0", "flipper1",
                              "flipper2", "flipper3", "flipper4", "flipper5", "flipper6", "flipper7", "flipper8",
                              "red bounce", "green bounce", "leaf"})
            anims.remove(n);
    }

    // SpendTime (0x402150).
    void tick() override {
        ToolbarNode::tick();
        if (!m_started || m_won)
            return;
        if (m_inPlay < 1 && !isScriptRunning("Flippers"))
            runScript("Flippers");
        if (m_inPlay > 0)
            updateApple();
    }

    // MouseDown (0x4021b0).
    void onPress(const engine::Pick& pick) override {
        const int id = pick.element ? pick.element->clickId() : 0;
        if (id == kBucketsClick) {
            if (m_started)
                return;
            log::debug("AppleSort: Game Started.");
            m_started = true;
            m_bucketsRollover.reset();
            return;
        }
        if (id > kFlipperClick && id <= kFlipperClick + 8) {
            toggleFlipper(m_flippers[static_cast<std::size_t>(id - kFlipperClick - 1)]);
            return;
        }
        if (id == kHelpClickId)
            playVoice("db03018");
        ToolbarNode::onPress(pick);
    }

    // ListenToMessage (0x402260).
    void listen(int message, const engine::MessageData& data) override {
        if (message == -102) { // mouse button down (event manager)
            if (!m_won)
                ToolbarNode::listen(message, data);
            return;
        }
        if (message == kMsgEdgeScroll) {
            app().session().arriveFromMiniGame = 1;
            gotoNode("appleW");
            return;
        }
        if (message == engine::msg::kInactivity) {
            const int r = std::rand() % 4;
            if (r == 0)
                playEffect("thunder");
            else if (r == 1)
                playEffect("thunder2");
            else
                m_leaf->setSequence(0, true);
            app().idleTimer().setInterval(static_cast<std::uint32_t>((std::rand() % 5 + 1) * 2000));
            return;
        }
        ToolbarNode::listen(message, data);
    }

    // ScriptCommand (0x403a70): "initFlippers" picks four different flippers
    // for the Flippers script to flip, and flips each that is at sequence 0.
    // Other commands are not passed to the toolbar's handler (so the
    // "DelayVoice" script's delay stays 0 here).
    void scriptMessage(engine::Script& script, std::string_view message, int) override {
        if (!str::iequals(message, "initFlippers"))
            return;
        // The draws, in the original's order: a = 1..8, then b, c, d drawn
        // 0..7, each moved on by one and redrawn until all four differ.
        const int a = std::rand() % 8 + 1;
        int b = std::rand() % 8;
        int c = std::rand() % 8;
        int d = std::rand() % 8;
        while (++b == a)
            b = std::rand() % 8;
        while (++c == a || c == b)
            c = std::rand() % 8;
        while (++d == a || d == b || d == c)
            d = std::rand() % 8;
        const std::array<int, 4> picks = {a, b, c, d};
        for (std::size_t k = 0; k < 4; ++k) {
            const int n = picks[k];
            setScriptVar(script.name(), "flipper" + std::to_string(k + 1), "flipper" + std::to_string(n));
            if (flipper(n) == 0)
                setScriptVar(script.name(), "seq" + std::to_string(k + 1), 1.0);
        }
        setScriptVar(script.name(), "delay", 250.0);
    }

    // ScriptEnded (0x403ca0).
    void scriptEnded(engine::Script& script) override {
        if (str::iequals(script.name(), "Flippers"))
            addApple();
        ToolbarNode::scriptEnded(script);
    }

    // SoundEnded (0x403cd0): the win chain back to the apple orchard. The
    // toolbar's handler is not called.
    void soundDone(const std::string& name, const void*, bool) override {
        if (str::iequals(name, "db0112")) {
            playEffect("whinney2");
        } else if (str::iequals(name, "whinney2")) {
            playVoice("db04151");
        } else if (str::iequals(name, "db04151")) {
            app().session().arriveFromMiniGame = 1;
            gotoNode("appleW");
        }
    }

    // KeyDown (0x403d70): the first three characters typed before the game
    // starts are remembered; "YmA" then 'B' with two apples of each colour
    // sorted drops a bonus green apple from the top left.
    void onKeyDown(const engine::InputEvent& e) override {
        const char ch = static_cast<char>(e.ch);
        if (ch != 0) {
            if (!m_started && m_typedCount < 3) {
                m_typed[static_cast<std::size_t>(m_typedCount++)] = ch;
                log::debug("AppleSort: typed \"{}\"", std::string_view(m_typed.data()));
                ToolbarNode::onKeyDown(e);
                return;
            }
            if (m_typed[0] == 'Y' && m_typed[1] == 'm' && m_typed[2] == 'A' && ch == 'B') {
                if (!m_started || m_sorted[0] != m_sorted[1] || m_sorted[1] != 2 || m_egg)
                    return;
                m_egg = createAnimAtViewport("green apple", "green appleZ");
                m_egg->setSequence(1, true);
                m_egg->setSpeed(-100, 70, 0);
                return;
            }
        }
        ToolbarNode::onKeyDown(e);
    }

private:
    void playEffect(std::string_view name, int loops = 1, int volume = 0x70) {
        engine().sounds().playSound(name, -1, false, false, loops, false, volume, -1, 0, false);
    }

    // ToggleFlipper (0x4020e0).
    void toggleFlipper(engine::Anim* f) {
        playEffect("lever");
        m_flipperUsed = true;
        f->setSequence(f->sequence() != 0 ? 0 : 1, true);
    }

    // AddApple (0x4023a0).
    void addApple() {
        log::debug("AppleSort::AddApple()");
        if (m_inPlay != 0)
            return;
        bool red;
        if (m_sorted[0] == 3 || m_sorted[1] == 3) {
            // One colour is done: three times in four the other one comes.
            if (std::rand() % 4 == 0)
                red = m_sorted[0] != 3;
            else
                red = m_sorted[0] == 3;
        } else {
            red = std::rand() % 2 != 0;
        }
        float vx;
        if (red) {
            m_apple = m_red;
            m_red->setPosition(36, 0, 75);
            vx = -m_sx;
        } else {
            m_apple = m_green;
            m_green->setPosition(0, 0, 75);
            vx = m_sx;
        }
        m_apple->setSequence(1, true);
        m_apple->setSpeed(vx, 0, 0);
        playEffect("pop");
        ++m_inPlay;
        if (m_hints)
            playVoiceNoFlaps(red ? "db03016" : "db03017");
    }

    int flipper(int n) const { return m_flippers[static_cast<std::size_t>(n - 1)]->sequence(); }
    void right(engine::Anim* a) const { a->setSpeed(m_sx, m_sy, 0); }
    void left(engine::Anim* a) const { a->setSpeed(-m_sx, m_sy, 0); }
    void fall(engine::Anim* a) const { a->setSpeed(0, m_sf, 0); }
    void bonk() { playEffect("bonk"); }
    // The direction tests compare the mover's stored speed (units per ms)
    // times 1000 with the speed exactly, as the original does.
    bool movingRight(engine::Anim* a) const { return static_cast<double>(a->speedX()) * 1000.0 == m_sx; }
    bool movingLeft(engine::Anim* a) const { return -m_sx == static_cast<double>(a->speedX()) * 1000.0; }

    // UpdateApple (0x402520): the falling apple's path through the pegs. The
    // flippers sit at (row, lane): F1 1/1, F2 1/3, F3 2/2, F4 3/1, F5 3/3,
    // F6 4/2, F7 4/0, F8 6/2; at sequence 0 a flipper sends the apple right.
    void updateApple() {
        engine::Anim* a = m_apple;
        if (!a)
            return;
        const float y = a->y();
        if (y == 0.0f) {
            // Rolling in along the top: drop at the middle lane.
            if (snapToLane(a, 2)) {
                engine().sounds().playSound("apple", -1, false, false, 0, false, 0x30, -1, 0, false);
                fall(a);
                m_row = 0;
            }
        } else if (y <= 315.0f) {
            stepRows(a);
        } else if (!settle(a)) {
            return;
        }
        if (m_egg && m_egg->y() > 165.0f) {
            playEffect("boing");
            engine().anims().remove(m_egg);
            m_egg = nullptr;
        }
    }

    void stepRows(engine::Anim* a) {
        switch (m_row) {
        case 0:
            if (!isAtRow(a, 0))
                return;
            bonk();
            m_row = 1;
            snapToRow(a, 0);
            if (std::rand() % 2 == 0)
                right(a);
            else
                left(a);
            return;
        case 1:
            if (!isAtRow(a, 1))
                return;
            if (snapToLane(a, 1)) {
                if (flipper(1) == 0) {
                    bonk();
                    right(a);
                }
                snapToRow(a, 1);
                m_row = 2;
            } else if (snapToLane(a, 3)) {
                if (flipper(2) == 0) {
                    fall(a);
                    m_row = 3;
                } else {
                    bonk();
                    left(a);
                    m_row = 2;
                }
                snapToRow(a, 1);
            }
            return;
        case 2:
            if (!isAtRow(a, 2))
                return;
            if (snapToLane(a, 0)) {
                bonk();
                right(a);
                m_row = 3;
                snapToRow(a, 2);
            } else if (snapToLane(a, 2)) {
                if (flipper(3) == 0) {
                    bonk();
                    right(a);
                    m_row = 3;
                } else {
                    fall(a);
                    m_row = 4;
                }
                snapToRow(a, 2);
            }
            return;
        case 3:
            if (!isAtRow(a, 3))
                return;
            if (snapToLane(a, 1)) {
                m_row = 4;
                snapToRow(a, 3);
                deflect(a, flipper(4) == 0);
            } else if (snapToLane(a, 3)) {
                m_row = 4;
                snapToRow(a, 3);
                deflect(a, flipper(5) == 0);
            }
            return;
        case 4:
            if (!isAtRow(a, 4))
                return;
            if (snapToLane(a, 0)) {
                if (flipper(7) == 0) {
                    bonk();
                    right(a);
                    m_row = 5;
                } else {
                    fall(a);
                    m_row = 6;
                }
                snapToRow(a, 4);
            } else if (snapToLane(a, 2)) {
                m_row = 5;
                snapToRow(a, 4);
                deflect(a, flipper(6) == 0);
            } else if (snapToLane(a, 4)) {
                m_row = 5;
                snapToRow(a, 4);
                left(a);
                bonk();
            }
            return;
        case 5:
            if (!isAtRow(a, 5))
                return;
            snapToRow(a, 5);
            if (snapToLane(a, 1)) {
                m_row = 6;
                if (movingLeft(a))
                    bonk();
                right(a);
            } else if (snapToLane(a, 3)) {
                m_row = 7;
                fall(a);
            }
            return;
        case 6:
            if (!isAtRow(a, 6))
                return;
            if (snapToLane(a, 0)) {
                right(a);
                snapToRow(a, 6);
                m_row = 7;
                bonk();
            } else if (snapToLane(a, 2)) {
                snapToRow(a, 6);
                if (flipper(8) == 0) {
                    if (movingLeft(a))
                        bonk();
                    right(a);
                    m_row = 8;
                } else {
                    if (movingRight(a))
                        bonk();
                    left(a);
                    m_row = 7;
                }
            }
            return;
        case 7:
            if (!isAtRow(a, 7))
                return;
            if (snapToLane(a, 1)) {
                m_row = 8;
                snapToRow(a, 7);
                if (!movingLeft(a))
                    bonk();
                left(a);
            } else if (snapToLane(a, 3)) {
                m_row = 8;
                snapToRow(a, 7);
                if (!movingRight(a))
                    bonk();
                right(a);
            }
            return;
        case 8:
            if (!isAtRow(a, 8))
                return;
            bonk();
            if (snapToLane(a, 0) || snapToLane(a, 4)) {
                fall(a);
                m_row = 9;
                snapToRow(a, 8);
            }
            return;
        default:
            return;
        }
    }

    // A flipper (or peg) at sequence 0 sends the apple right, otherwise left;
    // "bonk" when that reverses the apple.
    void deflect(engine::Anim* a, bool toRight) {
        if (toRight) {
            if (movingLeft(a))
                bonk();
            right(a);
        } else {
            if (movingRight(a))
                bonk();
            left(a);
        }
    }

    // The apple has reached the buckets (y > 315); false when it missed.
    bool settle(engine::Anim* a) {
        engine().sounds().stopSound("apple");
        std::size_t colour; // 0 green, 1 red
        if (a == m_red && snapToLane(a, 4)) {
            colour = 1;
        } else if (a == m_green && snapToLane(a, 0)) {
            colour = 0;
        } else {
            // Wrong bucket (or none).
            if (!voicePlaying()) {
                const int r = std::rand() % 3;
                playVoice(r == 0 ? "db0110" : r == 2 ? "db0111" : "db04150");
            }
            a->hide(true);
            engine::Anim* bounce = a == m_red ? m_redBounce : m_greenBounce;
            playEffect("boing");
            forceShow(bounce);
            bounce->setSequence(0, true);
            --m_inPlay;
            --m_streak;
            if (m_streak < -2 && kSxMin < m_sx && kSyMin < m_sy && kSfMin < m_sf) {
                m_sx = static_cast<float>(m_sx - 9.6);
                m_sy = static_cast<float>(m_sy - 4.0);
                m_sf = static_cast<float>(m_sf - 10.4);
            }
            playEffect("wrong");
            return false;
        }
        playEffect("right");
        if (m_streak < 0)
            m_streak = 0;
        else
            ++m_streak;
        m_sx = static_cast<float>(m_sx + 9.6);
        m_sy = static_cast<float>(m_sy + 4.0);
        m_sf = static_cast<float>(m_sf + 10.4);
        a->setSpeed(0, 0, 0);
        int& count = m_sorted[colour];
        if (count < 3) {
            unhide(m_bucket[colour][static_cast<std::size_t>(count)]);
            ++count;
            if (m_flipperUsed)
                m_hints = false;
        }
        a->setSequence(0, true);
        a->hide(true);
        --m_inPlay;
        if (m_sorted[0] == 3 && m_sorted[1] == 3) {
            log::debug("AppleSort: *** Game Won ***");
            m_won = true;
            playEffect("win");
            playVoice("db0112");
        } else if (count == 1) {
            playVoice("db0109");
        }
        return true;
    }

    bool m_started = false;           // +0x1C0
    bool m_won = false;               // +0x1C2
    int m_inPlay = 0;                 // +0x1C4
    int m_row = 0;                    // +0x1C8
    engine::Anim* m_apple = nullptr;  // +0x1CC
    std::array<std::array<engine::Anim*, 3>, 2> m_bucket{}; // +0x1D0 [green, red][n]
    std::array<int, 2> m_sorted{};    // +0x1E8 green, +0x1EC red
    engine::Anim* m_red = nullptr;    // +0x1F0
    engine::Anim* m_green = nullptr;  // +0x1F4
    engine::Anim* m_egg = nullptr;    // +0x1F8
    engine::Anim* m_greenBounce = nullptr; // +0x1FC
    engine::Anim* m_redBounce = nullptr;   // +0x200
    std::array<engine::Anim*, 8> m_flippers{}; // +0x208
    std::array<std::unique_ptr<ACRollover>, 8> m_flipperRollovers; // +0x22C
    std::unique_ptr<ACRollover> m_bucketsRollover; // +0x24C
    engine::Anim* m_leaf = nullptr;   // +0x250
    int m_streak = 0;                 // +0x254
    float m_sx = 0, m_sy = 0, m_sf = 0; // +0x258 / +0x25C / +0x260
    bool m_hints = true;              // +0x264
    bool m_flipperUsed = false;       // +0x266
    std::array<char, 4> m_typed{};    // +0x268
    int m_typedCount = 0;             // +0x26C
};

BRC_REGISTER_NODE("applesort", [](Godiva& app, const std::string& name) { return std::make_unique<AppleSort>(app, name); });

} // namespace
} // namespace brc::game
