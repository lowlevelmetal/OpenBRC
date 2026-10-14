#pragma once

#include "engine/Image8.h"

#include <cstdint>

namespace brc::engine {

class Node;

// An object in a node's world (OMediaElement, OMT 1.x): position and depth,
// hide and pause counters, picking. Anims, movies and text are elements.
// See docs/engine/animation.md §2, §7–9.
class Element {
public:
    Element() = default;
    Element(const Element&) = delete;
    Element& operator=(const Element&) = delete;
    virtual ~Element();

    // --- position (local position = absolute: the game never nests anims) ----
    float x() const { return m_x; }
    float y() const { return m_y; }
    float z() const { return m_z; }
    // vt+0x04 / vt+0x08. A depth change makes the node re-sort its elements.
    void setPosition(float x, float y, float z);
    void move(float dx, float dy, float dz);

    // --- visibility and pause (counters, vt+0x30 / vt+0x38) ------------------
    void hide(bool on);
    bool visible() const { return m_hideCount == 0; }
    int hideCount() const { return m_hideCount; }
    void setHideCount(int n);
    virtual void pause(bool on);
    int motionPauseCount() const { return m_motionPause; }
    void setMotionPauseCount(int n) { m_motionPause = n < 0 ? 0 : n; } // element +0x5c written directly

    // --- motion (mover, §9.3) ------------------------------------------------
    void setVelocityPerUpdate(float dx, float dy, float dz);
    void setSpeed(float unitsPerSecondX, float unitsPerSecondY, float unitsPerSecondZ);

    // --- world membership ------------------------------------------------------
    Node* node() const { return m_node; }
    void link(Node* node); // vt+0x58: add to the node's element list
    void unlink();         // vt+0x1c

    // --- picking -----------------------------------------------------------------
    bool pickable() const { return m_pickable; } // +0x62 cleared = pickable
    void setPickable(bool p) { m_pickable = p; }
    bool perPixelHit() const { return m_perPixel; } // +0x84
    void setPerPixelHit(bool p) { m_perPixel = p; }
    void setClip(const Rect& r) { m_clip = r, m_clipEnabled = true; }
    void clearClip() { m_clipEnabled = false; }
    int clickId() const { return m_clickId; } // HC2DAnim +0x11c
    void setClickId(int id) { m_clickId = id; }
    int elementId() const { return m_elementId; } // +0x68
    void setElementId(int id) { m_elementId = id; }

    bool deleteMe() const { return m_deleteMe; } // +0x64: the world deletes it at its next update
    void setDeleteMe() { m_deleteMe = true; }
    bool changed() const { return m_changed; }
    void markChanged() { m_changed = true; }
    void clearChanged() { m_changed = false; }

    // --- current picture ------------------------------------------------------
    virtual const Image8* image() const = 0;
    // Offset of the image's top-left from the position (the frame's source offset).
    virtual float imageOffsetX() const { return 0; }
    virtual float imageOffsetY() const { return 0; }

    // vt+0x68: left = trunc(x + offsetX), top = trunc(y + offsetY), size of the image.
    Rect rect() const;
    // vt+0x60: rect() in view coordinates (camera subtracted).
    Rect screenRect(float camX, float camY) const;
    // vt+0x70 (0x4680b0): visible, inside the clip and the rectangle, and (per
    // pixel) not transparent. x, y in view coordinates.
    bool hitTest(int x, int y, float camX, float camY) const;
    // vt+0x78: draw clipped to `viewRect` (destination coordinates).
    virtual void draw(Image8& dst, float camX, float camY, const Rect& viewRect) const;

    // vt+0x20: per-update tick; the base applies motion.
    virtual void update(std::uint32_t elapsedMs);

protected:
    float m_x = 0, m_y = 0, m_z = 0;

private:
    Node* m_node = nullptr;
    int m_hideCount = 0;
    int m_motionPause = 0;
    float m_vx = 0, m_vy = 0, m_vz = 0;    // per update
    float m_sx = 0, m_sy = 0, m_sz = 0;    // per millisecond
    bool m_pickable = true;
    bool m_perPixel = true;
    bool m_clipEnabled = false;
    Rect m_clip;
    int m_clickId = 0;
    int m_elementId = 0;
    bool m_deleteMe = false;
    bool m_changed = true;
};

} // namespace brc::engine
