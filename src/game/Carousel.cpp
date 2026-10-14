// Inside the cottage (Carousel.cpp, class Carousel, ctor 0x40f860, mission
// M4): the door opens (movie), Barbie walks about the room, and the quilt,
// the chest and the carousel music box can be clicked. The room pans right
// once she has walked in. See docs/engine/nodes.md "Carousel".
#include "core/Log.h"
#include "core/StringUtil.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "engine/Movie.h"
#include "engine/Scripts.h"
#include "engine/SoundManager.h"
#include "game/MiniGameFlags.h"
#include "game/ToolbarNode.h"
#include "game/Widgets.h"

#include <cstdlib>
#include <memory>
#include <string>

namespace brc::game {
namespace {

// Click ids (anim +0x11c).
constexpr int kQuiltClick = 0x24B6;
constexpr int kChestClick = 0x24B7;
constexpr int kMusicBoxClick = 0x24B8;
constexpr int kChestCloseupClick = 0x24B9;

// Mission M4's completion counter (app+0x264): counts visits to the cottage.
constexpr std::size_t kCottageDone = PlayerRecord::kMissionDone + 0x0C;

// Shows an anim hidden any number of times (hide count set to 1, then shown).
void unhide(engine::Anim* a) {
    if (a->hideCount() != 0) {
        a->setHideCount(1);
        a->hide(false);
    }
}

class Carousel final : public ToolbarNode {
public:
    Carousel(Godiva& app, const std::string& name) : ToolbarNode(app, name) {
        // 0x40f860.
        engine().anims().preloadCache("carousel.omt.cache");
        setSkin(kOut);
        addToolbarButton("tbQuitOut", kQuit);
        addToolbarButton("tbPagerOut", kPager);
        addToolbarButton("tbCameraOut", kCamera);
        addToolbarButton("tbMapOut", kMap);
        addToolbarButton("tbBagOut", kBag);
        addToolbarButton("tbHelpOut", kHelp);
        addToolbarButton("tbHomeOut", kHome);
        moveRollovers(cameraX(), cameraY());
        m_sceneNotBuilt = true;
        m_swallowRelease = false;
        flags::carouselVisited = true;
        setPanLimits(0, 0x31D); // the room is 797 pixels wide
        // (+0xFC = 1: a toolbar field of unknown meaning)
        m_quiltViewed = false;
        m_firstWalk = true;
        m_movieState = 0;
        m_quiltOpen = false;
        m_chestOpen = false;
        setCursor("cursorOut");
        if (engine::Anim* c = cursor())
            c->setPosition(c->x(), c->y(), 5000.0f);
        edgeScroll = true; // +0xC4
        playOpenDoorMovie();
        PlayerRecord& rec = app.record();
        rec.setU32(kCottageDone, rec.u32(kCottageDone) + 1);
        app.saveGame();
        listenToMouseButtons(); // [0x80a5a0]+0xC
    }

    ~Carousel() override {
        // 0x40fa70.
        m_musicBoxRollover.reset();
        m_chestRollover.reset();
        m_quiltRollover.reset();
        engine().resources().close("trlAnims.omt");
        engine().resources().close("trails.omt");
        auto& anims = engine().anims();
        for (const char* n : {"barbieWalkOutside", "bed", "carousel", "Chest", "chestCloseUp", "Door", "Quilt",
                              "quiltCloseUp"})
            anims.remove(n);
    }

    // SpendTime (0x40fb90): between walks Barbie idles (a random ambient
    // sequence beside where she stopped); a finished movie goes away, and
    // after the door movie the room is built.
    void tick() override {
        if (!m_movie) {
            if (m_walker && !isScriptRunning("CarouselWalk") &&
                (m_walker->stopped() || m_walker->playbackPauseCount() != 0) &&
                (m_ambient->stopped() || m_ambient->playbackPauseCount() != 0)) {
                m_walker->hide(true);
                unhide(m_ambient);
                m_ambient->setPosition(m_walker->x() + static_cast<float>(m_lastDirection != 0 ? 33 : -66),
                                       m_walker->y(), m_walker->z());
                m_ambient->setSequence(std::rand() % 3, true);
            }
        } else if (m_movie->done()) {
            m_movieState = 0;
            if (m_sceneNotBuilt) {
                m_sceneNotBuilt = false;
                buildScene();
                startWalk();
            }
            closeMovie();
            edgeScroll = true;
        }
        ToolbarNode::tick();
    }

    // ListenToMessage (0x40fd10): nothing is taken until the room has panned
    // right (view x >= 156).
    void listen(int message, const engine::MessageData& data) override {
        if (cameraX() < 156.0f)
            return;
        if (message == kMsgEdgeScroll) {
            if (m_quiltOpen) {
                closeQuilt();
                return;
            }
            if (!m_movie) {
                app().session().arriveFromMiniGame = 1;
                gotoNode("cottageL");
                return;
            }
            stopMovie();
            return;
        }
        if (message != -103) { // not the raw button release
            ToolbarNode::listen(message, data);
            return;
        }
        if (m_swallowRelease) {
            m_swallowRelease = false;
            return;
        }
        const auto* e = static_cast<const engine::InputEvent*>(data.ptr);
        if (!e || e->type != engine::InputEvent::LeftUp)
            return;
        if (m_chestOpen) {
            playVoiceNoFlaps("DB0096");
            return;
        }
        if (m_movieState != 0) {
            if (m_movieState == 2) {
                m_movieState = 1; // the release of the click that started it
                return;
            }
            stopMovie();
            return;
        }
        if (m_quiltOpen) {
            if (!m_quiltJustClicked)
                quiltSquareClicked();
            else
                m_quiltJustClicked = false;
            return;
        }
        // Walk towards the click: one cycle per 200 pixels beyond 166.
        const auto [mx, my] = mouseWorldPos();
        (void)my;
        m_lastClickX = static_cast<int>(mx);
        const int dx = static_cast<int>(static_cast<float>(m_lastClickX) - m_walker->x());
        m_cycles = (dx - 0xA6) / 200;
        m_direction = m_cycles > 0 ? 1 : 0;
        const int endX = static_cast<int>(static_cast<float>(m_cycles * 200) + m_walker->x());
        m_cycles = std::abs(m_cycles);
        if (endX > -70 && endX < 300 && m_cycles != 0)
            startWalk();
        ToolbarNode::listen(message, data);
    }

    // KeyDown (0x410020): Esc does nothing while a movie plays.
    void onKeyDown(const engine::InputEvent& e) override {
        if (m_movie && e.key == engine::key::kEscape)
            return;
        ToolbarNode::onKeyDown(e);
    }

    // MouseDown (0x410050).
    void onPress(const engine::Pick& pick) override {
        const int id = pick.element ? pick.element->clickId() : 0;
        switch (id) {
        case kQuiltClick:
            quiltClicked();
            break;
        case kChestClick:
            chestClicked();
            break;
        case kMusicBoxClick:
            playMusicBox();
            break;
        case kChestCloseupClick: {
            // Closing the chest close-up; the first time the chest stays open.
            PlayerRecord& rec = app().record();
            if (rec.u16(PlayerRecord::kChestOpened) == 0)
                playVoiceNoFlaps("DB04137");
            m_swallowRelease = true;
            m_chestOpen = false;
            m_chestCloseup->hide(true);
            rec.setU16(PlayerRecord::kChestOpened, 1);
            m_chest->setSequence(rec.u16(PlayerRecord::kChestOpened) != 0 ? 1 : 0, true);
            break;
        }
        default:
            break;
        }
        ToolbarNode::onPress(pick);
    }

    // AnimEnded (0x410150): once Barbie has walked, the room pans right.
    void sequenceEnded(engine::Anim& anim, int seq) override {
        if (&anim == m_walker)
            setScrollVelocity(10.0f, 0.0f);
        ToolbarNode::sequenceEnded(anim, seq);
    }

    // ScriptCommand (0x410420): the walk's parameters. Other commands are not
    // passed on.
    void scriptMessage(engine::Script& script, std::string_view message, int) override {
        if (!str::iequals(message, "initWalk"))
            return;
        setScriptVar(script.name(), "cycles", static_cast<double>(m_cycles));
        setScriptVar(script.name(), "direction", static_cast<double>(m_direction));
        setScriptVar(script.name(), "lastDirection", static_cast<double>(m_lastDirection));
        m_lastDirection = m_direction;
    }

private:
    // BuildScene (0x410180).
    void buildScene() {
        m_walker = createAnimAtViewport("barbieWalkOutside");
        engine().resources().open("trlAnims.omt");
        engine().resources().open("trails.omt");
        m_ambient = createAnimAtViewport("barbie ambients");
        m_ambient->hide(true);
        m_bed = createAnimAtViewport("bed");
        m_bed->move(200, 0, 0);
        m_door = createAnimAtViewport("door");
        m_door->setPosition(m_door->x(), m_door->y(), 300.0f);
        m_quilt = createAnimAtViewport("quilt");
        m_quilt->setClickId(kQuiltClick);
        m_quiltRollover = std::make_unique<ACRollover>(*this, m_quilt, 7);
        m_quiltCloseup = createAnimAtViewport("quiltCloseup");
        m_quiltCloseup->move(158, 0, 0);
        m_quiltCloseup->hide(true);
        m_chest = createAnimAtViewport("chest");
        m_chest->setClickId(kChestClick);
        m_chestRollover = std::make_unique<ACRollover>(*this, m_chest, 7);
        m_chest->setSequence(app().record().u16(PlayerRecord::kChestOpened) != 0 ? 1 : 0, true);
        m_chestCloseup = createAnimAtViewport("chestCloseup");
        m_chestCloseup->move(158, 0, 0);
        m_chestCloseup->hide(true);
        m_musicBox = createAnimAtViewport("carousel");
        m_musicBox->setClickId(kMusicBoxClick);
        m_musicBox->move(200, 0, 0);
        m_musicBoxRollover = std::make_unique<ACRollover>(*this, m_musicBox, 7);
        playVoiceNoFlaps("DB0080");
    }

    // StartWalk (0x4104a0): unless she is walking already, or a close-up is
    // open. Her first walk starts outside the door.
    void startWalk() {
        if (isScriptRunning("CarouselWalk"))
            return;
        m_ambient->hide(true);
        unhide(m_walker);
        if (m_chestOpen || m_quiltOpen)
            return;
        if (m_firstWalk) {
            m_walker->setPosition(-335, 0, 100);
            m_firstWalk = false;
            m_cycles = 1;
            m_direction = 1;
            m_lastDirection = 1;
        }
        runScript("CarouselWalk");
    }

    bool openMovie(std::string_view path, bool preload, float z) {
        m_movie = std::make_unique<engine::HcMovie>(engine(), *this);
        if (!m_movie->open(path, preload, false, false, true, 0, true)) {
            log::warn("Smacker File Failed to Load!");
            return false;
        }
        m_movie->setPosition(cameraX(), 0, z);
        m_movie->link(this);
        m_movie->setPaused(false);
        return true;
    }

    void closeMovie() {
        if (!m_movie)
            return;
        m_movie->hide(true);
        m_movie.reset();
    }

    // Skipping the door movie or stopping the music box: the room is built
    // if it is not yet.
    void stopMovie() {
        if (m_sceneNotBuilt) {
            m_sceneNotBuilt = false;
            buildScene();
            startWalk();
        }
        m_movieState = 0;
        closeMovie();
        engine().sounds().stopSound("MusicBox");
        edgeScroll = true;
    }

    // PlayOpenDoorMovie (0x410be0).
    void playOpenDoorMovie() {
        if (!m_movie && !openMovie("smk\\OpenDoor.smk", true, 4000.0f))
            return;
        m_movieState = 2;
        edgeScroll = false;
    }

    // PlayMusicBox (0x410540): the music box tune (channel 13) and its movie.
    void playMusicBox() {
        engine().sounds().playSound("MusicBox", 13, false, false, 9999, false, -1, -1, 0, false);
        playVoice("DB9045");
        if (!m_movie)
            openMovie("smk\\Carousel.smk", false, 2000.0f);
    }

    // OnQuiltClick (0x410660).
    void quiltClicked() {
        m_quiltJustClicked = true;
        if (m_movieState != 0)
            return;
        playVoiceNoFlaps(m_quiltViewed ? "DB0090" : "DB0081N");
        m_quiltOpen = true;
        unhide(m_quiltCloseup);
    }

    void closeQuilt() {
        m_quiltViewed = true;
        app().record().setU16(PlayerRecord::kQuiltViewed, 1);
        m_quiltOpen = false;
        m_quiltCloseup->hide(true);
    }

    // OnQuiltSquareClick (0x4106c0): the close-up's 3 x 2 picture squares,
    // each with its story; a click off the squares (above y 400) closes it.
    void quiltSquareClicked() {
        const auto [x, y] = mouseWorldPos();
        int column = -1;
        if (x > 122.0f && x < 254.0f)
            column = 0;
        else if (x > 254.0f && x < 387.0f)
            column = 1;
        else if (x > 387.0f && x < 522.0f)
            column = 2;
        if (column < 0) {
            if (m_quiltOpen && y < 400.0f)
                closeQuilt();
            return;
        }
        int row = -1;
        if (y > 78.0f && y < 208.0f)
            row = 0;
        else if (y > 208.0f && y < 345.0f)
            row = 1;
        if (row < 0)
            return;
        m_quiltCloseup->setSequence(row * 3 + column + 1, true);
        engine().sounds().playSound("square", -1, false, false, 1, false, 0x70, -1, 0, false);
        switch (row * 3 + column) {
        case 0:
            playVoice("DB04129");
            break;
        case 1:
            playVoice("DB04130");
            break;
        case 2:
            playVoice("DB04131");
            playVoiceNoFlaps("DB9046");
            break;
        case 3:
            playVoice("DB9047");
            break;
        case 4:
            if (app().record().u32(PlayerRecord::kMissionDone + 0x1C) != 0) { // M8 done
                playVoice("DB04134");
            } else {
                playVoice("DB04133");
                playVoiceNoFlaps("DB0954");
            }
            break;
        default:
            playVoice("DB04135N");
            break;
        }
    }

    // OnChestClick (0x410b50).
    void chestClicked() {
        const bool opened = app().record().u16(PlayerRecord::kChestOpened) != 0;
        playVoice(opened ? "DB9024" : "DB04136");
        m_chestCloseup->setSequence(opened ? 1 : 0, true);
        unhide(m_chestCloseup);
        m_chestCloseup->setClickId(kChestCloseupClick);
        m_chestOpen = true;
    }

    bool m_swallowRelease = false;    // +0x1C0
    bool m_quiltJustClicked = false;  // +0x1C2
    bool m_sceneNotBuilt = true;      // +0x1C4
    engine::Anim* m_walker = nullptr; // +0x1C8
    engine::Anim* m_ambient = nullptr; // +0x1CC
    engine::Anim* m_bed = nullptr;    // +0x1D0
    engine::Anim* m_door = nullptr;   // +0x1D4
    engine::Anim* m_quilt = nullptr;  // +0x1D8
    engine::Anim* m_quiltCloseup = nullptr; // +0x1DC
    engine::Anim* m_chest = nullptr;  // +0x1E0
    engine::Anim* m_chestCloseup = nullptr; // +0x1E4
    engine::Anim* m_musicBox = nullptr; // +0x1E8
    std::unique_ptr<ACRollover> m_quiltRollover, m_chestRollover, m_musicBoxRollover; // +0x1EC/+0x1F0/+0x1F4
    std::unique_ptr<engine::HcMovie> m_movie; // +0x1F8
    bool m_quiltOpen = false;         // +0x1FC
    int m_movieState = 0;             // +0x200: 2 door movie started, 1 its click released, 0 none
    bool m_chestOpen = false;         // +0x204
    bool m_quiltViewed = false;       // +0x208
    bool m_firstWalk = true;          // +0x210
    int m_lastDirection = 0;          // +0x214
    int m_cycles = 0;                 // +0x218
    int m_direction = 0;              // +0x21C
    int m_lastClickX = 0;             // +0x220
};

BRC_REGISTER_NODE("carousel", [](Godiva& app, const std::string& name) { return std::make_unique<Carousel>(app, name); });

} // namespace
} // namespace brc::game
