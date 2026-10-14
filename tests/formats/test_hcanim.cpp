#include "TestData.h"

#include "formats/HcAnim.h"
#include "formats/OmtFile.h"
#include "vfs/DiscManifest.h"

#include <gtest/gtest.h>

namespace brc {
namespace {

using namespace formats;

std::vector<std::byte> bytes(std::initializer_list<int> v) {
    std::vector<std::byte> out;
    for (int b : v)
        out.push_back(static_cast<std::byte>(b));
    return out;
}

TEST(HcAnim, ParsesThePreferencesMusicButton) {
    // preferences.omt HCAn 0 "musicbtn" (docs/formats/hcanim.md §2.4).
    const auto d = bytes({0, 0, 0, 2, 0, 0x42, 0, 0x6e, 0, 0, 0, 1,                     // header
                          0, 3, 0, 0x4a, 0, 0xdb, 0, 0x49, 0, 0xda, 0, 0x4a, 0, 0xdb, // sources
                          0, 3,                                                       // sequences
                          0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 2,                         //
                          0, 1, 0, 0, 0, 0, 0, 2, 0, 0, 0, 2,                         //
                          0, 1, 0, 0, 0, 0, 0, 3, 0, 0, 0, 2});
    auto a = parseHcAnim(d);
    ASSERT_TRUE(a) << a.error();
    EXPECT_EQ(a->defaultFrameMs, 66);
    EXPECT_EQ(a->defaultZ, 110);
    ASSERT_EQ(a->sourceOffsets.size(), 3u);
    EXPECT_EQ(a->sourceOffsets[1].x, 73);
    EXPECT_EQ(a->sourceOffsets[1].y, 218);
    ASSERT_EQ(a->sequences.size(), 3u);
    EXPECT_EQ(a->sequences[2].frames[0].source, 3);
    EXPECT_EQ(a->sequences[2].frames[0].flags, anim_flags::kFrameStop);
    EXPECT_EQ(a->sequences[2].z, 110);
}

TEST(HcAnim, RejectsTruncatedAndOutOfRange) {
    EXPECT_FALSE(parseHcAnim(bytes({0, 0, 0, 2, 0, 0x42})));
    // one source, one frame referring to source 2
    EXPECT_FALSE(parseHcAnim(bytes({0, 0, 0, 2, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0,
                                    0, 2, 0, 0, 0, 0})));
    EXPECT_FALSE(parseHcAnim(bytes({0, 0, 0, 9})));
}

TEST(HcAnim, EveryAnimAndCacheOnTheDisc) {
    BRC_REQUIRE_GAME_DATA();
    int anims = 0, caches = 0, records = 0, smki = 0;
    for (const auto& e : vfs::kDiscManifest) {
        const bool isCache = e.path.ends_with(".omt.cache");
        if (!e.path.ends_with(".omt") && !isCache)
            continue;
        auto f = test::gameData()->open(e.path);
        ASSERT_TRUE(f);
        if (isCache) {
            auto cache = openAnimCache(f, std::string(e.path));
            ASSERT_TRUE(cache) << cache.error();
            ++caches;
            for (const auto& rec : cache->records) {
                EXPECT_TRUE(cache->container.find(chunk::kAnim, rec.name)) << e.path << " " << rec.name;
                ++records;
            }
            for (const auto* c : cache->container.ofType(chunk::kAnim)) {
                auto a = parseHcAnim(cache->container.read(*c));
                ASSERT_TRUE(a) << e.path << " " << c->name << ": " << a.error();
                ++anims;
            }
            continue;
        }
        auto omt = OmtFile::open(f, std::string(e.path));
        ASSERT_TRUE(omt);
        for (const auto* c : omt->ofType(chunk::kAnim)) {
            auto a = parseHcAnim(omt->read(*c));
            ASSERT_TRUE(a) << e.path << " " << c->name << ": " << a.error();
            EXPECT_EQ(a->version, 2);
            ++anims;
        }
        for (const auto* c : omt->ofType(chunk::kSmackerInfo)) {
            EXPECT_FALSE(parseSmki(omt->read(*c)).empty());
            EXPECT_TRUE(omt->find(chunk::kSmacker, c->name)) << c->name;
            ++smki;
        }
    }
    EXPECT_EQ(anims, 991);
    EXPECT_EQ(caches, 27);
    EXPECT_EQ(records, 262);
    EXPECT_EQ(smki, 439);
}

} // namespace
} // namespace brc
