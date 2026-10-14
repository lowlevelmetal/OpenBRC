#include "render/DisplaySettings.h"
#include "render/Overlay.h"

#include <gtest/gtest.h>

#include <string>

namespace brc::render {
namespace {

TEST(DisplaySettings, NamesRoundTrip) {
    for (BackendKind k : {BackendKind::Auto, BackendKind::Vulkan, BackendKind::OpenGL})
        EXPECT_EQ(parseBackendKind(toString(k)), k);
    for (Aspect a : {Aspect::Original, Aspect::Stretch, Aspect::Integer})
        EXPECT_EQ(parseAspect(toString(a)), a);
    for (Filter f : {Filter::Sharp, Filter::Nearest, Filter::Smooth, Filter::Crt})
        EXPECT_EQ(parseFilter(toString(f)), f);
}

TEST(DisplaySettings, ParsingIsLenient) {
    EXPECT_EQ(parseBackendKind(" VK "), BackendKind::Vulkan);
    EXPECT_EQ(parseBackendKind("GL"), BackendKind::OpenGL);
    EXPECT_EQ(parseBackendKind("OpenGL"), BackendKind::OpenGL);
    EXPECT_EQ(parseAspect("4:3"), Aspect::Original);
    EXPECT_EQ(parseFilter("CRT"), Filter::Crt);
    EXPECT_FALSE(parseBackendKind("direct3d"));
    EXPECT_FALSE(parseAspect(""));
    EXPECT_FALSE(parseFilter("blurry"));
}

TEST(DisplaySettings, CyclesVisitEveryMode) {
    Aspect a = Aspect::Original;
    for (int i = 0; i < kAspectCount; ++i)
        a = nextAspect(a);
    EXPECT_EQ(a, Aspect::Original);
    EXPECT_EQ(nextAspect(Aspect::Original), Aspect::Stretch);
    EXPECT_EQ(nextAspect(Aspect::Integer), Aspect::Original);

    Filter f = Filter::Sharp;
    for (int i = 0; i < kFilterCount; ++i)
        f = nextFilter(f);
    EXPECT_EQ(f, Filter::Sharp);
    EXPECT_EQ(nextFilter(Filter::Crt), Filter::Sharp);
}

TEST(DisplaySettings, DefaultsAreSensible) {
    DisplaySettings s;
    EXPECT_EQ(s.backend, BackendKind::Auto);
    EXPECT_EQ(s.aspect, Aspect::Original);
    EXPECT_EQ(s.filter, Filter::Sharp);
    EXPECT_TRUE(s.vsync);
    EXPECT_FALSE(s.fullscreen);
    EXPECT_EQ(s.windowWidth * 3, s.windowHeight * 4);
}

TEST(Toast, RasterizesTextWithMargin) {
    OverlayBitmap b = rasterizeToast("Hi");
    EXPECT_EQ(b.width, 2 * 8 + 8);
    EXPECT_EQ(b.height, 16);
    int lit = 0;
    for (int y = 0; y < kOverlayTexH; ++y)
        for (int x = 0; x < kOverlayTexW; ++x) {
            bool on = b.pixels[static_cast<std::size_t>(y * kOverlayTexW + x)] != 0;
            lit += on;
            if (on) {
                // Glyphs stay inside the 4-texel margin.
                EXPECT_GE(x, 4);
                EXPECT_LT(x, 4 + 16);
                EXPECT_GE(y, 4);
                EXPECT_LT(y, 12);
            }
        }
    EXPECT_GT(lit, 0);
}

TEST(Toast, NonAsciiAndLongText) {
    // "é" is two UTF-8 bytes but one character.
    EXPECT_EQ(rasterizeToast("caf\xc3\xa9").width, 4 * 8 + 8);
    std::string longText(100, 'x');
    EXPECT_EQ(rasterizeToast(longText).width, 31 * 8 + 8); // cut to the texture width
    EXPECT_EQ(rasterizeToast("").width, 8);
}

} // namespace
} // namespace brc::render
