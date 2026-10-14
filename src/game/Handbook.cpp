// The club handbook of the bulletin board (0x415250–0x41934f): the creed, the
// photo album (souvenirs, camera photos, the final payoff award), the horses
// page, the awards page, printing and the print dialog for photos. See
// game/BulletinBoard.h and docs/engine/classes.md "Handbook".
#include "game/BulletinBoard.h"

#include "engine/Anim.h"
#include "engine/Elements.h"
#include "engine/Engine.h"
#include "engine/Font.h"
#include "game/Camera.h"
#include "game/Godiva.h"
#include "game/Print.h"
#include "game/Widgets.h"

#include <array>
#include <format>
#include <memory>
#include <vector>

namespace brc::game {
namespace {

// Handbook click ids.
constexpr int kCameraTab = 0x3478, kHorseTab = 0x3479, kRibbonTab = 0x347A, kPrint = 0x347B, kCreed = 0x347C,
              kHorses = 0x347D, kPrintYes = 0x347E, kPrintNo = 0x347F;
// Photo album click ids.
constexpr int kTrash = 33000, kLeftFrame = 0x80E9, kRightFrame = 0x80EA, kTrashNo = 0x80EB, kTrashYes = 0x80EC,
              kPayoffAward = 0x80ED;
// Awards page: the certificates anim.
constexpr int kAward = 0x80EB;
// Print dialog click ids.
constexpr int kFrame1 = 30000, kPrinter = 0x7534, kDialogYes = 0x7535, kDialogNo = 0x7536;

void playSound(ToolbarNode& node, std::string_view name) {
    node.engine().sounds().playSound(name, -1, false, false, 1, false, 0x70, -1, 0, false);
}

void setHidden(engine::Element* e, bool hidden) {
    if (e)
        e->hide(hidden);
}

engine::Anim* buttonAnim(const std::unique_ptr<ACButton>& b) { return b ? b->anim() : nullptr; }

std::unique_ptr<ACButton> makeButton(ToolbarNode& node, std::string_view anim, int clickId) {
    auto b = std::make_unique<ACButton>(node);
    b->init(anim);
    if (b->anim())
        b->anim()->setClickId(clickId);
    return b;
}

void removeAnim(ToolbarNode& node, engine::Anim*& anim) {
    if (anim)
        node.engine().anims().remove(anim);
    anim = nullptr;
}

// A yes/no question on a background anim ("print_bg" + "print_yes"/"print_no").
struct Confirm {
    engine::Anim* bg = nullptr;
    std::unique_ptr<ACButton> yes, no;

    explicit operator bool() const { return bg != nullptr; }
    void open(ToolbarNode& node, std::string_view background, std::string_view yesAnim, int yesId,
              std::string_view noAnim, int noId) {
        bg = node.createAnimAtViewport(background);
        yes = makeButton(node, yesAnim, yesId);
        no = makeButton(node, noAnim, noId);
    }
    void close(ToolbarNode& node) {
        removeAnim(node, bg);
        yes.reset();
        no.reset();
    }
};

// ---------------------------------------------------------------------------------------------
// HandbookPrintDialog (name inferred, ctor 0x415250, vtable 0x7f1780): a photo
// or souvenir in a small frame, four frames to choose from, the printer.
class HandbookPrintDialog final : public BBPanel {
public:
    explicit HandbookPrintDialog(BulletinBoard& node) : BBPanel(node, "printing_bg") {
        m_printer = makeButton(node, "printer", kPrinter); // +0x0C
        for (int i = 0; i < 4; ++i)                         // +0x10..+0x1C
            m_frames[static_cast<std::size_t>(i)] = makeButton(node, std::format("frame{}btn", i + 1), kFrame1 + i);
        m_small = node.createAnimAtViewport("smallframe"); // +0x20
        if (m_small)
            m_small->setPosition(m_small->x(), m_small->y(), 411.0f);
    }

    ~HandbookPrintDialog() override {
        // 0x415670.
        m_printer.reset();
        for (auto& f : m_frames)
            f.reset();
        removeAnim(m_node, m_small);
        m_element.reset();
        m_confirm.close(m_node);
    }

    // 0x415490: camera photo `id` (the 320x240 view is what gets printed; the
    // dialog shows the full photo scaled to 220x166).
    void setPhotoByIndex(int id) {
        Godiva& app = m_node.app();
        const auto& display = m_node.engine().gamePalette();
        m_photo = BarbieCamera::loadPhoto(app, id, true, display); // +0x24
        if (!m_photo)
            return;
        if (auto full = BarbieCamera::loadPhoto(app, id, false, display))
            showPicture(BarbieCamera::scale(*full, 220, 166, false));
    }

    // 0x4155a0: a souvenir's 320x240 picture.
    void setPhotoCanvas(std::shared_ptr<engine::Image8> canvas) {
        if (!canvas)
            return;
        m_photo = canvas;
        showPicture(BarbieCamera::scale(*canvas, 220, 166, true));
    }

    void hide() override {
        // 0x4157c0: the pictures are dropped.
        BBPanel::hide();
        setHidden(buttonAnim(m_printer), true);
        for (auto& f : m_frames)
            setHidden(buttonAnim(f), true);
        setHidden(m_small, true);
        m_element.reset();
        m_photo.reset();
        if (m_confirm)
            m_confirm.close(m_node);
    }

    void show() override {
        // 0x4158d0.
        BBPanel::show();
        forceShow(buttonAnim(m_printer));
        for (auto& f : m_frames)
            forceShow(buttonAnim(f));
        forceShow(m_small);
        forceShow(m_element.get());
    }

    void mouseDown(const engine::Pick& pick) override {
        // 0x4159a0.
        const int id = pick.element ? pick.element->clickId() : 0;
        if (id >= kFrame1 && id < kFrame1 + 4) {
            if (m_small)
                m_small->setSequence(id - kFrame1, true);
            return;
        }
        switch (id) {
        case ToolbarNode::kHelpClickId:
            m_node.playVoice("db0414");
            return;
        case kPrinter:
            if (!m_confirm)
                m_confirm.open(m_node, "print_bg", "print_yes", kDialogYes, "print_no", kDialogNo);
            return;
        case kDialogYes: {
            if (!m_confirm)
                return;
            // The picture centred on "bigframe<n>" for the chosen small frame.
            const int frame = (m_small ? m_small->sequence() : 0) + 1;
            engine::Anim* big = m_node.createAnimAtViewport(std::format("bigframe{}", frame)); // +0x2C
            m_confirm.close(m_node);
            if (big && big->image() && m_photo)
                print::canvasCentered(m_node.app(), *m_photo, *big->image());
            removeAnim(m_node, big);
            playSound(m_node, "print");
            return;
        }
        case kDialogNo:
            if (m_confirm) {
                m_confirm.close(m_node);
                playSound(m_node, "bookopen");
            }
            return;
        default:
            m_node.toolbarClick(pick);
        }
    }

private:
    void showPicture(std::shared_ptr<const engine::Image8> img) {
        // +0x28: a canvas element at (100, 93), depth 410.
        m_element = std::make_unique<engine::ImageElement>(std::move(img));
        m_element->link(&m_node);
        m_element->setPosition(100.0f, 93.0f, 410.0f);
    }

    std::unique_ptr<ACButton> m_printer;
    std::array<std::unique_ptr<ACButton>, 4> m_frames;
    engine::Anim* m_small = nullptr;
    std::shared_ptr<engine::Image8> m_photo;          // +0x24
    std::unique_ptr<engine::ImageElement> m_element;  // +0x28 (its 220x166 canvas +0x30)
    Confirm m_confirm;                                // +0x34..+0x3C
};

// ---------------------------------------------------------------------------------------------
// HandbookPhotoAlbum (name inferred, ctor 0x416610, vtable 0x7f17a0): first
// the souvenirs of the finished missions and certificates (one per page, with
// Barbie's comment), then the camera photos two by two, and before them all
// the final payoff award once it has been seen.
class HandbookPhotoAlbum {
public:
    enum Mode { kAwardPage = -1, kSouvenirs = 0, kPhotos = 1 };

    // Souvenir anims by kind (0x800460) and their captions (0x800498).
    static constexpr std::array<std::string_view, 13> kStock = {
        "stock_cottage", "stock_coin", "stock_shoe", "stock_locket", "stock_bell", "stock_well", "stock_flower1",
        "stock_flower2", "stock_flower3", "stock_hawk", "stock_owl", "stock_bbird", "stock_rainbowpic"};
    static constexpr std::array<std::string_view, 13> kSaying = {
        "saying_cottage", "saying_coin", "saying_shoe", "saying_locket", "saying_bell", "saying_well"};
    // Comment on a click on a souvenir (0x8004d0).
    static constexpr std::array<std::string_view, 13> kVoice = {
        "db0435", "db04112", "db04113", "db04114", "db0437", "db0436", "db0231",
        "db0231", "db0231", "db0231", "db0231", "db0231", "db0231"};

    explicit HandbookPhotoAlbum(BulletinBoard& node) : m_node(node) {
        // 0x416610.
        m_trash = makeButton(node, "trashbtn", kTrash); // +0x114
        if (m_trash->anim())
            m_trash->anim()->move(-200.0f, 0.0f, 0.0f);
        buildSouvenirList();
        m_cur = 0;
        auto& rec = node.app().record();
        if (rec.u16(PlayerRecord::kPayoffSeen) == 1) {
            m_payoff = node.createAnimAtViewport("payoff_award"); // +0xB0
            if (m_payoff)
                m_payoff->setClickId(kPayoffAward);
        }
        // The ten photos as 220x165 thumbnails, hidden; even ones on the left page.
        for (int i = 0; i < BarbieCamera::kMaxPhotos; ++i) {
            auto thumb = BarbieCamera::loadThumbnail(node.app(), i + 1, node.engine().gamePalette());
            if (!thumb) {
                m_missing = true;
                continue;
            }
            auto& e = m_photos[static_cast<std::size_t>(i)];
            e = std::make_unique<engine::ImageElement>(std::move(thumb));
            e->link(&node);
            e->hide(true);
            e->setPosition(i % 2 == 0 ? 88.0f : 328.0f, 69.0f, 410.0f);
        }
        m_selected = 0;
        m_spread = 1;
        m_left = 0;
        m_right = 1;
        m_leftFrame = node.createAnimAtViewport("leftframe");   // +0xB4
        m_rightFrame = node.createAnimAtViewport("rightframe"); // +0xB8
        m_transpLeft = node.createAnimAtViewport("transp_leftframe"); // +0x2C
        if (m_transpLeft) {
            m_transpLeft->setClickId(kLeftFrame);
            m_transpLeft->setPerPixelHit(false);
            m_transpLeft->move(0.0f, 0.0f, 415.0f);
        }
        m_transpLeftHand = std::make_unique<ACRollover>(node, m_transpLeft, 7);
        m_transpRight = node.createAnimAtViewport("transp_rightframe"); // +0x30
        if (m_transpRight) {
            m_transpRight->setClickId(kRightFrame);
            m_transpRight->setPerPixelHit(false);
            m_transpRight->move(0.0f, 0.0f, 415.0f);
        }
        m_transpRightHand = std::make_unique<ACRollover>(node, m_transpRight, 7);
    }

    ~HandbookPhotoAlbum() {
        // 0x416a60.
        for (int kind : m_list) {
            removeAnim(m_node, m_stockAnim[static_cast<std::size_t>(kind)]);
            removeAnim(m_node, m_sayingAnim[static_cast<std::size_t>(kind)]);
        }
        m_trash.reset();
        for (auto& e : m_photos)
            e.reset();
        removeAnim(m_node, m_payoff);
        removeAnim(m_node, m_leftFrame);
        removeAnim(m_node, m_rightFrame);
        m_transpLeftHand.reset();
        m_transpRightHand.reset();
        removeAnim(m_node, m_transpLeft);
        removeAnim(m_node, m_transpRight);
        closeTrash();
    }

    int mode() const { return m_mode; }
    // +0x14: the selected photo (its index + 1), 0 when none.
    int selected() const { return m_selected; }
    // 0x4162f0 GetAwardAnim.
    engine::Anim* awardAnim() const { return m_mode == kAwardPage ? m_payoff : nullptr; }
    // The souvenir on the page.
    engine::Anim* souvenir() const { return m_cur < m_list.size() ? stock(m_cur) : nullptr; }

    // 0x417200 SetHidden.
    void setHidden(bool hide) {
        if (!hide) {
            if (m_payoff) {
                m_mode = kAwardPage;
                m_spread = -1;
                forceShow(m_payoff);
                return;
            }
            m_selected = 0;
            if (m_list.empty()) {
                m_spread = 1;
                m_mode = kPhotos;
                m_left = 0;
                m_right = 1;
                forceShow(buttonAnim(m_trash));
                forceShow(photo(m_left));
                forceShow(photo(m_right));
                forceShow(m_transpLeft);
                forceShow(m_transpRight);
                sayAlbumFull();
            } else {
                m_mode = kSouvenirs;
                m_spread = 0;
                m_left = 0;
                m_right = 1;
                showSouvenir(m_cur);
                forceShow(m_transpRight);
            }
            return;
        }
        setHiddenIf(buttonAnim(m_trash));
        setHiddenIf(m_leftFrame);
        setHiddenIf(m_rightFrame);
        setHiddenIf(photo(m_left));
        setHiddenIf(photo(m_right));
        setHiddenIf(m_transpLeft);
        setHiddenIf(m_transpRight);
        m_cur = 0;
        for (std::size_t i = 0; i < m_list.size(); ++i) {
            setHiddenIf(stock(i));
            setHiddenIf(saying(i));
        }
        setHiddenIf(m_payoff);
        closeTrash();
    }

    // 0x416d00 NextPage: award page -> souvenirs -> photo spreads 1..5.
    bool nextPage() {
        for (;;) {
            m_selected = 0;
            if (m_mode == kAwardPage) {
                setHidden(m_payoff, true);
                if (!m_list.empty()) {
                    m_mode = kSouvenirs;
                    m_cur = 0;
                    showSouvenir(m_cur);
                    return true;
                }
                startPhotos();
                continue;
            }
            if (m_mode == kSouvenirs) {
                hideSouvenir(m_cur);
                setHidden(m_rightFrame, true);
                ++m_cur;
                if (m_cur < m_list.size()) {
                    showSouvenir(m_cur);
                    return true;
                }
                startPhotos();
                continue;
            }
            if (m_mode != kPhotos || m_spread == 5)
                return false;
            setHidden(m_leftFrame, true);
            setHidden(m_rightFrame, true);
            if (m_spread == 0) {
                sayAlbumFull();
            } else {
                setHidden(photo(m_left), true);
                setHidden(photo(m_right), true);
                m_right += 2;
                m_left += 2;
            }
            ++m_spread;
            forceShow(photo(m_left));
            forceShow(photo(m_right));
            return true;
        }
    }

    // 0x416fa0 PrevPage.
    bool prevPage() {
        m_selected = 0;
        if (m_mode == kAwardPage)
            return false;
        if (m_mode == kPhotos) {
            if (m_spread != 1) {
                setHidden(m_leftFrame, true);
                setHidden(m_rightFrame, true);
                setHidden(photo(m_left), true);
                setHidden(photo(m_right), true);
                --m_spread;
                m_left -= 2;
                m_right -= 2;
                forceShow(photo(m_left));
                forceShow(photo(m_right));
                return true;
            }
            if (m_list.empty())
                return false;
            setHidden(m_leftFrame, true);
            setHidden(m_rightFrame, true);
            setHidden(photo(m_left), true);
            setHidden(photo(m_right), true);
            setHidden(buttonAnim(m_trash), true);
            setHidden(m_transpLeft, true);
            m_mode = kSouvenirs;
        }
        if (m_mode != kSouvenirs)
            return false;
        if (m_cur != 0) {
            setHidden(m_rightFrame, true);
            if (m_cur < m_list.size())
                hideSouvenir(m_cur);
            --m_cur;
            showSouvenir(m_cur);
            return true;
        }
        if (m_node.app().record().u16(PlayerRecord::kPayoffSeen) == 1) {
            m_mode = kAwardPage;
            forceShow(m_payoff);
            hideSouvenir(m_cur);
            setHidden(m_rightFrame, true);
            setHidden(m_transpRight, true);
            return true;
        }
        return false;
    }

    // vt+0 MouseDown (0x415d30).
    void mouseDown(const engine::Pick& pick) {
        const int id = pick.element ? pick.element->clickId() : 0;
        switch (id) {
        case kTrash:
            if (m_mode != kPhotos)
                return;
            if (hidden(m_leftFrame) && hidden(m_rightFrame)) {
                playSound(m_node, "null"); // no photo selected (the sound does not exist)
                return;
            }
            m_trashBg = m_node.createAnimAtViewport("trash_bg");          // +0x118
            m_trashBorder = m_node.createAnimAtViewport("exitBorderIn");  // +0x11C
            if (m_trashBorder)
                m_trashBorder->setPerPixelHit(false);
            m_trashYes = makeButton(m_node, "trash_yesbtn", kTrashYes); // +0x120
            m_trashNo = makeButton(m_node, "trash_nobtn", kTrashNo);    // +0x124
            return;
        case kLeftFrame:
            setHidden(m_rightFrame, true);
            if (photo(m_left)) {
                forceShow(m_leftFrame);
                m_selected = m_left + 1;
            }
            return;
        case kRightFrame:
            if (m_mode == kSouvenirs) {
                forceShow(m_rightFrame);
                if (m_cur < m_list.size())
                    m_node.playVoice(kVoice[static_cast<std::size_t>(m_list[m_cur])]);
            } else if (m_mode == kPhotos) {
                setHidden(m_leftFrame, true);
                if (photo(m_right)) {
                    forceShow(m_rightFrame);
                    m_selected = m_right + 1;
                }
            }
            return;
        case kTrashNo:
            closeTrash();
            return;
        case kTrashYes:
            if (!m_trashBg)
                return;
            closeTrash();
            if (!hidden(m_leftFrame)) {
                deletePhoto(m_left);
                setHidden(m_leftFrame, true);
            } else if (!hidden(m_rightFrame)) {
                deletePhoto(m_right);
                setHidden(m_rightFrame, true);
            }
            m_selected = 0;
            return;
        case kPayoffAward:
            playSound(m_node, "select");
            m_node.app().gotoNode("finalpayoff");
            return;
        default:
            m_node.toolbarClick(pick);
        }
    }

private:
    // 0x416300 BuildSouvenirList: one souvenir per finished mission or
    // certificate, in this order; their anims are created now.
    void buildSouvenirList() {
        const auto& rec = m_node.app().record();
        auto mission = [&](int i) { return rec.u32(PlayerRecord::kMissionDone + 4 * static_cast<std::size_t>(i)) != 0; };
        auto cert = [&](int i) { return rec.u32(PlayerRecord::kCertificate + 4 * static_cast<std::size_t>(i)) != 0; };
        if (mission(0))
            m_list.push_back(3); // locket
        if (mission(7))
            m_list.push_back(2); // horseshoe
        if (mission(1))
            m_list.push_back(1); // coin
        if (mission(3))
            m_list.push_back(0); // cottage
        if (mission(5))
            m_list.push_back(5); // wishing well
        if (cert(0))
            m_list.insert(m_list.end(), {6, 7, 8}); // the three flowers
        if (cert(1))
            m_list.insert(m_list.end(), {9, 10, 11}); // the three birds
        if (cert(2))
            m_list.push_back(12); // rainbow
        for (int kind : m_list) {
            const auto k = static_cast<std::size_t>(kind);
            m_stockAnim[k] = m_node.createAnimAtViewport(kStock[k]);
            if (!kSaying[k].empty())
                m_sayingAnim[k] = m_node.createAnimAtViewport(kSaying[k]);
        }
    }

    engine::Anim* stock(std::size_t i) const { return m_stockAnim[static_cast<std::size_t>(m_list[i])]; }
    engine::Anim* saying(std::size_t i) const { return m_sayingAnim[static_cast<std::size_t>(m_list[i])]; }
    engine::Element* photo(int i) const {
        return i >= 0 && i < BarbieCamera::kMaxPhotos ? m_photos[static_cast<std::size_t>(i)].get() : nullptr;
    }
    static bool hidden(const engine::Element* e) { return !e || e->hideCount() != 0; }
    static void setHiddenIf(engine::Element* e) { setHidden(e, true); }
    static void setHidden(engine::Element* e, bool on) { game::setHidden(e, on); }

    void showSouvenir(std::size_t i) {
        if (i >= m_list.size())
            return;
        forceShow(stock(i));
        forceShow(saying(i));
    }
    void hideSouvenir(std::size_t i) {
        if (i >= m_list.size())
            return;
        setHidden(stock(i), true);
        setHidden(saying(i), true);
    }

    // Leaving the souvenirs: the first photo spread comes next.
    void startPhotos() {
        m_mode = kPhotos;
        m_spread = 0;
        m_selected = 0;
        m_left = 0;
        m_right = 1;
        forceShow(buttonAnim(m_trash));
        forceShow(m_transpLeft);
        forceShow(m_transpRight);
    }

    // db02033 once, when all ten photos are taken.
    void sayAlbumFull() {
        if (!m_missing && !m_saidFull) {
            m_saidFull = true;
            m_node.playVoice("db02033");
        }
    }

    void deletePhoto(int i) {
        auto& e = m_photos[static_cast<std::size_t>(i)];
        if (!e)
            return;
        e->hide(true);
        e.reset();
        BarbieCamera::deletePhoto(m_node.app(), i + 1);
        playSound(m_node, "trash");
    }

    void closeTrash() {
        removeAnim(m_node, m_trashBg);
        removeAnim(m_node, m_trashBorder);
        m_trashYes.reset();
        m_trashNo.reset();
    }

    BulletinBoard& m_node;                                // +0x08
    bool m_saidFull = false;                              // +0x04
    int m_selected = 0;                                   // +0x14
    bool m_missing = false;                               // +0x18: a photo id is free
    int m_mode = kSouvenirs;                              // +0x1C
    int m_spread = 1;                                     // +0x20
    int m_left = 0, m_right = 1;                          // +0x24 / +0x28
    engine::Anim* m_transpLeft = nullptr;                 // +0x2C
    engine::Anim* m_transpRight = nullptr;                // +0x30
    std::unique_ptr<ACRollover> m_transpLeftHand, m_transpRightHand; // +0x34 / +0x38
    std::vector<int> m_list;                              // +0x3C
    std::size_t m_cur = 0;                                // +0x44 (m_list.size() = end)
    std::array<engine::Anim*, 13> m_stockAnim{};          // +0x48
    std::array<engine::Anim*, 13> m_sayingAnim{};         // +0x7C
    engine::Anim* m_payoff = nullptr;                     // +0xB0
    engine::Anim* m_leftFrame = nullptr;                  // +0xB4
    engine::Anim* m_rightFrame = nullptr;                 // +0xB8
    std::array<std::unique_ptr<engine::ImageElement>, 10> m_photos; // +0xE4 (thumbnails +0xBC)
    std::unique_ptr<ACButton> m_trash;                    // +0x114
    engine::Anim* m_trashBg = nullptr;                    // +0x118
    engine::Anim* m_trashBorder = nullptr;                // +0x11C
    std::unique_ptr<ACButton> m_trashYes, m_trashNo;      // +0x120 / +0x124
};

// ---------------------------------------------------------------------------------------------
// HandbookAwardsPage (name inferred, ctor 0x417490, vtable 0x7f17b0): the
// certificates and badges earned, then a ribbon per race (paired wins and
// losses, then the extra wins or losses).
class HandbookAwardsPage {
public:
    explicit HandbookAwardsPage(BulletinBoard& node) : m_node(node) {
        m_certs = node.createAnimAtViewport("certificates"); // +0x28
        if (m_certs) {
            m_certs->hide(true);
            m_certs->setClickId(kAward);
        }
        const auto& rec = node.app().record();
        for (int i = 0; i < 7; ++i)
            if (rec.u32(PlayerRecord::kCertificate + 4 * static_cast<std::size_t>(i)) != 0)
                m_list.push_back(i);
        const auto wins = static_cast<std::int32_t>(rec.u32(PlayerRecord::kRaceWins));
        const auto losses = static_cast<std::int32_t>(rec.u32(PlayerRecord::kRaceLosses));
        for (std::int32_t k = 1; k <= std::min(wins, losses); ++k)
            m_list.push_back(9);
        if (losses < wins)
            m_list.insert(m_list.end(), static_cast<std::size_t>(wins - losses), 8);
        else if (wins < losses)
            m_list.insert(m_list.end(), static_cast<std::size_t>(losses - wins), 7);
        // +0x34/+0x38: the player's name, centred on x 320 at y 270, in front.
        m_name = makeNameField(node);
        placeNameField(*m_name, static_cast<float>(320 - nameFieldWidth(*m_name) / 2), 270.0f,
                       (m_certs ? m_certs->z() : 0.0f) + 3.0f);
        firstPage();
    }

    ~HandbookAwardsPage() {
        // 0x417920.
        m_name.reset();
        removeAnim(m_node, m_certs);
        removeAnim(m_node, m_prints);
    }

    // 0x417a50.
    void firstPage() {
        m_saidRibbon = m_saidBadge = false;
        m_cur = 0;
        if (!m_list.empty() && m_certs)
            m_certs->setSequence(m_list[0], true);
    }

    // 0x417a80 GetPrintAnim: the printable picture of the current award.
    engine::Anim* printAnim() {
        if (m_list.empty())
            return nullptr;
        if (!m_prints) {
            m_prints = m_node.createAnimAtViewport("award_prints"); // +0x2C
            setHidden(m_prints, true);
        }
        if (!m_prints || !m_certs)
            return nullptr;
        m_prints->setSequenceFrame(m_certs->sequence(), 0);
        m_prints->hide(true);
        return m_prints;
    }
    // 0x417ae0: the four certificates come first in the anim.
    bool isCertificate() const { return m_cur < m_list.size() && m_list[m_cur] < 4; }
    const engine::TextElement& nameText() const { return *m_name; } // 0x417af0

    // vt+4 SetHidden (0x417b00).
    void setHidden(bool hide) {
        if (!hide) {
            if (!m_list.empty()) {
                if (m_certs)
                    m_certs->setSequence(m_list[m_cur], true);
                if (m_list[m_cur] < 4) {
                    forceShow(m_name.get());
                    m_node.playVoice("db03061");
                }
            }
            return;
        }
        firstPage();
        setHidden(m_certs, true);
        setHidden(m_name.get(), true);
        setHidden(m_prints, true);
    }

    // 0x417b90.
    bool nextPage() {
        if (m_cur + 1 >= m_list.size())
            return false; // the original steps to end() and back to the last award
        ++m_cur;
        showCurrent();
        const int award = m_list[m_cur];
        if (award < 7) {
            if (award > 3 && !m_saidBadge) {
                m_node.playVoice("db03060");
                m_saidBadge = true;
            }
        } else if (!m_saidRibbon) {
            m_saidRibbon = true;
            m_node.playVoice("db03059");
        }
        return true;
    }

    // 0x417c50.
    bool prevPage() {
        if (m_cur == 0)
            return false;
        --m_cur;
        showCurrent();
        return true;
    }

    // vt+8 MouseDown (0x417cc0).
    void mouseDown(const engine::Pick& pick) {
        const int id = pick.element ? pick.element->clickId() : 0;
        if (id != kAward) {
            m_node.toolbarClick(pick);
            return;
        }
        const auto* anim = dynamic_cast<const engine::Anim*>(pick.element);
        const int award = anim ? anim->sequence() : 0;
        static constexpr std::array<std::string_view, 7> kVoice = {"", "", "", "", "db03063", "db03064", "db03062"};
        if (award > 6)
            m_node.playVoice(m_node.engine().rand() % 2 != 0 ? "db03069" : "db03070");
        else if (award >= 0 && !kVoice[static_cast<std::size_t>(award)].empty())
            m_node.playVoice(kVoice[static_cast<std::size_t>(award)]);
    }

private:
    static void setHidden(engine::Element* e, bool on) { game::setHidden(e, on); }

    void showCurrent() {
        if (m_certs)
            m_certs->setSequence(m_list[m_cur], true);
        if (m_list[m_cur] < 4)
            forceShow(m_name.get());
        else
            setHidden(m_name.get(), true);
    }

    BulletinBoard& m_node;                     // +0x24
    bool m_saidRibbon = false;                 // +0x04
    bool m_saidBadge = false;                  // +0x08
    std::vector<int> m_list;                   // +0x0C
    std::size_t m_cur = 0;                     // +0x14
    engine::Anim* m_certs = nullptr;           // +0x28
    engine::Anim* m_prints = nullptr;          // +0x2C
    std::unique_ptr<engine::TextElement> m_name; // +0x34 / +0x38
};

// ---------------------------------------------------------------------------------------------
// Handbook (name inferred, ctor 0x417d40, vtable 0x7f17c0): opens on the club
// creed; three tabs (camera, horse, ribbon) switch between the photo album,
// the horses page and the awards page.
class Handbook final : public BBPanel {
public:
    enum Page { kAlbumPage = 0, kHorsesPage = 1, kAwardsPage = 2, kCreedPage = 3 };

    explicit Handbook(BulletinBoard& node) : BBPanel(node, "handbook") {
        m_leftArrow = makeButton(node, "leftarrow", bb::kPrev);   // +0x14
        m_rightArrow = makeButton(node, "rightarrow", bb::kNext); // +0x18
        m_print = makeButton(node, "printbtn", kPrint);           // +0x40
        if (m_print->anim())
            m_print->anim()->setPerPixelHit(false);
        m_cameraL = makeButton(node, "leftcamerabtn", kCameraTab);   // +0x1C
        m_cameraR = makeButton(node, "rightcamerabtn", kCameraTab);  // +0x20
        m_horseL = makeButton(node, "lefthorsebtn", kHorseTab);      // +0x24
        m_horseR = makeButton(node, "righthorsebtn", kHorseTab);     // +0x28
        m_ribbonL = makeButton(node, "leftribbonbtn", kRibbonTab);   // +0x2C
        m_ribbonR = makeButton(node, "rightribbonbtn", kRibbonTab);  // +0x30
        m_cameraTab = m_cameraR.get();                               // +0x34
        m_horseTab = m_horseR.get();                                 // +0x38
        m_ribbonTab = m_ribbonR.get();                               // +0x3C
        setHidden(buttonAnim(m_cameraL), true);
        setHidden(buttonAnim(m_horseL), true);
        setHidden(buttonAnim(m_ribbonL), true);
        m_creed = node.createAnimAtViewport("creed"); // +0x0C
        if (m_creed)
            m_creed->setClickId(kCreed);
        m_page = kCreedPage; // +0x68
    }

    ~Handbook() override {
        // 0x4180f0.
        m_album.reset();
        m_awards.reset();
        removeAnim(m_node, m_horses);
        removeAnim(m_node, m_creed);
        m_print.reset();
        m_leftArrow.reset();
        m_rightArrow.reset();
        m_cameraL.reset();
        m_cameraR.reset();
        m_horseL.reset();
        m_horseR.reset();
        m_ribbonL.reset();
        m_ribbonR.reset();
        m_confirm.close(m_node);
    }

    void soundEnded(const std::string&) override {
        // 0x4192f0: every sound end restarts the inactivity timer.
        m_node.app().idleTimer().start();
    }

    void hide() override {
        // 0x419110.
        BBPanel::hide();
        setHidden(buttonAnim(m_cameraTab), true);
        setHidden(buttonAnim(m_horseTab), true);
        setHidden(buttonAnim(m_ribbonTab), true);
        setHidden(buttonAnim(m_print), true);
        setHidden(buttonAnim(m_leftArrow), true);
        setHidden(buttonAnim(m_rightArrow), true);
        showAwardsPage(false);
        showHorsesPage(false);
        showPhotoAlbum(false);
        setHidden(m_creed, true);
        if (m_confirm)
            m_confirm.close(m_node);
        m_node.stopVoice();
    }

    void show() override {
        // 0x4191f0: back to the creed.
        BBPanel::show();
        setHidden(buttonAnim(m_cameraTab), false);
        setHidden(buttonAnim(m_horseTab), false);
        setHidden(buttonAnim(m_ribbonTab), false);
        forceShow(buttonAnim(m_print));
        setCameraTab(1);
        setRibbonTab(1);
        setHorseTab(1);
        m_page = kCreedPage;
        forceShow(m_creed);
        auto& rec = m_node.app().record();
        const std::uint32_t opens = rec.u32(PlayerRecord::kHandbookOpens);
        rec.setU32(PlayerRecord::kHandbookOpens, opens + 1);
        if (opens == 0)
            m_node.app().idleTimer().stop();
        m_node.playVoice("db0425");
    }

    void mouseDown(const engine::Pick& pick) override {
        // 0x418460.
        const int id = pick.element ? pick.element->clickId() : 0;
        switch (id) {
        case ToolbarNode::kHelpClickId:
            if (!m_confirm) {
                m_node.app().idleTimer().stop();
                m_node.playVoice("db02032");
                m_node.playVoiceAfterPause("db0414");
                if (m_page == kAlbumPage)
                    m_node.playVoiceAfterPause("db0413");
                else if (m_page == kHorsesPage)
                    m_node.playVoiceAfterPause("db02031");
                else if (m_page == kAwardsPage)
                    m_node.playVoiceAfterPause("db0417");
            }
            return;
        case bb::kNext:
        case bb::kPrev:
            if (!m_confirm)
                turnPage(id == bb::kNext);
            return;
        case kCameraTab:
        case kHorseTab:
        case kRibbonTab:
            if (!m_confirm)
                selectTab(id);
            return;
        case kPrint:
            printButton();
            return;
        case kCreed:
            m_node.swallowMouseUp = true;
            m_node.playVoice("db0425");
            return;
        case kHorses: {
            static constexpr std::array<std::string_view, 4> kHorseVoice = {"db0429", "db0430", "db0428", "db0427"};
            m_node.swallowMouseUp = true;
            const int seq = m_horses ? m_horses->sequence() : -1;
            if (seq >= 0 && seq < 4)
                m_node.playVoice(kHorseVoice[static_cast<std::size_t>(seq)]);
            return;
        }
        case kPrintYes:
            printConfirmed();
            return;
        case kPrintNo:
            m_confirm.close(m_node);
            return;
        default:
            if (m_confirm)
                return;
            m_node.swallowMouseUp = true;
            if (m_page == kAlbumPage && m_album)
                m_album->mouseDown(pick);
            else if (m_page == kAwardsPage && m_awards)
                m_awards->mouseDown(pick);
            else
                m_node.toolbarClick(pick);
        }
    }

private:
    void turnPage(bool next) {
        m_node.swallowMouseUp = true;
        bool turned = false;
        if (m_page == kAlbumPage && m_album) {
            turned = next ? m_album->nextPage() : m_album->prevPage();
        } else if (m_page == kHorsesPage && m_horses) {
            const int seq = m_horses->sequence() + (next ? 1 : -1);
            if (seq < 0 || seq > 3)
                return;
            m_horses->setSequence(seq, true);
            turned = true;
        } else if (m_page == kAwardsPage && m_awards) {
            turned = next ? m_awards->nextPage() : m_awards->prevPage();
        }
        if (turned)
            playSound(m_node, "page");
    }

    void selectTab(int id) {
        m_node.swallowMouseUp = true;
        const Page page = id == kCameraTab ? kAlbumPage : id == kHorseTab ? kHorsesPage : kAwardsPage;
        if (m_page == page)
            return;
        forceShow(buttonAnim(m_leftArrow));
        forceShow(buttonAnim(m_rightArrow));
        hideCreed();
        m_page = page;
        // The tabs of the pages before the open one move to the left side.
        setCameraTab(0);
        setRibbonTab(page == kAlbumPage ? 1 : 0);
        setHorseTab(page == kAwardsPage ? 1 : page == kAlbumPage ? 1 : 0);
        showAwardsPage(page == kAwardsPage);
        showHorsesPage(page == kHorsesPage);
        showPhotoAlbum(page == kAlbumPage);
        playSound(m_node, "page");
    }

    // 0x419050 / 0x419090 / 0x4190d0: 0 = the left button, 1 = the right one.
    static void setTab(ACButton*& current, ACButton* left, ACButton* right, int side) {
        setHidden(buttonAnim(current), true);
        current = side == 0 ? left : right;
        setHidden(buttonAnim(current), false);
    }
    void setCameraTab(int side) { setTab(m_cameraTab, m_cameraL.get(), m_cameraR.get(), side); }
    void setHorseTab(int side) { setTab(m_horseTab, m_horseL.get(), m_horseR.get(), side); }
    void setRibbonTab(int side) { setTab(m_ribbonTab, m_ribbonL.get(), m_ribbonR.get(), side); }
    static engine::Anim* buttonAnim(const std::unique_ptr<ACButton>& b) { return game::buttonAnim(b); }
    static engine::Anim* buttonAnim(ACButton* b) { return b ? b->anim() : nullptr; }
    static void setHidden(engine::Element* e, bool on) { game::setHidden(e, on); }

    void hideCreed() {
        // 0x418270.
        if (m_creed && m_creed->hideCount() == 0)
            m_creed->hide(true);
    }

    void showAwardsPage(bool show) {
        // 0x418290.
        if (show) {
            if (!m_awards)
                m_awards = std::make_unique<HandbookAwardsPage>(m_node);
            m_awards->setHidden(false);
        } else if (m_awards) {
            m_awards->setHidden(true);
        }
    }

    void showHorsesPage(bool show) {
        // 0x418330.
        if (!show) {
            setHidden(m_horses, true);
            return;
        }
        if (!m_horses) {
            m_horses = m_node.createAnimAtViewport("horses"); // +0x50
            if (m_horses)
                m_horses->setClickId(kHorses);
        }
        if (!m_horses)
            return;
        m_horses->hide(false);
        m_horses->setSequence(0, true);
        auto& rec = m_node.app().record();
        const std::uint32_t views = rec.u32(PlayerRecord::kHorsesPageViews);
        rec.setU32(PlayerRecord::kHorsesPageViews, views + 1);
        if (views == 0)
            m_node.playVoice("db02031");
    }

    void showPhotoAlbum(bool show) {
        // 0x4183c0.
        if (show) {
            if (!m_album) {
                m_album = std::make_unique<HandbookPhotoAlbum>(m_node);
                m_album->setHidden(true);
            }
            m_album->setHidden(false);
        } else if (m_album) {
            m_album->setHidden(true);
        }
    }

    HandbookPrintDialog& printDialog() {
        auto& slot = m_node.printDialog(); // node+0x1C0
        if (!slot) {
            slot = std::make_unique<HandbookPrintDialog>(m_node);
            slot->hide();
        }
        return static_cast<HandbookPrintDialog&>(*slot);
    }

    void openPrintDialog() {
        // Closing the dialog reopens the handbook (node+0x1D4).
        m_node.returnToHandbook = true;
        m_node.openPanel(m_node.printDialog().get());
        hideCreed();
    }

    void openConfirm() { m_confirm.open(m_node, "print_bg", "print_yes", kPrintYes, "print_no", kPrintNo); }

    // The print button (id 0x347B).
    void printButton() {
        m_printAward = false; // +0x48
        switch (m_page) {
        case kAlbumPage: {
            if (!m_album)
                return;
            m_node.swallowMouseUp = true;
            const int selected = m_album->mode() == HandbookPhotoAlbum::kAwardPage ? -1 : m_album->selected();
            if (selected > 0) {
                // A camera photo: the print dialog.
                playSound(m_node, "print");
                printDialog().setPhotoByIndex(selected);
                openPrintDialog();
            } else if (selected == 0) {
                // A souvenir: its picture (sequence 1) in the print dialog.
                std::shared_ptr<engine::Image8> picture;
                if (m_album->mode() == HandbookPhotoAlbum::kSouvenirs) {
                    if (engine::Anim* s = m_album->souvenir()) {
                        s->setSequenceFrame(1, 0);
                        if (const engine::Image8* img = s->image())
                            picture = std::make_shared<engine::Image8>(*img);
                        s->setSequence(0, true);
                    }
                }
                if (!picture) {
                    playSound(m_node, "null");
                    return;
                }
                playSound(m_node, "print");
                printDialog().setPhotoCanvas(std::move(picture));
                openPrintDialog();
            } else if (!m_confirm) {
                // The final payoff award.
                playSound(m_node, "print");
                m_printAward = true;
                openConfirm();
            }
            return;
        }
        case kHorsesPage:
            if (m_confirm)
                return;
            m_node.swallowMouseUp = true;
            openConfirm();
            return;
        case kAwardsPage:
            if (m_confirm)
                return;
            m_node.swallowMouseUp = true;
            if (!m_awards || !m_awards->printAnim()) {
                playSound(m_node, "null");
                return;
            }
            openConfirm();
            return;
        case kCreedPage:
            if (!m_confirm)
                openConfirm();
            return;
        }
    }

    // "Yes" in the print question (id 0x347E).
    void printConfirmed() {
        if (!m_confirm)
            return;
        m_confirm.close(m_node);
        Godiva& app = m_node.app();
        switch (m_page) {
        case kAlbumPage:
            if (m_printAward && m_album) {
                if (engine::Anim* award = m_album->awardAnim())
                    print::element(app, *award);
                m_printAward = false;
            }
            break;
        case kHorsesPage:
            if (m_horses)
                print::element(app, *m_horses);
            break;
        case kAwardsPage:
            if (engine::Anim* a = m_awards ? m_awards->printAnim() : nullptr) {
                if (m_awards->isCertificate()) {
                    const engine::TextElement& name = m_awards->nameText();
                    print::elementWithText(app, *a, name, nameFieldWidth(name), kNameHeight, kNameBorder);
                }
                else
                    print::element(app, *a);
            } else {
                playSound(m_node, "null");
            }
            break;
        case kCreedPage:
            if (m_creed)
                print::element(app, *m_creed);
            break;
        }
    }

    engine::Anim* m_creed = nullptr;                        // +0x0C
    std::unique_ptr<ACButton> m_leftArrow, m_rightArrow;   // +0x14 / +0x18
    std::unique_ptr<ACButton> m_cameraL, m_cameraR, m_horseL, m_horseR, m_ribbonL, m_ribbonR; // +0x1C..+0x30
    ACButton* m_cameraTab = nullptr;                       // +0x34
    ACButton* m_horseTab = nullptr;                        // +0x38
    ACButton* m_ribbonTab = nullptr;                       // +0x3C
    std::unique_ptr<ACButton> m_print;                     // +0x40
    bool m_printAward = false;                             // +0x48
    std::unique_ptr<HandbookAwardsPage> m_awards;          // +0x4C
    engine::Anim* m_horses = nullptr;                      // +0x50
    std::unique_ptr<HandbookPhotoAlbum> m_album;           // +0x54
    Confirm m_confirm;                                     // +0x58..+0x60
    int m_page = kCreedPage;                               // +0x68
};

} // namespace

std::unique_ptr<BBPanel> makeHandbook(BulletinBoard& node) { return std::make_unique<Handbook>(node); }

} // namespace brc::game
