#include "game/Popups.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "game/Godiva.h"
#include "game/ToolbarNode.h"

#include <array>
#include <format>
#include <string_view>

namespace brc::game {

// --- BarbiePager -----------------------------------------------------------------------------

BarbiePager::BarbiePager(ToolbarNode& node) : engine::Task(node.engine().tasks()), m_node(node) {
    setSuspended(true); // +0x08: blinking only while a page is pending
}

BarbiePager::~BarbiePager() = default;

void BarbiePager::pageMessage(int lcd, int page) {
    // 0x406970: remember the page and start flashing the toolbar button.
    m_lcd = lcd;
    m_page = page;
    if (m_button)
        m_button->setSequence(3, true);
    // TODO(popups): the beeps of Blink (0x4069b0) and StopBlink (0x406aa0).
    log::debug("BarbiePager::PageMessage({}, {:#x})", lcd, page);
}

void BarbiePager::open(ToolbarNode&) {
    // TODO(popups): Open 0x4063d0 (Show, the LCD, the page's voice lines).
    log::info("BarbiePager::Open: not ported yet ({}, page {:#x})", m_node.name(), m_page);
    m_open = true;
}

void BarbiePager::close() {
    // TODO(popups): Close 0x406690 (Hide, StopVoice, deliver the accepted page).
    m_open = false;
}

bool BarbiePager::mouseDown(const engine::Pick&) { return false; } // TODO(popups): 0x4067a0

// The popup is not drawn yet, so any click outside the toolbar button closes it.
bool BarbiePager::hitTest(int, int) { return false; } // TODO(popups): 0x406940

bool BarbiePager::isShowing() const { return m_open; }

void BarbiePager::spendTime() {} // TODO(popups): 0x406af0

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

// The certificate checkmark anims "cN checkK"+sfx (Close 0x4070f0).
void deleteCheckmarks(engine::AnimRegistry& anims, std::string_view sfx) {
    for (int c = 1; c <= 4; ++c)
        for (int k = 1; k <= 5; ++k) {
            const std::string name = std::format("c{} check{}{}", c, k, sfx);
            if (anims.find(name))
                anims.remove(name);
        }
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
    if (m_notify && !quiet) {
        // TODO(popups): StartBlink 0x409b20 (the bag button blinks 3 times).
        log::debug("BarbieSaddlebag: item {} added (blink not ported)", item);
    }
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
    // Open 0x407a80 -> Show 0x4078b0.
    if (!m_bag || m_bag->visible())
        return;
    m_bag->hide(false);
    if (m_flap)
        m_flap->hide(false);
    auto showItem = [&](const std::string& name) {
        if (name.empty())
            return;
        if (engine::Anim* a = m_node.createAnimAtViewport(name)) {
            while (!a->visible())
                a->hide(false);
            a->setSequence(0, true);
        }
    };
    for (const auto& s : g_bagSlot)
        showItem(s);
    for (const auto& p : g_bagPic)
        showItem(p);
    // TODO(popups): MouseDown 0x4086b0 (opening the items) is not ported; the bag only shows its contents.
}

void BarbieSaddlebag::close() {
    // 0x4070f0.
    auto& anims = m_node.engine().anims();
    if (m_bag && m_bag->visible())
        m_bag->hide(true);
    if (m_flap && m_flap->visible())
        m_flap->hide(true);
    auto hideItem = [&](const std::string& name) {
        if (name.empty())
            return;
        if (engine::Anim* a = anims.find(name))
            if (a->visible())
                a->hide(true);
    };
    for (const auto& s : g_bagSlot)
        hideItem(s);
    for (const auto& p : g_bagPic)
        hideItem(p);
    deleteCheckmarks(anims, m_node.skinSuffix());
    m_node.engine().sounds().stopSound("musicbox");
}

bool BarbieSaddlebag::mouseDown(const engine::Pick&) { return false; } // TODO(popups): 0x4086b0

bool BarbieSaddlebag::hitTest(int x, int y) {
    // 0x409ad0: a click on the flap closes the bag.
    const float cx = m_node.cameraX(), cy = m_node.cameraY();
    if (m_flap && m_flap->hitTest(x, y, cx, cy))
        return false;
    return m_bag && m_bag->hitTest(x, y, cx, cy);
}

bool BarbieSaddlebag::isShowing() const { return m_bag && m_bag->visible(); } // 0x406b20

void BarbieSaddlebag::spendTime() {} // TODO(popups): 0x409c20 blinking

// --- BarbieMap -------------------------------------------------------------------------------

void BarbieMap::open(ToolbarNode& node) {
    // TODO(popups): BarbieMap (0x404f70) is not ported yet.
    log::info("BarbieMap::Open: not ported yet ({})", node.name());
    m_open = true;
}

void BarbieMap::close() { m_open = false; }

bool BarbieMap::mouseDown(const engine::Pick&) { return false; }

// Not drawn yet: any click closes it.
bool BarbieMap::hitTest(int, int) { return false; }

// --- camera ----------------------------------------------------------------------------------

namespace camera {

bool takePhoto(Godiva&) {
    // TODO(popups): BarbieCamera::TakePhoto 0x404200 (Photos/photos.omt<slot>).
    log::info("BarbieCamera::TakePhoto: photos are not ported yet");
    return false;
}

void deletePhotoFile(int slot) {
    // TODO(popups): BarbieCamera::DeletePhotoFile 0x404e50.
    log::debug("BarbieCamera::DeletePhotoFile({}): photos are not ported yet", slot);
}

} // namespace camera

} // namespace brc::game
