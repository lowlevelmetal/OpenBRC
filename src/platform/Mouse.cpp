#include "platform/Mouse.h"

#include "core/Log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace brc::platform {

bool Mouse::handleEvent(const SDL_Event& event, int* gx, int* gy) {
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        if (m_relative) {
            // Window units per game pixel, from the current scaling.
            auto [x0, y0] = m_view.gameToWindow(0, 0);
            auto [x1, y1] = m_view.gameToWindow(1, 1);
            float sx = x1 - x0 > 0.0f ? x1 - x0 : 1.0f;
            float sy = y1 - y0 > 0.0f ? y1 - y0 : 1.0f;
            float fx = m_fracX + event.motion.xrel / sx;
            float fy = m_fracY + event.motion.yrel / sy;
            float ix = std::trunc(fx);
            float iy = std::trunc(fy);
            m_fracX = fx - ix;
            m_fracY = fy - iy;
            auto [w, h] = m_view.gameSize();
            m_x = std::clamp(m_x + static_cast<int>(ix), 0, std::max(w - 1, 0));
            m_y = std::clamp(m_y + static_cast<int>(iy), 0, std::max(h - 1, 0));
        } else {
            std::tie(m_x, m_y) = m_view.windowToGame(event.motion.x, event.motion.y);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (!m_relative)
            std::tie(m_x, m_y) = m_view.windowToGame(event.button.x, event.button.y);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (!m_relative)
            std::tie(m_x, m_y) = m_view.windowToGame(event.wheel.mouse_x, event.wheel.mouse_y);
        break;
    default:
        return false;
    }
    if (gx)
        *gx = m_x;
    if (gy)
        *gy = m_y;
    return true;
}

void Mouse::showCursor(bool show) {
    if (show == m_cursorVisible)
        return;
    m_cursorVisible = show;
    if (show)
        SDL_ShowCursor();
    else
        SDL_HideCursor();
}

void Mouse::warp(int gx, int gy) {
    auto [w, h] = m_view.gameSize();
    m_x = std::clamp(gx, 0, std::max(w - 1, 0));
    m_y = std::clamp(gy, 0, std::max(h - 1, 0));
    m_fracX = m_fracY = 0.0f;
    if (m_relative)
        return; // the hidden pointer stays where SDL keeps it
    SDL_Window* window = m_view.window();
    if (!window)
        return;
    auto [wx, wy] = m_view.gameToWindow(m_x, m_y);
    SDL_WarpMouseInWindow(window, wx, wy);
}

bool Mouse::setRelativeMode(bool enabled) {
    if (enabled == m_relative)
        return true;
    SDL_Window* window = m_view.window();
    if (!window || !SDL_SetWindowRelativeMouseMode(window, enabled)) {
        log::warn("platform: cannot {} relative mouse mode: {}", enabled ? "enable" : "disable",
                  SDL_GetError());
        return false;
    }
    m_relative = enabled;
    m_fracX = m_fracY = 0.0f;
    if (!enabled) {
        // Put the visible pointer where the game thinks it is.
        auto [wx, wy] = m_view.gameToWindow(m_x, m_y);
        SDL_WarpMouseInWindow(window, wx, wy);
    }
    return true;
}

} // namespace brc::platform
