#pragma once

#include "engine/Node.h"
#include "game/Godiva.h"

#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace brc::engine {
class Anim;
}

namespace brc::game {

class BarbiePager;
class BarbieSaddlebag;
class Popup;
class RolloverButton;

// HCToolbarNode (HCToolbarNode.cpp, ctor 0x41b690): the base of every
// interactive scene. It adds the toolbar (Quit, Pager, Camera, Map, Bag, Help,
// Home), the toolbar popups, the exit dialog, Barbie's voice with ducking and
// mouth flaps, the cursor anim with its edge arrows, panorama scrolling and the
// script commands every scene shares. See docs/engine/classes.md
// "HCToolbarNode". Method comments give the original addresses.
class ToolbarNode : public engine::Node {
public:
    // Toolbar skins (+0x108): the suffix of the shared HUD anims.
    enum Skin { kIn = 0, kOut = 1, kFrog = 2, kSign = 3, kVine = 4 };
    // Toolbar button ids (anim element id +0x68).
    enum ButtonId { kQuit = 0, kPager = 1, kCamera = 2, kMap = 3, kBag = 4, kHelp = 5, kHome = 6 };
    // Click id of the Help button (handled by each scene's own onPress).
    static constexpr int kHelpClickId = 0x32CE;
    // Messages the toolbar posts to its own listener (and so to onMessage).
    static constexpr int kMsgEdgeScroll = 13000;  // left/right edge click with edgeScroll
    static constexpr int kMsgEdgeLeft = 0x32C9;   // left edge click
    static constexpr int kMsgEdgeRight = 0x32CA;  // right edge click
    static constexpr int kMsgEdgeRightAlt = 0x32CC;
    static constexpr int kMsgCamera = 0x32CD;     // the camera button was clicked
    // Exit dialog click ids.
    static constexpr int kExitYes = 0x32CF, kExitNo = 0x32D0, kExitCredits = 0x32D1;

    ToolbarNode(Godiva& app, const std::string& name);
    ~ToolbarNode() override;

    Godiva& app() const { return m_app; } // +0x1B8

    // --- skin, cursor, toolbar -------------------------------------------------------
    void setSkin(int skin) { m_skin = skin; } // +0x108
    int skin() const { return m_skin; }
    std::string_view skinSuffix() const; // 0x41d150: "In", "Out", "Frog", "Sign", "Vine"
    void setCursor(std::string_view name); // 0x41beb0
    engine::Anim* cursor() const { return m_cursor; } // +0xEC
    // 0x41bf40: a toolbar button on `anim` with `id` (ButtonId): depth 3000 at
    // the view origin, rectangle hit test. Pager (1) and Bag (4) create their popups.
    RolloverButton* addToolbarButton(std::string_view anim, int id);
    void showToolbar(bool show, bool redraw); // 0x41c190
    void moveRollovers(float x, float y);     // 0x41c1f0
    RolloverButton* findButtonById(int id);   // 0x41c4f0
    void onToolbarButton(const engine::Pick& pick); // 0x41c530
    void takePhoto(const engine::Pick& pick);       // 0x41c840
    bool skipNextPhoto = false;                     // +0x194

    // --- popups ------------------------------------------------------------------------
    void openPopup(int kind);   // 0x41c760: 1 pager, 3 map, 4 saddlebag
    void closePopup();          // 0x41c7e0
    void popupMouseDown(const engine::Pick& pick); // 0x41c810
    int popupKind() const { return m_popupKind; }  // +0xF4
    Popup* popup() const { return m_popup; }       // +0xF0
    BarbiePager* pager() const { return m_pager.get(); }          // +0xD8
    BarbieSaddlebag* saddlebag() const { return m_saddlebag.get(); } // +0xD4

    // --- exit dialog and quitting ----------------------------------------------------------
    virtual void showExitDialog(bool save); // +0xD0 (0x41e150)
    virtual void quitGame();                // +0xCC (0x41e630)
    bool exitDialogShowing() const { return m_exitDialog; } // +0x19C
    void closeExitDialog();

    // --- pointer, scrolling, edges -----------------------------------------------------------
    // +0xD4 (0x41dda0): the pointer in view coordinates.
    virtual std::pair<float, float> mouseWorldPos() const;
    // The pointer as the last tick saw it (+0x168/+0x16C, layer coordinates).
    float mouseX() const { return m_mouseX; }
    float mouseY() const { return m_mouseY; }
    void setScrollVelocity(float vx, float vy); // 0x41cb40
    void setPanLimits(int left, int right) { m_panLeft = left, m_panRight = right; } // +0x164 / +0x160
    // Edge cursor / edge click modes (cleared by setCursor, except edgeRightAlt).
    bool edgeScroll = false;    // +0xC4: arrows 1/2, message 13000 on both edges
    bool edgeBoth = false;      // +0xC6: arrows 3/4, messages 0x32C9 / 0x32CA
    bool edgeRightAlt = false;  // +0xC8: arrow 6, message 0x32CC on the right
    bool edgeLeft = false;      // +0xCA: arrow 3, message 0x32C9 on the left
    bool edgeRight = false;     // +0xCC: arrow 4, message 0x32CA on the right
    // Registers the node with the application's mouse-button broadcaster, as
    // the scenes that use edge clicks do in their constructors (input.md §4.3).
    void listenToMouseButtons();

    // --- mini-games ------------------------------------------------------------------------
    bool miniGame = false;      // +0xCE: Pager/Map/Bag leave the game instead
    int miniGameReturn = 0;     // +0xD0: node-name table index to return to
    bool pagerClickEnabled = true; // +0x1B4
    bool closePagerRequested = false; // +0x170
    bool rabbitLatch = false;   // +0x1BC: go to rabbitE when DB9011 ends

    // --- navigation ---------------------------------------------------------------------------
    virtual void gotoNodeById(int id); // +0xD8 (0x41de40)
    void gotoNode(std::string_view name) { m_app.gotoNode(name); }

    // --- anims ------------------------------------------------------------------------------
    engine::Anim* createAnim(std::string_view name, std::string_view instance = {}); // 0x41d0e0
    engine::Anim* createAnimAtViewport(std::string_view name, std::string_view instance = {}); // 0x41d100
    // 0x41d980 GetCollision (with the original's last test of A at its own centre).
    bool getCollision(engine::Anim* a, engine::Anim* b);
    // 0x41db70: all five points of B (corners and centre) hit A.
    bool getCollisionContains(engine::Anim* a, engine::Anim* b);

    // --- saddlebag ---------------------------------------------------------------------------
    void addBagItem(int item, bool quiet = false); // 0x41d1a0
    bool hasBagItem(int item) const;                // 0x41d200
    void removeBagItem(int item);                   // 0x41d220

    // --- voice, sound and scripts -----------------------------------------------------------
    void runDelay(int ms);                   // 0x41d250: the "Delay" script
    void playVoice(std::string_view name);   // 0x41d270: channel 14, preempt, flaps
    void playVoiceNoFlaps(std::string_view name); // 0x41d330: queued on channel 14
    void playVoiceAfterPause(std::string_view name); // 0x41d310: "pause", then name
    void delayVoice(std::string_view name, int ms);  // 0x41d3c0: the "DelayVoice" script
    bool pollVoiceDone();                    // 0x41d3f0
    void stopVoice();                        // 0x41d480
    void updateMouthFlaps(bool on);          // 0x41d510
    void stopMusic();                        // 0x41d860: channel 13
    // The node's ambient loop (+0x178): started with playAmbientLoop and
    // released by releaseAmbient (0x41d870) when the node ends.
    void playAmbientLoop(std::string_view name, int volume = 0x28);
    void releaseAmbient();                   // 0x41d870
    const std::string& ambientLoop() const { return m_ambient; }
    virtual void sayHorseName();             // +0xC8 (0x41e720)
    void sayHorseNameThen(std::string_view name); // 0x41d8b0
    std::string horseNameSound() const;      // 0x41e670: "DB0%d"
    bool isVoiceChannelBusy() const;         // 0x41e8c0
    bool voicePlaying() const { return m_voicePlaying; } // +0x17C
    void playHelpHint();                     // 0x41e8f0
    bool checkAllMissionsDone();             // 0x41e7e0
    void choosePagerMessage();               // 0x41df10
    int delayMs = 0;                         // +0x190: for the "Delay" script

    // --- engine hooks ---------------------------------------------------------------------------
    void tick() override;                                   // 0x41cba0
    void leaving(const std::string& nextNode) override;     // +0x4C (0x41e8d0)
    void onPress(const engine::Pick& pick) override;        // +0x54 (0x41c340)
    void onRelease(const engine::Pick& pick) override;      // +0x58 (0x41c450)
    void onKeyDown(const engine::InputEvent& e) override;   // +0x6C (0x41c310)
    void frameEvent(engine::Anim& anim, int seq, int index) override; // +0x7C (0x41c270)
    void soundDone(const std::string& name, const void* sound, bool completed) override; // +0x80 (0x41c460)
    void scriptEnded(engine::Script& script) override;      // +0x90 (0x41dc80)
    void scriptMessage(engine::Script& script, std::string_view message, int counter) override; // +0x98 (0x41dcd0)
    void listen(int message, const engine::MessageData& data) override; // +0x18 (0x41c910)

protected:
    // HCToolbarNode::MouseDown (0x41c340), also used for MouseUp.
    void toolbarMouseDown(const engine::Pick& pick);

private:
    Godiva& m_app;
    int m_skin = kIn;
    engine::Anim* m_cursor = nullptr;
    std::list<std::unique_ptr<RolloverButton>> m_buttons; // +0xDC
    std::unique_ptr<BarbiePager> m_pager;
    std::unique_ptr<BarbieSaddlebag> m_saddlebag;
    Popup* m_popup = nullptr;
    int m_popupKind = 0;

    bool m_exitDialog = false;      // +0x19C
    engine::Anim* m_exitBg = nullptr;     // +0x1A0
    engine::Anim* m_exitBorder = nullptr; // +0x1A4
    std::unique_ptr<RolloverButton> m_exitYes, m_exitNo, m_exitCredits; // +0x1A8..+0x1B0

    float m_mouseX = 0, m_mouseY = 0; // +0x168 / +0x16C
    float m_scrollVx = 0, m_scrollVy = 0; // +0x158 / +0x15C
    std::uint32_t m_scrollTick = 0;  // +0x114
    int m_panRight = 640, m_panLeft = 0;

    std::string m_ambient;          // +0x178
    bool m_voicePlaying = false;    // +0x17C
    std::string m_delayedVoice;     // +0x180
    int m_delayedVoiceMs = 0;       // +0x184
    bool m_flapsActive = false;     // +0x188
    engine::Anim* m_flaps = nullptr; // +0x18C
};

} // namespace brc::game
