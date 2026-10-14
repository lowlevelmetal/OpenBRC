// The leapfrog pond (Leapfrog.cpp, class Leapfrog, ctor 0x41f1f0, mission M2):
// triangular peg solitaire on 15 lily pads. Frogs jump over frogs; the evil
// frog may also hop to a neighbouring pad, and must reach the top pad to win
// the coin. See docs/engine/nodes.md "Leapfrog".
#include "core/Log.h"
#include "core/StringUtil.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "engine/Scripts.h"
#include "engine/SoundManager.h"
#include "game/MiniGameFlags.h"
#include "game/ToolbarNode.h"
#include "game/TrailFlags.h"
#include "game/Widgets.h"

#include <array>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>

namespace brc::game {
namespace {

constexpr int kPads = 15;
// Click ids (anim +0x11c).
constexpr int kPadClick = 1234; // + pad index
constexpr int kCoinClick = 0x22E5;
constexpr int kCutOutClick = 0x22E6;
constexpr int kCoinlessFrogClick = 0x22E7;
constexpr int kBigCoinClick = 0x22E8;

// Mission M2's completion counter (app+0x25C).
constexpr std::size_t kLeapfrogDone = PlayerRecord::kMissionDone + 0x04;
// The saddlebag item for the coin.
constexpr int kCoinItem = 0x18;

// The board, pads numbered row by row (0 / 1 2 / 3 4 5 / 6..9 / 10..14), and
// six directions: up-right, right, down-right, down-left, left, up-left.
// [0x800fa8] the neighbouring pad in each direction, [0x800ef0] the pad two
// steps away (a jump target, over the neighbour), -1 = none.
using Table = std::array<std::array<int, 6>, kPads>;
constexpr Table kAdjacent = {{{-1, -1, 2, 1, -1, -1},
                              {0, 2, 4, 3, -1, -1},
                              {-1, -1, 5, 4, 1, 0},
                              {1, 4, 7, 6, -1, -1},
                              {2, 5, 8, 7, 3, 1},
                              {-1, -1, 9, 8, 4, 2},
                              {3, 7, 11, 10, -1, -1},
                              {4, 8, 12, 11, 6, 3},
                              {5, 9, 13, 12, 7, 4},
                              {-1, -1, 14, 13, 8, 5},
                              {6, 11, -1, -1, -1, -1},
                              {7, 12, -1, -1, 10, 6},
                              {8, 13, -1, -1, 11, 7},
                              {9, 14, -1, -1, 12, 8},
                              {-1, -1, -1, -1, 13, 9}}};
constexpr Table kJump = {{{-1, -1, 5, 3, -1, -1},
                          {-1, -1, 8, 6, -1, -1},
                          {-1, -1, 9, 7, -1, -1},
                          {0, 5, 12, 10, -1, -1},
                          {-1, -1, 13, 11, -1, -1},
                          {-1, -1, 14, 12, 3, 0},
                          {1, 8, -1, -1, -1, -1},
                          {2, 9, -1, -1, -1, -1},
                          {-1, -1, -1, -1, 6, 1},
                          {-1, -1, -1, -1, 7, 2},
                          {3, 12, -1, -1, -1, -1},
                          {4, 13, -1, -1, -1, -1},
                          {5, 14, -1, -1, 10, 3},
                          {-1, -1, -1, -1, 11, 4},
                          {-1, -1, -1, -1, 12, 5}}};

// The frog anims' sequences per direction (GetJumpSequence 0x420820 /
// GetStepSequence 0x420890; steps add 6).
constexpr std::array<int, 6> kJumpSequence = {3, 5, 1, 4, 6, 2};
constexpr std::array<int, 6> kStepSequence = {3, 5, 1, 2, 6, 4};

// [0x80105c] the depth of the next frog to move: each move puts the frog
// above everything before it.
float s_frogZ = 3001.0f;

// [0x80a048] when the ducks last swam by (a function static in SpendTime,
// started on its first call).
bool s_duckClockStarted = false;
std::uint32_t s_duckTime = 0;

// Shows an anim hidden any number of times (hide count set to 1, then shown).
void unhide(engine::Anim* a) {
    if (a->hideCount() != 0) {
        a->setHideCount(1);
        a->hide(false);
    }
}

// LeapfrogPad (vtable 0x7f1d38, size 0xCC; name inferred): a rollover button
// on a lily pad plus the frog sitting on it (+0xC8).
struct Pad {
    std::unique_ptr<RolloverButton> button;
    engine::Anim* frog = nullptr;
    engine::Anim* anim() const { return button->anim(); }
};

class Leapfrog final : public ToolbarNode {
public:
    Leapfrog(Godiva& app, const std::string& name) : ToolbarNode(app, name) {
        // 0x41f1f0.
        engine().anims().preloadCache("leapfrog.omt.cache");
        trailflags::bridgeArrival = true; // [0x80a04c]
        s_frogZ = 3001.0f;
        setSkin(kOut);
        setCursor("frogcursor");
        frogCursor();
        addToolbarButton("frogTBQuit", kQuit);
        addToolbarButton("frogTBPager", kPager);
        addToolbarButton("frogTBCamera", kCamera);
        addToolbarButton("frogTBMap", kMap);
        addToolbarButton("frogTBBag", kBag);
        addToolbarButton("frogTBHelp", kHelp);
        addToolbarButton("frogTBHome", kHome);
        m_ducks = createAnimAtViewport("ducks");
        m_ducks->hide(true);
        m_ducks->pause(true);
        m_coin = createAnimAtViewport("coin");
        m_coin->setClickId(kCoinClick);
        m_coin->hide(true);
        m_coinRollover = std::make_unique<ACRollover>(*this, m_coin, 7);
        m_coinlessFrog = createAnimAtViewport("coinlessFrog");
        m_coinlessFrog->setClickId(kCoinlessFrogClick);
        m_coinlessFrog->pause(true);
        m_coinlessFrog->hide(true);
        m_coinlessRollover = std::make_unique<ACRollover>(*this, m_coinlessFrog, 7);
        setSkin(kFrog);
        m_selected = -1;

        // The board: one empty pad anywhere, the evil frog on one of the
        // bottom two rows' pads (6..14), normal frogs everywhere else.
        std::srand(static_cast<unsigned>(std::time(nullptr)));
        const int empty = std::rand() % kPads;
        int evil;
        do
            evil = std::rand() % 9 + 6;
        while (evil == empty);
        for (int i = 0; i < kPads; ++i) {
            Pad& pad = m_pads[static_cast<std::size_t>(i)];
            pad.button = std::make_unique<RolloverButton>(*this);
            pad.button->init("lilly" + std::to_string(i + 1));
            engine::Anim* a = pad.anim();
            if (!a)
                continue;
            a->setClickId(kPadClick + i);
            pad.button->setSequences(1, 2, 0);
            a->setSequence(1, true);
        }
        for (int i = 0; i < kPads; ++i) {
            if (i == empty || i == evil)
                continue;
            engine::Anim* frog = createAnimAtViewport("NormalFrog", "frog" + std::to_string(i + 1));
            frog->setPickable(false);
            frog->addHeaderFlags(8); // keeps its depth across sequences
            normalizeAnimOrigin(frog);
            frog->setPosition(0, 0, 3001.0f);
            placeFrogOnPad(frog, m_pads[static_cast<std::size_t>(i)]);
        }
        m_evilFrog = createAnimAtViewport("evilFrog");
        m_evilFrog->setPosition(0, 0, 3001.0f);
        m_evilFrog->setPickable(false);
        normalizeAnimOrigin(m_evilFrog);
        placeFrogOnPad(m_evilFrog, m_pads[static_cast<std::size_t>(evil)]);

        if (!ambientLoop().empty())
            releaseAmbient();
        playAmbientLoop("frogloop", 0x28);
        m_cutOut = createAnimAtViewport("BarbieCutOut");
        m_cutOut->setClickId(kCutOutClick);
        m_cutOut->setPosition(m_cutOut->x(), m_cutOut->y(), 4200.0f);
        edgeScroll = true;      // +0xC4
        miniGame = true;        // +0xCE
        miniGameReturn = 0x13;  // +0xD0: bridgeS
        if (app.record().u32(kLeapfrogDone) == 0) {
            playVoiceNoFlaps("pause");
            playVoiceNoFlaps("pause");
            playVoiceNoFlaps("db04119A");
            playVoiceNoFlaps("db04119B");
            playVoiceNoFlaps("db04119C");
            playVoiceNoFlaps("db04119D");
            playVoiceNoFlaps("DB9065");
        }
        listenToMouseButtons(); // [0x80a5a0]+0xC
    }

    ~Leapfrog() override {
        // 0x41f9e0.
        releaseAmbient();
        m_coinRollover.reset();
        m_coinlessRollover.reset();
        m_bigCoinRollover.reset();
        miniGame = false;
        for (Pad& pad : m_pads) {
            pad.button.reset();
            pad.frog = nullptr;
        }
        auto& anims = engine().anims();
        for (const char* n :
             {"exit_bgFrog", "exit_yesFrog", "exit_noFrog", "exit_creditsFrog", "BarbieCutOut", "bigCoin", "Coin",
              "CoinlessFrog", "Ducks", "EvilFrog", "FrogAtRest", "FrogSplash", "Lilly1", "Lilly10", "Lilly11",
              "Lilly12", "Lilly13", "Lilly14", "Lilly15", "Lilly2", "Lilly3", "Lilly4", "Lilly5", "Lilly6", "Lilly7",
              "Lilly8", "Lilly9", "normalFrog", "FrogTBBag", "FrogTBCamera", "frogTBhelp", "FrogTBHome", "FrogTBMap",
              "FrogTBPager", "FrogTBQuit", "FrogCursor"})
            anims.remove(n);
    }

    // SpendTime (0x420b30): the ducks every 30 seconds, and the frog cursor
    // except at the screen edges, which lead back to the bridge.
    void tick() override {
        if (!s_duckClockStarted) {
            s_duckClockStarted = true;
            s_duckTime = engine().ticks();
        }
        if (engine().ticks() - s_duckTime > 30000) {
            s_duckTime = engine().ticks();
            m_ducks->setSequence(0, true);
            unhide(m_ducks);
            m_ducks->setPlaybackPauseCount(0);
            m_ducks->setMotionPauseCount(0);
            runScript("ducks");
        }
        const float x = mouseWorldPos().first;
        if (x < 15.0f || x > 604.0f) {
            setCursor("cursorOut");
            if (engine::Anim* c = cursor()) {
                c->addHeaderFlags(8);
                c->setPosition(c->x(), c->y(), 50001.0f);
            }
            edgeScroll = true;
        } else {
            setCursor("frogcursor");
            frogCursor();
        }
        ToolbarNode::tick();
    }

    // MouseDown (0x41fcd0).
    void onPress(const engine::Pick& pick) override {
        const int id = pick.element ? pick.element->clickId() : 0;
        switch (id) {
        case kHelpClickId:
            playVoice("db04119A");
            playVoiceNoFlaps("db04119B");
            playVoiceNoFlaps("db04119C");
            playVoiceNoFlaps("db04119D");
            break;
        case kCoinClick:
            takeCoin();
            break;
        case kCutOutClick:
            m_clickTaken = true;
            gotoNode("leapfrog"); // a new board
            break;
        case kCoinlessFrogClick:
            m_clickTaken = true;
            revealCoin();
            break;
        case kBigCoinClick:
            if (!m_won)
                break;
            m_clickTaken = true;
            m_bigCoin->hide(true);
            playVoiceNoFlaps("db9015");
            break;
        default:
            break;
        }
        for (int i = 0; i < kPads; ++i) {
            if (id != kPadClick + i)
                continue;
            if (!padClicked(i))
                return; // swallowed while a frog is moving
        }
        ToolbarNode::onPress(pick);
    }

    // AnimEnded (0x420270). The base (which tells the scripts) is not called.
    void sequenceEnded(engine::Anim& anim, int seq) override {
        if (&anim == m_evilFrog && m_won && seq == 9 && !m_nudge)
            revealCoin();
        if (&anim == m_moving && seq > 0 && seq < 13) {
            if (m_jumping) {
                finishJump();
                anim.setPosition(anim.x(), anim.y(), 200.0f);
                return;
            }
            if (m_stepping && !m_won) {
                finishStep();
                anim.setPosition(anim.x(), anim.y(), 200.0f);
                return;
            }
        }
        if (m_nudge && &anim == m_evilFrog && seq == 5)
            m_evilFrog->setSpeed(34, 58, 0); // NudgeEvilFrog (0x420cd0); never reached
    }

    // ListenToMessage (0x420d80).
    void listen(int message, const engine::MessageData& data) override {
        if (message == -103 && m_clickTaken) { // the release of a click used above
            m_clickTaken = false;
            return;
        }
        if (message != kMsgEdgeScroll) {
            ToolbarNode::listen(message, data);
            return;
        }
        const float x = mouseWorldPos().first;
        if (x < 15.0f || x > 604.0f) {
            app().session().arriveFromMiniGame = 1;
            gotoNode("bridgeS");
        }
    }

    // ScriptCommand (0x420cf0): the Ducks script's "duck" / "babyduck" /
    // "frog2" do nothing; "initDelay" gives delayJump its delay.
    void scriptMessage(engine::Script& script, std::string_view message, int counter) override {
        if (str::iequals(message, "duck") || str::iequals(message, "babyduck") || str::iequals(message, "frog2"))
            return;
        if (str::iequals(message, "initDelay")) {
            setScriptVar(script.name(), "delay", static_cast<double>(m_jumpDelay));
            return;
        }
        ToolbarNode::scriptMessage(script, message, counter);
    }

    // SoundEnded (0x420e70): back to the bridge after the big coin's line.
    void soundDone(const std::string& name, const void* sound, bool completed) override {
        if (str::iequals(name, "db9015") && m_won) {
            app().session().arriveFromMiniGame = 1;
            gotoNode("bridgeS");
        }
        ToolbarNode::soundDone(name, sound, completed);
    }

private:
    void playEffect(std::string_view name, int volume = 0x70) {
        engine().sounds().playSound(name, -1, false, false, 1, false, volume, -1, 0, false);
    }

    // The frog cursor: re-centred, own depth, above everything.
    void frogCursor() {
        engine::Anim* c = cursor();
        if (!c)
            return;
        c->recenter(); // 0x449e40
        c->addHeaderFlags(8);
        c->setPosition(c->x(), c->y(), 50001.0f);
    }

    // NormalizeAnimOrigin (0x41f920): every sequence's first frame is moved to
    // the origin (the others keep their place relative to it), then the anim
    // goes to (0, 0).
    static void normalizeAnimOrigin(engine::Anim* a) {
        for (int s = 0; s < a->sequenceCount(); ++s) {
            const auto [fx, fy] = a->frameOffset(s, 0);
            for (int f = 0; f < a->frameCount(s); ++f) {
                const auto [x, y] = a->frameOffset(s, f);
                a->setFrameOffset(s, f, x - fx, y - fy);
            }
        }
        a->setPosition(0, 0, a->z());
    }

    // PlaceFrogOnPad (0x41fc80): the frog sits 5 pixels below the pad frame's
    // top-left.
    static void placeFrogOnPad(engine::Anim* frog, Pad& pad) {
        engine::Anim* a = pad.anim();
        frog->setPosition(a->imageOffsetX(), a->imageOffsetY() + 5.0f, frog->z());
        pad.frog = frog;
    }

    Pad& pad(int i) { return m_pads[static_cast<std::size_t>(i)]; }

    // FindJumpDirection (0x4201c0): the direction from `from` to `to` over an
    // occupied pad, or -1.
    int findJump(int from, int to) {
        for (int k = 0; k < 6; ++k) {
            if (kJump[static_cast<std::size_t>(from)][static_cast<std::size_t>(k)] != to)
                continue;
            return pad(kAdjacent[static_cast<std::size_t>(from)][static_cast<std::size_t>(k)]).frog ? k : -1;
        }
        return -1;
    }

    // FindStepDirection (0x420220): the direction to a free neighbour, or -1.
    int findStep(int from, int to) {
        for (int k = 0; k < 6; ++k) {
            if (kAdjacent[static_cast<std::size_t>(from)][static_cast<std::size_t>(k)] != to)
                continue;
            return pad(to).frog ? -1 : k;
        }
        return -1;
    }

    // GetJumpedPad (0x4207d0).
    static int jumpedPad(int from, int to) {
        for (std::size_t k = 0; k < 6; ++k)
            if (kJump[static_cast<std::size_t>(from)][k] == to)
                return kAdjacent[static_cast<std::size_t>(from)][k];
        return -1;
    }

    static int jumpSequence(int from, int to) {
        for (std::size_t k = 0; k < 6; ++k)
            if (kJump[static_cast<std::size_t>(from)][k] == to)
                return kJumpSequence[k];
        return -1;
    }

    static int stepSequence(int from, int to) {
        for (std::size_t k = 0; k < 6; ++k)
            if (kAdjacent[static_cast<std::size_t>(from)][k] == to)
                return kStepSequence[k];
        return -1;
    }

    // The pad part of MouseDown; false when the click is dropped without the
    // toolbar seeing it.
    bool padClicked(int i) {
        m_clickTaken = true;
        if (m_selected == -1) {
            if (!pad(i).frog) {
                playVoice("DB04120");
            } else {
                m_selected = i;
                static constexpr std::array<std::string_view, 4> kCroaks = {"Croak", "Croak2", "Croak3", "Croak4"};
                playEffect(kCroaks[static_cast<std::size_t>(std::rand() % 4)]);
            }
            return true;
        }
        if (m_moving || m_jumping || m_stepping)
            return false;
        if (m_selected == i) {
            m_selected = -1;
            playVoice("DB04120");
            return true;
        }
        if (pad(i).frog) {
            m_selected = i; // another frog
            return true;
        }
        if (pad(m_selected).frog == m_evilFrog) {
            if (findJump(m_selected, i) != -1) {
                startJump(m_selected, i);
                playVoice("DB04121");
                m_selected = -1;
            } else if (findStep(m_selected, i) != -1) {
                startStep(m_selected, i);
                m_selected = -1;
            } else {
                playVoice("DB04120");
            }
            return true;
        }
        // A normal frog only jumps, and never over the evil frog.
        if (findJump(m_selected, i) == -1) {
            playEffect("croak4", -1);
            return true;
        }
        if (pad(jumpedPad(m_selected, i)).frog == m_evilFrog)
            return false;
        startJump(m_selected, i);
        m_selected = -1;
        return true;
    }

    // StartJump (0x4203e0): lift the frog and its victim off the board and
    // play the jump; the delayJump script times the splash sound.
    void startJump(int from, int to) {
        m_from = from;
        m_to = to;
        m_moving = pad(from).frog;
        pad(from).frog = nullptr;
        m_victim = pad(jumpedPad(m_from, m_to)).frog;
        m_jumped = jumpedPad(m_from, m_to);
        pad(jumpedPad(m_from, m_to)).frog = nullptr;
        m_jumping = true;
        m_moving->setSequence(jumpSequence(m_from, m_to), true);
        s_frogZ += 1.0f;
        m_moving->setPosition(m_moving->x(), m_moving->y(), s_frogZ);
        switch (jumpSequence(m_from, m_to)) {
        case 1:
        case 4:
            m_jumpDelay = 1400;
            break;
        case 2:
        case 3:
            m_jumpDelay = 800;
            break;
        case 5:
        case 6:
            m_jumpDelay = 1100;
            break;
        default:
            return;
        }
        runScript("delayJump");
    }

    // StartStep (0x420900): the evil frog hops to a neighbouring pad.
    void startStep(int from, int to) {
        m_from = from;
        m_to = to;
        m_moving = pad(from).frog;
        pad(from).frog = nullptr;
        m_stepping = true;
        m_moving->setSequence(stepSequence(m_from, m_to) + 6, true);
        switch (stepSequence(m_from, m_to)) {
        case 1:
        case 4:
            m_jumpDelay = 1300;
            break;
        case 2:
        case 3:
            m_jumpDelay = 700;
            break;
        case 5:
        case 6:
            m_jumpDelay = 1000;
            break;
        default:
            return;
        }
        runScript("delayJump");
    }

    // FinishJump (0x420580): land on the target pad; the jumped frog falls
    // into the pond (sequence 7..10 by where it sat) with a splash.
    void finishJump() {
        m_moving->setSpeed(0, 0, 0);
        m_moving->setSequence(0, true);
        s_frogZ += 1.0f;
        engine::Anim* target = pad(m_to).anim();
        m_moving->setPosition(target->imageOffsetX(), target->imageOffsetY(), s_frogZ);
        pad(m_to).frog = m_moving;
        m_jumping = false;
        int seq = 0;
        switch (m_jumped) {
        case 1:
        case 3:
        case 6:
            seq = 7;
            break;
        case 2:
        case 4:
        case 7:
        case 11:
            seq = 8;
            break;
        case 5:
        case 8:
        case 12:
            seq = 9;
            break;
        case 9:
        case 13:
            seq = 10;
            break;
        default:
            break;
        }
        if (seq != 0) {
            m_victim->setSequence(seq, true);
            s_frogZ += 1.0f;
            m_victim->setPosition(m_victim->x(), m_victim->y(), s_frogZ);
            runScript("Croak");
        }
        m_victim = nullptr;
        if (m_moving == m_evilFrog && m_to == 0)
            win();
        m_moving = nullptr;
    }

    // FinishStep (0x420a10).
    void finishStep() {
        m_moving->setSequence(0, true);
        s_frogZ += 1.0f;
        engine::Anim* target = pad(m_to).anim();
        m_moving->setPosition(target->imageOffsetX(), target->imageOffsetY(), s_frogZ);
        pad(m_to).frog = m_moving;
        m_stepping = false;
        if (m_moving == m_evilFrog && m_to == 0)
            win();
        m_moving = nullptr;
    }

    // Win (0x420ad0): the evil frog's celebration (sequence 9). The step flag
    // stays set, so the board takes no more moves.
    void win() {
        m_stepping = true;
        m_evilFrog->setSequence(9, true);
        m_won = true;
        playEffect("Win");
    }

    // The coin appears where the evil frog was (AnimEnded on its sequence 9,
    // or a click on the coinless frog).
    void revealCoin() {
        m_coinlessFrog->setPlaybackPauseCount(0);
        m_coinlessFrog->setMotionPauseCount(0);
        m_evilFrog->hide(true);
        unhide(m_coin);
        playEffect("coin");
        playVoice("db9016n");
    }

    // Clicking the coin: it goes into the saddlebag; the big coin shows and
    // the mission counts. Saves the game.
    void takeCoin() {
        m_clickTaken = true;
        m_coin->hide(true);
        m_bigCoin = createAnimAtViewport("bigCoin");
        m_bigCoin->setClickId(kBigCoinClick);
        m_bigCoin->setPosition(m_bigCoin->x(), m_bigCoin->y(), s_frogZ + 100.0f);
        m_bigCoinRollover = std::make_unique<ACRollover>(*this, m_bigCoin, 7);
        playVoice("DB9016");
        Godiva& a = app();
        a.session().arriveFromMiniGame = 1;
        a.record().setU32(kLeapfrogDone, a.record().u32(kLeapfrogDone) + 1);
        a.saveGame();
        flags::leapfrogCoinTaken = true;
        addBagItem(kCoinItem, true);
        a.session().missionTrail = 7;
    }

    bool m_clickTaken = false;        // +0x1C0: swallow the next button release
    int m_jumpDelay = 0;              // +0x1C4
    int m_jumped = 0;                 // +0x1C8
    bool m_nudge = false;             // +0x1CC: never set
    bool m_won = false;               // +0x1D0
    int m_selected = -1;              // +0x1D8
    std::array<Pad, kPads> m_pads;    // +0x1DC
    engine::Anim* m_evilFrog = nullptr; // +0x218
    std::unique_ptr<ACRollover> m_coinRollover, m_coinlessRollover, m_bigCoinRollover; // +0x234/+0x238/+0x23C
    engine::Anim* m_moving = nullptr; // +0x240
    engine::Anim* m_ducks = nullptr;  // +0x244
    engine::Anim* m_coin = nullptr;   // +0x248
    engine::Anim* m_cutOut = nullptr; // +0x24C
    engine::Anim* m_victim = nullptr; // +0x250
    engine::Anim* m_coinlessFrog = nullptr; // +0x254
    engine::Anim* m_bigCoin = nullptr; // +0x258
    bool m_jumping = false;           // +0x260
    bool m_stepping = false;          // +0x264
    int m_from = -1, m_to = -1;       // +0x268 / +0x26C
};

BRC_REGISTER_NODE("leapfrog", [](Godiva& app, const std::string& name) { return std::make_unique<Leapfrog>(app, name); });

} // namespace
} // namespace brc::game
