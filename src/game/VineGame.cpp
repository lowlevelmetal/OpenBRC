// The vine bush by the rainbow trail (VineGame.cpp, class VineGame, ctor
// 0x43c0c0, mission M8): pull the seven rows of flowering vines off the bush,
// then click it twice to find and take the horseshoe. See
// docs/engine/nodes.md "VineGame".
#include "core/Log.h"
#include "core/StringUtil.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "engine/Scripts.h"
#include "engine/SoundManager.h"
#include "game/ToolbarNode.h"
#include "game/Widgets.h"

#include <array>
#include <cstdlib>
#include <memory>
#include <string>

namespace brc::game {
namespace {

// Click ids (anim +0x11c).
constexpr int kVinesClick = 1000;
constexpr int kPullableClick = 0x3E9;
constexpr int kInertClick = 0x3EA;
constexpr int kEdgeClick = 13000;

// Mission M8's completion counter (app+0x274).
constexpr std::size_t kVineGameDone = PlayerRecord::kMissionDone + 0x1C;
// The saddlebag item for the horseshoe.
constexpr int kHorseshoeItem = 0;

constexpr int kRows = 7;
// [0x803ea8] the rows' flower anims ("<colour>flower" + 1/2/3) and
// [0x803ec8] the middle flower of rows 2, 3, 4 and 6 once the horseshoe has
// been found before.
constexpr std::array<std::string_view, kRows> kFlower = {"redflower",  "orangeflower", "yellowflower", "greenflower",
                                                         "blueflower", "indigoflower", "violetflower"};
constexpr std::array<std::string_view, kRows> kFlower2 = {"red2flower",    "orange2flower", "yellow2flower",
                                                          "green2flower",  "blue2flower",   "indigo2flower",
                                                          "violet2flower"};

class VineGame final : public ToolbarNode {
public:
    VineGame(Godiva& app, const std::string& name) : ToolbarNode(app, name) {
        // 0x43c0c0.
        m_rows = 0;
        m_hideIndex = 0;
        m_activateIndex = 0;
        m_state = 0;
        const bool found = app.record().u32(kVineGameDone) != 0;
        for (std::size_t r = 0; r < kRows; ++r) {
            const std::string base(kFlower[r]);
            engine::Anim* a1 = createAnimAtViewport(base + "1");
            a1->setClickId(kInertClick);
            const bool alt = found && (r == 2 || r == 3 || r == 4 || r == 6);
            engine::Anim* a2 = createAnimAtViewport(alt ? std::string(kFlower2[r]) : base + "2");
            a2->setClickId(kInertClick);
            engine::Anim* a3 = createAnimAtViewport(base + "3");
            a3->setClickId(kInertClick);
            const std::array<engine::Anim*, 3> row = {a1, a2, a3};
            for (std::size_t k = 0; k < 3; ++k) {
                m_rollovers[r * 3 + k] = std::make_unique<ACRollover>(*this, row[k], 7);
                m_flowers[r * 3 + k] = row[k];
            }
        }
        advanceVineRow();
        m_state = 0;
        setSkin(kOut);
        addToolbarButton("quit", kQuit);
        addToolbarButton("pager", kPager);
        addToolbarButton("camera", kCamera);
        addToolbarButton("map", kMap);
        addToolbarButton("saddlebag", kBag);
        addToolbarButton("help", kHelp);
        addToolbarButton("home", kHome);
        m_firstPull = true;
        m_field27E = true;
        m_vines = createAnimAtViewport(found ? "vines2" : "vines");
        m_vines->setClickId(kVinesClick);
        setSkin(kVine);
        setCursor("vinecursor");
        listenToMouseButtons(); // [0x80a5a0]+0xC
        if (!ambientLoop().empty())
            releaseAmbient();
        playAmbientLoop("falls", 0x28);
        edgeScroll = true;      // +0xC4
        miniGame = true;        // +0xCE
        miniGameReturn = 0x15;  // +0xD0: rainbowS
        runScript("GameAmb");
        delayVoice("db9021", 1500);
    }

    ~VineGame() override {
        // 0x43c770.
        releaseAmbient();
        stopScript("GameAmb");
        auto& anims = engine().anims();
        anims.remove(m_vines->name());
        for (std::size_t i = 0; i < m_flowers.size(); ++i) {
            m_rollovers[i].reset();
            anims.remove(m_flowers[i]->name());
        }
        miniGame = false;
    }

    // MouseDown (0x43c930).
    void onPress(const engine::Pick& pick) override {
        const int id = pick.element ? pick.element->clickId() : 0;
        switch (id) {
        case kEdgeClick:
            app().session().arriveFromMiniGame = 1;
            gotoNode("rainbowS");
            return;
        case kVinesClick:
            clickVines();
            break;
        case kPullableClick:
            pullVine();
            break;
        case kInertClick:
            engine().sounds().playSound("null", -1, false, false, 1, false, 0x70, -1, 0, false);
            break;
        case kHelpClickId:
            playVoice("db9021");
            break;
        default:
            break;
        }
        ToolbarNode::onPress(pick);
    }

    // ListenToMessage (0x43cd00).
    void listen(int message, const engine::MessageData& data) override {
        if (message != kMsgEdgeScroll) {
            ToolbarNode::listen(message, data);
            return;
        }
        app().session().arriveFromMiniGame = 1;
        gotoNode("rainbowS");
    }

private:
    void playEffect(std::string_view name) {
        engine().sounds().playSound(name, -1, false, false, 1, false, 0x70, -1, 0, false);
    }

    void nextVinesSequence() { m_vines->setSequence(m_vines->sequence() + 1, true); }

    // AdvanceVineRow (0x43c850): the pulled row goes; the next row can be
    // pulled. After the seventh row the bush is bare (state 1).
    void advanceVineRow() {
        if (m_rows != 0)
            for (int k = 0; k < 3; ++k)
                m_flowers[static_cast<std::size_t>(m_hideIndex++)]->hide(true);
        if (m_rows == kRows) {
            m_state = 1;
            return;
        }
        for (int k = 0; k < 3; ++k)
            m_flowers[static_cast<std::size_t>(m_activateIndex++)]->setClickId(kPullableClick);
        ++m_rows;
    }

    // A pullable flower was clicked.
    void pullVine() {
        nextVinesSequence();
        advanceVineRow();
        playEffect("tugvine");
        const bool found = app().record().u32(kVineGameDone) != 0;
        if (m_state == 1) {
            delayVoice(found ? "db03045" : "db03030", 500);
            return;
        }
        if (m_firstPull) {
            m_firstPull = false;
            delayVoice("db0178", 500);
            return;
        }
        switch (std::rand() % 5) {
        case 0:
            playVoiceAfterPause("db03052");
            break;
        case 1:
            playVoiceAfterPause("db03046");
            break;
        case 2:
            playVoiceAfterPause("db03047");
            break;
        default:
            break; // 3 and 4: nothing
        }
    }

    // The bush itself: the first click on the bare bush shows the horseshoe
    // (and saves), the second takes it; with every mission done the game
    // goes straight back to the trail.
    void clickVines() {
        PlayerRecord& rec = app().record();
        if (m_state == 1) {
            if (rec.u32(kVineGameDone) == 0) {
                nextVinesSequence();
                playEffect("horseshoe");
                playVoice(rec.u16(PlayerRecord::kVoiceVariantB) == 0 ? "db04069" : "db04071");
                playVoiceAfterPause("db04070");
                app().saveGame();
            }
            m_state = 2;
            return;
        }
        if (m_state == 2 && rec.u32(kVineGameDone) == 0) {
            nextVinesSequence();
            m_state = 3;
            rec.setU32(kVineGameDone, rec.u32(kVineGameDone) + 1);
            addBagItem(kHorseshoeItem, false);
            bool all = true;
            for (std::size_t i = 0; i < 8; ++i)
                all = all && rec.u32(PlayerRecord::kMissionDone + 4 * i) != 0;
            if (all) {
                app().session().arriveFromMiniGame = 1;
                gotoNode("rainbowS");
            }
        }
    }

    engine::Anim* m_vines = nullptr;                // +0x1CC
    std::array<engine::Anim*, kRows * 3> m_flowers{}; // +0x1D0
    std::array<std::unique_ptr<ACRollover>, kRows * 3> m_rollovers; // +0x224
    int m_state = 0;            // +0x278: 0 pulling, 1 bare, 2 horseshoe shown, 3 taken
    bool m_firstPull = true;    // +0x27C
    bool m_field27E = true;     // +0x27E: set, never read
    int m_rows = 0;             // +0x280
    int m_hideIndex = 0;        // +0x284
    int m_activateIndex = 0;    // +0x288
};

BRC_REGISTER_NODE("vinegame", [](Godiva& app, const std::string& name) { return std::make_unique<VineGame>(app, name); });

} // namespace
} // namespace brc::game
