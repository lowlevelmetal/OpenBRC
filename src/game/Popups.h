#pragma once

// The toolbar popups (pager, map, saddlebag) and the camera. The toolbar
// (ToolbarNode) only uses the interface below; see docs/engine/classes.md
// "Toolbar popups and the camera" for the full behaviour of each popup.

#include "engine/Node.h"
#include "engine/Task.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace brc::engine {
class Anim;
}

namespace brc::game {

class Godiva;
class MapVoiceRollover;
class ToolbarNode;

// Popup kinds kept by the toolbar at node+0xF4.
namespace popup {
inline constexpr int kNone = 0;
inline constexpr int kPager = 1;
inline constexpr int kMap = 3;
inline constexpr int kSaddlebag = 4;
} // namespace popup

// BarbiePopup (vtable 0x7f0218): what the toolbar calls on its active popup.
class Popup {
public:
    virtual ~Popup() = default;
    virtual void open(ToolbarNode& node) = 0;            // +0x04
    virtual void close() = 0;                            // +0x08
    // +0x0C: a click while the popup is open; true = close the popup.
    virtual bool mouseDown(const engine::Pick& pick) = 0;
    // +0x10: false when (x, y) (view coordinates) misses the popup, which
    // makes the toolbar close it. The base answers true.
    virtual bool hitTest(int x, int y) { return (void)x, (void)y, true; }
    // 0x406b20 IsShowing (pager, saddlebag) / map vt+0x18 IsOpen: blocks panorama scrolling.
    virtual bool isShowing() const = 0;
};

// BarbiePager (0x405a70, toolbar node+0xD8): Christie and Teresa page Barbie.
// TODO(popups): Build/Show/Hide/Open/Close/MouseDown/Blink are not ported yet.
class BarbiePager final : public Popup, public engine::Task {
public:
    explicit BarbiePager(ToolbarNode& node);
    ~BarbiePager() override;

    // node+0xD8 -> +0x24: the toolbar pager button's anim (sequence 3 = flashing).
    void setButtonAnim(engine::Anim* anim) { m_button = anim; }
    // 0x406970 PageMessage(lcd, page): 0 Christie / 1 Teresa; page 0 none,
    // 0x3415 Christie chat, 0x3416 Teresa chat, 0x3417/0x3419/0x3418 races.
    void pageMessage(int lcd, int page);
    int currentPage() const { return m_page; }

    void open(ToolbarNode& node) override;
    void close() override;
    bool mouseDown(const engine::Pick& pick) override;
    bool hitTest(int x, int y) override;
    bool isShowing() const override;
    void spendTime() override; // 0x406af0: blinking

private:
    ToolbarNode& m_node;
    engine::Anim* m_button = nullptr;
    int m_lcd = 0;
    int m_page = 0;
    bool m_open = false;
};

// BarbieSaddlebag (0x406bb0, toolbar node+0xD4): the inventory. The item set
// is global (static in the original), so it survives node changes.
class BarbieSaddlebag final : public Popup, public engine::Task {
public:
    // Item ids (AddItem/HasItem/RemoveItem and the toolbar wrappers).
    enum Item : int {
        kHorseshoe = 0, kMission1 = 1, /* … */ kMission8 = 8, kCertificate1 = 9, /* … */ kCertificate4 = 12,
        kLocketNote1 = 13, kLocketNote2 = 14, kLocket = 15, kC1RosePic = 16, kC1TulipPic = 17, kC1DaisyPic = 18,
        kC2OwlPic = 19, kC2HawkPic = 20, kC2BirdPic = 21, kC3RainbowPic = 22, kBottle = 23, kCoin = 24,
        kItemCount = 25
    };

    explicit BarbieSaddlebag(ToolbarNode& node);
    ~BarbieSaddlebag() override;

    void addItem(int item, bool quiet = false); // 0x407b40
    bool hasItem(int item) const;               // 0x408310
    void removeItem(int item);                  // 0x4084e0 (with its bug for pictures)
    void restoreFromRecord();                   // 0x407aa0
    void build();                               // 0x406c60

    void open(ToolbarNode& node) override;      // 0x407a80 -> Show 0x4078b0
    void close() override;                      // 0x4070f0
    bool mouseDown(const engine::Pick& pick) override; // 0x4086b0 (TODO(popups))
    bool hitTest(int x, int y) override;        // 0x409ad0
    bool isShowing() const override;            // 0x406b20
    void spendTime() override;                  // 0x409c20 (TODO(popups): blinking)

    // Clears the global inventory (a new session / player).
    static void resetInventory();

private:
    std::string itemAnimName(int item) const;

    ToolbarNode& m_node;
    engine::Anim* m_bag = nullptr;  // +0x18
    engine::Anim* m_flap = nullptr; // +0x1C
    bool m_notify = true;           // +0x20
};

// BarbieMap (0x404f70, app+0x188): the trail map. TODO(popups): not ported yet.
class BarbieMap final : public Popup {
public:
    void open(ToolbarNode& node) override;
    void close() override;
    bool mouseDown(const engine::Pick& pick) override;
    bool hitTest(int x, int y) override;
    bool isShowing() const override { return m_open; }
    void setYouAreHere(int location) { m_youAreHere = location; } // 0x4055f0

private:
    bool m_open = false;
    int m_youAreHere = 0;
};

// BarbieCamera (app+0x184): toolbar camera photos in `Photos/photos.omt<slot>`.
// TODO(popups): photo storage is not ported yet; takePhoto reports failure.
namespace camera {
bool takePhoto(Godiva& app);  // 0x404200: true if the photo was stored
void deletePhotoFile(int slot); // 0x404e50
} // namespace camera

} // namespace brc::game
