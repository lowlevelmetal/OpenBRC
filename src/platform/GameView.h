#pragma once

#include <utility>

struct SDL_Window;

namespace brc::platform {

// The window showing the game screen and the mapping between window coordinates (SDL
// event coordinates, in window units — not pixels on HiDPI displays) and game pixels.
// Implemented by render::Presenter; the platform layer uses it to place the mouse.
class GameView {
public:
    virtual ~GameView() = default;

    // May change when the renderer has to recreate its window (see Presenter::pump()).
    virtual SDL_Window* window() const = 0;
    // Size of the game screen in game pixels (640x480 for Barbie Riding Club).
    virtual std::pair<int, int> gameSize() const = 0;
    // Window -> game pixel, clamped to the game screen.
    virtual std::pair<int, int> windowToGame(float wx, float wy) const = 0;
    // Game pixel -> window coordinates of its centre.
    virtual std::pair<float, float> gameToWindow(int gx, int gy) const = 0;
};

} // namespace brc::platform
