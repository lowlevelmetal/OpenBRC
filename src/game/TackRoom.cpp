// The tack room (tackroom.cpp, class TackRoom, ctor 0x42c0b0): a panorama with
// the player's horse, the pad, the two saddles and the bridle on the wall
// (picked up as the cursor and dropped on the horse), four treats that start
// the feeding close-up movie, the cat, the tour on the first visit, and the
// ways to the stable (left edge, horse sign), the yard (right edge), the
// bulletin board and grooming (brush or comb). See docs/engine/nodes.md
// "TackRoom".
#include "core/Log.h"
#include "core/StringUtil.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "engine/Movie.h"
#include "engine/Resources.h"
#include "engine/SoundManager.h"
#include "game/MiniGameFlags.h"
#include "game/ToolbarNode.h"
#include "game/Widgets.h"

#include <array>
#include <format>
#include <memory>

namespace brc::game {
namespace {

// Click ids (anim +0x11c).
constexpr int kBulletinBoardClick = 0x348B;
constexpr int kCarrotClick = 0x4D9F, kAppleClick = 0x4DA0, kSugarClick = 0x4DA1, kStrawberryClick = 0x4DA2;
constexpr int kPadClick = 0x4DA3, kEnglishSaddleClick = 0x4DA4, kFancySaddleClick = 0x4DA5,
              kBridleClick = 0x4DA6;
constexpr int kHorseClick = 0x4DA7, kCatClick = 0x4DA8, kCombClick = 0x4DA9, kBrushClick = 0x4DAA;
constexpr int kTackOnClick = 0x4DAB, kHorseSignClick = 0x4DAC;

// Treats: barbieFeed's sequence for each.
constexpr int kCarrot = 1, kApple = 2, kStrawberry = 3, kSugar = 4;

constexpr int kPanRight = 1200; // +0x160 = 0x4B0

// The process-wide tack state; the original keeps it in globals, so it lasts
// for the session but is not saved.
int s_tackState = 0;        // [0x80a150]: 0 none, 1 pad, 2 pad + English saddle, 3 pad + fancy saddle
bool s_bridleOn = false;    // [0x80a154]
bool s_tourRunning = false; // [0x80a14c]: the TackTour script is running (until DB9059 ends)
std::uint32_t s_lastIdleTick = 0xFFFFFFFF; // [0x801ff8] (set, never compared)

// Per-coat assets, by the player's horse (0 palomino, 1 white, 2 paint, 3 dapple).
struct Coat {
    std::string_view suffix; // TackAmb<suffix>, TackFeed<suffix>, TackSubAmb<suffix>
    std::string_view file;   // tackroom<file>.omt(.cache)
    std::string_view movie;  // smk\FeedCloseUp<movie>.smk
};
constexpr std::array<Coat, 4> kCoats = {{
    {"Pal", "pal", "Pal"},
    {"White", "white", "White"},
    {"Paint", "paint", "Paint"},
    {"Dap", "dap", "Dap"},
}};

// The anims the tack room releases when it is left (ReleaseAnims 0x42d3c0).
constexpr std::array<std::string_view, 42> kReleasedAnims = {"BridleEat",
                                                             "brushWall",
                                                             "BulletinBoardSmall",
                                                             "combWall",
                                                             "EnglishSaddle",
                                                             "EnglishSaddleCursor",
                                                             "FancySaddleCursor",
                                                             "HorseSign",
                                                             "MouthFlapTackroom",
                                                             "PadCursor",
                                                             "SubtleAmbBridle",
                                                             "TackAmbBridle",
                                                             "tackroom_apple",
                                                             "barbieAmbient",
                                                             "bridle",
                                                             "cat",
                                                             "fancysaddle",
                                                             "pad",
                                                             "posTR",
                                                             "tackOn",
                                                             "tackroom_carrot",
                                                             "tackroom_strawberry",
                                                             "tackroom_sugar",
                                                             "TackAmbPal",
                                                             "TackFeedPal",
                                                             "TackSubAmbPal",
                                                             "TackAmbWhite",
                                                             "TackFeedWhite",
                                                             "TackSubAmbWhite",
                                                             "TackAmbPaint",
                                                             "TackFeedPaint",
                                                             "TackSubAmbPaint",
                                                             "TackAmbDap",
                                                             "TackFeedDap",
                                                             "TackSubAmbDap",
                                                             "certificatebtn",
                                                             "handbookbtn",
                                                             "cluebtn",
                                                             "starriderbtn",
                                                             "moonriderbtn",
                                                             "secretvalleyriderbtn",
                                                             "photobtn"};

// The inline "show" of the original: a hidden anim gets its hide counter set
// to 1, then Show.
void show(engine::Anim* a) {
    if (a && a->hideCount() != 0) {
        a->setHideCount(1);
        a->hide(false);
    }
}

void hide(engine::Anim* a) {
    if (a)
        a->hide(true);
}

// anim+0xe0 = 0 (playback) and +0x5c = 0 (motion).
void run(engine::Anim* a) {
    if (!a)
        return;
    a->setPlaybackPauseCount(0);
    a->setMotionPauseCount(0);
}

// The bridle overlays copy the horse anim's sequence and frame and sit just
// in front of it.
void follow(engine::Anim* overlay, const engine::Anim* horse) {
    if (!overlay || !horse)
        return;
    overlay->setSequenceFrame(horse->sequence(), horse->frameIndex());
    run(overlay);
    show(overlay);
    overlay->setPosition(overlay->x(), overlay->y(), horse->z() + 1.0f);
}

class TackRoom final : public ToolbarNode {
public:
    TackRoom(Godiva& app, const std::string& name) : ToolbarNode(app, name) {
        // 0x42c0b0.
        if (!engine().anims().find("BridleEat"))
            engine().anims().preloadCache("tackroom.omt.cache");
        app.session().arriveFromMiniGame = 0;
        if (app.record().horse() == PlayerRecord::kNoHorse) {
            gotoNode("stable");
            m_redirected = true;
            return;
        }
        // ([0x80a40c] = 1: logs anim creation times; not ported.)
        setSkin(kIn);
        setCamera(cameraX() + 320.0f, cameraY());
        addToolbarButton("tbQuitIn", kQuit);
        addToolbarButton("tbPagerIn", kPager);
        addToolbarButton("tbCameraIn", kCamera);
        addToolbarButton("tbMapIn", kMap);
        addToolbarButton("tbBagIn", kBag);
        addToolbarButton("tbHelpIn", kHelp);
        addToolbarButton("tbHomeIn", kHome);
        moveRollovers(cameraX(), cameraY());
        m_panLeft = 0;
        m_panRight = kPanRight;
        setPanLimits(m_panLeft, m_panRight);
        // (+0xFC = 1: a toolbar field nothing is known to read.)
        flags::carouselVisited = false; // [0x809afc] = 0
        setCursor("cursorIn");
        edgeBoth = true;
        if (!ambientLoop().empty())
            releaseAmbient();
        playAmbientLoop("stableamb2", 0x28);
        createScene();
        startTackTourIfFirstVisit();
        auto& session = app.session();
        if (session.finaleGreeting == 0) {
            if (!s_tourRunning) {
                playVoiceNoFlaps("pause");
                std::string_view greeting;
                if (engine().rand() % 5 != 0)
                    greeting = "DB0537";
                else if (engine().rand() % 5 != 0)
                    greeting = "DB0530";
                else
                    greeting = s_tackState == 0 ? "DB04107" : "DB9023";
                playVoiceNoFlaps(greeting);
            }
        } else {
            session.finaleGreeting = 0;
            playVoiceNoFlaps("DB9025");
        }
        auto& rec = app.record();
        rec.setU32(PlayerRecord::kTackroomVisits, rec.u32(PlayerRecord::kTackroomVisits) + 1);
        checkAllMissionsDone();
        listenToMouseButtons(); // [0x80a5a0]+0xC
    }

    ~TackRoom() override {
        // 0x42c3f0.
        releaseAmbient();
        m_bulletinBoardRollover.reset();
        m_appleRollover.reset();
        m_bridleRollover.reset();
        m_brushRollover.reset();
        m_carrotRollover.reset();
        m_catRollover.reset();
        m_combRollover.reset();
        m_englishSaddleRollover.reset();
        m_fancySaddleRollover.reset();
        m_padRollover.reset();
        m_strawberryRollover.reset();
        m_sugarRollover.reset();
        m_horseSignRollover.reset();
        m_movie.reset();
        app().session().wasRiding = 0;
        engine().sounds().getSound("toolbar", false, false);
    }

    // Exit (0x42d380): the anims stay for the bulletin board, which returns here.
    void leaving(const std::string& nextNode) override {
        releaseAnims(str::iequals(nextNode, "bulletinboard"));
        ToolbarNode::leaving(nextNode);
    }

    // ListenToMessage (0x42ea00): the left edge leads to the stable and the
    // right edge to the yard once the view has scrolled to that end.
    void listen(int message, const engine::MessageData& data) override {
        if (message == kMsgEdgeLeft) {
            if (cameraX() < 10.0f)
                gotoNode("stable");
        } else if (message == kMsgEdgeRight) {
            if (cameraX() > 555.0f)
                gotoNode("yard");
        }
        ToolbarNode::listen(message, data);
    }

    // KeyDown (0x42ec30): F in debug mode jumps to the finale.
    void onKeyDown(const engine::InputEvent& e) override {
        if ((e.ch == 'F' || e.ch == 'f') && app().session().debugMode != 0)
            gotoNode("finalpayoff");
        ToolbarNode::onKeyDown(e);
    }

    // SpendTime (0x42ec70).
    void tick() override {
        if (m_redirected) {
            ToolbarNode::tick();
            return;
        }
        auto& rec = app().record();
        if (rec.u32(PlayerRecord::kFinaleState) == 2) {
            rec.setU32(PlayerRecord::kFinaleState, 3);
            playVoiceNoFlaps("DB04163");
            app().saveGame();
        }
        if (m_movie && m_movie->done() && m_moviePlaying) {
            m_movie->setPaused(true);
            m_movie->gotoFrame(1);
            m_movie->hide(true);
            m_movie.reset();
            finishFeeding();
            m_moviePlaying = false;
        }
        if (m_bridleOn) {
            for (auto [overlay, horse] :
                 {std::pair{m_tackAmbBridle, m_tackAmb}, std::pair{m_subtleAmbBridle, m_tackSubAmb},
                  std::pair{m_bridleEat, m_tackFeed}}) {
                if (horse && horse->visible())
                    follow(overlay, horse);
                else
                    hide(overlay);
            }
        }
        // Panorama scrolling at the screen edges.
        const int x = static_cast<std::int16_t>(static_cast<int>(mouseWorldPos().first));
        if (x < 20)
            scrollLeft();
        else if (x < 620)
            scrollStop();
        else
            scrollRight();
        if (!m_busy)
            idleAnimations();
        // Barbie's mouth flaps while she talks standing still.
        if (!voicePlaying())
            hide(m_mouthFlap);
        else if (!barbieFeeding() && m_barbieAmbient && m_barbieAmbient->stopped() && !m_busy) {
            show(m_mouthFlap);
            run(m_mouthFlap);
        } else {
            hide(m_mouthFlap);
        }
        ToolbarNode::tick();
    }

    // MouseDown (0x42d590): ignored while a feeding runs.
    void onPress(const engine::Pick& pick) override {
        if (!m_busy && !m_redirected)
            dispatch(pick.element ? pick.element->clickId() : 0);
        toolbarMouseDown(pick);
    }

    // AnimEnded (0x42ea90).
    void sequenceEnded(engine::Anim& anim, int seq) override {
        if (&anim == m_barbieFeed && seq > 0) {
            // Barbie has held out the treat: the horse eats.
            startEatingPose();
            m_lastFood = seq;
        } else if (&anim == m_tackFeed && seq == 0) {
            playFeedCloseUp();
        } else if (&anim != m_tackSubAmb) {
            if (&anim == m_tackAmb && m_tackFeed && m_tackFeed->stopped()) {
                hide(m_tackAmb);
                if (m_bridleOn)
                    hide(m_tackAmbBridle);
                show(m_tackSubAmb);
                if (m_bridleOn)
                    show(m_subtleAmbBridle);
            } else if (&anim == m_tackFeed && seq == 1) {
                // The horse has finished eating.
                hide(m_tackFeed);
                if (m_bridleOn)
                    hide(m_bridleEat);
                hide(m_tackAmb);
                if (m_bridleOn)
                    hide(m_tackAmbBridle);
                show(m_tackSubAmb);
                if (m_bridleOn)
                    show(m_subtleAmbBridle);
                m_busy = false;
            }
        }
        ToolbarNode::sequenceEnded(anim, seq);
    }

    // SoundEnded (0x42f3d0): the tour is over when its last line ends.
    void soundDone(const std::string& name, const void* sound, bool completed) override {
        if (str::iequals(name, "DB9059"))
            s_tourRunning = false;
        ToolbarNode::soundDone(name, sound, completed);
    }

    // ScriptCommand (0x42f280): the TackTour script's messages.
    void scriptMessage(engine::Script& script, std::string_view message, int counter) override {
        if (str::iequals(message, "toolbartour")) {
            playVoiceNoFlaps("DB9002");
            playVoiceNoFlaps("toolbar");
            playVoiceNoFlaps("DB02050n");
        } else if (str::iequals(message, "ScrollMiddle")) {
            tourScrollMiddle();
        } else if (str::iequals(message, "ScrollRight")) {
            tourScrollRight();
        } else if (str::iequals(message, "db0560")) {
            playVoiceNoFlaps("db0560");
        } else if (str::iequals(message, "db0462")) {
            playVoiceNoFlaps("db0462");
        } else if (str::iequals(message, "db0530")) {
            playVoiceNoFlaps("db0530");
        } else if (str::iequals(message, "db0800")) {
            // nothing
        } else if (str::iequals(message, "tackTourEnded")) {
            // (The left limit set by ScrollMiddle stays.)
            m_panRight = kPanRight;
            setPanLimits(m_panLeft, m_panRight);
            setScrollVelocity(0.0f, 0.0f);
            playVoiceNoFlaps("DB0560");
            playVoiceNoFlaps("DB0540");
            playVoiceNoFlaps("DB0541");
            playVoiceNoFlaps("DB9059");
        }
        ToolbarNode::scriptMessage(script, message, counter);
    }

private:
    void playEffect(std::string_view name, int volume = 0x70) {
        engine().sounds().playSound(name, -1, false, false, 1, false, volume, -1, 0, false);
    }

    engine::Anim* viewAnim(std::string_view name) { return createAnimAtViewport(name); }

    std::unique_ptr<ACRollover> rollover(engine::Anim* a) {
        return std::make_unique<ACRollover>(*this, a, 7);
    }

    // CreateScene (0x42c550): the anims are placed relative to the view at x 320.
    void createScene() {
        m_barbieAmbient = viewAnim("barbieAmbient");
        if (m_barbieAmbient)
            m_barbieAmbient->setPosition(324.0f, m_barbieAmbient->y(), m_barbieAmbient->z());
        run(m_barbieAmbient);
        m_bridle = viewAnim("bridle");
        if (m_bridle) {
            m_bridle->move(8.0f, 0, 0);
            m_bridle->setClickId(kBridleClick);
        }
        m_bridleRollover = rollover(m_bridle);
        run(m_bridle);
        m_bridleEat = viewAnim("bridleEat");
        if (m_bridleEat)
            m_bridleEat->move(7.0f, 0, 200.0f);
        run(m_bridleEat);
        m_brushWall = viewAnim("brushWall");
        if (m_brushWall) {
            m_brushWall->move(-40.0f, 0, 0);
            m_brushWall->setClickId(kBrushClick);
        }
        m_brushRollover = rollover(m_brushWall);
        m_bulletinBoard = createAnim("bulletinBoardSmall"); // not placed at the view
        if (m_bulletinBoard) {
            m_bulletinBoard->setClickId(kBulletinBoardClick);
            m_bulletinBoard->move(-162.0f, 0, 0);
        }
        m_bulletinBoardRollover = rollover(m_bulletinBoard);
        m_cat = viewAnim("cat");
        if (m_cat) {
            m_cat->setSequence(0, true);
            m_cat->move(240.0f, 0, 0);
            m_cat->setClickId(kCatClick);
        }
        m_catRollover = rollover(m_cat);
        run(m_cat);
        m_combWall = viewAnim("combWall");
        if (m_combWall) {
            m_combWall->move(-40.0f, 0, 0);
            m_combWall->setClickId(kCombClick);
        }
        m_combRollover = rollover(m_combWall);
        m_englishSaddle = viewAnim("englishsaddle");
        if (m_englishSaddle) {
            m_englishSaddle->move(-40.0f, 0, 0);
            m_englishSaddle->setClickId(kEnglishSaddleClick);
        }
        m_englishSaddleRollover = rollover(m_englishSaddle);
        m_horseSign = viewAnim("horseSign");
        if (m_horseSign) {
            m_horseSign->move(-340.0f, 0, 0);
            m_horseSign->setClickId(kHorseSignClick);
        }
        m_horseSignRollover = rollover(m_horseSign);
        m_mouthFlap = viewAnim("MouthFlapTackroom");
        if (m_mouthFlap) {
            m_mouthFlap->move(4.0f, 0, 0);
            m_mouthFlap->setPosition(m_mouthFlap->x(), m_mouthFlap->y(), 500.0f);
            m_mouthFlap->hide(true);
        }
        m_pad = viewAnim("pad");
        if (m_pad) {
            m_pad->move(10.0f, 0, 0);
            m_pad->setPosition(m_pad->x(), m_pad->y(), m_englishSaddle ? m_englishSaddle->z() : m_pad->z());
            m_pad->setClickId(kPadClick);
        }
        m_padRollover = rollover(m_pad);
        m_posTR = viewAnim("posTR");
        if (m_posTR) {
            m_posTR->setClickId(kBulletinBoardClick);
            m_posTR->setPosition(m_posTR->x() - 457.0f, m_posTR->y() - 16.0f,
                                 (m_bulletinBoard ? m_bulletinBoard->z() : 0.0f) + 1.0f);
        }
        m_subtleAmbBridle = viewAnim("subtleAmbBridle");
        if (m_subtleAmbBridle)
            m_subtleAmbBridle->move(7.0f, 0, 200.0f);
        run(m_subtleAmbBridle);
        m_tackAmbBridle = viewAnim("TackAmbBridle");
        if (m_tackAmbBridle)
            m_tackAmbBridle->move(-92.0f, 0, 200.0f);
        run(m_tackAmbBridle);
        m_tackOn = viewAnim("tackOn");
        if (m_tackOn) {
            m_tackOn->move(10.0f, 0, 300.0f);
            m_tackOn->addHeaderFlags(formats::anim_flags::kKeepDepth);
            m_tackOn->setClickId(kTackOnClick);
            m_tackOn->hide(true);
        }
        m_apple = treat("tackroom_apple", kAppleClick, m_appleRollover);
        m_carrot = treat("tackroom_carrot", kCarrotClick, m_carrotRollover);
        m_strawberry = treat("tackroom_strawberry", kStrawberryClick, m_strawberryRollover);
        m_sugar = treat("tackroom_sugar", kSugarClick, m_sugarRollover);
        m_fancyOn = m_englishOn = m_bridleOn = m_padOn = false;
        // The fancy saddle is in the chest until it is opened (cottage).
        if (app().record().u16(PlayerRecord::kChestOpened) != 0) {
            m_fancySaddle = viewAnim("FancySaddle");
            if (m_fancySaddle) {
                m_fancySaddle->move(10.0f, 0, 0);
                m_fancySaddle->setClickId(kFancySaddleClick);
            }
            m_fancySaddleRollover = rollover(m_fancySaddle);
        }

        std::uint32_t horse = app().record().horse();
        if (horse > 3) {
            log::debug("What the hey!  They are in the tackroom with no horse chosen.");
            horse = 2;
        }
        const Coat& coat = kCoats[horse];
        auto& anims = engine().anims();
        if (!anims.find(std::format("TackAmb{}", coat.suffix)))
            anims.preloadCache(std::format("tackroom{}.omt.cache", coat.file));
        const std::string omt = std::format("tackroom{}.omt", coat.file);
        engine().resources().open(omt);
        m_tackAmb = viewAnim(std::format("TackAmb{}", coat.suffix));
        m_tackFeed = viewAnim(std::format("TackFeed{}", coat.suffix));
        m_tackSubAmb = viewAnim(std::format("TackSubAmb{}", coat.suffix));
        engine().resources().close(omt);
        run(m_tackAmb);
        run(m_tackFeed);
        run(m_tackSubAmb);
        for (auto* a : {m_tackFeed, m_bridleEat, m_tackAmb, m_tackAmbBridle, m_subtleAmbBridle})
            hide(a);
        for (auto* a : {m_tackFeed, m_bridleEat})
            if (a)
                a->pause(true);
        constexpr float kHorseX = 223.0f; // [0x801ff4]
        if (m_tackFeed)
            m_tackFeed->setPosition(kHorseX + 12.0f, m_tackFeed->y(), 499.0f);
        if (m_tackAmb)
            m_tackAmb->setPosition(kHorseX + 6.0f, m_tackAmb->y(), 499.0f);
        if (m_tackSubAmb)
            m_tackSubAmb->setPosition(kHorseX, m_tackSubAmb->y(), 499.0f);
        if (m_tackAmb)
            m_tackAmb->setClickId(kHorseClick);
        if (m_tackSubAmb)
            m_tackSubAmb->setClickId(kHorseClick);
        // ([0x80a148] = 0.) The tack from the last visit is still on the horse.
        if (s_tackState >= 1 && s_tackState <= 3) {
            show(m_tackOn);
            hide(m_pad);
            if (s_tackState == 2)
                hide(m_englishSaddle);
            else if (s_tackState == 3)
                hide(m_fancySaddle);
            m_padOn = true;
            m_englishOn = s_tackState == 2;
            m_fancyOn = s_tackState == 3;
            if (m_tackOn)
                m_tackOn->setSequence(s_tackState - 1, true);
        }
        if (s_bridleOn) {
            m_bridleOn = true;
            hide(m_bridle);
            if (m_tackAmb && m_tackAmb->visible())
                follow(m_tackAmbBridle, m_tackAmb);
            else if (m_tackSubAmb && m_tackSubAmb->visible())
                follow(m_subtleAmbBridle, m_tackSubAmb);
        }
        setCamera(cameraX() - 275.0f, cameraY());
        moveRollovers(cameraX(), cameraY());
        // ([0x80a158] = this.)
    }

    engine::Anim* treat(std::string_view name, int click, std::unique_ptr<ACRollover>& roll) {
        engine::Anim* a = viewAnim(name);
        if (a) {
            a->move(-40.0f, 0, 0);
            a->setClickId(click);
        }
        roll = rollover(a);
        return a;
    }

    // ReleaseAnims (0x42d3c0): kept anims are only unlinked from the node.
    void releaseAnims(bool keep) {
        auto& anims = engine().anims();
        for (auto name : kReleasedAnims) {
            if (keep) {
                if (engine::Anim* a = anims.find(name))
                    a->unlink();
            } else {
                anims.remove(name);
            }
        }
    }

    bool holding(std::string_view cursorName) const {
        return cursor() && str::iequals(cursor()->name(), cursorName);
    }

    // Takes a piece of tack off the wall: it becomes the cursor.
    void pickUp(engine::Anim* wallAnim, std::string_view cursorName) {
        hide(wallAnim);
        edgeBoth = false;
        setCursor(cursorName);
        if (engine::Anim* c = cursor()) {
            c->recenter();
            c->setPosition(c->x(), c->y(), 2000.0f);
            c->setSequence(0, true);
        }
    }

    // Back to the hand cursor (the piece went on the horse or back on the wall).
    void dropToHand() {
        setCursor("cursorIn");
        edgeBoth = true;
        if (cursor())
            cursor()->setSequence(0, true);
    }

    void showTack() {
        if (m_tackOn)
            m_tackOn->setSequence(s_tackState - 1, true);
    }

    void putOnEnglishSaddle() {
        m_englishOn = true;
        s_tackState = 2;
        app().record().setU32(PlayerRecord::kSaddle, 0);
        showTack();
        dropToHand();
    }

    void putOnFancySaddle() {
        m_fancyOn = true;
        s_tackState = 3;
        app().record().setU32(PlayerRecord::kSaddle, 1);
        showTack();
        dropToHand();
    }

    // "You need to put the pad on first."
    void needPad(bool pause) {
        if (pause)
            playVoiceNoFlaps("pause");
        playVoice("DB0565");
    }

    void dispatch(int id) {
        switch (id) {
        case kBulletinBoardClick:
            gotoNode("bulletinboard");
            break;
        case kHelpClickId:
            playVoice("Db04107");
            break;
        case kCarrotClick:
            feedHorse(kCarrot);
            break;
        case kAppleClick:
            feedHorse(kApple);
            break;
        case kSugarClick:
            feedHorse(kSugar);
            break;
        case kStrawberryClick:
            feedHorse(kStrawberry);
            break;
        case kPadClick:
            // The pad comes off the wall only when no saddle is on the horse.
            if (m_englishOn || m_fancyOn)
                break;
            if (holding("EnglishSaddleCursor"))
                show(m_englishSaddle);
            else if (holding("fancySaddleCursor"))
                show(m_fancySaddle);
            else if (holding("BridleCursor"))
                show(m_bridle);
            pickUp(m_pad, "PadCursor");
            m_padOn = false;
            break;
        case kEnglishSaddleClick:
        case kFancySaddleClick: {
            const bool english = id == kEnglishSaddleClick;
            if (holding("BridleCursor")) {
                show(m_bridle);
                dropToHand();
            }
            if (s_tackState == 0) {
                // Taking a saddle puts the pad on the horse first.
                m_padOn = true;
                s_tackState = 1;
                show(m_tackOn);
                hide(m_pad);
                showTack();
                dropToHand();
            }
            if (holding("PadCursor"))
                show(m_pad);
            else if (holding(english ? "fancySaddleCursor" : "BridleCursor"))
                show(english ? m_fancySaddle : m_bridle);
            else if (holding(english ? "BridleCursor" : "EnglishSaddleCursor"))
                show(english ? m_bridle : m_englishSaddle);
            if (english) {
                pickUp(m_englishSaddle, "EnglishSaddleCursor");
                m_englishOn = false;
            } else {
                pickUp(m_fancySaddle, "fancySaddleCursor");
                m_fancyOn = false;
            }
            break;
        }
        case kBridleClick:
            if (holding("PadCursor"))
                show(m_pad);
            else if (holding("fancySaddleCursor"))
                show(m_fancySaddle);
            else if (holding("EnglishSaddleCursor"))
                show(m_englishSaddle);
            pickUp(m_bridle, "BridleCursor");
            m_bridleOn = false;
            break;
        case kHorseClick:
            dropOnHorse();
            break;
        case kCatClick:
            run(m_cat);
            if (m_cat)
                m_cat->setSequence(0, true);
            if (engine().rand() % 2 != 0)
                playEffect("meow");
            else
                playEffect("purrs", 0xFA);
            break;
        case kCombClick:
        case kBrushClick:
            gotoNode("grooming");
            break;
        case kTackOnClick:
            clickTackOn();
            break;
        case kHorseSignClick:
            gotoNode("stable");
            break;
        default:
            break;
        }
    }

    // The held piece is dropped on the horse (0x42d590, id 0x4DA7).
    void dropOnHorse() {
        if (holding("PadCursor")) {
            playEffect("Pad");
            m_padOn = true;
            s_tackState = 1;
            show(m_tackOn);
            showTack();
            dropToHand();
        } else if (holding("EnglishSaddleCursor")) {
            if (!m_padOn) {
                needPad(true);
                return;
            }
            playEffect("SaddleOn");
            if (m_fancyOn) {
                m_fancyOn = false;
                show(m_fancySaddle);
            }
            putOnEnglishSaddle();
        } else if (holding("BridleCursor")) {
            playEffect("bridle");
            m_bridleOn = true;
            s_bridleOn = true;
            if (m_tackAmb && m_tackAmb->visible())
                follow(m_tackAmbBridle, m_tackAmb);
            else if (m_tackSubAmb && m_tackSubAmb->visible())
                follow(m_subtleAmbBridle, m_tackSubAmb);
            dropToHand();
        } else if (holding("fancySaddleCursor")) {
            if (!m_padOn) {
                needPad(false);
                return;
            }
            playEffect("SaddleOn");
            if (m_englishOn) {
                m_englishOn = false;
                show(m_englishSaddle);
            }
            putOnFancySaddle();
        }
    }

    // A click on the tack on the horse (0x42d590, id 0x4DAB): takes the top
    // piece off, or swaps the saddle for the one in hand.
    void clickTackOn() {
        if (holding("BridleCursor")) {
            show(m_bridle);
            dropToHand();
        }
        switch (s_tackState) {
        case 1:
            if (holding("EnglishSaddleCursor")) {
                if (!m_padOn) {
                    needPad(true);
                    return;
                }
                if (m_fancyOn) {
                    m_fancyOn = false;
                    if (m_fancySaddle) {
                        m_fancySaddle->setPosition(600.0f, m_fancySaddle->y(), m_fancySaddle->z());
                        show(m_fancySaddle);
                    }
                }
                putOnEnglishSaddle();
            } else if (holding("fancySaddleCursor")) {
                if (!m_padOn) {
                    needPad(true);
                    return;
                }
                if (m_englishOn) {
                    m_englishOn = false;
                    if (m_englishSaddle) {
                        m_englishSaddle->setPosition(600.0f, m_englishSaddle->y(), m_englishSaddle->z());
                        show(m_englishSaddle);
                    }
                }
                putOnFancySaddle();
            } else {
                // The pad goes back on the wall.
                s_tackState = 0;
                show(m_pad);
                m_padOn = false;
                hide(m_tackOn);
            }
            break;
        case 2:
            if (holding("fancySaddleCursor")) {
                s_tackState = 3;
                app().record().setU32(PlayerRecord::kSaddle, 1);
                show(m_englishSaddle);
                m_englishOn = false;
                m_fancyOn = true;
            } else {
                s_tackState = 1;
                show(m_englishSaddle);
                m_englishOn = false;
            }
            show(m_tackOn);
            showTack();
            if (!holding("BridleSaddleCursor"))
                dropToHand();
            break;
        case 3:
            if (holding("EnglishSaddleCursor")) {
                s_tackState = 2;
                app().record().setU32(PlayerRecord::kSaddle, 0);
                show(m_fancySaddle);
                m_englishOn = true;
                m_fancyOn = false;
            } else {
                s_tackState = 1;
                show(m_fancySaddle);
                m_fancyOn = false;
            }
            show(m_tackOn);
            showTack();
            if (!holding("BridleSaddleCursor"))
                dropToHand();
            break;
        default:
            break;
        }
    }

    // ScrollLeft / ScrollRight / ScrollStop (0x42f200 / 0x42f230 / 0x42f260).
    void scrollLeft() {
        if (!exitDialogShowing() && !s_tourRunning && !m_moviePlaying)
            setScrollVelocity(-20.0f, 0.0f);
    }
    void scrollRight() {
        if (!exitDialogShowing() && !s_tourRunning && !m_moviePlaying)
            setScrollVelocity(20.0f, 0.0f);
    }
    void scrollStop() {
        if (!s_tourRunning)
            setScrollVelocity(0.0f, 0.0f);
    }

    // TourScrollMiddle / TourScrollRight (0x42fa50 / 0x42fa70).
    void tourScrollMiddle() {
        m_panLeft = 240;
        setPanLimits(m_panLeft, m_panRight);
        setScrollVelocity(-20.0f, 0.0f);
    }
    void tourScrollRight() {
        m_panRight = 960;
        setPanLimits(m_panLeft, m_panRight);
        setScrollVelocity(20.0f, 0.0f);
    }

    // StartTackTourIfFirstVisit (0x42fa00).
    void startTackTourIfFirstVisit() {
        if (app().record().u32(PlayerRecord::kTackroomVisits) == 0) {
            engine().sounds().getSound("toolbar", true, false);
            runScript("TackTour");
            s_tourRunning = true;
            return;
        }
        s_tourRunning = false;
    }

    // IsIdle (0x42fa90), despite the name: Barbie is holding out a treat.
    bool barbieFeeding() const {
        return m_barbieFeed && !m_barbieFeed->stopped() && m_barbieFeed->playbackPauseCount() == 0 && !m_busy;
    }

    // SpendTime's idle part: Barbie's ambient moves and the horse's.
    void idleAnimations() {
        const std::uint32_t now = engine().ticks();
        if (s_lastIdleTick == 0xFFFFFFFF)
            s_lastIdleTick = now;
        if (!s_tourRunning && m_barbieAmbient && m_barbieAmbient->stopped() && !barbieFeeding()) {
            hide(m_barbieFeed);
            show(m_barbieAmbient);
            int seq = engine().rand() % 7;
            if (seq == 2)
                seq = 8;
            if (seq == 1)
                seq = 7;
            m_barbieAmbient->setSequence(seq, true);
            s_lastIdleTick = now;
        }
        if (!m_tackSubAmb || !m_tackAmb || !m_tackSubAmb->stopped() || !m_tackAmb->stopped())
            return;
        int seq;
        engine::Anim* bridle;
        if (engine().rand() % 4 == 2) {
            hide(m_tackSubAmb);
            if (m_bridleOn)
                hide(m_subtleAmbBridle);
            show(m_tackAmb);
            if (m_bridleOn)
                show(m_tackAmbBridle);
            seq = engine().rand() % 3;
            m_tackAmb->setSequence(seq, true);
            bridle = m_tackAmbBridle;
        } else {
            seq = engine().rand() % 4;
            m_tackSubAmb->setSequence(seq, true);
            hide(m_tackAmb);
            bridle = m_subtleAmbBridle;
        }
        if (m_bridleOn && bridle)
            bridle->setSequence(seq, true);
    }

    // FeedHorse (0x42f410): Barbie holds out the treat, with a line about it.
    void feedHorse(int food) {
        if (s_tourRunning)
            return;
        m_busy = true;
        if (!m_barbieFeed) {
            m_barbieFeed = viewAnim("barbieFeed");
            if (m_barbieFeed)
                m_barbieFeed->setPosition(324.0f, m_barbieFeed->y(), m_barbieFeed->z());
        }
        if (!m_barbieFeed)
            return;
        m_barbieFeed->hide(false);
        run(m_barbieFeed);
        m_barbieFeed->setSequence(food, true);
        m_barbieFeed->move(0, 0, 200.0f);
        hide(m_barbieAmbient);
        playEffect("getFood");
        switch (engine().rand() % 3) {
        case 0:
            switch (food) {
            case kCarrot:
                sayHorseNameThen("DB0533");
                break;
            case kApple:
                sayHorseNameThen("DB0532");
                break;
            case kStrawberry:
                sayHorseNameThen("DB0534");
                break;
            case kSugar:
                sayHorseNameThen("DB0535");
                break;
            default:
                break;
            }
            break;
        case 1:
            sayHorseNameThen("DB0531");
            break;
        default:
            sayHorseNameThen("DB0536");
            break;
        }
    }

    // PlayFeedCloseUp (0x42f590): the close-up of the horse eating, over the
    // whole view.
    void playFeedCloseUp() {
        const std::uint32_t horse = app().record().horse();
        const Coat& coat = kCoats[horse <= 3 ? horse : 2];
        const std::string path = std::format("smk\\FeedCloseUp{}.smk", coat.movie);
        if (!m_movie) {
            m_movie = std::make_unique<engine::SmackMovie>(engine(), 10, true);
            if (!m_movie->open(path)) {
                log::warn("Smacker File Failed to Load! {}", path);
                return;
            }
            m_movie->link(this);
            m_movie->setLoop(false);
        }
        if (m_movie->hideCount() != 0) {
            m_movie->setHideCount(1);
            m_movie->hide(false);
        }
        m_movie->setPosition(cameraX(), 0, 40000.0f);
        m_movie->gotoFrame(1);
        m_movie->setPaused(false);
        scrollStop();
        m_moviePlaying = true;
        playEffect("eating");
    }

    // StartEatingPose (0x42f7b0): the horse leans down to the treat.
    void startEatingPose() {
        hide(m_tackAmb);
        if (m_bridleOn)
            hide(m_tackAmbBridle);
        hide(m_tackSubAmb);
        if (m_bridleOn)
            hide(m_subtleAmbBridle);
        show(m_tackFeed);
        if (m_bridleOn)
            show(m_bridleEat);
        run(m_tackFeed);
        show(m_tackFeed);
        if (m_tackFeed)
            m_tackFeed->setSequence(0, true);
        if (m_bridleOn && m_bridleEat) {
            m_bridleEat->setPosition(m_bridleEat->x(), m_bridleEat->y(),
                                     (m_tackFeed ? m_tackFeed->z() : 0) + 1.0f);
            run(m_bridleEat);
            show(m_bridleEat);
            m_bridleEat->setSequence(0, true);
        }
        hide(m_tackSubAmb);
        if (m_bridleOn)
            hide(m_subtleAmbBridle);
        hide(m_tackAmb);
        if (m_bridleOn)
            hide(m_tackAmb); // (sic: the original hides TackAmb again, not its bridle)
    }

    // FinishFeeding (0x42f920): Barbie stands up and the horse raises its head.
    void finishFeeding() {
        show(m_barbieFeed);
        if (m_barbieFeed)
            m_barbieFeed->setSequence(0, true);
        if (m_tackFeed) {
            m_tackFeed->setSequence(1, true);
            m_tackFeed->setSequence(1, true);
        }
        run(m_barbieFeed);
        playEffect("getfood2");
        run(m_tackFeed);
        if (m_bridleOn && m_bridleEat) {
            m_bridleEat->setSequence(1, true);
            run(m_bridleEat);
        }
    }

    bool m_redirected = false;                 // no horse: on the way to the stable
    int m_panLeft = 0, m_panRight = kPanRight; // what was passed to setPanLimits

    engine::Anim* m_bulletinBoard = nullptr;          // +0x1CC
    engine::Anim* m_tackFeed = nullptr;               // +0x1D0 TackFeed<Coat>
    engine::Anim* m_tackAmb = nullptr;                // +0x1D4 TackAmb<Coat>
    engine::Anim* m_tackSubAmb = nullptr;             // +0x1D8 TackSubAmb<Coat>
    engine::Anim* m_barbieFeed = nullptr;             // +0x1DC
    engine::Anim* m_mouthFlap = nullptr;              // +0x1E0
    engine::Anim* m_barbieAmbient = nullptr;          // +0x1E4
    engine::Anim* m_bridleEat = nullptr;              // +0x1E8
    engine::Anim* m_tackAmbBridle = nullptr;          // +0x1EC
    engine::Anim* m_subtleAmbBridle = nullptr;        // +0x1F0
    engine::Anim* m_apple = nullptr;                  // +0x1F4
    engine::Anim* m_strawberry = nullptr;             // +0x1F8
    engine::Anim* m_sugar = nullptr;                  // +0x1FC
    engine::Anim* m_carrot = nullptr;                 // +0x200
    engine::Anim* m_pad = nullptr;                    // +0x204
    engine::Anim* m_englishSaddle = nullptr;          // +0x208
    engine::Anim* m_fancySaddle = nullptr;            // +0x20C
    engine::Anim* m_bridle = nullptr;                 // +0x210
    engine::Anim* m_cat = nullptr;                    // +0x214
    engine::Anim* m_brushWall = nullptr;              // +0x218
    engine::Anim* m_combWall = nullptr;               // +0x21C
    engine::Anim* m_tackOn = nullptr;                 // +0x228: the tack on the horse, sequence = state - 1
    engine::Anim* m_horseSign = nullptr;              // +0x22C
    engine::Anim* m_posTR = nullptr;                  // +0x230
    std::unique_ptr<ACRollover> m_appleRollover;      // +0x234
    std::unique_ptr<ACRollover> m_strawberryRollover; // +0x238
    std::unique_ptr<ACRollover> m_sugarRollover;      // +0x23C
    std::unique_ptr<ACRollover> m_carrotRollover;     // +0x240
    std::unique_ptr<ACRollover> m_padRollover;        // +0x244
    std::unique_ptr<ACRollover> m_englishSaddleRollover; // +0x248
    std::unique_ptr<ACRollover> m_fancySaddleRollover;   // +0x24C
    std::unique_ptr<ACRollover> m_bridleRollover;        // +0x250
    std::unique_ptr<ACRollover> m_catRollover;           // +0x254
    std::unique_ptr<ACRollover> m_brushRollover;         // +0x258
    std::unique_ptr<ACRollover> m_combRollover;          // +0x25C
    std::unique_ptr<ACRollover> m_horseSignRollover;     // +0x260
    std::unique_ptr<ACRollover> m_bulletinBoardRollover; // +0x264
    std::unique_ptr<engine::SmackMovie> m_movie;         // +0x268 FeedCloseUp
    bool m_englishOn = false;                            // +0x270
    bool m_bridleOn = false;                             // +0x27C
    bool m_fancyOn = false;                              // +0x284
    bool m_padOn = false;                                // +0x288
    bool m_busy = false;                                 // +0x28C: a feeding is running
    bool m_moviePlaying = false;                         // +0x290
    int m_lastFood = 0;                                  // +0x294
};

} // namespace

BRC_REGISTER_NODE("tackroom",
                  [](Godiva& app, const std::string& name) { return std::make_unique<TackRoom>(app, name); });

} // namespace brc::game
