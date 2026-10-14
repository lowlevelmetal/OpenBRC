#include "game/Popups.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "game/Camera.h"
#include "game/Godiva.h"
#include "game/ToolbarNode.h"
#include "game/Widgets.h"

#include <array>
#include <format>
#include <string_view>

namespace brc::game {

namespace {

// The popups' one-shot sounds: PlaySound(name, -1, 0, 0, 1, 0, 0x70, -1, 0, 0).
void playSfx(ToolbarNode& node, std::string_view name) {
    node.engine().sounds().playSound(name, -1, false, false, 1, false, 0x70, -1, 0, false);
}

// The "hide count = 1, then unhide" idiom the popups use to show an anim
// whatever its hide count was.
void forceShow(engine::Anim* a) {
    if (a && a->hideCount() != 0) {
        a->setHideCount(1);
        a->hide(false);
    }
}

// SetSequence shows a hidden anim; the blink code hides it again afterwards.
void setSequenceKeepHidden(engine::Anim* a, int seq) {
    if (!a)
        return;
    const bool hidden = !a->visible();
    a->setSequence(seq, true);
    if (hidden)
        a->hide(true);
}

} // namespace

// --- BarbiePager -----------------------------------------------------------------------------

namespace {

// Per race type (0 Christie / bird badge, 1 Teresa / horseshoe, 2 Teresa /
// branches), table 0x7fe5d0: [0] intro, [1] question, [2] "no" answer,
// [3] unused (see mouseDown), [4] "yes" answer.
constexpr std::array<std::array<std::string_view, 5>, 3> kRaceVoices = {{
    {"dc0264", "db0265", "db0266", "db0267", "db0268"},
    {"dt0248c", "db0248d", "db0249", "db0250", "db0251"},
    {"dt0281", "db0282", "db0283", "db0284", "db0285"},
}};

// [0x809968]: a race offer of that type was declined (never saved).
std::array<bool, 3> g_raceDeclined{};

void releaseButton(std::unique_ptr<RolloverButton>& b) {
    // The buttons sit on the shared HUD anims, which outlive the pager.
    if (b)
        b->releaseAnim();
    b.reset();
}

} // namespace

BarbiePager::BarbiePager(ToolbarNode& node) : engine::Task(node.engine().tasks()), m_node(node) {
    // 0x405a70.
    build();
    hide();
    setSuspended(true); // +0x08: blinking only while a page is pending
}

BarbiePager::~BarbiePager() {
    // 0x406110.
    releaseButton(m_yes);
    releaseButton(m_no);
    releaseButton(m_replay);
    releaseButton(m_race);
}

void BarbiePager::build() {
    // 0x405b60: only the In (0) and Out (1) palettes have a pager.
    int skin = m_node.skin();
    std::string sfx(m_node.skinSuffix());
    if (skin != ToolbarNode::kIn && skin != ToolbarNode::kOut) {
        log::debug("BarbiePager::Build() - trying to build a pager in a invalid palette");
        skin = ToolbarNode::kOut;
        sfx = "Out";
    }
    m_body = m_node.createAnimAtViewport("pager" + sfx);
    auto button = [&](std::string_view base, int clickId) {
        auto b = std::make_unique<RolloverButton>(m_node, false);
        b->init(std::string(base) + sfx);
        if (b->anim())
            b->anim()->setClickId(clickId);
        return b;
    };
    m_yes = button("pagerYes", kYes);
    m_no = button("pagerNo", kNo);
    m_replay = button("pagerReplay", kReplay);
    m_race = button("pagerRace", kRace);
    m_christieLcd = m_node.createAnimAtViewport(skin == ToolbarNode::kIn ? "christieLCDIn" : "christieLCDOut");
    if (m_christieLcd)
        m_christieLcd->hide(true);
    m_teresaLcd = m_node.createAnimAtViewport(skin == ToolbarNode::kIn ? "teresaLCDIn" : "teresaLCDOut");
    if (m_teresaLcd)
        m_teresaLcd->hide(true);
}

void BarbiePager::hide() {
    // 0x4061f0.
    if (!m_body || !m_body->visible())
        return;
    m_body->hide(true);
    for (auto* b : {m_yes.get(), m_no.get(), m_replay.get(), m_race.get()})
        if (b)
            b->setEnabled(false);
    if (m_christieLcd)
        m_christieLcd->hide(true);
    if (m_teresaLcd)
        m_teresaLcd->hide(true);
}

void BarbiePager::show() {
    // 0x406250: everything moves to the view origin (the scene may have
    // scrolled). No, Replay and Race take Yes's depth, Teresa's LCD Christie's.
    if (!m_body || m_body->visible())
        return;
    const float x = m_node.cameraX(), y = m_node.cameraY();
    m_body->setPosition(x, y, m_body->z());
    engine::Anim* yes = m_yes ? m_yes->anim() : nullptr;
    if (yes)
        yes->setPosition(x, y, yes->z());
    for (auto* b : {m_no.get(), m_replay.get(), m_race.get()})
        if (b && b->anim())
            b->anim()->setPosition(x, y, yes ? yes->z() : b->anim()->z());
    if (m_christieLcd)
        m_christieLcd->setPosition(x, y, m_christieLcd->z());
    if (m_teresaLcd)
        m_teresaLcd->setPosition(x, y, m_christieLcd ? m_christieLcd->z() : m_teresaLcd->z());
    if (m_christieLcd)
        m_christieLcd->hide(true);
    if (m_teresaLcd)
        m_teresaLcd->hide(true);
    m_body->hide(false);
    for (auto* b : {m_yes.get(), m_no.get(), m_replay.get(), m_race.get()})
        if (b)
            b->setEnabled(true);
    m_answered = false;
}

void BarbiePager::showLcd(int which) {
    // 0x406b40: 0 Christie, otherwise Teresa.
    engine::Anim* on = which == 0 ? m_christieLcd : m_teresaLcd;
    engine::Anim* off = which == 0 ? m_teresaLcd : m_christieLcd;
    if (off)
        off->hide(true);
    forceShow(on);
}

void BarbiePager::pageMessage(int lcd, int page) {
    // 0x406970: remember the page and start flashing the toolbar button.
    if (!m_button)
        return;
    m_lcd = lcd;
    m_page = page;
    m_button->setSequence(3, true);
    setSuspended(false);
    m_beeps = 0;
}

void BarbiePager::blink(bool on) {
    // 0x4069b0: beeps on the 1st and 3rd flash; stops after the 3rd.
    m_blinkOn = on;
    if (on) {
        if (m_beeps++ > 2) {
            stopBlink();
            return;
        }
        if (m_beeps == 1) {
            playSfx(m_node, "pager");
            m_node.playVoiceNoFlaps("db0248a");
        } else if (m_beeps != 2) {
            playSfx(m_node, "pager");
            if (m_node.popupKind() != popup::kPager)
                m_node.playVoiceAfterPause("db0248b");
        }
    }
    if (m_button)
        m_button->setSequence(m_blinkOn ? 3 : 0, true);
    m_blinkTick = m_node.engine().ticks();
}

void BarbiePager::stopBlink() {
    // 0x406aa0.
    setSuspended(true);
    setSequenceKeepHidden(m_button, 0);
}

void BarbiePager::spendTime() {
    // 0x406af0: one flash phase per second.
    if (m_node.engine().ticks() - m_blinkTick > 1000)
        blink(!m_blinkOn);
}

void BarbiePager::open(ToolbarNode&) {
    // 0x4063d0.
    m_open = true;
    m_result = 0;
    show();
    m_prevPage = m_page;
    if (m_page == kRace0 || m_page == kRace1 || m_page == kRace2) {
        m_raceType = m_page == kRace0 ? 0 : m_page == kRace1 ? 1 : 2;
        showLcd(m_lcd);
        stopBlink();
        const auto& voices = kRaceVoices[static_cast<std::size_t>(m_raceType)];
        m_replayVoice = voices[0];
        m_node.playVoice(voices[0]);
        m_node.playVoiceNoFlaps(voices[1]);
        return;
    }

    // A chat (the page's friend), or with nothing pending a random friend.
    stopBlink();
    const bool chat = m_page == kChristieChat || m_page == kTeresaChat;
    int friendId = chat ? (m_page != kChristieChat ? 1 : 0) : (m_node.engine().rand() % 2 != 0 ? 1 : 0);
    if (chat && m_keepFriend) {
        m_keepFriend = false;
    } else {
        // The friend whose horse the player rides answers (app+0x1C0).
        const std::uint32_t horse = m_node.app().record().horse();
        if (horse == 1 && friendId == 0)
            friendId = 1;
        else if (horse == 0 && friendId == 1)
            friendId = 0;
    }
    // Barbie explains the pager the first time (rec+0x140).
    auto& rec = m_node.app().record();
    if (rec.u32(PlayerRecord::kPagerIntroCount) == 0) {
        m_node.playVoice("db04027");
        m_node.playVoiceNoFlaps("pause");
        m_node.playVoiceNoFlaps("pause");
        rec.setU32(PlayerRecord::kPagerIntroCount, rec.u32(PlayerRecord::kPagerIntroCount) + 1);
    } else {
        m_node.stopVoice();
    }
    std::string_view voice;
    if (friendId == 0) {
        m_page = kChristieChat;
        voice = chat ? "dc0495" : "db0494";
    } else {
        m_page = kTeresaChat;
        voice = chat ? "dte3002" : "db0503";
    }
    m_node.playVoiceNoFlaps(voice);
    m_replayVoice = voice;
    showLcd(friendId);
}

void BarbiePager::close() {
    // 0x406690: delivers the answer.
    if (!m_open)
        return;
    m_open = false;
    hide();
    m_node.stopVoice();
    const int r = m_result;
    if (r == kChristieChat || r == kTeresaChat) {
        m_node.app().startRace(r); // a friend's chat ends in a race with her
    } else {
        if (r == kRace0 || r == kRace1 || r == kRace2) {
            // An accepted rescue offer becomes the active mission.
            auto& s = m_node.app().session();
            if (m_raceType == 0) {
                s.activeMission = 0xD;
                s.missionTrail = 0;
            } else if (m_raceType == 2) {
                s.activeMission = 0xE;
                s.missionTrail = 4;
            } else {
                s.activeMission = 0xC;
                s.missionTrail = 3;
            }
        }
        m_node.listen(r, {}); // node+0x18 ListenToMessage(r, 0), also for r = 0
    }
    m_page = 0;
    m_raceType = 3;
}

bool BarbiePager::mouseDown(const engine::Pick& pick) {
    // 0x4067a0: on the button release.
    if (!pick.element || pick.eventType != engine::InputEvent::LeftUp)
        return false;
    const int id = pick.element->clickId();
    if (id == kReplay) {
        if (!m_replayVoice.empty())
            m_node.playVoice(m_replayVoice);
        return false;
    }
    if (id != kYes && id != kRace && id != kNo)
        return false;
    if (m_answered) { // the next answer click closes the pager
        m_answered = false;
        m_node.stopVoice();
        return true;
    }
    playSfx(m_node, "click");
    const auto t = static_cast<std::size_t>(m_raceType);
    if (id == kNo) {
        if (m_raceType == 3) {
            m_node.stopVoice();
            return true;
        }
        g_raceDeclined[t] = true;
        m_node.playVoice(kRaceVoices[t][2]);
    } else {
        if (m_raceType == 3) {
            m_node.playVoice(m_page == kChristieChat ? "dc0496" : "dt0504");
        } else {
            // The flag is cleared just before it is tested, so [3] never plays.
            g_raceDeclined[t] = false;
            m_node.playVoice(kRaceVoices[t][g_raceDeclined[t] ? 3 : 4]);
        }
        m_result = m_page;
        m_page = 0;
    }
    m_node.closePagerRequested = true; // the toolbar closes the pager when the voice ends
    m_answered = true;
    return false;
}

bool BarbiePager::hitTest(int x, int y) {
    // 0x406940.
    return m_body && m_body->hitTest(x, y, m_node.cameraX(), m_node.cameraY());
}

bool BarbiePager::isShowing() const { return m_body && m_body->visible(); } // 0x406b20

// --- BarbieSaddlebag ------------------------------------------------------------------------

namespace {

constexpr int kSlotCount = 6;
constexpr int kPicCount = 3;

// The global inventory (statics of the original).
std::array<std::string, kSlotCount> g_bagSlot; // 0x809970: item anim name per slot
std::array<std::string, kPicCount> g_bagPic;   // 0x809a30: picture sub-slots
int g_bagPalette = -1;                         // 0x809a90: the palette the names were built for
std::array<int, kSlotCount> g_slotAnimId = {0x36B9, 0x36B9, 0x36B9, 0x36B9, 0x36B9, 0x36B9}; // 0x7fe958

// Per item (tables at 0x7fe7b8 names, 0x7fe888 slot, 0x7fe8f0 anim id).
struct ItemInfo {
    std::string_view name;
    int slot;
    int animId;
};
constexpr std::array<ItemInfo, BarbieSaddlebag::kItemCount> kItems = {{
    {"horseshoe", 0, 0x36B0},     {"mission1", 1, 0x36B1},     {"mission2", 1, 0x36B1},
    {"mission3", 1, 0x36B1},      {"mission4", 1, 0x36B1},     {"mission5", 1, 0x36B1},
    {"mission6", 1, 0x36B1},      {"mission7", 1, 0x36B1},     {"mission8", 1, 0x36B1},
    {"certificate1", 1, 0x36B1},  {"certificate2", 1, 0x36B1}, {"certificate3", 1, 0x36B1},
    {"certificate4", 1, 0x36B1},  {"locketNote1", 2, 0x36B2},  {"locketNote2", 2, 0x36B3},
    {"locket", 2, 0x36B4},        {"c1 rosePic", 3, 0x36B5},   {"c1 tulipPic", 3, 0x36B5},
    {"c1 daisyPic", 3, 0x36B5},   {"c2 owlpic", 3, 0x36B6},    {"c2 hawkpic", 3, 0x36B6},
    {"c2 bbirdpic", 3, 0x36B6},   {"c3 rainbowPic", 3, 0x36B7}, {"bottle", 4, 0x36B8},
    {"coin", 5, 0x36B9},
}};

// Click ids of the item anims (MouseDown 0x4086b0).
constexpr int kClickHorseshoe = 0x36B0, kClickNote = 0x36B1, kClickLocketNote1 = 0x36B2, kClickLocketNote2 = 0x36B3,
              kClickLocket = 0x36B4, kClickC1Pics = 0x36B5, kClickC2Pics = 0x36B6, kClickC3Pic = 0x36B7,
              kClickBottle = 0x36B8;

bool validItem(int item) { return item >= 0 && item < BarbieSaddlebag::kItemCount; }
bool isPicture(int item) { return item >= BarbieSaddlebag::kC1RosePic && item <= BarbieSaddlebag::kC3RainbowPic; }
bool isCertificate(int item) { return item >= BarbieSaddlebag::kCertificate1 && item <= BarbieSaddlebag::kCertificate4; }

// The picture sub-slot of a picture item.
int picSlot(int item) {
    if (item <= BarbieSaddlebag::kC1DaisyPic)
        return item - BarbieSaddlebag::kC1RosePic;
    if (item <= BarbieSaddlebag::kC2BirdPic)
        return item - BarbieSaddlebag::kC2OwlPic;
    return 0;
}

// Build 0x406c60, palette change: the stored name's In/Out suffix (the first
// 'I' or 'O' in the name) is swapped.
void swapSuffix(std::string& name) {
    const auto pos = name.find_first_of("IO");
    if (pos == std::string::npos)
        return;
    name = name.substr(0, pos) + (name[pos] == 'I' ? "Out" : "In");
}

engine::Anim* findNamed(engine::AnimRegistry& anims, const std::string& name) {
    return name.empty() ? nullptr : anims.find(name);
}

// 0x408670 ToggleItemAnim: sequence 0 (closed) <-> 1 (open). Returns
// 1 opened, 0 closed, 2 no anim.
int toggleItem(engine::Anim* a) {
    if (!a)
        return 2;
    const bool opening = a->sequence() == 0;
    a->setSequence(opening ? 1 : 0, true);
    return opening ? 1 : 0;
}

} // namespace

BarbieSaddlebag::BarbieSaddlebag(ToolbarNode& node) : engine::Task(node.engine().tasks()), m_node(node) {
    setSuspended(true); // +0x0C: blinking only after an item was added
}

BarbieSaddlebag::~BarbieSaddlebag() = default;

void BarbieSaddlebag::resetInventory() {
    for (auto& s : g_bagSlot)
        s.clear();
    for (auto& p : g_bagPic)
        p.clear();
    g_bagPalette = -1;
    g_slotAnimId.fill(0x36B9);
}

std::string BarbieSaddlebag::itemAnimName(int item) const {
    const std::string_view sfx = m_node.skinSuffix();
    if (item == kHorseshoe && sfx == "Vine")
        return "horseshoeOut";
    if (item == kCoin && sfx == "Frog")
        return "coinOut";
    return std::string(kItems[static_cast<std::size_t>(item)].name) + std::string(sfx);
}

bool BarbieSaddlebag::hasItem(int item) const {
    // 0x408310.
    if (!validItem(item))
        return false;
    const std::string name = itemAnimName(item);
    const int slot = kItems[static_cast<std::size_t>(item)].slot;
    if (str::iequals(g_bagSlot[static_cast<std::size_t>(slot)], name))
        return true;
    if (slot == 3)
        for (const auto& p : g_bagPic)
            if (str::iequals(p, name))
                return true;
    return false;
}

void BarbieSaddlebag::addItem(int item, bool quiet) {
    // 0x407b40.
    if (!validItem(item) || hasItem(item))
        return;
    auto& anims = m_node.engine().anims();
    const ItemInfo& info = kItems[static_cast<std::size_t>(item)];
    const std::string name = itemAnimName(item);
    if (isPicture(item)) {
        g_bagPic[static_cast<std::size_t>(picSlot(item))] = name;
    } else if (isCertificate(item)) {
        // A certificate replaces the pictures. (The original does not delete
        // the slot's previous anim here; it goes when the node is deleted.)
        for (auto& p : g_bagPic) {
            if (!p.empty()) {
                if (anims.find(p))
                    anims.remove(p);
                p.clear();
            }
        }
        g_bagSlot[static_cast<std::size_t>(info.slot)] = name;
    } else {
        auto& slot = g_bagSlot[static_cast<std::size_t>(info.slot)];
        anims.remove(slot);
        slot = name;
    }
    if (engine::Anim* a = m_node.createAnimAtViewport(name)) {
        a->setClickId(info.animId); // anim+0x11c
        a->hide(true);
    }
    g_slotAnimId[static_cast<std::size_t>(info.slot)] = info.animId;
    if (item == kCoin)
        m_node.app().record().setU16(PlayerRecord::kHasCoin, 1);
    if (m_notify && !quiet)
        startBlink();
}

void BarbieSaddlebag::removeItem(int item) {
    // 0x4084e0.
    if (!validItem(item))
        return;
    auto& anims = m_node.engine().anims();
    const ItemInfo& info = kItems[static_cast<std::size_t>(item)];
    if (info.slot == 3) {
        // Original bug kept: a matching picture's anim is deleted, but the
        // slot cleared is g_bagSlot[i] (i = 0..2), not the picture sub-slot.
        const std::string name = std::string(info.name) + std::string(m_node.skinSuffix());
        for (std::size_t i = 0; i < kPicCount; ++i) {
            if (str::iequals(g_bagPic[i], name)) {
                anims.remove(name);
                g_bagSlot[i].clear();
            }
        }
        return;
    }
    auto& slot = g_bagSlot[static_cast<std::size_t>(info.slot)];
    anims.remove(slot);
    slot.clear();
}

void BarbieSaddlebag::restoreFromRecord() {
    // 0x407aa0: the items whose state is saved (quiet, no blinking).
    m_notify = false;
    const auto& rec = m_node.app().record();
    if (rec.u32(PlayerRecord::kMissionDone + 0x1C) != 0) // app+0x274
        addItem(kHorseshoe, true);
    if (rec.u32(PlayerRecord::kMissionDone) != 0) // app+0x258
        addItem(kLocket, true);
    if (rec.u32(PlayerRecord::kMissionDone + 0x14) != 0) // app+0x26C
        addItem(kBottle, true);
    if (rec.u32(PlayerRecord::kMissionDone + 0x04) != 0 && rec.u16(PlayerRecord::kHasCoin) != 0) // app+0x25C, +0x2AC
        addItem(kCoin, true);
    m_notify = true;
}

void BarbieSaddlebag::build() {
    // 0x406c60.
    const std::string sfx(m_node.skinSuffix());
    m_bag = m_node.createAnimAtViewport("saddlebag" + sfx);
    m_flap = m_node.createAnimAtViewport("sbFlap" + sfx);
    if (m_flap)
        m_flap->setPerPixelHit(false);
    const int palette = m_node.skin() == ToolbarNode::kIn ? 0 : 1;
    if (g_bagPalette != palette) {
        for (auto& s : g_bagSlot)
            if (!s.empty())
                swapSuffix(s);
        for (auto& p : g_bagPic)
            if (!p.empty())
                swapSuffix(p);
    }
    for (std::size_t i = 0; i < kSlotCount; ++i) {
        if (g_bagSlot[i].empty())
            continue;
        if (engine::Anim* a = m_node.createAnimAtViewport(g_bagSlot[i]))
            a->setClickId(g_slotAnimId[i]);
    }
    for (const auto& p : g_bagPic) {
        if (p.empty())
            continue;
        if (engine::Anim* a = m_node.createAnimAtViewport(p))
            a->setClickId(str::istartsWith(p, "c1") ? 0x36B5 : 0x36B6);
    }
    g_bagPalette = palette;
}

void BarbieSaddlebag::open(ToolbarNode&) {
    // Open 0x407a80 -> Show 0x4078b0: the bag and its items, closed, at the
    // view origin.
    if (!m_bag || m_bag->visible())
        return;
    auto& anims = m_node.engine().anims();
    const float x = m_node.cameraX(), y = m_node.cameraY();
    m_bag->hide(false);
    if (m_flap)
        m_flap->hide(false);
    auto item = [&](const std::string& name, int clickId) {
        engine::Anim* a = anims.find(name);
        if (!a) {
            a = m_node.createAnimAtViewport(name);
            if (a && clickId != 0)
                a->setClickId(clickId);
        }
        if (a) {
            a->hide(false);
            a->setSequence(0, true);
        }
        return a;
    };
    for (std::size_t i = 0; i < kSlotCount; ++i) {
        if (g_bagSlot[i].empty())
            continue;
        if (engine::Anim* a = item(g_bagSlot[i], g_slotAnimId[i]))
            a->setPosition(x, y, a->z());
        if (i == 3) // (slot 3 is always empty: the pictures live in g_bagPic)
            for (const auto& p : g_bagPic)
                if (!p.empty())
                    item(p, 0);
    }
    for (const auto& p : g_bagPic) {
        if (p.empty())
            continue;
        if (engine::Anim* a = item(p, 0))
            a->setPosition(x, y, a->z());
    }
    m_bag->setPosition(x, y, m_bag->z());
    if (m_flap)
        m_flap->setPosition(x, y, m_bag->z()); // the flap takes the bag's depth
}

std::vector<std::string> BarbieSaddlebag::certificateChecks() const {
    // The checkmarks drawn on the certificate in slot 1 (MouseDown 0x4086b0,
    // Close 0x4070f0): one per C1/C2 photo taken (rec+0xFE / rec+0x104), and
    // for C4 per pair of trail-sign answers (app+0x322..+0x32E).
    std::vector<std::string> names;
    const std::string& cert = g_bagSlot[1];
    const std::string sfx(m_node.skinSuffix());
    const auto& rec = m_node.app().record();
    auto photos = [&](std::size_t flags, int c) {
        for (int k = 0; k < 3; ++k)
            if (rec.u16(flags + 2 * static_cast<std::size_t>(k)) != 0)
                names.push_back(std::format("c{} check{}{}", c, k + 1, sfx));
    };
    if (str::istartsWith(cert, "certificate1"))
        photos(PlayerRecord::kC1Photos, 1);
    if (str::istartsWith(cert, "certificate2"))
        photos(PlayerRecord::kC2Photos, 2);
    if (str::istartsWith(cert, "certificate4")) {
        const auto& p = m_node.app().session().c4Progress;
        if (p[3] != 0 && p[4] != 0)
            names.push_back("c4 check1" + sfx);
        if (p[5] != 0)
            names.push_back("c4 check2" + sfx);
        if (p[0] != 0 && p[1] != 0)
            names.push_back("c4 check3" + sfx);
        if (p[6] != 0)
            names.push_back("c4 check4" + sfx);
        if (p[2] != 0)
            names.push_back("c4 check5" + sfx);
    }
    return names;
}

void BarbieSaddlebag::close() {
    // 0x4070f0.
    if (!m_bag || !m_bag->visible())
        return;
    auto& anims = m_node.engine().anims();
    m_bag->hide(true);
    if (m_flap)
        m_flap->hide(true);
    for (const auto& s : g_bagSlot)
        if (engine::Anim* a = findNamed(anims, s))
            a->hide(true);
    for (const auto& p : g_bagPic)
        if (engine::Anim* a = findNamed(anims, p))
            a->hide(true);
    for (const auto& name : certificateChecks())
        anims.remove(name);
    m_node.engine().sounds().stopSound("musicbox");
}

bool BarbieSaddlebag::mouseDown(const engine::Pick& pick) {
    // 0x4086b0: clicking an item opens or closes it. Never closes the bag.
    if (!pick.element || pick.eventType != engine::InputEvent::LeftDown)
        return false;
    auto& anims = m_node.engine().anims();
    auto slot = [&](int i) { return findNamed(anims, g_bagSlot[static_cast<std::size_t>(i)]); };
    switch (pick.element->clickId()) {
    case kClickHorseshoe: {
        engine::Anim* shoe = slot(0);
        if (toggleItem(shoe) == 0) {
            shoe->setPerPixelHit(true);
            playSfx(m_node, "shoeclose");
        } else {
            if (shoe)
                shoe->setPerPixelHit(false); // the open close-up is clicked anywhere
            playSfx(m_node, "shoeopen");
            const bool variantB = m_node.app().record().u16(PlayerRecord::kVoiceVariantB) != 0; // app+0x29E
            m_node.delayVoice(variantB ? "db04071" : "db04069", 800);
        }
        break;
    }
    case kClickNote: {
        // A mission note or certificate: an open locket closes first.
        if (engine::Anim* locket = slot(2); locket && locket->sequence() == 1)
            toggleItem(locket);
        const int r = toggleItem(slot(1));
        if (r == 0) {
            playSfx(m_node, "notein");
            for (const auto& name : certificateChecks())
                anims.remove(name);
        } else if (r == 1) {
            playSfx(m_node, "noteopen");
            for (const auto& name : certificateChecks()) {
                engine::Anim* check = m_node.createAnimAtViewport(name);
                // The C1/C2 checkmarks sit (-1, 3) off in the In palette.
                if (check && name[1] != '4' && m_node.skin() == ToolbarNode::kIn)
                    check->move(-1.0f, 3.0f, 0.0f);
            }
        }
        break;
    }
    case kClickLocketNote1:
        if (toggleItem(slot(2)) != 0) {
            playSfx(m_node, "noteopen");
            m_node.playVoice("db0013");
            m_node.playVoiceNoFlaps("db04003");
        } else {
            playSfx(m_node, "noteclose");
        }
        break;
    case kClickLocketNote2:
        if (toggleItem(slot(2)) != 0) {
            playSfx(m_node, "noteopen");
            m_node.playVoice("db04004");
        } else {
            playSfx(m_node, "noteclose");
        }
        break;
    case kClickLocket:
        if (toggleItem(slot(2)) == 0) {
            m_node.engine().sounds().stopSound("musicbox");
            playSfx(m_node, "locketClose");
        } else {
            playSfx(m_node, "locketOpen");
            m_node.engine().sounds().playSound("musicbox", -1, false, false, 0, false, 0x70, -1, 0, false); // loops
            if (hasItem(kBottle))
                m_node.playVoice("db02062");
        }
        break;
    case kClickC1Pics:
    case kClickC2Pics:
    case kClickC3Pic: {
        // The pictures toggle slot 3 first, which is always empty (so the
        // sound is always "noteopen"), then the picture anims themselves.
        playSfx(m_node, toggleItem(slot(3)) == 0 ? "noteclose" : "noteopen");
        if (pick.element->clickId() == kClickC3Pic) {
            toggleItem(findNamed(anims, g_bagPic[0]));
        } else {
            for (const auto& p : g_bagPic)
                toggleItem(findNamed(anims, p));
        }
        break;
    }
    case kClickBottle:
        if (toggleItem(slot(4)) != 0)
            m_node.playVoice("db04021");
        break;
    default:
        break; // the coin (0x36B9) does nothing
    }
    return false;
}

bool BarbieSaddlebag::hitTest(int x, int y) {
    // 0x409ad0: a click on the flap closes the bag.
    const float cx = m_node.cameraX(), cy = m_node.cameraY();
    if (m_flap && m_flap->hitTest(x, y, cx, cy))
        return false;
    return m_bag && m_bag->hitTest(x, y, cx, cy);
}

bool BarbieSaddlebag::isShowing() const { return m_bag && m_bag->visible(); } // 0x406b20

void BarbieSaddlebag::startBlink() {
    // 0x409b20: the toolbar bag button flashes three times.
    RolloverButton* b = m_node.findButtonById(ToolbarNode::kBag);
    if (!b || !b->anim())
        return;
    m_button = b->anim();
    m_button->setSequence(3, true);
    m_blinks = 0;
    setSuspended(false);
}

void BarbieSaddlebag::stopBlink() {
    // 0x409b60.
    setSuspended(true);
    setSequenceKeepHidden(m_button, 0);
}

void BarbieSaddlebag::blink(bool on) {
    // 0x409bb0.
    m_blinkOn = on;
    if (on && m_blinks++ > 2) {
        stopBlink();
        return;
    }
    if (m_button && m_button->visible())
        m_button->setSequence(on ? 3 : 0, true);
    m_blinkTick = m_node.engine().ticks();
}

void BarbieSaddlebag::spendTime() {
    // 0x409c20.
    if (m_node.engine().ticks() - m_blinkTick > 1000)
        blink(!m_blinkOn);
}

// --- BarbieMap -------------------------------------------------------------------------------

namespace {

// Hotspots in object order (+0x14..+0x2C, click ids 54000..54006), their
// rollover voices and their GotoNodeById targets (MouseDown 0x405650).
constexpr int kMapClickBase = 54000;
constexpr std::array<std::string_view, 7> kMapSpots = {"stable", "apple", "turtle", "rainbow",
                                                       "bridge", "rabbit", "rabbit2"};
constexpr std::array<std::string_view, 7> kMapVoices = {"db9029", "db0804", "db0801", "db0802",
                                                        "db0803", "db0800", "db0805"};
// tackroom, appleW, beachE, rainbowS, bridgeS, rabbitE, rabbitE
constexpr std::array<int, 7> kMapTargets = {7, 0x11, 0x16, 0x15, 0x13, 0x14, 0x14};

} // namespace

BarbieMap::BarbieMap() = default; // 0x404f70
BarbieMap::~BarbieMap() = default;

void BarbieMap::open(ToolbarNode& node) {
    // 0x405000.
    m_hideCottage = m_hideWell = m_youAreHereAnim = nullptr;
    m_node = &node;
    m_in = node.skinSuffix() == "In";
    const std::string sfx = m_in ? "In" : "Out";
    // CreateAnimsIn 0x405390 / CreateAnimsOut 0x405450 (in their order).
    m_bg = node.createAnimAtViewport("map" + sfx);
    for (std::size_t i : {0u, 2u, 1u, 3u, 4u, 5u, 6u})
        m_spots[i] = node.createAnimAtViewport("map" + std::string(kMapSpots[i]) + sfx);
    m_hideWell = node.createAnimAtViewport("maphideWell" + sfx);
    m_hideCottage = node.createAnimAtViewport("maphideCottage" + sfx);
    if (!m_in) {
        m_youAreHereAnim = node.createAnimAtViewport("youAreHereOut");
        if (m_youAreHereAnim)
            m_youAreHereAnim->setSequence(m_youAreHere, true);
    }

    // The overlays cover the cottage and the well path until they are opened.
    const auto& rec = node.app().record();
    if (rec.u16(PlayerRecord::kCottageGateOpen) == 0) // app+0x2AA
        forceShow(m_hideCottage);
    else if (m_hideCottage)
        m_hideCottage->hide(true);
    if (rec.u16(PlayerRecord::kWellPathOpen) == 0) // app+0x2A8
        forceShow(m_hideWell);
    else if (m_hideWell)
        m_hideWell->hide(true);
    forceShow(m_bg);
    for (std::size_t i : {0u, 2u, 1u, 3u, 4u, 5u, 6u})
        forceShow(m_spots[i]);
    forceShow(m_youAreHereAnim);

    for (std::size_t i = 0; i < kSpots; ++i) {
        forceShow(m_spots[i]);
        if (m_spots[i])
            m_spots[i]->setClickId(kMapClickBase + static_cast<int>(i));
        m_rollovers[i] = std::make_unique<MapVoiceRollover>(node, m_spots[i], std::string(kMapVoices[i]));
    }
    node.engine().sounds().getSound("trailsign", true, false);
    playSfx(node, "map");
}

void BarbieMap::hideAll() {
    // 0x405530.
    for (engine::Anim* a : {m_bg, m_spots[0], m_spots[2], m_spots[1], m_spots[3], m_spots[4], m_spots[5], m_spots[6],
                            m_hideWell, m_hideCottage, m_youAreHereAnim})
        if (a)
            a->hide(true);
}

void BarbieMap::destroyRollovers() {
    // 0x4055b0.
    if (!m_rollovers[0])
        return;
    for (auto& r : m_rollovers)
        r.reset();
    hideAll();
}

void BarbieMap::close() {
    // 0x4059f0. The original releases "trailnode", not the "trailsign" that
    // Open cached (kept: Retain on a sound that is not loaded does nothing).
    destroyRollovers();
    if (m_node)
        m_node->engine().sounds().retain("trailnode", false);
    m_bg = nullptr;
    m_youAreHereAnim = nullptr;
}

bool BarbieMap::mouseDown(const engine::Pick& pick) {
    // 0x405650: a hotspot travels there.
    if (!pick.element || pick.eventType != engine::InputEvent::LeftDown || !m_node)
        return false;
    const int spot = pick.element->clickId() - kMapClickBase;
    if (spot < 0 || spot >= kSpots)
        return false;
    Godiva& app = m_node->app();
    if (app.record().u16(PlayerRecord::kFirstRideDone) == 0) { // app+0x254
        // Before the first ride Barbie says to follow the rabbit instead.
        m_node->playVoice("db9011");
        m_node->rabbitLatch = true; // +0x1BC: go to rabbitE when DB9011 ends
        return true;
    }
    auto& s = app.session();
    if (spot != 0)
        s.arriveByMap = 1;
    playSfx(*m_node, "trailsign");
    hideAll();
    s.c4LastTrail = 7;
    if (spot == 6)
        s.mapRabbit2 = 1;
    m_node->gotoNodeById(kMapTargets[static_cast<std::size_t>(spot)]);
    return true;
}

bool BarbieMap::hitTest(int x, int y) {
    // 0x405a20.
    return m_bg && m_node && m_bg->hitTest(x, y, m_node->cameraX(), m_node->cameraY());
}

bool BarbieMap::isShowing() const { return m_bg && m_bg->visible(); } // 0x405a50

void BarbieMap::setYouAreHere(int location) {
    // 0x4055f0: the marker shows only while the map does.
    m_youAreHere = location;
    if (!m_youAreHereAnim)
        return;
    m_youAreHereAnim->setSequence(location, true);
    if (!m_bg || !m_bg->visible())
        m_youAreHereAnim->hide(true);
    else
        forceShow(m_youAreHereAnim);
}

// --- camera ----------------------------------------------------------------------------------

namespace camera {

// The photo store is game/Camera.cpp (BarbieCamera).
bool takePhoto(Godiva& app) { return BarbieCamera::takePhoto(app); }
void deletePhotoFile(int slot) { BarbieCamera::deletePhotoFile(slot); }

} // namespace camera

} // namespace brc::game
