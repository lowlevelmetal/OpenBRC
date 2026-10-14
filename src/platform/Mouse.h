#pragma once

#include "platform/GameView.h"

#include <utility>

union SDL_Event;

namespace brc::platform {

// The mouse as the game sees it: a position in game pixels, a cursor that the game can
// hide, and warping. Main thread only.
class Mouse {
public:
    explicit Mouse(const GameView& view) : m_view(view) {}

    // Updates position() from mouse motion and button events. Returns true for mouse
    // events, with the event's game coordinates in *gx / *gy when given.
    bool handleEvent(const SDL_Event& event, int* gx = nullptr, int* gy = nullptr);

    // Last known pointer position in game pixels.
    std::pair<int, int> position() const { return {m_x, m_y}; }

    // Shows or hides the system cursor over the game window (the game draws its own).
    void showCursor(bool show);
    bool cursorVisible() const { return m_cursorVisible; }

    // Moves the pointer to the centre of game pixel (gx, gy), like SetCursorPos.
    void warp(int gx, int gy);

    // Relative mode: the pointer is captured and hidden, and motion is accumulated into
    // position() (scaled to game pixels and clamped to the screen). Returns false when
    // the platform refuses it.
    bool setRelativeMode(bool enabled);
    bool relativeMode() const { return m_relative; }

private:
    const GameView& m_view;
    int m_x = 0;
    int m_y = 0;
    float m_fracX = 0.0f; // sub-pixel remainder of relative motion
    float m_fracY = 0.0f;
    bool m_cursorVisible = true;
    bool m_relative = false;
};

} // namespace brc::platform
