#pragma once

#include "engine/Broadcaster.h"
#include "engine/Task.h"

#include <memory>
#include <string>
#include <string_view>

namespace brc::engine {
class Anim;
class Node;
} // namespace brc::engine

namespace brc::game {

class ToolbarNode;

// Rollover (0x4269e0): an idle task that polls the mouse every pass and calls
// onRollIn / onRollOut when the pointer enters or leaves an anim's opaque area.
// See docs/engine/classes.md "Rollover".
class Rollover : public engine::Task {
public:
    explicit Rollover(engine::Node& owner);
    ~Rollover() override;

    void setAnim(engine::Anim* anim);   // 0x426a80: also enables
    void setEnabled(bool enabled);      // 0x426ad0: disabling while inside rolls out
    bool enabled() const { return m_enabled; }
    bool inside() const { return m_inside; }
    engine::Anim* anim() const { return m_anim; }
    // 0x426af0: hit-test the anim at view coordinates (x, y), firing on a change.
    void checkPoint(int x, int y);
    void spendTime() override; // 0x426b90: the mouse position, then checkPoint

    static int liveCount(); // [0x80a13c]

protected:
    virtual void onRollIn() {}  // vt+0x08
    virtual void onRollOut() {} // vt+0x0c

    engine::Node& m_owner;

private:
    engine::Anim* m_anim = nullptr;
    bool m_enabled = false;
    bool m_inside = false;
    std::string m_animName = "No anim initialized";
};

// HCRolloverButton (0x40ec40): a 3-state button on one anim, sequences
// 0 normal, 1 rollover, 2 pressed. It listens to the node's mouse relay for
// visual feedback only; the node dispatches the click itself by the anim's
// click id (anim+0x11c). See docs/engine/classes.md "HCRolloverButton".
class RolloverButton : public engine::Listener {
public:
    // `noViewportOffset` (+0xc4): create the anim with CreateAnim (0x41d0e0)
    // instead of CreateAnimAtViewport (0x41d100).
    explicit RolloverButton(engine::Node& owner, bool noViewportOffset = false);
    ~RolloverButton() override; // deletes the anim unless releaseAnim() was called

    // vt+0x0c Init(resource, instance) (0x40ed80): creates the anim, marks the
    // last frame of sequences 0..2 as stop frames, shows sequence 0.
    virtual void init(std::string_view resource, std::string_view instance = {});
    void setEnabled(bool enabled);   // 0x40f240
    void setSequence(int seq);       // 0x40f2b0: unless paused or hidden
    void press();                    // 0x40f2f0
    void release();                  // 0x40f310
    bool pressed() const { return m_pressed; }
    // The sequences for normal / rollover / pressed (+0xb4 / +0xb8 / +0xbc),
    // which some scenes rewrite after Init (Leapfrog's lily pads: 1 / 2 / 0).
    void setSequences(int normal, int rollover, int pressed) {
        m_seqNormal = normal, m_seqRollover = rollover, m_seqPressed = pressed;
    }
    engine::Anim* anim() const { return m_anim; }
    // Forget the anim so the destructor leaves it alone (callers zero +0xac).
    engine::Anim* releaseAnim();

    void listen(int message, const engine::MessageData& data) override; // 0x40f390

protected:
    engine::Node& m_owner;

private:
    class Tracker;
    void onMouseEnter(); // 0x40f330
    void onMouseLeave(); // 0x40f360

    std::unique_ptr<Tracker> m_tracker; // the Rollover base at +0x10
    engine::Anim* m_anim = nullptr;      // +0xac
    int m_seqNormal = 0, m_seqRollover = 1, m_seqPressed = 2;
    bool m_pressed = false;              // +0xc0
    bool m_noViewport = false;           // +0xc4
};

// ACRollover (0x401160): shows the hand cursor (cursor sequence 7) while the
// pointer is over one element of a toolbar node.
class ACRollover : public engine::Task {
public:
    ACRollover(ToolbarNode& node, engine::Anim* anim, int cursorSeq = 7);
    ~ACRollover() override; // 0x4011f0: cursor back to sequence 0
    void spendTime() override; // 0x401290
    bool hovering() const { return m_hover; }
    // A new cursor sequence (+0x14), with the hover state (+0x18) forgotten so
    // the next pass shows it, as the trail missions do (M4_MouseDown 0x4423d0,
    // M6_MouseDown 0x443710).
    void setCursorSequence(int seq) {
        m_cursorSeq = seq;
        m_hover = false;
    }

    static int liveCount(); // [0x809930]

protected:
    ToolbarNode& m_node;
    engine::Anim* m_anim;
    int m_cursorSeq;
    bool m_hover = false;
};

// MapVoiceRollover (vtable 0x7f0230): an ACRollover that also plays a voice
// line once each time the pointer enters (when no voice is playing).
class MapVoiceRollover final : public ACRollover {
public:
    MapVoiceRollover(ToolbarNode& node, engine::Anim* anim, std::string voice);
    void spendTime() override; // 0x405300

private:
    std::string m_voice;
    bool m_armed = true;
};

// ACButton (0x401000): a RolloverButton with an ACRollover for the hand cursor.
class ACButton final : public RolloverButton {
public:
    explicit ACButton(ToolbarNode& node);
    ~ACButton() override; // 0x4010e0
    void init(std::string_view resource, std::string_view instance = {}) override; // 0x401060

private:
    ToolbarNode& m_node;
    std::unique_ptr<ACRollover> m_rollover; // +0xc8
};

} // namespace brc::game
