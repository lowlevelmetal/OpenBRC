#include "TestData.h"

#include "core/Png.h"
#include "engine/Font.h"
#include "engine/Resources.h"

#include <gtest/gtest.h>

#include <cstdlib>

namespace brc {
namespace {

TEST(Font, BureauGrotesqueWidthsAndRendering) {
    BRC_REQUIRE_GAME_DATA();
    engine::Resources res(*test::gameData());
    ASSERT_TRUE(res.open("common.omt"));
    ASSERT_TRUE(res.open("signin.omt"));
    auto pal = res.canvasPalette("signin background");
    ASSERT_TRUE(pal);
    auto f18 = engine::Font::bureau18(res, *pal);
    ASSERT_TRUE(f18);
    // font.md §4: 18-pt cell 0 -> 19, '!' -> 5, '"' -> 11, '#' -> 17.
    EXPECT_EQ(f18->textWidth("!"), 5);
    EXPECT_EQ(f18->textWidth("\""), 11);
    EXPECT_EQ(f18->textWidth("#"), 17);
    EXPECT_EQ(f18->textWidth("!!"), 5 + 1 + 5);
    EXPECT_EQ(f18->textWidth(" "), 12);
    auto f10 = engine::Font::bureau10(res, *pal);
    ASSERT_TRUE(f10);
    EXPECT_EQ(f10->textWidth("!"), 4);

    engine::TextElement t(f18);
    t.setText("Barbie Riding Club");
    ASSERT_TRUE(t.image());
    EXPECT_EQ(t.image()->width, f18->textWidth("Barbie Riding Club"));
    if (const char* dir = std::getenv("OPENBRC_TEST_OUTPUT")) {
        engine::Image8 canvas(t.image()->width + 20, 49, 255);
        engine::blit(canvas, *t.image(), 10, 10, canvas.bounds());
        png::write(std::filesystem::path(dir) / "font18.png",
                   png::encodeIndexed(canvas.pixels, canvas.width, canvas.height, canvas.width, pal->rgb));
    }
}

} // namespace
} // namespace brc
