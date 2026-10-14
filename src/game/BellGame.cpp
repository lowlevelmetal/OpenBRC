// The bell by the waterfall (BellGame.cpp, class BellGame, ctor 0x40a980,
// mission M7): the player carries objects to the bell pole ("poll") to mend
// it; once it is whole, ringing it plays a movie and counts the mission.
// See docs/engine/nodes.md "BellGame".
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

#include <array>
#include <cstdlib>
#include <memory>
#include <string>

namespace brc::game {
namespace {

// Click ids (anim +0x11c).
constexpr int kPoleClick = 0x577E;
constexpr int kObjectClick = 0x577F; // + object index 0..7

// Mission M7's completion counter (app+0x270).
constexpr std::size_t kBellGameDone = PlayerRecord::kMissionDone + 0x18;

// [0x7feda0] when DB9023 last reminded the player (-1 = not yet).
std::uint32_t s_reminderTime = 0xFFFFFFFFu;

// Shows an anim hidden any number of times (hide count set to 1, then shown).
void unhide(engine::Anim* a) {
    if (a && a->hideCount() != 0) {
        a->setHideCount(1);
        a->hide(false);
    }
}

class BellGame final : public ToolbarNode {
public:
    BellGame(Godiva& app, const std::string& name) : ToolbarNode(app, name) {
        // 0x40a980.
        flags::bellGameVisited = true;
        engine().anims().preloadCache("bellgame.omt.cache");
        setSkin(kOut);
        addToolbarButton("tbQuitOut", kQuit);
        addToolbarButton("tbPagerOut", kPager);
        addToolbarButton("tbCameraOut", kCamera);
        addToolbarButton("tbMapOut", kMap);
        addToolbarButton("tbBagOut", kBag);
        addToolbarButton("tbHelpOut", kHelp);
        addToolbarButton("tbHomeOut", kHome);
        moveRollovers(cameraX(), cameraY());
        setCursor("cursorOut");
        edgeScroll = true; // +0xC4: either edge leads back to the bridge (13000)
        if (engine::Anim* c = cursor())
            c->setPosition(c->x(), c->y(), 5000.0f);
        if (!ambientLoop().empty())
            releaseAmbient();
        playAmbientLoop("waterfall", 0x28);
        setupScene();
        m_field1D4 = 0;
        m_carried = -1;
        m_state = 0;
        m_movie.reset();
        m_mouseLowerLeft = m_mouseUpperLeft = false;
        m_complete = false;
        playVoiceNoFlaps("pause");
        playVoiceNoFlaps("pause");
        playVoiceNoFlaps("DB04146N");
        listenToMouseButtons(); // [0x80a5a0]+0xC
    }

    ~BellGame() override {
        // 0x40ab90.
        m_poleRollover.reset();
        for (auto& r : m_objectRollovers)
            r.reset();
        stopScript("GameAmb");
        engine().resources().close("GameBarb.omt");
        auto& anims = engine().anims();
        for (const char* n : {"GrassOverlay", "BellgameObject1", "BellgameObject2", "BellgameObject3",
                              "BellgameObject4", "BellgameObject5", "BellgameObject6", "BellgameObject7",
                              "BellgameObject8", "BellgameOverlay1", "BellgameOverlay2", "BellgameOverlay3",
                              "BellgameOverlay4", "BellgameOverlay5", "BellGameOverlay6", "Poll", "RingBell",
                              "WaterFall"})
            anims.remove(n);
        if (m_movie) {
            m_movie->hide(true);
            m_movie.reset();
        }
        app().session().arriveFromMiniGame = 1;
    }

    // SpendTime (0x40bab0): drop the finished movie; after the bell has rung,
    // DB9023 every 30 seconds.
    void tick() override {
        if (m_movie) {
            if (m_movie->done()) {
                m_movie->hide(true);
                m_movie.reset();
                edgeScroll = true;
            }
            s_reminderTime = 0xFFFFFFFFu;
        }
        if (m_complete) {
            if (s_reminderTime == 0xFFFFFFFFu)
                s_reminderTime = engine().ticks();
            if (engine().ticks() - s_reminderTime > 30000) {
                s_reminderTime = engine().ticks();
                playVoice("DB9023");
            }
        }
        ToolbarNode::tick();
    }

    // ListenToMessage (0x40ad70).
    void listen(int message, const engine::MessageData& data) override {
        if (message == -102) { // mouse button down (event manager)
            // Records which side of the screen was clicked; dead code, as
            // +0x1D4 is never 1.
            const auto* e = static_cast<const engine::InputEvent*>(data.ptr);
            if (e && e->type == engine::InputEvent::LeftDown && m_field1D4 == 1) {
                const auto [x, y] = mouseWorldPos();
                if (x < 350.0f) {
                    if (y >= 200.0f)
                        m_mouseLowerLeft = true;
                    else
                        m_mouseUpperLeft = true;
                }
            }
        } else if (message == kMsgEdgeScroll) {
            app().session().arriveFromMiniGame = 1;
            gotoNode("bridgeS");
            return;
        }
        ToolbarNode::listen(message, data);
    }

    // MouseDown (0x40aeb0).
    void onPress(const engine::Pick& pick) override {
        const int id = pick.element ? pick.element->clickId() : 0;
        if (id == kHelpClickId) {
            playVoice("Db03027N");
        } else if (id == kPoleClick) {
            usePole();
        } else if (id >= kObjectClick && id <= kObjectClick + 7 && m_state != 4) {
            pickUpObject(id);
            static constexpr std::array<std::string_view, 8> kSounds = {"hinge", "",       "Rope",   "Hinge",
                                                                        "",      "teapot", "hammer", "candle"};
            const std::string_view sound = kSounds[static_cast<std::size_t>(id - kObjectClick)];
            if (!sound.empty())
                playEffect(sound);
        }
        ToolbarNode::onPress(pick);
    }

    // AnimEnded (0x40b4e0): the ring has finished.
    void sequenceEnded(engine::Anim& anim, int seq) override {
        if (&anim == m_pole && seq == 5 && !m_complete)
            onBellRung();
        ToolbarNode::sequenceEnded(anim, seq);
    }

private:
    void playEffect(std::string_view name) {
        engine().sounds().playSound(name, -1, false, false, 1, false, 0x70, -1, 0, false);
    }

    engine::Anim* object(int i) const {
        return i >= 0 && i < static_cast<int>(m_objects.size()) ? m_objects[static_cast<std::size_t>(i)] : nullptr;
    }

    // SetupScene (0x40b520).
    void setupScene() {
        m_pole = createAnimAtViewport("poll");
        m_pole->setClickId(kPoleClick);
        m_poleRollover = std::make_unique<ACRollover>(*this, m_pole, 7);
        m_pole->pause(true);
        m_waterfall = createAnimAtViewport("waterfall");
        for (std::size_t i = 0; i < 6; ++i) {
            m_overlays[i] = createAnimAtViewport("Bellgameoverlay" + std::to_string(i + 1));
            m_overlays[i]->setPickable(false);
            if (i == 3)
                m_overlays[i]->move(-1, 7, 0);
        }
        for (int i = 1; i <= 3; ++i) {
            engine::Anim* a =
                createAnimAtViewport("bellgameObject" + std::to_string(i), "cursorBellGame" + std::to_string(i));
            m_objects[static_cast<std::size_t>(i - 1)] = a;
            a->setClickId(kPoleClick + i);
            m_objectRollovers[static_cast<std::size_t>(i - 1)] = std::make_unique<ACRollover>(*this, a, 7);
            a->setPerPixelHit(false);
        }
        engine().resources().open("GameBarb.omt");
        runScript("GameAmb");
        engine::Anim* barbie = createAnimAtViewport("GameBarbie");
        barbie->addHeaderFlags(8);
        barbie->move(342, -25, 150);
        createAnimAtViewport("GameFlaps")->move(342, -25, 0);
    }

    // PickUpObject (0x40ba00): the object becomes the cursor; the one carried
    // before goes back.
    void pickUpObject(int id) {
        if (engine::Anim* a = object(id - kObjectClick))
            a->hide(true);
        unhide(object(m_carried));
        m_carried = id - kObjectClick;
        setCursor("bellgameObject" + std::to_string(id - kPoleClick));
        if (engine::Anim* c = cursor()) {
            c->recenter(); // 0x449e40
            c->setPosition(c->x(), c->y(), 5000.0f);
            c->addHeaderFlags(8);
        }
    }

    // Puts the carried object back and the normal cursor up.
    void dropObject() {
        setCursor("cursorOut");
        edgeScroll = true;
        m_carried = -1;
    }

    // The pole part of MouseDown: ring it when it is mended (state 4), else
    // try the carried object on it. Object 1 (hinge) first, then object 2
    // (rope) and 3 (the third piece) in either order.
    void usePole() {
        if (m_state == 4) {
            m_pole->setPlaybackPauseCount(0);
            m_pole->setMotionPauseCount(0);
            m_pole->setSequence(5, true);
            playEffect(std::rand() % 2 == 0 ? "ring2" : "ring1");
        }
        auto mend = [&](int seq, int state, std::string_view voice) {
            m_pole->setSequence(seq, true);
            dropObject();
            m_state = state;
            playVoice(voice);
        };
        switch (m_carried) {
        case 0:
            if (m_state == 0) {
                mend(1, 1, "DB0157");
                return;
            }
            unhide(object(0)); // silently back
            dropObject();
            return;
        case 1:
            if (m_state == 1) {
                mend(3, 2, "DB0458");
                return;
            }
            if (m_state == 3) {
                mend(4, 4, "DB9020N");
                return;
            }
            playVoice("DB9019");
            unhide(object(1));
            dropObject();
            return;
        case 2:
            if (m_state == 2) {
                mend(4, 4, "DB9020N");
                playEffect("Rope");
                return;
            }
            if (m_state == 1) {
                mend(2, 3, "DB0157");
                playEffect("Rope");
                return;
            }
            playVoice("DB9019");
            unhide(object(2));
            dropObject();
            return;
        case 3:
        case 4:
        case 5:
        case 6:
            // Objects 4..8 are never created by this class.
            playVoice("DB0156");
            unhide(object(m_carried));
            dropObject();
            return;
        case 7:
            playVoice("DB0156");
            unhide(object(7));
            dropObject();
            playEffect("Candle");
            return;
        default:
            return;
        }
    }

    // OnBellRung (0x40b7b0): once per visit. Counts the mission, saves, and
    // plays the bell movie: smk\MH7.smk the first time, then BellMH1/2/3 by
    // the count modulo 3.
    void onBellRung() {
        if (m_complete)
            return;
        m_complete = true;
        playEffect("complete");
        PlayerRecord& rec = app().record();
        rec.setU32(kBellGameDone, rec.u32(kBellGameDone) + 1);
        app().saveGame();
        std::rand(); // drawn and unused
        const int count = static_cast<int>(rec.u32(kBellGameDone));
        std::string path;
        if (count == 1)
            path = "smk\\MH7.smk";
        else if (count > 1)
            path = count % 3 == 0 ? "smk\\BellMH1.smk" : count % 3 == 1 ? "smk\\BellMH2.smk" : "smk\\BellMH3.smk";
        if (count >= 1)
            playHelpHint();
        m_movie = std::make_unique<engine::HcMovie>(engine(), *this);
        if (!m_movie->open(path, false, false, false, true, 2300000, true)) {
            log::warn("Smacker File Failed to Load!");
            return;
        }
        m_movie->setPosition(0, 0, 2000.0f);
        m_movie->link(this);
        m_movie->setPaused(false);
        if (count > 1 && (count % 3 == 0 || count % 3 == 1))
            createAnimAtViewport("GameBarbie")->move(192, -25, 0);
    }

    bool m_mouseLowerLeft = false;    // +0x1C0
    bool m_mouseUpperLeft = false;    // +0x1C2
    bool m_complete = false;          // +0x1C4
    int m_state = 0;                  // +0x1C8
    std::unique_ptr<engine::HcMovie> m_movie; // +0x1CC
    int m_carried = -1;               // +0x1D0
    int m_field1D4 = 0;               // +0x1D4: never set
    engine::Anim* m_pole = nullptr;   // +0x1D8
    std::array<engine::Anim*, 8> m_objects{}; // +0x1DC (only 1..3 exist)
    std::array<engine::Anim*, 6> m_overlays{}; // +0x21C
    engine::Anim* m_waterfall = nullptr; // +0x234
    std::unique_ptr<ACRollover> m_poleRollover; // +0x238
    std::array<std::unique_ptr<ACRollover>, 3> m_objectRollovers; // +0x23C
};

BRC_REGISTER_NODE("bellgame", [](Godiva& app, const std::string& name) { return std::make_unique<BellGame>(app, name); });

} // namespace
} // namespace brc::game
