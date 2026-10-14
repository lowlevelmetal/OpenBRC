#include "game/Widgets.h"

#include "engine/Anim.h"
#include "engine/Engine.h"
#include "engine/Node.h"
#include "formats/HcAnim.h"
#include "game/ToolbarNode.h"

namespace brc::game {
namespace {

int g_rollovers = 0;   // [0x80a13c]
int g_acRollovers = 0; // [0x809930]

// The pointer in view coordinates (the toolbar's +0xfa test: the original
// subtracts the view origin unless the mouse is already layer-relative).
std::pair<int, int> viewMouse(engine::Node& node) {
    auto [x, y] = node.engine().mousePosition();
    return {x - node.viewRect().left, y - node.viewRect().top};
}

} // namespace

// --- Rollover ------------------------------------------------------------------------

Rollover::Rollover(engine::Node& owner) : engine::Task(owner.engine().tasks()), m_owner(owner) { ++g_rollovers; }

Rollover::~Rollover() { --g_rollovers; }

int Rollover::liveCount() { return g_rollovers; }

void Rollover::setAnim(engine::Anim* anim) {
    m_enabled = true;
    m_anim = anim;
    m_animName = anim ? anim->name() : std::string();
}

void Rollover::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled && m_inside)
        onRollOut();
}

void Rollover::checkPoint(int x, int y) {
    if (!m_anim || !m_enabled || m_anim->motionPauseCount() != 0 || !m_anim->visible())
        return;
    const bool was = m_inside;
    m_inside = m_anim->hitTest(x, y, m_owner.cameraX(), m_owner.cameraY());
    if (m_inside != was) {
        if (m_inside)
            onRollIn();
        else
            onRollOut();
    }
}

void Rollover::spendTime() {
    if (!m_anim || !m_enabled || m_anim->motionPauseCount() != 0 || !m_anim->visible())
        return;
    const auto [x, y] = viewMouse(m_owner);
    checkPoint(x, y);
}

// --- RolloverButton ---------------------------------------------------------------------

class RolloverButton::Tracker final : public Rollover {
public:
    Tracker(engine::Node& owner, RolloverButton& button) : Rollover(owner), m_button(button) {}

protected:
    void onRollIn() override { m_button.onMouseEnter(); }
    void onRollOut() override { m_button.onMouseLeave(); }

private:
    RolloverButton& m_button;
};

RolloverButton::RolloverButton(engine::Node& owner, bool noViewportOffset)
    : m_owner(owner), m_tracker(std::make_unique<Tracker>(owner, *this)), m_noViewport(noViewportOffset) {
    // 0x40ec40: listen to the node's mouse relay (node+0xa0).
    m_owner.mouseRelay().addListener(this);
}

RolloverButton::~RolloverButton() {
    // 0x40ed00: DeleteAnim(+0xac) by pointer.
    if (m_anim)
        m_owner.engine().anims().remove(m_anim);
}

engine::Anim* RolloverButton::releaseAnim() {
    engine::Anim* a = m_anim;
    m_anim = nullptr;
    m_tracker->setAnim(nullptr);
    m_tracker->setEnabled(false);
    return a;
}

void RolloverButton::init(std::string_view resource, std::string_view instance) {
    // 0x40ed80.
    m_seqNormal = 0;
    m_seqRollover = 1;
    m_seqPressed = 2;
    m_anim = m_noViewport ? m_owner.engine().anims().getOrCreate(resource, instance)
                          : m_owner.getAnim(resource, instance);
    if (!m_anim)
        return;
    // The last frame of sequences 0..2 holds (stop flag).
    for (int s = 0; s < 3; ++s)
        m_anim->addFrameFlags(s, m_anim->frameCount(s) - 1, formats::anim_flags::kFrameStop);
    setSequence(m_seqNormal);
    m_tracker->setAnim(m_anim);
    m_anim->pause(false);
}

void RolloverButton::setEnabled(bool enabled) {
    // 0x40f240.
    if (m_anim)
        while (m_anim->visible() != enabled)
            m_anim->hide(!enabled);
    m_tracker->setEnabled(enabled);
    if (enabled && m_anim)
        m_anim->setSequence(0, true);
}

void RolloverButton::setSequence(int seq) {
    if (m_anim && m_anim->motionPauseCount() == 0 && m_anim->visible())
        m_anim->setSequence(seq, true);
}

void RolloverButton::press() {
    m_pressed = true;
    setSequence(m_seqPressed);
}

void RolloverButton::release() {
    m_pressed = false;
    setSequence(m_seqNormal);
}

void RolloverButton::onMouseEnter() { setSequence(m_pressed ? m_seqPressed : m_seqRollover); }

void RolloverButton::onMouseLeave() { setSequence(m_pressed ? m_seqRollover : m_seqNormal); }

void RolloverButton::listen(int message, const engine::MessageData&) {
    // 0x40f390: feedback only.
    if (!m_anim || m_anim->motionPauseCount() != 0 || !m_anim->visible())
        return;
    if (message == engine::Node::kMouseRelease) {
        if (m_pressed)
            release();
    } else if (message == engine::Node::kMousePress) {
        m_tracker->spendTime(); // CheckMouse
        if (m_tracker->inside())
            press();
        else
            release();
    }
}

// --- ACRollover ------------------------------------------------------------------------

ACRollover::ACRollover(ToolbarNode& node, engine::Anim* anim, int cursorSeq)
    : engine::Task(node.engine().tasks()), m_node(node), m_anim(anim), m_cursorSeq(cursorSeq) {
    ++g_acRollovers;
}

ACRollover::~ACRollover() {
    // 0x4011f0: the cursor goes back to its normal sequence (and stays hidden
    // if it was hidden: SetSequence shows it).
    if (engine::Anim* cursor = m_node.cursor()) {
        const bool hidden = !cursor->visible();
        cursor->setSequence(0, true);
        if (hidden)
            cursor->hide(true);
    }
    --g_acRollovers;
}

int ACRollover::liveCount() { return g_acRollovers; }

void ACRollover::spendTime() {
    // 0x401290.
    if (!m_anim || !m_anim->visible())
        return;
    const auto [x, y] = viewMouse(m_node);
    const bool was = m_hover;
    m_hover = m_anim->hitTest(x, y, m_node.cameraX(), m_node.cameraY());
    if (m_hover == was)
        return;
    engine::Anim* cursor = m_node.cursor();
    if (!cursor)
        return;
    if (!m_hover) {
        if (cursor->sequenceCount() > 0)
            cursor->setSequence(0, true);
    } else if (m_cursorSeq < cursor->sequenceCount()) {
        cursor->setSequence(m_cursorSeq, true);
    }
}

MapVoiceRollover::MapVoiceRollover(ToolbarNode& node, engine::Anim* anim, std::string voice)
    : ACRollover(node, anim, 7), m_voice(std::move(voice)) {}

void MapVoiceRollover::spendTime() {
    // 0x405300.
    ACRollover::spendTime();
    if (!hovering()) {
        m_armed = true;
    } else if (m_armed && !m_voice.empty() && !m_node.isVoiceChannelBusy()) {
        m_armed = false;
        m_node.playVoice(m_voice);
    }
}

// --- ACButton --------------------------------------------------------------------------

ACButton::ACButton(ToolbarNode& node) : RolloverButton(node, false), m_node(node) {}

ACButton::~ACButton() { m_rollover.reset(); }

void ACButton::init(std::string_view resource, std::string_view instance) {
    RolloverButton::init(resource, instance);
    m_rollover = std::make_unique<ACRollover>(m_node, anim(), 7);
}

} // namespace brc::game
