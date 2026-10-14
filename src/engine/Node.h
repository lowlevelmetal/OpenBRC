#pragma once

#include "engine/Broadcaster.h"
#include "engine/Image8.h"
#include "engine/StopClock.h"
#include "engine/Task.h"
#include "formats/Canvas.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace brc::engine {

class Anim;
class Element;
class Engine;
class Script;

// Script-event messages on the engine's script broadcaster (scripts.md "Messages").
namespace msg {
inline constexpr int kClick = 0x64;
inline constexpr int kPaused = 0x66;
inline constexpr int kResumed = 0x67;
inline constexpr int kTick = 0x68;
inline constexpr int kNodeEntered = 0x69;
inline constexpr int kSoundDone = 0x6a;
inline constexpr int kMovieDone = 0x6b;
inline constexpr int kAnimDone = 0x6c;
inline constexpr int kScriptDone = 0x6d;
inline constexpr int kInactivity = 50000; // the application's inactivity timer fired
} // namespace msg

// A translated input event (input.md §2).
struct InputEvent {
    enum Type { LeftDown = 1, LeftUp = 2, RightDown = 3, RightUp = 4, KeyDown = 5, KeyUp = 6, Paint = 7 };
    int type = 0;
    std::uint16_t modifiers = 0; // bit 0 Shift, 0xA Alt, bit 2 Ctrl
    int key = 0;                 // key code below, 0 for characters
    std::uint8_t ch = 0;         // typed character (WM_CHAR), 0 otherwise
    int x = 0, y = 0;            // pointer position in game pixels
};

namespace key {
inline constexpr int kEscape = 1, kBackspace = 2, kEnter = 3, kDelete = 4, kLeft = 6, kRight = 7, kUp = 8,
                     kDown = 9, kTab = 10;
inline constexpr std::uint16_t kShift = 0x1, kCtrl = 0x4, kAlt = 0xA;
} // namespace key

// Result of picking at a point (the pick request of input.md §4.2).
struct Pick {
    enum Result { None = 0, Background = 1, Image = 2, NoImage = 4 };
    int result = None;
    Element* element = nullptr;
    int x = 0, y = 0;    // view coordinates
    std::uint16_t modifiers = 0;
    bool notLeftPress = false; // +0x1e bit 0
    int eventType = 0;
};

// A scene: HCNode (HCNode.cpp) on an OMT world. Holds the elements linked to
// it (sorted by depth), the background canvas, the view camera and the world
// clock; is an idle task; and has the virtual hooks scenes override.
// See docs/engine/application.md §3, §6 and display.md §3.
class Node : public Listener, public Task {
public:
    // HCNode::HCNode (0x454320): registers as the current node, installs the
    // palette of "<name> background" (or `backgroundName`) as the screen
    // palette and the background canvas.
    Node(Engine& engine, std::string name, std::string_view backgroundName = {});
    ~Node() override;

    Engine& engine() const { return m_engine; }
    const std::string& name() const { return m_name; }

    // --- world ---------------------------------------------------------------------
    void addElement(Element* e);
    void removeElement(Element* e);
    void markUnsorted() { m_unsorted = true; }
    const std::vector<Element*>& elements() const { return m_elements; }
    void setBackground(std::shared_ptr<const Image8> background);
    const Image8* background() const { return m_background.get(); }
    // The background palette (node+0x98), which became the screen palette.
    const formats::Palette& palette() const { return m_palette; }

    float cameraX() const { return m_camX; }
    float cameraY() const { return m_camY; }
    void setCamera(float x, float y) { m_camX = x, m_camY = y; }
    // The view's mover (view vt+8 SetSpeed, units per second; 0 stops it): the
    // camera moves by the frame delta before the elements update (display.md
    // §3 step 2). The race scrolls with Barbie's horse this way (0x426940).
    void setCameraSpeed(float unitsPerSecondX, float unitsPerSecondY) {
        m_camSx = static_cast<float>(unitsPerSecondX * 0.001);
        m_camSy = static_cast<float>(unitsPerSecondY * 0.001);
    }
    const Rect& viewRect() const { return m_view; }

    // --- clock and updates (application.md §6.3) ---------------------------------
    void setUpdateMode(int mode) { m_updateMode = mode; }    // vt+0x14: 0, 1 or 2 (adaptive)
    int updateMode() const { return m_updateMode; }
    void setFrameInterval(std::uint32_t ms) { m_interval = ms; } // vt+0x18
    std::uint32_t frameInterval() const { return m_interval; }
    void setMaxDelta(std::uint32_t ms) { m_maxDelta = ms; }
    int pauseCount() const { return m_pauseCount; }
    // vt+0x2c: counter; stops the clock and pauses every element; broadcasts
    // script events 0x66 / 0x67.
    virtual void pause(bool on);
    void restartClock();
    std::uint32_t clockElapsed() const;
    // World::Idle (0x45cf60): update and draw a frame when it is time.
    void worldIdle();
    // UpdateFrame (0x456d50 / 0x45cff0): update elements, sort, draw, present.
    void updateFrame(bool force);
    // Draws the view into `target` (background, then visible elements by depth).
    void render(Image8& target) const;
    // Point pick (0x495c40): frontmost pickable element under (x, y).
    Pick pick(int x, int y) const;

    // The mouse relay (node+0xa0, 0x492cb0): broadcasts kMousePress (-600, only
    // when something was hit) and kMouseRelease (-601, always) with the Pick to
    // the node itself first, then to the buttons that listen to it.
    Broadcaster& mouseRelay() { return m_mouseRelay; }
    static constexpr int kMousePress = -600;
    static constexpr int kMouseRelease = -601;

    bool inputDisabled() const { return m_inputDisabled; } // +0x9c
    void setInputDisabled(bool d) { m_inputDisabled = d; }

    // GetAnim (0x41d100): get or create an anim and put it at the view's origin.
    Anim* getAnim(std::string_view name, std::string_view newName = {});

    // --- scripts (HCNode.cpp) -----------------------------------------------------
    bool runScript(std::string_view name);  // 0x455890
    bool stopScript(std::string_view name); // 0x455c10
    void stopAllScripts();                  // 0x455bb0
    bool isScriptRunning(std::string_view name) const; // 0x455840
    bool setScriptVar(std::string_view script, std::string_view var, std::string_view value); // 0x455cb0
    bool setScriptVar(std::string_view script, std::string_view var, double value);           // 0x455d60

    // --- the idle task: HCNode tick 0x456f30 ----------------------------------------
    void spendTime() override { tick(); }
    // Game nodes override this to poll the mouse first (0x41cba0) and then call it.
    virtual void tick();

    // --- hooks (HCNode vtable 0x7f45f8) ---------------------------------------------
    virtual void start();                                   // vt+0x48: broadcasts 0x69
    virtual void leaving(const std::string& nextNode);      // vt+0x4c
    virtual void quitting() {}                              // vt+0x50
    virtual void onPress(const Pick& pick);                 // vt+0x54: 0x64 on an image
    virtual void onRelease(const Pick&) {}                  // vt+0x58
    virtual void onMouseDown(const InputEvent&) {}          // vt+0x5c (left) / +0x64 (right)
    virtual void onMouseUp(const InputEvent&) {}            // vt+0x60 / +0x68
    virtual void onKeyDown(const InputEvent&) {}            // vt+0x6c
    virtual void onKeyUp(const InputEvent& e);              // vt+0x70
    virtual void sequenceStarted(Anim&, int) {}             // vt+0x74
    virtual void sequenceEnded(Anim& anim, int seq);        // vt+0x78: posts 0x6c
    virtual void frameEvent(Anim&, int, int) {}             // vt+0x7c
    virtual void soundDone(const std::string& name, const void* sound, bool completed); // vt+0x80: posts 0x6a
    virtual void movieDone(const std::string& movie);       // vt+0x84: posts 0x6b
    virtual void scriptStarted(const std::string&) {}       // vt+0x88
    virtual void scriptPreempted(Script& script);           // vt+0x8c: posts 0x6d
    virtual void scriptEnded(Script& script);               // vt+0x90: posts 0x6d
    virtual void scriptNumber(const std::string&, float) {} // vt+0x94
    virtual void scriptMessage(Script&, std::string_view, int) {} // vt+0x98
    // The node's listener (+0x18, 0x454b50): relay clicks become onPress /
    // onRelease (unless input is disabled or only the background was hit);
    // everything else (timers, the inactivity timer 50000, ...) goes to onMessage.
    void listen(int message, const MessageData& data) override;
    virtual void onMessage(int, const MessageData&) {}

protected:
    void setPalette(const formats::Palette& p) { m_palette = p; }

private:
    bool timeToUpdate();
    void sortElements();

    Engine& m_engine;
    std::string m_name;
    std::vector<Element*> m_elements; // ascending depth after sorting
    bool m_unsorted = false;
    std::shared_ptr<const Image8> m_background;
    formats::Palette m_palette;
    Rect m_view{0, 0, 640, 480};
    float m_camX = 0, m_camY = 0;
    float m_camSx = 0, m_camSy = 0; // camera speed per millisecond

    StopClock m_clock;
    int m_updateMode = 2;
    std::uint32_t m_interval = 40;
    std::uint32_t m_sampleCount = 0, m_sampleSum = 0;
    std::uint32_t m_maxDelta = 500;
    int m_pauseCount = 0;
    bool m_forceRedraw = true;
    bool m_inputDisabled = false;
    Broadcaster m_mouseRelay;
};

} // namespace brc::engine
