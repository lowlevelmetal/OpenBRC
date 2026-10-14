#pragma once

#include "render/DisplaySettings.h"

#include <utility>

namespace brc::render {

// A rectangle in pixels with the origin at the top-left of the drawable.
struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool empty() const { return w <= 0 || h <= 0; }
    friend bool operator==(const Rect&, const Rect&) = default;
};

// Where a srcW x srcH game image goes in an outW x outH drawable (pixels), centred.
//   Original: the largest 4:3 rectangle (the game was made for 4:3 monitors whatever
//             its resolution), bars on the other axis.
//   Stretch:  the whole drawable.
//   Integer:  the largest whole multiple of the source size that fits; when not even
//             1x fits, as Original.
// Degenerate sizes give the whole (possibly empty) drawable.
Rect computeView(int outW, int outH, int srcW, int srcH, Aspect aspect);

// Where the toast overlay (overlayW x overlayH texels) goes: the top-left corner, scaled
// by a whole factor that grows with the drawable, clipped to it.
Rect overlayRect(int outW, int outH, int overlayW, int overlayH);

// Everything needed to map between window coordinates (SDL event coordinates, in window
// units) and game pixels.
struct ViewMapping {
    int windowW = 0; // window size in window units
    int windowH = 0;
    int pixelW = 0; // drawable size in pixels (larger than the window size on HiDPI)
    int pixelH = 0;
    int srcW = 640;
    int srcH = 480;
    Aspect aspect = Aspect::Original;
};

// Window coordinates -> game pixel under them, clamped to the game screen (positions in
// the bars map to the nearest edge pixel).
std::pair<int, int> windowToGame(const ViewMapping& m, float wx, float wy);
// Game pixel -> window coordinates of its centre.
std::pair<float, float> gameToWindow(const ViewMapping& m, int gx, int gy);

} // namespace brc::render
