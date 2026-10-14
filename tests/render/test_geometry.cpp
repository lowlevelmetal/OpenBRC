#include "render/Geometry.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <tuple>
#include <vector>

namespace brc::render {
namespace {

// ---------------------------------------------------------------------------- computeView

TEST(ComputeView, OriginalAtNativeSize) {
    EXPECT_EQ(computeView(640, 480, 640, 480, Aspect::Original), (Rect{0, 0, 640, 480}));
}

TEST(ComputeView, OriginalPillarboxesWideScreens) {
    EXPECT_EQ(computeView(1920, 1080, 640, 480, Aspect::Original), (Rect{240, 0, 1440, 1080})); // 16:9
    EXPECT_EQ(computeView(3840, 2160, 640, 480, Aspect::Original), (Rect{480, 0, 2880, 2160})); // 4K
    EXPECT_EQ(computeView(2560, 1080, 640, 480, Aspect::Original), (Rect{560, 0, 1440, 1080})); // 21:9
    EXPECT_EQ(computeView(3440, 1440, 640, 480, Aspect::Original), (Rect{760, 0, 1920, 1440})); // 21:9
    EXPECT_EQ(computeView(1920, 1200, 640, 480, Aspect::Original), (Rect{160, 0, 1600, 1200})); // 16:10
    // 16:10 HiDPI drawable (2560x1600): 4/3 * 1600 = 2133.3 rounds to 2133.
    EXPECT_EQ(computeView(2560, 1600, 640, 480, Aspect::Original), (Rect{213, 0, 2133, 1600}));
}

TEST(ComputeView, OriginalLetterboxesTallScreens) {
    EXPECT_EQ(computeView(1280, 1024, 640, 480, Aspect::Original), (Rect{0, 32, 1280, 960}));  // 5:4
    EXPECT_EQ(computeView(1080, 1920, 640, 480, Aspect::Original), (Rect{0, 555, 1080, 810})); // portrait
}

TEST(ComputeView, OriginalIsFourThreeWhateverTheSource) {
    // The game was made for 4:3 monitors; a 640x400 mode is still shown at 4:3.
    EXPECT_EQ(computeView(1920, 1080, 640, 400, Aspect::Original), (Rect{240, 0, 1440, 1080}));
}

TEST(ComputeView, OriginalKeepsFourThreeAtEverySize) {
    for (int w = 1; w <= 4000; w += 37)
        for (int h = 1; h <= 2400; h += 29) {
            Rect r = computeView(w, h, 640, 480, Aspect::Original);
            ASSERT_GE(r.x, 0);
            ASSERT_GE(r.y, 0);
            ASSERT_LE(r.x + r.w, w);
            ASSERT_LE(r.y + r.h, h);
            // One axis fills the drawable, the other is centred.
            ASSERT_TRUE(r.w == w || r.h == h) << w << "x" << h;
            ASSERT_LE(std::abs((w - r.w) - 2 * r.x), 1);
            ASSERT_LE(std::abs((h - r.h) - 2 * r.y), 1);
            // 4:3 to within rounding of the shorter side.
            ASSERT_LE(std::abs(r.w * 3 - r.h * 4), 4) << w << "x" << h << " -> " << r.w << "x" << r.h;
        }
}

TEST(ComputeView, StretchFillsTheDrawable) {
    EXPECT_EQ(computeView(2560, 1080, 640, 480, Aspect::Stretch), (Rect{0, 0, 2560, 1080}));
    EXPECT_EQ(computeView(500, 800, 640, 480, Aspect::Stretch), (Rect{0, 0, 500, 800}));
}

TEST(ComputeView, IntegerUsesTheLargestWholeScale) {
    EXPECT_EQ(computeView(640, 480, 640, 480, Aspect::Integer), (Rect{0, 0, 640, 480}));
    EXPECT_EQ(computeView(1920, 1080, 640, 480, Aspect::Integer), (Rect{320, 60, 1280, 960}));
    EXPECT_EQ(computeView(2560, 1440, 640, 480, Aspect::Integer), (Rect{320, 0, 1920, 1440}));
    EXPECT_EQ(computeView(3840, 2160, 640, 480, Aspect::Integer), (Rect{640, 120, 2560, 1920}));
    EXPECT_EQ(computeView(2560, 1080, 640, 480, Aspect::Integer), (Rect{640, 60, 1280, 960}));  // 21:9
    EXPECT_EQ(computeView(3440, 1440, 640, 480, Aspect::Integer), (Rect{760, 0, 1920, 1440}));  // 21:9
    EXPECT_EQ(computeView(1920, 1200, 640, 480, Aspect::Integer), (Rect{320, 120, 1280, 960})); // 16:10
    EXPECT_EQ(computeView(2560, 1600, 640, 480, Aspect::Integer), (Rect{320, 80, 1920, 1440})); // 16:10 HiDPI
}

TEST(ComputeView, IntegerFallsBackToFourThreeWhenTooSmall) {
    EXPECT_EQ(computeView(600, 400, 640, 480, Aspect::Integer),
              computeView(600, 400, 640, 480, Aspect::Original));
    EXPECT_EQ(computeView(600, 400, 640, 480, Aspect::Integer), (Rect{33, 0, 533, 400}));
}

TEST(ComputeView, DegenerateSizes) {
    for (Aspect a : {Aspect::Original, Aspect::Stretch, Aspect::Integer}) {
        EXPECT_TRUE(computeView(0, 0, 640, 480, a).empty());
        EXPECT_EQ(computeView(-5, 100, 640, 480, a), (Rect{0, 0, 0, 100}));
        EXPECT_EQ(computeView(800, 600, 0, 480, a), (Rect{0, 0, 800, 600}));
    }
    // One-pixel drawables still give a (tiny) view.
    EXPECT_FALSE(computeView(1, 1, 640, 480, Aspect::Original).empty());
}

TEST(OverlayRect, ScalesWithTheDrawable) {
    EXPECT_EQ(overlayRect(640, 480, 100, 16), (Rect{8, 8, 100, 16}));
    EXPECT_EQ(overlayRect(1920, 1080, 100, 16), (Rect{24, 24, 300, 48}));
    EXPECT_EQ(overlayRect(3840, 2160, 100, 16), (Rect{48, 48, 600, 96}));
    EXPECT_EQ(overlayRect(7680, 4320, 100, 16), (Rect{48, 48, 600, 96})); // capped at 6x
    EXPECT_EQ(overlayRect(50, 20, 100, 16), (Rect{8, 8, 42, 12}));        // clipped
    EXPECT_TRUE(overlayRect(4, 4, 100, 16).empty());
}

// ---------------------------------------------------------------------------- window <-> game

struct MappingCase {
    const char* name;
    int windowW, windowH, pixelW, pixelH;
};

const MappingCase kMappings[] = {
    {"native", 640, 480, 640, 480},
    {"2x", 1280, 960, 1280, 960},
    {"16:9", 1920, 1080, 1920, 1080},
    {"21:9", 2560, 1080, 2560, 1080},
    {"32:9", 5120, 1440, 5120, 1440},
    {"16:10", 1680, 1050, 1680, 1050},
    {"5:4", 1280, 1024, 1280, 1024},
    {"portrait", 1080, 1920, 1080, 1920},
    {"small", 320, 200, 320, 200},
    {"odd", 1001, 777, 1001, 777},
    {"hidpi 2x 16:10", 1440, 900, 2880, 1800},
    {"hidpi 2x 16:9", 1280, 720, 2560, 1440},
    {"hidpi 1.5x 21:9", 2293, 960, 3440, 1440},
    {"hidpi 1.25x", 1536, 864, 1920, 1080},
};

ViewMapping mapping(const MappingCase& c, Aspect aspect, int srcW = 640, int srcH = 480) {
    return ViewMapping{c.windowW, c.windowH, c.pixelW, c.pixelH, srcW, srcH, aspect};
}

TEST(Mapping, RoundTripsEveryGamePixel) {
    for (const auto& c : kMappings)
        for (Aspect aspect : {Aspect::Original, Aspect::Stretch, Aspect::Integer}) {
            ViewMapping m = mapping(c, aspect);
            int bad = 0;
            for (int gy = 0; gy < 480; ++gy)
                for (int gx = 0; gx < 640; ++gx) {
                    auto [wx, wy] = gameToWindow(m, gx, gy);
                    auto [x, y] = windowToGame(m, wx, wy);
                    bad += x != gx || y != gy;
                }
            EXPECT_EQ(bad, 0) << c.name << " aspect " << toString(aspect);
        }
}

TEST(Mapping, PixelCentresLandInsideTheView) {
    for (const auto& c : kMappings)
        for (Aspect aspect : {Aspect::Original, Aspect::Stretch, Aspect::Integer}) {
            ViewMapping m = mapping(c, aspect);
            Rect v = computeView(c.pixelW, c.pixelH, 640, 480, aspect);
            float sx = static_cast<float>(c.pixelW) / static_cast<float>(c.windowW);
            float sy = static_cast<float>(c.pixelH) / static_cast<float>(c.windowH);
            for (auto [gx, gy] : {std::pair{0, 0}, std::pair{639, 479}, std::pair{320, 240}}) {
                auto [wx, wy] = gameToWindow(m, gx, gy);
                EXPECT_GT(wx * sx, static_cast<float>(v.x)) << c.name;
                EXPECT_LT(wx * sx, static_cast<float>(v.x + v.w)) << c.name;
                EXPECT_GT(wy * sy, static_cast<float>(v.y)) << c.name;
                EXPECT_LT(wy * sy, static_cast<float>(v.y + v.h)) << c.name;
            }
        }
}

TEST(Mapping, HiDpiUsesDrawablePixels) {
    // 1280x720 window units, 2560x1440 pixels: the 4:3 view is pixels 320..2239.
    ViewMapping m{1280, 720, 2560, 1440, 640, 480, Aspect::Original};
    EXPECT_EQ(windowToGame(m, 640.0f, 360.0f), (std::pair{320, 240})); // window centre = game centre
    EXPECT_EQ(windowToGame(m, 160.0f, 0.0f), (std::pair{0, 0}));       // left edge of the view
    EXPECT_EQ(windowToGame(m, 161.5f, 1.5f), (std::pair{1, 1}));       // 1.5 units = 3 px = 1 game px
    auto [wx, wy] = gameToWindow(m, 0, 0);
    EXPECT_FLOAT_EQ(wx, 160.75f);
    EXPECT_FLOAT_EQ(wy, 0.75f);
    // The same window without HiDPI maps identically in window units.
    ViewMapping lo{1280, 720, 1280, 720, 640, 480, Aspect::Original};
    for (float x : {170.0f, 640.0f, 1100.0f})
        EXPECT_EQ(windowToGame(m, x, 300.0f), windowToGame(lo, x, 300.0f));
}

TEST(Mapping, BarsAndOutsideClampToTheEdges) {
    ViewMapping m{2560, 1080, 2560, 1080, 640, 480, Aspect::Original}; // view x 560..1999
    EXPECT_EQ(windowToGame(m, 10.0f, 540.0f).first, 0);                // left bar
    EXPECT_EQ(windowToGame(m, 2550.0f, 540.0f).first, 639);            // right bar
    EXPECT_EQ(windowToGame(m, -100.0f, -100.0f), (std::pair{0, 0}));
    EXPECT_EQ(windowToGame(m, 1e9f, 1e9f), (std::pair{639, 479}));
    EXPECT_EQ(windowToGame(m, -1e9f, 1e9f), (std::pair{0, 479}));
    EXPECT_EQ(windowToGame(m, std::nanf(""), 540.0f).first, 0);
}

TEST(Mapping, StretchScalesAxesIndependently) {
    ViewMapping m{1280, 480, 1280, 480, 640, 480, Aspect::Stretch};
    EXPECT_EQ(windowToGame(m, 1279.0f, 479.0f), (std::pair{639, 479}));
    EXPECT_EQ(windowToGame(m, 2.0f, 1.0f), (std::pair{1, 1}));
}

TEST(Mapping, FollowsTheSourceSize) {
    ViewMapping m{1280, 960, 1280, 960, 320, 240, Aspect::Original};
    EXPECT_EQ(windowToGame(m, 1279.0f, 959.0f), (std::pair{319, 239}));
    EXPECT_EQ(windowToGame(m, 640.0f, 480.0f), (std::pair{160, 120}));
}

TEST(Mapping, NoWindowYet) {
    ViewMapping m{0, 0, 0, 0, 640, 480, Aspect::Original};
    EXPECT_EQ(windowToGame(m, 10.0f, 20.0f), (std::pair{10, 20}));
    EXPECT_EQ(windowToGame(m, 5000.0f, 5000.0f), (std::pair{639, 479}));
    EXPECT_EQ(gameToWindow(m, 10, 20), (std::pair{10.0f, 20.0f}));
}

} // namespace
} // namespace brc::render
