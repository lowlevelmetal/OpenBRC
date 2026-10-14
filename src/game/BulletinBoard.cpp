// The bulletin board (bulletinboard.cpp, 0x40bb90–0x40ec3f): the board scene
// and its panels — clue book, team photo, membership card and certificates
// book. The handbook and its print dialog are in Handbook.cpp. See
// game/BulletinBoard.h and docs/engine/classes.md.
#include "game/BulletinBoard.h"

#include "core/StringUtil.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "engine/Font.h"
#include "game/Godiva.h"
#include "game/Popups.h"
#include "game/Widgets.h"

#include <algorithm>
#include <array>
#include <format>
#include <memory>

namespace brc::game {

bool BulletinBoard::rabbitPending = false;

namespace {

constexpr int kHandbookId = 10000;     // handbookbtn
constexpr int kCertificatesId = 0x2711; // certificatebtn
constexpr int kClueBookId = 0x2712;    // cluebtn
constexpr int kTeamPhotoId = 0x2713;   // photobtn
constexpr int kMembershipId = 0x2714;  // the rider-rank button
constexpr std::uint32_t kIdleMs = 450000; // the board's inactivity interval

// The voice "go ride to Rabbit Run first" (the first trail ride is not done).
constexpr std::string_view kRabbitVoice = "db9011";

void playSound(ToolbarNode& node, std::string_view name, int volume = 0x70) {
    // 0x44c3c0(name, -1, 0, 0, 1, 0, volume, 0xffff, 0, 0).
    node.engine().sounds().playSound(name, -1, false, false, 1, false, volume, -1, 0, false);
}

void setHidden(engine::Element* e, bool hidden) {
    if (e)
        e->hide(hidden);
}

void removeAnim(ToolbarNode& node, engine::Anim*& anim) {
    if (anim)
        node.engine().anims().remove(anim);
    anim = nullptr;
}

engine::Anim* buttonAnim(const std::unique_ptr<ACButton>& b) { return b ? b->anim() : nullptr; }

std::unique_ptr<ACButton> makeButton(ToolbarNode& node, std::string_view anim, int clickId) {
    auto b = std::make_unique<ACButton>(node);
    b->init(anim);
    if (b->anim())
        b->anim()->setClickId(clickId);
    return b;
}

engine::Anim* makeAnim(ToolbarNode& node, std::string_view name, int clickId = 0, bool perPixel = true,
                       std::string_view instance = {}) {
    engine::Anim* a = node.createAnimAtViewport(name, instance);
    if (a) {
        if (clickId != 0)
            a->setClickId(clickId);
        if (!perPixel)
            a->setPerPixelHit(false);
    }
    return a;
}

// Removes `item` from the saddlebag when it is there.
void dropBagItem(ToolbarNode& node, int item) {
    if (node.hasBagItem(item))
        node.removeBagItem(item);
}

// TackRoom::ReleaseAnims (0x42d3c0), called by BulletinBoard::Exit: the tack
// room's anims (and the board's buttons) are unlinked to survive the switch
// when going back to the tack room, deleted otherwise.
constexpr std::array<std::string_view, 42> kTackRoomAnims = {
    "BridleEat", "brushWall", "BulletinBoardSmall", "combWall", "EnglishSaddle", "EnglishSaddleCursor",
    "FancySaddleCursor", "HorseSign", "MouthFlapTackroom", "PadCursor", "SubtleAmbBridle", "TackAmbBridle",
    "tackroom_apple", "barbieAmbient", "bridle", "cat", "fancysaddle", "pad", "posTR", "tackOn",
    "tackroom_carrot", "tackroom_strawberry", "tackroom_sugar", "TackAmbPal", "TackFeedPal", "TackSubAmbPal",
    "TackAmbWhite", "TackFeedWhite", "TackSubAmbWhite", "TackAmbPaint", "TackFeedPaint", "TackSubAmbPaint",
    "TackAmbDap", "TackFeedDap", "TackSubAmbDap", "certificatebtn", "handbookbtn", "cluebtn", "starriderbtn",
    "moonriderbtn", "secretvalleyriderbtn", "photobtn"};

void releaseTackRoomAnims(engine::Engine& eng, bool keep) {
    auto& anims = eng.anims();
    for (std::string_view name : kTackRoomAnims) {
        if (keep) {
            if (engine::Anim* a = anims.find(name))
                a->unlink();
        } else {
            anims.remove(name);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// BBClueBook (name inferred, ctor 0x40bb90, vtable 0x7f0bf8): eight clue pages,
// one per mission; "Go!" starts the page's mission.
class BBClueBook final : public BBPanel {
public:
    explicit BBClueBook(BulletinBoard& node) : BBPanel(node, "bb_clue") {
        m_clues = makeAnim(node, "clues", bb::kPage);                                   // +0x0C
        m_go = makeButton(node, "gobtn_clues", bb::kGo);                                // +0x24
        if (m_go->anim())
            m_go->anim()->setPerPixelHit(false);
        m_leftPage = makeAnim(node, "clueleftpage", bb::kPrev, false);                  // +0x10
        m_rightPage = makeAnim(node, "cluerightpage", bb::kNext, false);                // +0x14
        m_leftArrow = makeButton(node, "clue_leftarrow", bb::kPrev);                    // +0x18
        m_rightArrow = makeButton(node, "clue_rightarrow", bb::kNext);                  // +0x1C
        m_thanks = makeAnim(node, "thanks");                                            // +0x20
    }

    ~BBClueBook() override {
        // 0x40be50.
        m_go.reset();
        m_leftArrow.reset();
        m_rightArrow.reset();
        removeAnim(m_node, m_thanks);
        removeAnim(m_node, m_clues);
        removeAnim(m_node, m_leftPage);
        removeAnim(m_node, m_rightPage);
    }

    void soundEnded(const std::string& name) override {
        // 0x40c0a0 (strcmp: case-sensitive).
        if (BulletinBoard::rabbitPending && name == kRabbitVoice)
            m_node.app().gotoNode("rabbitE");
    }

    void hide() override {
        // 0x40c660.
        BBPanel::hide();
        setHidden(m_clues, true);
        setHidden(m_leftPage, true);
        setHidden(m_rightPage, true);
        setHidden(buttonAnim(m_go), true);
        setHidden(m_thanks, true);
        setHidden(buttonAnim(m_leftArrow), true);
        setHidden(buttonAnim(m_rightArrow), true);
    }

    void show() override {
        // 0x40c6d0.
        BBPanel::show();
        setHidden(m_clues, false);
        setHidden(m_leftPage, false);
        setHidden(m_rightPage, false);
        if (m_clues)
            m_clues->setSequence(0, true);
        forceShow(buttonAnim(m_go));
        if (missionDone(0))
            forceShow(m_thanks);
        forceShow(buttonAnim(m_rightArrow));
        forceShow(buttonAnim(m_leftArrow));
        auto& rec = m_node.app().record();
        const std::uint32_t opens = rec.u32(PlayerRecord::kClueBookOpens);
        rec.setU32(PlayerRecord::kClueBookOpens, opens + 1);
        if (opens == 0) {
            m_node.playVoice("db04138");
            m_node.playVoiceAfterPause("db9009");
            m_node.playVoiceAfterPause("db9008");
            m_node.playVoiceAfterPause("db04139");
        } else {
            m_node.playVoice("db04139");
        }
    }

    void mouseDown(const engine::Pick& pick) override {
        // 0x40c110.
        const int id = pick.element ? pick.element->clickId() : 0;
        switch (id) {
        case ToolbarNode::kHelpClickId:
            m_node.playVoice("db0411");
            return;
        case bb::kNext:
        case bb::kPrev: {
            if (!m_clues)
                return;
            const int page = m_clues->sequence() + (id == bb::kNext ? 1 : -1);
            if (page < 0 || page > 7)
                return;
            m_clues->setSequence(page, true);
            playSound(m_node, "page");
            if (missionDone(page))
                forceShow(m_thanks);
            else
                setHidden(m_thanks, true);
            sayPage();
            return;
        }
        case bb::kPage:
            sayPage();
            return;
        case bb::kGo:
            go();
            return;
        default:
            m_node.toolbarClick(pick);
        }
    }

private:
    bool missionDone(int i) const {
        return m_node.app().record().u32(PlayerRecord::kMissionDone + 4 * static_cast<std::size_t>(i)) != 0;
    }

    // vt+0x1C SayPage (0x40bf50).
    void sayPage() {
        switch (m_clues ? m_clues->sequence() : -1) {
        case 0:
            m_node.playVoice("db04139");
            break;
        case 1:
            m_node.playVoice("db1501");
            m_node.playVoiceAfterPause("db0031");
            break;
        case 2:
            m_node.playVoice("db1502");
            break;
        case 3:
            m_node.playVoice("db1503");
            break;
        case 4:
            m_node.playVoice("db04017");
            m_node.playVoiceAfterPause("db0105");
            playSound(m_node, "thunder2");
            break;
        case 5:
            m_node.playVoice("db1505");
            m_node.playVoiceAfterPause("db0131");
            break;
        case 6:
            m_node.playVoice("db1506");
            m_node.playVoiceAfterPause(m_node.engine().rand() % 2 != 0 ? "db0146" : "db0150");
            break;
        case 7:
            m_node.playVoice("db04025");
            m_node.playVoiceAfterPause("db0173");
            break;
        default:
            break;
        }
    }

    // The "Go!" button (0x40c110, id 0x3478): the page's mission becomes the
    // active one (app+0x310, its trail app+0x314), its clue item goes into the
    // saddlebag, and the map opens unless the first ride must come first.
    void go() {
        Godiva& app = m_node.app();
        Session& s = app.session();
        dropBagItem(m_node, 13);
        dropBagItem(m_node, 14);
        const bool firstRideDone = app.record().u16(PlayerRecord::kFirstRideDone) != 0;
        auto rabbitFirst = [&] {
            if (!firstRideDone) {
                BulletinBoard::rabbitPending = true;
                m_node.playVoice(kRabbitVoice);
            }
        };
        switch (m_clues ? m_clues->sequence() : -1) {
        case 0:
            if (!missionDone(0))
                m_node.playVoice("db04001");
            s.activeMission = 0, s.missionTrail = 4;
            m_node.addBagItem(1);
            rabbitFirst();
            break;
        case 1:
            s.activeMission = 1, s.missionTrail = 2;
            m_node.addBagItem(2);
            rabbitFirst();
            break;
        case 2:
            s.activeMission = 2, s.missionTrail = 7;
            m_node.addBagItem(3);
            m_node.playVoice(missionDone(2) ? "db02014" : "db04096");
            rabbitFirst();
            break;
        case 3:
            m_node.playVoice(missionDone(3) ? "db0068" : "db04015");
            s.activeMission = 3, s.missionTrail = 4;
            m_node.addBagItem(4);
            break;
        case 4:
            m_node.playVoice(missionDone(4) ? "db0101" : "db04016");
            s.activeMission = 4, s.missionTrail = 0;
            m_node.addBagItem(5);
            playSound(m_node, "thunder2");
            break;
        case 5:
            m_node.playVoice(missionDone(5) ? "db0127" : "db04020");
            s.activeMission = 5, s.missionTrail = 4;
            m_node.addBagItem(6);
            break;
        case 6:
            m_node.playVoice(missionDone(6) ? "db0147" : "db04022");
            s.activeMission = 6, s.missionTrail = 2;
            m_node.addBagItem(7);
            break;
        case 7:
            if (!missionDone(7)) {
                m_node.playVoice("db04024");
            } else {
                static constexpr std::array<std::string_view, 3> kDone = {"db0168", "db0165", "db0167"};
                m_node.playVoice(kDone[static_cast<std::size_t>(m_node.engine().rand() % 3)]);
            }
            s.activeMission = 7, s.missionTrail = 3;
            m_node.addBagItem(8);
            break;
        default:
            break;
        }
        if (!BulletinBoard::rabbitPending)
            m_node.openPopup(popup::kMap);
    }

    engine::Anim* m_clues = nullptr;
    engine::Anim* m_leftPage = nullptr;
    engine::Anim* m_rightPage = nullptr;
    engine::Anim* m_thanks = nullptr;
    std::unique_ptr<ACButton> m_go, m_leftArrow, m_rightArrow;
};

// ---------------------------------------------------------------------------------------------
// BBTeamPhoto (name inferred, ctor 0x40c7f0, vtable 0x7f0c38).
class BBTeamPhoto final : public BBPanel {
public:
    explicit BBTeamPhoto(BulletinBoard& node) : BBPanel(node, "bbcloseup", "bb_teamphoto") {
        m_photo = makeAnim(node, "teamphoto"); // +0x0C
    }
    ~BBTeamPhoto() override { m_node.engine().anims().remove("teamphoto"); } // 0x40c890

    void hide() override {
        // 0x40c910.
        BBPanel::hide();
        setHidden(m_photo, true);
    }
    void show() override {
        // 0x40c930.
        BBPanel::show();
        forceShow(m_photo);
    }

private:
    engine::Anim* m_photo = nullptr;
};

// ---------------------------------------------------------------------------------------------
// BBMembershipCards (name inferred, ctor 0x40c960, vtable 0x7f0d78): the club
// card of the player's rank with the player's name on it.
class BBMembershipCards final : public BBPanel {
public:
    explicit BBMembershipCards(BulletinBoard& node) : BBPanel(node, "bbcloseup", "bb_membership") {
        m_cards = makeAnim(node, "membershipcards"); // +0x0C
        const auto rank = static_cast<int>(node.app().record().u32(PlayerRecord::kRiderRank));
        if (m_cards)
            m_cards->setSequence(rank, true);
        // +0x18 text style, +0x1C name field: centred on the card, 72 below its top.
        m_name = makeNameField(node);
        if (m_cards) {
            const engine::Rect card = m_cards->rect();
            placeNameField(*m_name, static_cast<float>(card.left + card.width() / 2 - nameFieldWidth(*m_name) / 2),
                           static_cast<float>(card.top) + 72.0f, m_cards->z() + 10.0f);
        }
    }

    ~BBMembershipCards() override {
        // 0x40cf40.
        m_name.reset();
        removeAnim(m_node, m_cards);
    }

    void hide() override {
        // 0x40cff0.
        BBPanel::hide();
        setHidden(m_cards, true);
        setHidden(m_name.get(), true);
    }

    void show() override {
        // 0x40d020.
        BBPanel::show();
        setHidden(m_cards, false);
        forceShow(m_name.get());
        switch (m_node.app().record().u32(PlayerRecord::kRiderRank)) {
        case 0:
            m_node.playVoice("db03033");
            break;
        case 1:
            m_node.playVoice("db03031");
            break;
        case 2:
            m_node.playVoice("db03032b");
            break;
        default:
            break;
        }
    }

private:
    engine::Anim* m_cards = nullptr;
    std::unique_ptr<engine::TextElement> m_name;
};

// ---------------------------------------------------------------------------------------------
// BBCertificates (name inferred, ctor 0x40d090, vtable 0x7f0da8): the four
// certificate challenges C1–C4 with their check marks; "Go!" starts one.
class BBCertificates final : public BBPanel {
public:
    explicit BBCertificates(BulletinBoard& node) : BBPanel(node, "bbcloseup", "bb_cert") {
        m_missions = makeAnim(node, "certificatemissions", bb::kPage); // +0x0C
        m_go = makeButton(node, "gobtn_certs", bb::kGo);                 // +0x18
        if (engine::Anim* a = m_go->anim()) {
            a->setPerPixelHit(false);
            a->move(-80.0f, 0.0f, 0.0f);
        }
        m_leftPage = makeAnim(node, "certificateleftpage", bb::kPrev, false);   // +0x10
        m_rightPage = makeAnim(node, "certificaterightpage", bb::kNext, false); // +0x14
        const auto& rec = node.app().record();
        auto cert = [&](int i) { return rec.u32(PlayerRecord::kCertificate + 4 * static_cast<std::size_t>(i)) != 0; };
        m_has = {cert(0), cert(1), cert(3)}; // +0x48 / +0x4A / +0x4C
        if (cert(2)) {
            m_thanks = makeAnim(node, "thanks", 0, true, "new_thanks"); // +0x50
            if (m_thanks)
                m_thanks->move(80.0f, 0.0f, 0.0f);
        }
        // The check marks sit 2500 units further back than the saddlebag's.
        auto checks = [&](int c, int n, std::array<engine::Anim*, 5>& out) {
            for (int k = 0; k < n; ++k) {
                out[static_cast<std::size_t>(k)] = makeAnim(node, std::format("c{} check{}In", c, k + 1), 0, true,
                                                            std::format("c{}check{}bb", c, k + 1));
                if (engine::Anim* a = out[static_cast<std::size_t>(k)])
                    a->move(0.0f, 0.0f, -2500.0f);
            }
        };
        if (m_has[0]) {
            checks(1, 3, m_checks[0]); // +0x1C..+0x24
            hideChecks(0);
        }
        if (m_has[1]) {
            checks(2, 3, m_checks[1]); // +0x28..+0x30
            hideChecks(1);
        }
        if (m_has[2]) {
            checks(4, 5, m_checks[2]); // +0x34..+0x44
            hideChecks(3);
        }
    }

    ~BBCertificates() override {
        // 0x40d3e0.
        m_go.reset();
        removeAnim(m_node, m_missions);
        removeAnim(m_node, m_leftPage);
        removeAnim(m_node, m_rightPage);
        for (auto& group : m_checks)
            for (auto*& a : group)
                removeAnim(m_node, a);
        removeAnim(m_node, m_thanks);
    }

    void soundEnded(const std::string& name) override {
        // 0x40dc40.
        if (BulletinBoard::rabbitPending && name == kRabbitVoice)
            m_node.app().gotoNode("rabbitE");
    }

    void hide() override {
        // 0x40daf0.
        BBPanel::hide();
        setHidden(m_missions, true);
        setHidden(m_leftPage, true);
        setHidden(m_rightPage, true);
        setHidden(buttonAnim(m_go), true);
        for (int i = 0; i < 4; ++i)
            hideChecks(i);
    }

    void show() override {
        // 0x40db60.
        BBPanel::show();
        setHidden(m_missions, false);
        setHidden(m_leftPage, false);
        setHidden(m_rightPage, false);
        if (m_missions)
            m_missions->setSequence(0, true);
        forceShow(buttonAnim(m_go));
        auto& rec = m_node.app().record();
        const std::uint32_t opens = rec.u32(PlayerRecord::kCertBookOpens);
        rec.setU32(PlayerRecord::kCertBookOpens, opens + 1);
        if (opens == 0) {
            m_node.playVoice("db0407");
            m_node.playVoiceNoFlaps("db9009");
            m_node.playVoiceNoFlaps("db9008");
        } else {
            m_node.playVoice("db9007");
        }
        showChecks(0);
        hideChecks(1);
        hideChecks(2);
        hideChecks(3);
    }

    void mouseDown(const engine::Pick& pick) override {
        // 0x40d570.
        const int id = pick.element ? pick.element->clickId() : 0;
        switch (id) {
        case ToolbarNode::kHelpClickId:
            m_node.playVoice("db0407");
            return;
        case bb::kNext:
        case bb::kPrev: {
            if (!m_missions)
                return;
            const int page = m_missions->sequence();
            const int to = page + (id == bb::kNext ? 1 : -1);
            if (to < 0 || to > 3)
                return;
            hideChecks(page);
            m_missions->setSequence(to, true);
            showChecks(to);
            playSound(m_node, "page");
            return;
        }
        case bb::kPage: {
            static constexpr std::array<std::string_view, 4> kPageVoice = {"db04007", "db0216", "db0190", "db0239"};
            const int page = m_missions ? m_missions->sequence() : -1;
            if (page >= 0 && page < 4)
                m_node.playVoice(kPageVoice[static_cast<std::size_t>(page)]);
            m_node.toolbarClick(pick);
            return;
        }
        case bb::kGo:
            go();
            return;
        default:
            m_node.toolbarClick(pick);
        }
    }

private:
    // 0x40dcb0 / 0x40dd80: the marks of page `page` (0 C1, 1 C2, 2 the C3
    // "thanks" stamp, 3 C4).
    void hideChecks(int page) {
        if (page == 2) {
            setHidden(m_thanks, true);
            return;
        }
        const int g = page == 3 ? 2 : page;
        if (page < 0 || page > 3 || !m_has[static_cast<std::size_t>(g)])
            return;
        for (engine::Anim* a : m_checks[static_cast<std::size_t>(g)])
            setHidden(a, true);
    }
    void showChecks(int page) {
        if (page == 2) {
            forceShow(m_thanks);
            return;
        }
        const int g = page == 3 ? 2 : page;
        if (page < 0 || page > 3 || !m_has[static_cast<std::size_t>(g)])
            return;
        for (engine::Anim* a : m_checks[static_cast<std::size_t>(g)])
            forceShow(a);
    }

    // "Go!" (id 0x3478): certificate missions 8..11 (C1 flowers, C2 birds,
    // C3, C4 collection), their trail, item and progress reset.
    void go() {
        Godiva& app = m_node.app();
        Session& s = app.session();
        auto& rec = app.record();
        dropBagItem(m_node, 13);
        dropBagItem(m_node, 14);
        const bool firstRideDone = rec.u16(PlayerRecord::kFirstRideDone) != 0;
        auto intro = [&](std::string_view voice, std::string_view next) {
            if (firstRideDone) {
                m_node.playVoice(voice);
                m_node.playVoiceAfterPause(next);
            } else {
                BulletinBoard::rabbitPending = true;
                m_node.playVoice(kRabbitVoice);
            }
        };
        switch (m_missions ? m_missions->sequence() : -1) {
        case 0:
            for (int item = 16; item <= 21; ++item)
                dropBagItem(m_node, item);
            s.activeMission = 8, s.missionTrail = 7;
            m_node.addBagItem(9);
            for (int k = 0; k < 3; ++k)
                rec.setU16(PlayerRecord::kC1Photos + 2 * static_cast<std::size_t>(k), 0);
            intro("db0204", "db0202");
            break;
        case 1:
            for (int item = 16; item <= 21; ++item)
                dropBagItem(m_node, item);
            s.activeMission = 9, s.missionTrail = 7;
            m_node.addBagItem(10);
            for (int k = 0; k < 3; ++k)
                rec.setU16(PlayerRecord::kC2Photos + 2 * static_cast<std::size_t>(k), 0);
            intro("db0220", "db0217");
            break;
        case 2:
            s.activeMission = 10, s.missionTrail = 3;
            m_node.addBagItem(11);
            intro("db0194", "db0191");
            break;
        case 3:
            s.c4Progress.fill(0);
            s.activeMission = 11, s.missionTrail = 7;
            m_node.addBagItem(12);
            intro("db0244", "db0240");
            break;
        default:
            break;
        }
        if (!BulletinBoard::rabbitPending)
            m_node.openPopup(popup::kMap);
    }

    engine::Anim* m_missions = nullptr;
    engine::Anim* m_leftPage = nullptr;
    engine::Anim* m_rightPage = nullptr;
    engine::Anim* m_thanks = nullptr;
    std::unique_ptr<ACButton> m_go;
    std::array<bool, 3> m_has{};                        // C1, C2, C4 earned
    std::array<std::array<engine::Anim*, 5>, 3> m_checks{}; // C1 (3), C2 (3), C4 (5)
};

} // namespace

// --- shared helpers ------------------------------------------------------------------------------

void forceShow(engine::Element* e) {
    if (e && e->hideCount() != 0) {
        e->setHideCount(1);
        e->hide(false);
    }
}

std::unique_ptr<engine::TextElement> makeNameField(BulletinBoard& node) {
    // BBTextStyle (0x40cce0 + inline): BureauGrotesque-FiveThree_18bb, space 12,
    // char space 1, proportional. The name has at most 12 characters, so the
    // 300-pixel limit never cuts it.
    auto& eng = node.engine();
    auto field = std::make_unique<engine::TextElement>(engine::Font::bureau18(eng.resources(), eng.gamePalette(), true));
    field->setText(node.app().record().name());
    field->link(&node);
    return field;
}

int nameFieldWidth(const engine::TextElement& text) {
    return std::min(text.font().textWidth(text.text()) + kNameMargin + 2 * kNameBorder, kNameMaxWidth);
}

void placeNameField(engine::TextElement& text, float x, float y, float z) {
    text.setPosition(x + kNameBorder, y + kNameBorder, z);
}

// --- BBPanel -------------------------------------------------------------------------------------

BBPanel::BBPanel(BulletinBoard& node, std::string_view anim, std::string_view instance)
    : m_node(node), m_anim(node.createAnimAtViewport(anim, instance)) {}

BBPanel::~BBPanel() {
    if (m_anim)
        m_node.engine().anims().remove(m_anim);
}

void BBPanel::hide() { setHidden(m_anim, true); }
void BBPanel::show() { setHidden(m_anim, false); }
bool BBPanel::isVisible() const { return m_anim && m_anim->hideCount() == 0; }
void BBPanel::mouseDown(const engine::Pick& pick) { m_node.toolbarClick(pick); }

// --- BulletinBoard -------------------------------------------------------------------------------

BulletinBoard::BulletinBoard(Godiva& app, const std::string& name) : ToolbarNode(app, name) {
    // 0x40def0.
    auto& anims = engine().anims();
    if (!anims.find("certificatebtn"))
        anims.preloadCache("bulletinboard.omt.cache");
    rabbitPending = false;
    swallowMouseUp = false;
    m_active = nullptr;
    returnToHandbook = false;
    setCursor("cursorIn");
    setSkin(kIn);
    addToolbarButton("tbQuitIn", kQuit);
    addToolbarButton("tbPagerIn", kPager);
    addToolbarButton("tbCameraIn", kCamera);
    addToolbarButton("tbMapIn", kMap);
    addToolbarButton("tbBagIn", kBag);
    addToolbarButton("tbHelpIn", kHelp);
    addToolbarButton("tbHomeIn", kHome);

    m_handbookBtn = makeButton(*this, "handbookbtn", kHandbookId);
    m_certificateBtn = makeButton(*this, "certificatebtn", kCertificatesId);
    m_clueBtn = makeButton(*this, "cluebtn", kClueBookId);

    // The rider rank follows the certificates earned: 2 make a Moon Rider, 4 a Star Rider.
    auto& rec = app.record();
    int earned = 0;
    for (int i = 0; i < 4; ++i)
        if (rec.u32(PlayerRecord::kCertificate + 4 * static_cast<std::size_t>(i)) != 0)
            ++earned;
    if (earned == 2)
        rec.setU32(PlayerRecord::kRiderRank, 1);
    else if (earned == 4)
        rec.setU32(PlayerRecord::kRiderRank, 2);
    static constexpr std::array<std::string_view, 3> kRankButton = {"secretvalleyriderbtn", "moonriderbtn",
                                                                    "starriderbtn"};
    m_riderBtn = std::make_unique<ACButton>(*this);
    if (const std::uint32_t rank = rec.u32(PlayerRecord::kRiderRank); rank < kRankButton.size())
        m_riderBtn->init(kRankButton[rank]);
    if (m_riderBtn->anim())
        m_riderBtn->anim()->setClickId(kMembershipId);

    listenToMouseButtons(); // [0x80a5a0]+0xC
    edgeScroll = true;      // +0xC4: both edges post 13000
}

BulletinBoard::~BulletinBoard() {
    // 0x40e970.
    app().idleTimer().stop();
    engine().mouseListeners().removeListener(this);
    m_active = nullptr;
    m_teamPhoto.reset();
    m_printDialog.reset();
    // The buttons' anims stay for TackRoom::ReleaseAnims (+0xac zeroed first).
    for (auto* b : {&m_handbookBtn, &m_certificateBtn, &m_clueBtn, &m_photoBtn, &m_riderBtn}) {
        if (*b)
            (*b)->releaseAnim();
        b->reset();
    }
    m_handbook.reset();
    m_clueBook.reset();
    m_certificates.reset();
    m_membership.reset();
    edgeScroll = false;
}

void BulletinBoard::openPanel(BBPanel* panel) {
    // 0x40e240.
    if (m_active)
        m_active->hide();
    m_active = panel;
    if (panel)
        panel->show();
}

void BulletinBoard::showExitDialog(bool save) {
    // 0x40e290.
    if (m_active && m_active->isVisible())
        m_active->hide();
    ToolbarNode::showExitDialog(save);
}

void BulletinBoard::start() {
    // 0x40e2d0 (the base Start is not called).
    auto& rec = app().record();
    const std::uint32_t visits = rec.u32(PlayerRecord::kBbVisits);
    rec.setU32(PlayerRecord::kBbVisits, visits + 1);
    if (visits == 0) {
        playVoice("db9000");
        playVoiceAfterPause("db9001");
        playVoiceAfterPause("db9004");
    } else {
        playVoice("db9059");
    }
    m_photoBtn = makeButton(*this, "photobtn", kTeamPhotoId);
    engine::Timer& idle = app().idleTimer();
    idle.setInterval(kIdleMs);
    idle.start();
}

template <class T>
BBPanel* BulletinBoard::open(std::unique_ptr<BBPanel>& slot) {
    // Created on first use and hidden, then made the open panel and shown.
    if (!slot) {
        slot = std::make_unique<T>(*this);
        slot->hide();
    }
    m_active = slot.get();
    slot->show();
    return slot.get();
}

void BulletinBoard::onPress(const engine::Pick& pick) {
    // 0x40e410.
    if (m_active && m_active->isVisible()) {
        m_active->mouseDown(pick);
        return;
    }
    const int id = pick.element ? pick.element->clickId() : 0;
    switch (id) {
    case kHelpClickId:
        playVoice("db9000");
        playVoiceAfterPause("db9001");
        playVoiceAfterPause("db9004");
        break;
    case kHandbookId:
        swallowMouseUp = true;
        if (!m_handbook) {
            m_handbook = makeHandbook(*this);
            m_handbook->hide();
        }
        m_active = m_handbook.get();
        m_handbook->show();
        playSound(*this, "bookopen");
        break;
    case kCertificatesId:
        swallowMouseUp = true;
        open<BBCertificates>(m_certificates);
        break;
    case kClueBookId:
        swallowMouseUp = true;
        open<BBClueBook>(m_clueBook);
        break;
    case kTeamPhotoId: {
        swallowMouseUp = true;
        open<BBTeamPhoto>(m_teamPhoto);
        auto& rec = app().record();
        const std::uint32_t views = rec.u32(PlayerRecord::kTeamPhotoViews);
        rec.setU32(PlayerRecord::kTeamPhotoViews, views + 1);
        if (views == 0)
            playVoice("db0408");
        break;
    }
    case kMembershipId:
        swallowMouseUp = true;
        open<BBMembershipCards>(m_membership);
        break;
    default:
        break;
    }
    toolbarMouseDown(pick);
}

void BulletinBoard::soundDone(const std::string& name, const void* sound, bool completed) {
    // 0x40e730: HCNode::SoundEnded (0x457010) directly, not the toolbar's.
    if (rabbitLatch && str::iequals(name, "DB9011")) {
        rabbitLatch = false;
        app().gotoNode("rabbitE");
        engine::Node::soundDone(name, sound, completed);
        return;
    }
    if (m_active && m_active->isVisible())
        m_active->soundEnded(name);
    engine::Node::soundDone(name, sound, completed);
}

void BulletinBoard::listen(int message, const engine::MessageData& data) {
    // 0x40e7c0.
    if (message == -103 && swallowMouseUp) {
        // The button-up of the click that opened a panel.
        swallowMouseUp = false;
        return;
    }
    if (message == kMsgEdgeScroll) {
        if (m_active && m_active->isVisible()) {
            if (m_active == m_handbook.get())
                playSound(*this, "bookclose", 0x19);
            m_active->hide();
            if (returnToHandbook && m_handbook) {
                // Leaving the handbook's print dialog goes back to the handbook.
                m_active = m_handbook.get();
                m_active->show();
                returnToHandbook = false;
            }
            playVoice("pause");
            return;
        }
        playSound(*this, "close");
        app().gotoNode("tackroom");
        return;
    }
    if (message == engine::msg::kInactivity) {
        if (!voicePlaying()) {
            // The open panel is compared with the certificates book even when
            // neither exists yet, so before any panel is opened this says db0191.
            if (m_active == m_certificates.get()) {
                playVoice("db0191");
            } else {
                static constexpr std::array<std::string_view, 3> kIdle = {"db0476", "db0469", "db0198"};
                playVoice(kIdle[static_cast<std::size_t>(engine().rand() % 3)]);
            }
        }
        return;
    }
    ToolbarNode::listen(message, data);
}

void BulletinBoard::leaving(const std::string& nextNode) {
    // Exit (0x40eb60).
    releaseTackRoomAnims(engine(), str::iequals(nextNode, "tackroom"));
    ToolbarNode::leaving(nextNode);
}

BRC_REGISTER_NODE("bulletinboard", [](Godiva& app, const std::string& name) {
    return std::make_unique<BulletinBoard>(app, name);
});

} // namespace brc::game
