#include "render/Geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace brc::render {

Rect computeView(int outW, int outH, int srcW, int srcH, Aspect aspect) {
    Rect r{0, 0, std::max(outW, 0), std::max(outH, 0)};
    if (outW <= 0 || outH <= 0 || srcW <= 0 || srcH <= 0 || aspect == Aspect::Stretch)
        return r;
    int w = 0;
    int h = 0;
    if (aspect == Aspect::Integer) {
        int k = std::min(outW / srcW, outH / srcH);
        if (k >= 1) {
            w = srcW * k;
            h = srcH * k;
        }
    }
    if (w == 0) {
        // 4:3, rounded to the nearest pixel.
        if (static_cast<std::int64_t>(outW) * 3 >= static_cast<std::int64_t>(outH) * 4) {
            h = outH;
            w = static_cast<int>((static_cast<std::int64_t>(outH) * 4 + 1) / 3);
        } else {
            w = outW;
            h = static_cast<int>((static_cast<std::int64_t>(outW) * 3 + 2) / 4);
        }
        w = std::min(w, outW);
        h = std::min(h, outH);
    }
    r.x = (outW - w) / 2;
    r.y = (outH - h) / 2;
    r.w = w;
    r.h = h;
    return r;
}

Rect overlayRect(int outW, int outH, int overlayW, int overlayH) {
    int s = std::clamp(std::min(outW, outH) / 360, 1, 6);
    Rect r;
    r.x = 8 * s;
    r.y = 8 * s;
    r.w = std::max(std::min(overlayW * s, outW - r.x), 0);
    r.h = std::max(std::min(overlayH * s, outH - r.y), 0);
    return r;
}

namespace {

// The view in drawable pixels and the pixels-per-window-unit scale; false when there is
// nothing to map through (no window area yet).
bool currentView(const ViewMapping& m, Rect& view, float& sx, float& sy) {
    sx = m.windowW > 0 ? static_cast<float>(m.pixelW) / static_cast<float>(m.windowW) : 1.0f;
    sy = m.windowH > 0 ? static_cast<float>(m.pixelH) / static_cast<float>(m.windowH) : 1.0f;
    view = computeView(m.pixelW, m.pixelH, m.srcW, m.srcH, m.aspect);
    return !view.empty() && m.srcW > 0 && m.srcH > 0;
}

} // namespace

std::pair<int, int> windowToGame(const ViewMapping& m, float wx, float wy) {
    Rect v;
    float sx = 1.0f;
    float sy = 1.0f;
    float x = wx;
    float y = wy;
    if (currentView(m, v, sx, sy)) {
        x = (wx * sx - static_cast<float>(v.x)) * static_cast<float>(m.srcW) / static_cast<float>(v.w);
        y = (wy * sy - static_cast<float>(v.y)) * static_cast<float>(m.srcH) / static_cast<float>(v.h);
    }
    // Clamp in float first: casting an out-of-range float to int is undefined.
    float maxX = static_cast<float>(std::max(m.srcW - 1, 0));
    float maxY = static_cast<float>(std::max(m.srcH - 1, 0));
    x = std::isnan(x) ? 0.0f : std::clamp(std::floor(x), 0.0f, maxX);
    y = std::isnan(y) ? 0.0f : std::clamp(std::floor(y), 0.0f, maxY);
    return {static_cast<int>(x), static_cast<int>(y)};
}

std::pair<float, float> gameToWindow(const ViewMapping& m, int gx, int gy) {
    Rect v;
    float sx = 1.0f;
    float sy = 1.0f;
    if (!currentView(m, v, sx, sy))
        return {static_cast<float>(gx), static_cast<float>(gy)};
    float wx = (static_cast<float>(v.x) +
                (static_cast<float>(gx) + 0.5f) * static_cast<float>(v.w) / static_cast<float>(m.srcW)) /
               sx;
    float wy = (static_cast<float>(v.y) +
                (static_cast<float>(gy) + 0.5f) * static_cast<float>(v.h) / static_cast<float>(m.srcH)) /
               sy;
    return {wx, wy};
}

} // namespace brc::render
