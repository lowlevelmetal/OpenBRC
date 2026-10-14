// The stable (stable.cpp, class Stable, ctor 0x429d90): the four horses in
// their stalls, the close-up of the chosen horse, the name book with its ten
// pages of six names, the right edge to the tack room. A new player (no horse
// yet) picks a horse and names it here, then goes to the tack room. See
// docs/engine/nodes.md "Stable".
#include "core/Log.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "engine/Resources.h"
#include "engine/SoundManager.h"
#include "game/ToolbarNode.h"
#include "game/Widgets.h"

#include <array>
#include <memory>

namespace brc::game {
namespace {

// Click ids (anim +0x11c).
constexpr int kHeadDapClick = 0x3840, kHeadWhiteClick = 0x3841, kHeadPaintClick = 0x3842,
              kHeadPalClick = 0x3843;
constexpr int kNameListClick = 0x3844, kLeftClick = 0x3845, kRightClick = 0x3846, kOkClick = 0x3847;
constexpr int kSquirrelClick = 0x3848, kBirdClick = 0x3849;

constexpr int kNamePages = 10, kNamesPerPage = 6;

// The heads in field order (+0x220..+0x22C): dapple, white, paint, palomino.
constexpr std::array<std::string_view, 4> kHeadNames = {"headdap", "headwhite", "headpaint", "headpal"};
constexpr std::array<std::string_view, 4> kHeadSounds = {"horse1", "horse2", "horse3", "horse4"}; // 0x801d40
constexpr std::array<std::string_view, 4> kCoatDatabases = {"stablewhite.omt", "stabledap.omt",
                                                            "stablepaint.omt", "stablepal.omt"};

// The inline "show" of the original: a hidden anim gets its hide counter set
// to 1, then Show.
void show(engine::Anim* a) {
    if (a && a->hideCount() != 0) {
        a->setHideCount(1);
        a->hide(false);
    }
}

// anim+0xe0 = 0 (playback) and +0x5c = 0 (motion).
void run(engine::Anim* a) {
    if (!a)
        return;
    a->setPlaybackPauseCount(0);
    a->setMotionPauseCount(0);
}

// [0x801d74]: the last idle animation tick (SpendTime's static, -1 = not yet).
std::uint32_t s_lastIdleTick = 0xFFFFFFFF;

class Stable final : public ToolbarNode {
public:
    Stable(Godiva& app, const std::string& name) : ToolbarNode(app, name) {
        // 0x429d90. ([0x80a148] = 1 here; nothing reads it.)
        engine().anims().preloadCache("stable.omt.cache");
        setSkin(kIn);
        auto& rec = app.record();
        if (rec.horse() == PlayerRecord::kNoHorse) {
            m_newPlayer = true;
            for (int i = 0; i < 4; ++i)
                rec.setS32(PlayerRecord::kHorseName + 4 * static_cast<std::size_t>(i), -1);
        } else {
            addToolbarButton("tbQuitIn", kQuit);
            addToolbarButton("tbPagerIn", kPager);
            addToolbarButton("tbCameraIn", kCamera);
            addToolbarButton("tbMapIn", kMap);
            addToolbarButton("tbBagIn", kBag);
            addToolbarButton("tbHelpIn", kHelp);
            addToolbarButton("tbHomeIn", kHome);
            m_newPlayer = false;
        }
        // (+0xFC = 1: a toolbar field nothing is known to read.)
        setCursor("cursorIn");
        edgeRight = true;
        for (auto db : kCoatDatabases)
            engine().resources().open(db);

        static constexpr std::array<int, 4> kHeadClicks = {kHeadDapClick, kHeadWhiteClick, kHeadPaintClick,
                                                           kHeadPalClick};
        for (std::size_t i = 0; i < 4; ++i)
            m_heads[i] = createAnimAtViewport(kHeadNames[i]);
        for (std::size_t i = 0; i < 4; ++i)
            m_headRollovers[i] = std::make_unique<ACRollover>(*this, m_heads[i], 7);
        for (std::size_t i = 0; i < 4; ++i)
            if (m_heads[i])
                m_heads[i]->setClickId(kHeadClicks[i]);
        for (auto* h : m_heads)
            if (h)
                h->setSequence(0, true);

        m_nameLists = createAnimAtViewport("namelists");
        if (m_nameLists)
            m_nameLists->hide(true);
        m_nameListsRollover = std::make_unique<ACRollover>(*this, m_nameLists, 7);
        m_namePlates = createAnimAtViewport("nameplates");
        if (m_namePlates)
            m_namePlates->hide(true);
        m_namePlatesRollover = std::make_unique<ACRollover>(*this, m_namePlates, 7);

        m_bird = createAnimAtViewport("bird");
        if (m_bird)
            m_bird->setSequence(0, true);
        m_birdRollover = std::make_unique<ACRollover>(*this, m_bird, 7);
        if (m_bird) {
            m_bird->setClickId(kBirdClick);
            m_bird->pause(true);
        }
        m_overview = true;

        m_stallWalk = createAnimAtViewport("bstallwalk");
        if (m_stallWalk)
            m_stallWalk->setPosition(0, 0, 150.0f);
        m_barbieAmbient = createAnimAtViewport("barbieAmbient");
        if (m_barbieAmbient)
            m_barbieAmbient->hide(true);
        m_squirrel = createAnimAtViewport("squirrel");
        if (m_squirrel)
            m_squirrel->setClickId(kSquirrelClick);
        m_squirrelRollover = std::make_unique<ACRollover>(*this, m_squirrel, 7);
        m_mouthFlap = createAnimAtViewport("MouthFlapStable");
        if (m_mouthFlap) {
            m_mouthFlap->hide(true);
            m_mouthFlap->move(175.0f, -2.0f, 100.0f);
        }

        if (!ambientLoop().empty())
            releaseAmbient();
        playAmbientLoop("stableamb", 0x28);
        playVoiceNoFlaps("pause");
        playVoiceNoFlaps("pause");
        playVoiceNoFlaps("pause");
        const std::uint32_t visits = rec.u32(PlayerRecord::kStableVisits);
        playVoiceNoFlaps(visits == 0 ? "DB0554" : visits == 1 ? "DB0556" : "DB0557");
        playVoiceNoFlaps("DB04108");
        rec.setU32(PlayerRecord::kStableVisits, visits + 1);
        listenToMouseButtons(); // [0x80a5a0]+0xC
    }

    ~Stable() override {
        // 0x42a460.
        m_left.reset();
        m_right.reset();
        m_ok.reset();
        m_birdRollover.reset();
        m_squirrelRollover.reset();
        for (auto& r : m_headRollovers)
            r.reset();
        m_namePlatesRollover.reset();
        m_nameListsRollover.reset();
        releaseAmbient();
        for (auto db : kCoatDatabases)
            engine().resources().close(db);
        auto& anims = engine().anims();
        for (auto n :
             {"HeadDap",      "HeadPaint",  "HeadPal",    "HeadWhite",   "MouthFlapStable", "BarbieAmbient",
              "bird",         "bStallWalk", "buttonleft", "buttonok",    "buttonright",     "namelists",
              "namePlates",   "squirrel",   "DapCloseUp", "DappleStall", "palcloseup",      "stallpalamino",
              "PaintCloseup", "PaintStall", "stallwhite", "whitecloseup"})
            anims.remove(n);
    }

    // ListenToMessage (0x42a720).
    void listen(int message, const engine::MessageData& data) override {
        if (message == -103 && m_swallowRelease) { // the release of the click that opened the stall
            m_swallowRelease = false;
            return;
        }
        if (message == kMsgEdgeScroll) {
            leaveStallView();
            showOverview();
        } else if (message == -102) {
            const auto* e = static_cast<const engine::InputEvent*>(data.ptr);
            const auto [mx, my] = mouseWorldPos();
            const int x = static_cast<std::int16_t>(static_cast<int>(mx));
            const int y = static_cast<std::int16_t>(static_cast<int>(my));
            // A click on the closed book under the stall opens the name book.
            if (e && e->type == engine::InputEvent::LeftDown && m_stallActive && !m_overview && x > 240 &&
                x < 393 && y > 363 && y < 394) {
                m_stallActive = true;
                show(m_nameLists);
                if (m_nameLists)
                    m_nameLists->setClickId(kNameListClick);
                if (!m_left) {
                    createBookButtons();
                    if (m_ok && m_ok->anim())
                        m_ok->anim()->setPerPixelHit(false);
                }
                playEffect("bookopen");
                openNameBook();
            }
        } else if (message == kHeadWhiteClick) {
            selectWhite();
        } else if (message == kMsgEdgeRight) {
            if (app().record().horse() != PlayerRecord::kNoHorse)
                gotoNode("tackroom");
        } else if (message == kHeadPalClick) {
            selectPalomino();
        } else if (message == kLeftClick) {
            log::debug("Left button clicked");
        } else if (message == kRightClick) {
            log::debug("Right button clicked");
        } else if (message == kOkClick) {
            log::debug("OK button clicked");
        }
        ToolbarNode::listen(message, data);
    }

    // KeyDown (0x42aad0): Escape only opens the exit dialog after the second visit.
    void onKeyDown(const engine::InputEvent& e) override {
        if (e.key != engine::key::kEscape) {
            ToolbarNode::onKeyDown(e);
            return;
        }
        if (app().record().u32(PlayerRecord::kStableVisits) > 1)
            showExitDialog(true);
    }

    // SpendTime (0x42ab10).
    void tick() override {
        ToolbarNode::tick();
        const std::uint32_t now = engine().ticks();
        if (s_lastIdleTick == 0xFFFFFFFF)
            s_lastIdleTick = now;
        // The mouth flaps show while Barbie talks standing still in the overview.
        if (!voicePlaying() || !m_barbieAmbient || !m_barbieAmbient->stopped() ||
            !m_barbieAmbient->visible() || !m_overview || !m_stallWalk || m_stallWalk->visible())
            hideMouthFlap();
        else
            showMouthFlap();
        if (!m_overview || now - s_lastIdleTick <= 1000)
            return;
        // Every second one random idle: the squirrel, Barbie, or a horse.
        s_lastIdleTick = now;
        const int what = engine().rand() % 7;
        const int seq = engine().rand() % 4;
        if (what == 4 || what == 6) {
            if (m_squirrel && m_squirrel->sequence() != 2)
                m_squirrel->setSequence(engine().rand() % 2, true);
        } else if (what == 5) {
            if (m_stallWalk && !m_stallWalk->visible() && m_barbieAmbient && m_barbieAmbient->stopped() &&
                m_mouthFlap && !m_mouthFlap->visible()) {
                m_stallWalk->hide(true);
                m_barbieAmbient->setSequence(engine().rand() % 7, true);
            }
        } else {
            if (engine::Anim* head = m_heads[static_cast<std::size_t>(what)])
                head->setSequence(seq, true);
            engine().sounds().playSound(kHeadSounds[static_cast<std::size_t>((seq + 5) % 4)], -1, false,
                                        false, 1, false, 0x32, -1, 0, false);
        }
    }

    // MouseDown (0x42ad00).
    void onPress(const engine::Pick& pick) override {
        auto& rec = app().record();
        switch (pick.element ? pick.element->clickId() : 0) {
        case kHeadDapClick:
            rec.setU32(PlayerRecord::kHorse, 3);
            selectDapple();
            desktopHorseCoat(3);
            break;
        case kHeadWhiteClick:
            rec.setU32(PlayerRecord::kHorse, 1);
            selectWhite();
            desktopHorseCoat(1);
            break;
        case kHeadPaintClick:
            rec.setU32(PlayerRecord::kHorse, 2);
            selectPaint();
            desktopHorseCoat(2);
            break;
        case kHeadPalClick:
            rec.setU32(PlayerRecord::kHorse, 0);
            selectPalomino();
            desktopHorseCoat(0);
            break;
        case kHelpClickId:
            playVoice(m_stallActive ? "DB04109" : "Db04108");
            break;
        case kNameListClick: {
            const auto [mx, my] = mouseWorldPos();
            pickNameAt(static_cast<std::int16_t>(static_cast<int>(my)));
            break;
        }
        case kLeftClick:
            if (--m_page < 0)
                m_page += kNamePages;
            showPage();
            playEffect("Page");
            break;
        case kRightClick:
            m_page = (m_page + 1) % kNamePages;
            showPage();
            playEffect("Page");
            break;
        case kOkClick:
            playEffect("listclose");
            if (horseName() >= 0) {
                if (rec.horse() != PlayerRecord::kNoHorse)
                    app().requestNode("tackroom", 20, true);
                closeNameBook();
                showStallView();
                leaveStallView();
                showOverview();
            }
            break;
        case kSquirrelClick:
            m_squirrelOn = false;
            playEffect("squirl5");
            if (m_squirrel)
                m_squirrel->setSequence(2, true);
            break;
        case kBirdClick:
            if (m_bird) {
                m_bird->setSequence(0, true);
                run(m_bird);
            }
            playEffect("babybird");
            break;
        default:
            break;
        }
        toolbarMouseDown(pick);
    }

    // AnimEnded (0x42bf70); it does not pass the event on.
    void sequenceEnded(engine::Anim& anim, int seq) override {
        if (&anim == m_bird) {
            m_bird->setSequenceFrame(0, 0);
            m_bird->pause(true);
            return;
        }
        if (&anim != m_stallWalk)
            return;
        // Barbie walks out of the stall in three parts, then stands.
        if (seq == 0)
            m_stallWalk->setSequence(1, true);
        if (seq == 1)
            m_stallWalk->setSequence(2, true);
        if (seq == 2) {
            m_stallWalk->hide(true);
            show(m_barbieAmbient);
            run(m_barbieAmbient);
        }
    }

private:
    void playEffect(std::string_view name) {
        engine().sounds().playSound(name, -1, false, false, 1, false, 0x70, -1, 0, false);
    }

    // DesktopHorse::SetSelectedCoat (0x411800): the coat for the Desktop Horse
    // program, which OpenBRC does not install.
    static void desktopHorseCoat(int coat) { log::debug("Desktop Horse coat {} (not supported)", coat); }

    int horseName() const {
        const auto& rec = app().record();
        return rec.s32(PlayerRecord::kHorseName + 4 * static_cast<std::size_t>(rec.horse()));
    }

    void createBookButtons() {
        m_left = std::make_unique<RolloverButton>(*this, false);
        m_right = std::make_unique<RolloverButton>(*this, false);
        m_ok = std::make_unique<RolloverButton>(*this, false);
        m_left->init("buttonleft");
        m_right->init("buttonright");
        m_ok->init("buttonok");
        if (m_left->anim())
            m_left->anim()->setClickId(kLeftClick);
        if (m_right->anim())
            m_right->anim()->setClickId(kRightClick);
        if (m_ok->anim())
            m_ok->anim()->setClickId(kOkClick);
    }

    void showPage() {
        if (!m_nameLists)
            return;
        m_nameLists->setSequenceFrame(0, m_page);
        m_nameLists->pause(true);
    }

    void showNamePlate() {
        if (!m_namePlates)
            return;
        m_namePlates->hide(false);
        m_namePlates->setSequenceFrame(0, horseName());
        m_namePlates->pause(true);
    }

    // PickNameAt (0x42b140): the row under y on the open page names the horse.
    bool pickNameAt(int y) {
        int row;
        if (y >= 0x54 && y <= 0x72)
            row = 0;
        else if (y >= 0x78 && y <= 0x96)
            row = 1;
        else if (y >= 0x9B && y <= 0xB9)
            row = 2;
        else if (y >= 0xC0 && y <= 0xDE)
            row = 3;
        else if (y >= 0xE2 && y <= 0x100)
            row = 4;
        else if (y >= 0x104 && y <= 0x122)
            row = 5;
        else
            return false;
        auto& rec = app().record();
        rec.setS32(PlayerRecord::kHorseName + 4 * static_cast<std::size_t>(rec.horse()),
                   m_page * kNamesPerPage + row);
        playEffect("pickname");
        if (horseName() != -1)
            showNamePlate();
        // The voice of the name (the original also copies it to node+0x138).
        playVoice(horseNameSound());
        playVoiceNoFlaps("spause");
        return true;
    }

    // HideOverview (0x42b440).
    void hideOverview() {
        edgeRight = false;
        edgeScroll = true;
        if (m_stallWalk)
            m_stallWalk->hide(true);
        for (auto* h : m_heads)
            if (h)
                h->hide(true);
        if (m_squirrel)
            m_squirrel->hide(true);
    }

    // SelectDapple / SelectPaint / SelectWhite / SelectPalomino (0x42b4b0,
    // 0x42b5f0, 0x42b760, 0x42b8a0): the horse's sound (looping until the
    // overview returns), its stall and close-up, the name plate or the name book.
    void selectDapple() { selectHorse("dapple", "DappleStall", "DapCloseUp", 3, false); }
    void selectPaint() { selectHorse("Paint", "PaintStall", "PaintCloseup", 2, false); }
    void selectWhite() { selectHorse("white", "stallwhite", "whitecloseup", 1, false); }
    void selectPalomino() { selectHorse("Palamino", "stallpalamino", "palcloseup", 0, true); }

    void selectHorse(std::string_view sound, std::string_view stall, std::string_view closeUp,
                     std::uint32_t horse, bool palomino) {
        auto& rec = app().record();
        stopVoice();
        engine().sounds().playSound(sound, -1, false, false, 0, false, 0x70, -1, 0, false);
        hideOverview();
        m_page = 0;
        if (palomino) {
            // The palomino sets the horse first and shows the stall before the plate.
            rec.setU32(PlayerRecord::kHorse, horse);
            m_stall = createAnimAtViewport(stall);
            m_closeUp = createAnimAtViewport(closeUp);
            showStallView();
            if (horseName() != -1)
                showNamePlate();
            m_stallActive = true;
        } else {
            m_stall = createAnimAtViewport(stall);
            m_closeUp = createAnimAtViewport(closeUp);
            if (horse == 2 && m_closeUp) {
                m_closeUp->move(-30.0f, 0, 0);
                m_closeUp->setSequenceFrame(0, 50);
            }
            m_stallActive = true;
            rec.setU32(PlayerRecord::kHorse, horse);
            if (horseName() != -1)
                showNamePlate();
            showStallView();
        }
        if (horseName() == -1)
            openNameBook();
        if (cursor())
            cursor()->setSequence(0, true);
        const std::size_t chosen = PlayerRecord::kHorseChosen + 4 * static_cast<std::size_t>(horse);
        if (rec.u32(chosen) == 0)
            rec.setU32(chosen, 1);
    }

    // OpenNameBook (0x42b9e0).
    void openNameBook() {
        playVoice("DB9003");
        m_stallActive = true;
        show(m_nameLists);
        if (m_nameLists)
            m_nameLists->setClickId(kNameListClick);
        if (!m_left)
            createBookButtons();
        playEffect("bookopen");
        m_overview = false;
        m_stallActive = true;
        for (auto* b : {m_left.get(), m_right.get(), m_ok.get()})
            show(b->anim());
        if (horseName() != -1)
            show(m_namePlates);
        show(m_nameLists);
        m_page = 0;
        if (m_nameLists)
            m_nameLists->setSequenceFrame(0, 0);
    }

    // ShowStallView (0x42bc70).
    void showStallView() {
        m_swallowRelease = true;
        m_stallActive = true;
        m_overview = false;
        show(m_stall);
        if (m_stallWalk)
            m_stallWalk->pause(true);
        hideMouthFlap();
        show(m_closeUp);
        run(m_closeUp);
    }

    // LeaveStallView (0x42bcf0): a new player who named the horse goes on to
    // the tack room.
    void leaveStallView() {
        if (m_stall)
            m_stall->hide(true);
        if (m_closeUp) {
            m_closeUp->hide(true);
            m_closeUp->pause(true);
        }
        if (m_namePlates)
            m_namePlates->hide(true);
        if (m_nameLists)
            m_nameLists->hide(true);
        if (m_newPlayer && horseName() >= 0)
            app().requestNode("tackroom", 20, true);
        if (m_left)
            for (auto* b : {m_left.get(), m_right.get(), m_ok.get()})
                if (b->anim())
                    b->anim()->hide(true);
        if (m_nameLists)
            m_nameLists->hide(true);
        edgeRight = true;
        edgeScroll = false;
    }

    // CloseNameBook (0x42bdd0).
    void closeNameBook() {
        m_stallActive = false;
        for (auto* b : {m_left.get(), m_right.get(), m_ok.get()})
            if (b && b->anim())
                b->anim()->hide(true);
        if (horseName() != -1)
            show(m_namePlates);
        if (m_nameLists)
            m_nameLists->hide(true);
    }

    // ShowOverview (0x42be60).
    void showOverview() {
        m_overview = true;
        auto& sounds = engine().sounds();
        for (auto s : {"paint", "palamino", "white", "dapple"})
            sounds.stopSound(s);
        if (m_stallWalk)
            m_stallWalk->hide(true);
        show(m_barbieAmbient);
        for (auto* h : m_heads)
            show(h);
        if (m_squirrelOn)
            show(m_squirrel);
    }

    void showMouthFlap() { show(m_mouthFlap); } // 0x42c030
    void hideMouthFlap() {                      // 0x42c060
        if (m_mouthFlap)
            m_mouthFlap->hide(true);
    }

    engine::Anim* m_stallWalk = nullptr;                        // +0x1C0 "bstallwalk"
    engine::Anim* m_barbieAmbient = nullptr;                    // +0x1C4
    engine::Anim* m_bird = nullptr;                             // +0x1C8
    engine::Anim* m_mouthFlap = nullptr;                        // +0x1CC
    std::unique_ptr<ACRollover> m_birdRollover;                 // +0x1D0
    std::unique_ptr<ACRollover> m_squirrelRollover;             // +0x1D4
    std::array<std::unique_ptr<ACRollover>, 4> m_headRollovers; // +0x1D8..+0x1E4
    std::unique_ptr<ACRollover> m_namePlatesRollover;           // +0x1E8
    std::unique_ptr<ACRollover> m_nameListsRollover;            // +0x1EC
    std::unique_ptr<RolloverButton> m_left, m_right, m_ok;      // +0x1F0..+0x1F8
    engine::Anim* m_namePlates = nullptr;                       // +0x1FC
    engine::Anim* m_nameLists = nullptr;                        // +0x200
    int m_page = 0;                                             // +0x204
    bool m_newPlayer = false;                                   // +0x218: no horse when the stable opened
    engine::Anim* m_closeUp = nullptr;                          // +0x21C
    std::array<engine::Anim*, 4> m_heads{}; // +0x220..+0x22C: dapple, white, paint, palomino
    bool m_swallowRelease = false;          // +0x240
    bool m_squirrelOn = true;               // +0x248: cleared once the squirrel was clicked
    engine::Anim* m_squirrel = nullptr;     // +0x24C
    bool m_stallActive = false;             // +0x250: a horse is selected (stall or name book)
    engine::Anim* m_stall = nullptr;        // +0x254
    bool m_overview = true;                 // +0x258: the four stalls are showing
};

} // namespace

BRC_REGISTER_NODE("stable",
                  [](Godiva& app, const std::string& name) { return std::make_unique<Stable>(app, name); });

} // namespace brc::game
