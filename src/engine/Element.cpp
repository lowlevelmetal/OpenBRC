#include "engine/Element.h"

#include "engine/Node.h"

#include <cmath>

namespace brc::engine {
namespace {

int trunc(float v) { return static_cast<int>(v); } // C truncation toward zero, like __ftol

} // namespace

Element::~Element() { unlink(); }

void Element::setPosition(float x, float y, float z) {
    // 0x467540: marks changed; the layer re-sorts when the depth changes.
    const bool depthChanged = z != m_z;
    m_x = x;
    m_y = y;
    m_z = z;
    m_changed = true;
    if (depthChanged && m_node)
        m_node->markUnsorted();
}

void Element::move(float dx, float dy, float dz) { setPosition(m_x + dx, m_y + dy, m_z + dz); }

void Element::hide(bool on) {
    // 0x467f70: a counter; hiding marks the area for repainting.
    if (on) {
        ++m_hideCount;
        m_changed = true;
    } else if (m_hideCount > 0) {
        if (--m_hideCount == 0)
            m_changed = true;
    }
}

void Element::setHideCount(int n) {
    m_hideCount = n < 0 ? 0 : n;
    m_changed = true;
}

void Element::pause(bool on) {
    if (on)
        ++m_motionPause;
    else if (m_motionPause > 0)
        --m_motionPause;
}

void Element::setVelocityPerUpdate(float dx, float dy, float dz) { m_vx = dx, m_vy = dy, m_vz = dz; }

void Element::setSpeed(float x, float y, float z) {
    // 0x467290: units per second, stored per millisecond.
    m_sx = x * 0.001f;
    m_sy = y * 0.001f;
    m_sz = z * 0.001f;
}

void Element::link(Node* node) {
    if (m_node == node)
        return;
    unlink();
    m_node = node;
    if (m_node) {
        m_node->addElement(this);
        m_changed = true;
    }
}

void Element::unlink() {
    if (!m_node)
        return;
    Node* n = m_node;
    m_node = nullptr;
    n->removeElement(this);
}

void Element::update(std::uint32_t elapsedMs) {
    // Mover tick (0x468ef0): unless motion is paused or the element has no layer.
    if (m_motionPause != 0 || !m_node)
        return;
    const float dt = static_cast<float>(elapsedMs);
    const float dx = m_vx + m_sx * dt, dy = m_vy + m_sy * dt, dz = m_vz + m_sz * dt;
    if (dx != 0 || dy != 0 || dz != 0)
        move(dx, dy, dz);
}

Rect Element::rect() const {
    const Image8* img = image();
    const int left = trunc(m_x + imageOffsetX());
    const int top = trunc(m_y + imageOffsetY());
    return {left, top, left + (img ? img->width : 0), top + (img ? img->height : 0)};
}

Rect Element::screenRect(float camX, float camY) const { return rect().offset(-trunc(camX), -trunc(camY)); }

bool Element::hitTest(int x, int y, float camX, float camY) const {
    if (!visible())
        return false;
    const Rect r = screenRect(camX, camY);
    if (m_clipEnabled && !m_clip.contains(x, y))
        return false;
    if (!r.contains(x, y))
        return false;
    if (!m_perPixel)
        return true;
    const Image8* img = image();
    return img && !img->transparentAt(x - r.left, y - r.top);
}

void Element::draw(Image8& dst, float camX, float camY, const Rect& viewRect) const {
    const Image8* img = image();
    if (!img || !visible())
        return;
    const Rect r = screenRect(camX, camY).offset(viewRect.left, viewRect.top);
    Rect clip = viewRect;
    if (m_clipEnabled)
        clip = clip.intersect(m_clip.offset(viewRect.left, viewRect.top));
    blit(dst, *img, r.left, r.top, clip);
}

} // namespace brc::engine
