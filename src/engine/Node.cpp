#include "engine/Node.h"

#include "core/Log.h"
#include "engine/Anim.h"
#include "engine/Element.h"
#include "engine/Engine.h"
#include "engine/Scripts.h"

#include <algorithm>
#include <format>

namespace brc::engine {

Node::Node(Engine& engine, std::string name, std::string_view backgroundName)
    : Task(engine.tasks()), m_engine(engine), m_name(std::move(name)) {
    // HCNode::HCNode (0x454320).
    m_engine.setCurrentNode(this);
    const std::string bg = backgroundName.empty() ? m_name + " background" : std::string(backgroundName);
    if (auto pal = m_engine.resources().canvasPalette(bg)) {
        m_palette = *pal;
        m_engine.setScreenPalette(m_palette.rgb);
        m_engine.setGamePalette(m_palette);
    } else {
        m_palette = m_engine.gamePalette();
        log::warn("node {}: no background canvas '{}'", m_name, bg);
    }
    if (auto img = m_engine.resources().image(bg, m_palette))
        setBackground(std::move(img));
}

Node::~Node() {
    // 0x454880: anims linked to the node and scripts that die with it go too.
    m_engine.anims().deleteLinkedTo(this);
    m_engine.scripts().deleteForNode(*this);
    for (Element* e : std::vector<Element*>(m_elements))
        e->unlink();
    if (m_engine.currentNode() == this)
        m_engine.setCurrentNode(nullptr);
}

// --- world ------------------------------------------------------------------------

void Node::addElement(Element* e) {
    if (std::ranges::find(m_elements, e) == m_elements.end()) {
        m_elements.push_back(e);
        m_unsorted = true;
    }
}

void Node::removeElement(Element* e) { std::erase(m_elements, e); }

void Node::setBackground(std::shared_ptr<const Image8> background) {
    m_background = std::move(background);
    m_forceRedraw = true;
}

void Node::sortElements() {
    // Sort mode 3 (0x466ff0): repeated bubble passes swapping neighbours when
    // next.z < current.z, i.e. ascending depth, equal depths keep their order.
    std::ranges::stable_sort(m_elements, [](const Element* a, const Element* b) { return a->z() < b->z(); });
    m_unsorted = false;
}

// --- clock and updates --------------------------------------------------------------

void Node::restartClock() { m_clock.restart(m_engine.ticks()); }
std::uint32_t Node::clockElapsed() const { return m_clock.elapsed(m_engine.ticks()); }

void Node::pause(bool on) {
    // World pause 0x45d2f0 + HCNode 0x454bc0.
    const std::uint32_t now = m_engine.ticks();
    if (on) {
        if (++m_pauseCount == 1) {
            m_clock.stop(now);
            for (Element* e : std::vector<Element*>(m_elements))
                e->pause(true);
            m_engine.scriptEvents().broadcast(msg::kPaused);
        }
    } else if (m_pauseCount > 0) {
        if (--m_pauseCount == 0) {
            m_clock.restart(now);
            for (Element* e : std::vector<Element*>(m_elements))
                e->pause(false);
            m_engine.scriptEvents().broadcast(msg::kResumed);
        }
    }
}

bool Node::timeToUpdate() {
    // 0x45cdf0. Called while the clock is frozen, so elapsed() is the frame delta.
    if (m_pauseCount != 0)
        return false;
    const std::uint32_t now = m_engine.ticks();
    if (m_clock.stopped)
        m_clock.restart(now);
    const std::uint32_t e = m_clock.elapsed(now);
    if (m_updateMode == 2) {
        // Interval estimator 0x45cee0.
        if (e == 0) {
            m_interval = 20;
            m_sampleCount = m_sampleSum = 0;
        } else if (m_sampleCount == 16) {
            m_interval = m_sampleSum / 16;
            m_sampleCount = m_sampleSum = 0;
        } else if (e >= 500 && m_sampleCount != 0) {
            m_interval = m_sampleSum / m_sampleCount;
            m_sampleCount = m_sampleSum = 0;
        } else {
            ++m_sampleCount;
            m_sampleSum += e;
        }
    }
    if ((m_updateMode == 0 || m_updateMode == 2) && e < m_interval)
        return false;
    m_clock.restart(now); // keeps the frozen delta for this update
    return true;
}

void Node::worldIdle() {
    // World::Idle (0x45cf60): freeze the frame delta (time since the last
    // update, clamped to 500 ms), update and draw when it is time, unfreeze.
    if (!m_clock.frozen) {
        m_clock.frozenValue = m_clock.elapsed(m_engine.ticks());
        m_clock.frozen = true;
    }
    if (m_maxDelta != 0 && m_clock.frozenValue > m_maxDelta)
        m_clock.frozenValue = m_maxDelta;
    if (timeToUpdate())
        updateFrame(false);
    m_clock.frozen = false;
}

void Node::updateFrame(bool force) {
    // HCNode::UpdateFrame (0x456d50) -> OMediaWorld::UpdateFrame (0x45cff0).
    if (m_engine.modal())
        return;
    if (force)
        m_forceRedraw = true;
    if (m_pauseCount == 0) {
        const std::uint32_t delta = std::min(m_clock.elapsed(m_engine.ticks()), m_maxDelta ? m_maxDelta : UINT32_MAX);
        for (Element* e : std::vector<Element*>(m_elements)) {
            if (std::ranges::find(m_elements, e) == m_elements.end())
                continue; // removed by an earlier element's update
            e->update(delta);
        }
        for (Element* e : std::vector<Element*>(m_elements))
            if (e->deleteMe())
                if (auto* a = dynamic_cast<Anim*>(e))
                    m_engine.anims().remove(a);
        if (m_unsorted)
            sortElements();
    }
    if (m_pauseCount == 0 || force) {
        if (m_unsorted)
            sortElements();
        render(m_engine.screen());
        m_engine.present();
    }
    m_forceRedraw = false;
}

void Node::render(Image8& target) const {
    if (m_background) {
        // Opaque copy of the canvas area under the view, offset by the camera.
        blit(target, *m_background, m_view.left - static_cast<int>(m_camX), m_view.top - static_cast<int>(m_camY), m_view,
             true);
    } else {
        target.fill(m_view, 0);
    }
    for (const Element* e : m_elements)
        if (e->visible())
            e->draw(target, m_camX, m_camY, m_view);
}

Pick Node::pick(int x, int y) const {
    Pick p;
    p.x = x;
    p.y = y;
    if (!m_view.contains(x, y))
        return p;
    for (auto it = m_elements.rbegin(); it != m_elements.rend(); ++it) {
        Element* e = *it;
        if (!e->pickable())
            continue;
        if (e->hitTest(x - m_view.left, y - m_view.top, m_camX, m_camY)) {
            p.element = e;
            p.result = e->image() ? Pick::Image : Pick::NoImage;
            return p;
        }
    }
    if (m_background)
        p.result = Pick::Background;
    return p;
}

Anim* Node::getAnim(std::string_view name, std::string_view newName) {
    Anim* a = m_engine.anims().getOrCreate(name, newName);
    if (a)
        a->setPosition(m_camX, m_camY, a->z());
    return a;
}

// --- scripts --------------------------------------------------------------------------

bool Node::runScript(std::string_view name) { return m_engine.scripts().runScript(*this, name); }
bool Node::stopScript(std::string_view name) { return m_engine.scripts().stopScript(*this, name); }
void Node::stopAllScripts() { m_engine.scripts().stopAll(*this); }
bool Node::isScriptRunning(std::string_view name) const { return m_engine.scripts().isRunning(name); }
bool Node::setScriptVar(std::string_view script, std::string_view var, std::string_view value) {
    return m_engine.scripts().setVar(script, var, value);
}
bool Node::setScriptVar(std::string_view script, std::string_view var, double value) {
    return m_engine.scripts().setVar(script, var, value);
}

// --- tick and hooks ---------------------------------------------------------------------

void Node::tick() {
    // HCNode tick (0x456f30).
    m_engine.anims().flushDeleted();
    m_engine.scripts().deleteEnded();
    m_engine.scriptEvents().broadcast(msg::kTick, {this});
    worldIdle();
}

void Node::start() { m_engine.scriptEvents().broadcast(msg::kNodeEntered, {m_name.c_str()}); }

void Node::leaving(const std::string&) {}

void Node::onPress(const Pick& pick) {
    // 0x454c40: a press on an element with an image becomes script event 0x64.
    if (pick.result == Pick::Image)
        m_engine.scriptEvents().broadcast(msg::kClick, {pick.element, pick.eventType});
}

void Node::onKeyUp(const InputEvent&) {}

void Node::sequenceEnded(Anim& anim, int seq) {
    // 0x454ca0: what WaitAnim waits for.
    m_engine.scriptEvents().broadcast(msg::kAnimDone, {&anim, seq});
}

void Node::soundDone(const std::string&, const void* sound, bool) {
    m_engine.scriptEvents().broadcast(msg::kSoundDone, {nullptr, 0, sound});
}

void Node::movieDone(std::intptr_t argument) { m_engine.scriptEvents().broadcast(msg::kMovieDone, {nullptr, argument}); }

void Node::scriptPreempted(Script& script) { m_engine.scriptEvents().broadcast(msg::kScriptDone, {&script}); }

void Node::scriptEnded(Script& script) { m_engine.scriptEvents().broadcast(msg::kScriptDone, {&script}); }

void Node::listen(int message, const MessageData& data) { onMessage(message, data); }

} // namespace brc::engine
