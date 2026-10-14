#pragma once

// The toolbar popups (pager, map, saddlebag) and the camera. The toolbar
// (ToolbarNode) only uses the interface below; see docs/engine/classes.md
// "Toolbar popups and the camera" for the full behaviour of each popup.

#include "engine/Node.h"
#include "engine/Task.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace brc::engine {
class Anim;
}

namespace brc::game {

class Godiva;
class MapVoiceRollover;
class RolloverButton;
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

// BarbiePager (0x405a70, toolbar node+0xD8): Christie and Teresa page Barbie
// with a chat or a race challenge; the toolbar button blinks while a page is
// pending. Answering Yes to a chat starts a race (Godiva::startRace).
class BarbiePager final : public Popup, public engine::Task {
public:
    // Page ids (+0x64).
    static constexpr int kChristieChat = 0x3415, kTeresaChat = 0x3416;
    static constexpr int kRace0 = 0x3417, kRace2 = 0x3418, kRace1 = 0x3419;
    // Button click ids.
    static constexpr int kYes = 0x237, kNo = 0x238, kReplay = 0x239, kRace = 0x23A;

    explicit BarbiePager(ToolbarNode& node); // 0x405a70 (Build 0x405b60, then Hide)
    ~BarbiePager() override;                 // 0x406110

    // node+0xD8 -> +0x24: the toolbar pager button's anim (sequence 3 = flashing).
    void setButtonAnim(engine::Anim* anim) { m_button = anim; }
    // 0x406970 PageMessage(lcd, page): 0 Christie / 1 Teresa; page 0 none,
    // 0x3415 Christie chat, 0x3416 Teresa chat, 0x3417/0x3419/0x3418 races.
    void pageMessage(int lcd, int page);
    int currentPage() const { return m_page; }
    // +0x54: the next chat keeps its friend instead of following the player's
    // horse (set by the trail nodes' debug key 'P', 0x434f60).
    void setKeepFriend() { m_keepFriend = true; }

    void open(ToolbarNode& node) override;              // 0x4063d0
    void close() override;                              // 0x406690
    bool mouseDown(const engine::Pick& pick) override;  // 0x4067a0
    bool hitTest(int x, int y) override;                // 0x406940
    bool isShowing() const override;                    // 0x406b20
    void spendTime() override;                          // 0x406af0: blinking
    void stopBlink();         // 0x406aa0 (also TrailNode::HorseRestart)

private:
    void build();             // 0x405b60
    void hide();              // 0x4061f0
    void show();              // 0x406250
    void showLcd(int which);  // 0x406b40
    void blink(bool on);      // 0x4069b0

    ToolbarNode& m_node;                     // +0x14
    engine::Anim* m_body = nullptr;          // +0x18
    engine::Anim* m_christieLcd = nullptr;   // +0x1C
    engine::Anim* m_teresaLcd = nullptr;     // +0x20
    engine::Anim* m_button = nullptr;        // +0x24
    std::unique_ptr<RolloverButton> m_yes, m_no, m_replay, m_race; // +0x28..+0x34
    std::string m_replayVoice;               // +0x40
    int m_lcd = 0;                           // +0x44
    int m_raceType = 3;                      // +0x48: 0..2, 3 = a chat / none
    bool m_open = false;                     // +0x50
    bool m_keepFriend = false;               // +0x54
    int m_beeps = 0;                         // +0x56
    bool m_blinkOn = false;                  // +0x58
    std::uint32_t m_blinkTick = 0;           // +0x5C
    int m_result = 0;                        // +0x60: delivered by close()
    int m_page = 0;                          // +0x64
    int m_prevPage = 0;                      // +0x68
    bool m_answered = false;                 // +0x6C
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
    bool mouseDown(const engine::Pick& pick) override; // 0x4086b0
    bool hitTest(int x, int y) override;        // 0x409ad0
    bool isShowing() const override;            // 0x406b20
    void spendTime() override;                  // 0x409c20: blinking

    // Clears the global inventory (a new session / player).
    static void resetInventory();
    void stopBlink();        // 0x409b60 (also TrailNode::HorseRestart)

private:
    std::string itemAnimName(int item) const;
    void startBlink();       // 0x409b20
    void blink(bool on);     // 0x409bb0
    std::vector<std::string> certificateChecks() const;

    ToolbarNode& m_node;
    engine::Anim* m_bag = nullptr;    // +0x18
    engine::Anim* m_flap = nullptr;   // +0x1C
    bool m_notify = true;             // +0x20
    int m_blinks = 0;                 // +0x22
    bool m_blinkOn = false;           // +0x24
    std::uint32_t m_blinkTick = 0;    // +0x28
    engine::Anim* m_button = nullptr; // +0x2C: the toolbar bag button
};

// BarbieMap (0x404f70, app+0x188): the trail map. One object for the whole
// session; its anims and rollovers belong to the node it was opened on.
class BarbieMap final : public Popup {
public:
    BarbieMap();
    ~BarbieMap() override;

    void open(ToolbarNode& node) override;             // 0x405000
    void close() override;                             // 0x4059f0
    bool mouseDown(const engine::Pick& pick) override; // 0x405650
    bool hitTest(int x, int y) override;               // 0x405a20
    bool isShowing() const override;                   // 0x405a50 IsOpen
    void setYouAreHere(int location);                  // 0x4055f0

private:
    static constexpr int kSpots = 7;
    void hideAll();          // 0x405530
    void destroyRollovers(); // 0x4055b0

    int m_youAreHere = 0;                    // +0x04
    bool m_in = false;                       // +0x08
    ToolbarNode* m_node = nullptr;           // +0x0C
    engine::Anim* m_bg = nullptr;            // +0x10
    // +0x14..+0x2C: stable, apple, turtle, rainbow, bridge, rabbit, rabbit2.
    std::array<engine::Anim*, kSpots> m_spots{};
    std::array<std::unique_ptr<MapVoiceRollover>, kSpots> m_rollovers; // +0x30..+0x48
    engine::Anim* m_hideWell = nullptr;      // +0x4C
    engine::Anim* m_hideCottage = nullptr;   // +0x50
    engine::Anim* m_youAreHereAnim = nullptr; // +0x54: Out palette only
};

// BarbieCamera (app+0x184): toolbar camera photos in `Photos/photos.omt<slot>`.
// TODO(popups): photo storage is not ported yet; takePhoto reports failure.
namespace camera {
bool takePhoto(Godiva& app);  // 0x404200: true if the photo was stored
void deletePhotoFile(int slot); // 0x404e50
} // namespace camera

} // namespace brc::game
