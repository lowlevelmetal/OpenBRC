#include "TestData.h"

#include "core/StringUtil.h"
#include "formats/Canvas.h"
#include "formats/OmtFile.h"
#include "vfs/DiscManifest.h"

#include <gtest/gtest.h>

namespace brc {
namespace {

using namespace formats;

TEST(Canvas, RejectsGarbage) {
    std::vector<std::byte> junk(64, std::byte{0x41});
    EXPECT_FALSE(parseCanvas(junk));
    EXPECT_FALSE(parseBlit(junk));
}

TEST(Canvas, EveryCanvasAndBlitOnTheDisc) {
    BRC_REQUIRE_GAME_DATA();
    int canvases = 0, blits = 0, pairs = 0, identical = 0;
    for (const auto& e : vfs::kDiscManifest) {
        if (!e.path.ends_with(".omt"))
            continue;
        auto omt = OmtFile::open(test::gameData()->open(e.path), std::string(e.path));
        ASSERT_TRUE(omt);
        for (const auto* c : omt->ofType(chunk::kCanvas)) {
            auto cv = parseCanvas(omt->read(*c));
            ASSERT_TRUE(cv) << e.path << " " << c->name << ": " << cv.error();
            EXPECT_EQ(cv->depth <= 8 ? cv->indices.size() : cv->rgb.size(),
                      static_cast<std::size_t>(cv->width) * static_cast<std::size_t>(cv->height));
            if (cv->depth == 8) {
                EXPECT_TRUE(cv->palette) << c->name;
            }
            ++canvases;
        }
        for (const auto* c : omt->ofType(chunk::kBlit)) {
            auto b = parseBlit(omt->read(*c));
            ASSERT_TRUE(b) << e.path << " " << c->name << ": " << b.error();
            ++blits;
            const auto* cc = omt->find(chunk::kCanvas, c->name);
            if (!cc)
                continue;
            auto cv = parseCanvas(omt->read(*cc));
            ASSERT_TRUE(cv);
            if (cv->width != b->width || cv->height != b->height || cv->depth != 8)
                continue;
            ++pairs;
            bool same = true;
            for (std::size_t i = 0; i < b->pixels.size() && same; ++i) {
                const bool canvasOpaque = !(cv->hasKey && cv->indices[i] == cv->key);
                same = canvasOpaque == (b->opaque[i] != 0) && (!canvasOpaque || cv->indices[i] == b->pixels[i]);
            }
            identical += same;
        }
    }
    EXPECT_EQ(canvases, 323 - 0 /* cache files hold no canvases */);
    EXPECT_EQ(blits, 333);
    // docs/formats/blit.md §7: three BLITs differ from their canvas on purpose.
    EXPECT_EQ(pairs - identical, 3) << pairs << " pairs";
}

} // namespace
} // namespace brc
