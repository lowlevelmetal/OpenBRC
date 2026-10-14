#include "TestData.h"

#include "formats/OmtFile.h"
#include "vfs/DiscManifest.h"

#include <gtest/gtest.h>

namespace brc {
namespace {

using formats::OmtFile;

TEST(OmtFile, RejectsNonContainer) {
    std::vector<std::byte> junk(64, std::byte{0x41});
    auto r = OmtFile::open(std::make_shared<MemoryFile>(junk), "junk");
    EXPECT_FALSE(r);
}

TEST(OmtFile, RejectsDirectoryOutsideFile) {
    std::vector<std::byte> d = {std::byte{'0'}, std::byte{'M'}, std::byte{'F'}, std::byte{'S'},
                                std::byte{0x7F}, std::byte{0}, std::byte{0}, std::byte{0}};
    EXPECT_FALSE(OmtFile::open(std::make_shared<MemoryFile>(d), "bad"));
}

TEST(OmtFile, ParsesEveryContainerOnTheDisc) {
    BRC_REQUIRE_GAME_DATA();
    int containers = 0;
    for (const auto& e : vfs::kDiscManifest) {
        if (!e.path.ends_with(".omt") && !e.path.ends_with(".cache"))
            continue;
        auto f = test::gameData()->open(e.path);
        ASSERT_TRUE(f) << e.path;
        auto omt = OmtFile::open(f, std::string(e.path));
        ASSERT_TRUE(omt) << omt.error();
        EXPECT_FALSE(omt->chunks().empty()) << e.path;
        ++containers;
    }
    EXPECT_EQ(containers, 83);
}

TEST(OmtFile, NamesComeFromTheDirectory) {
    BRC_REQUIRE_GAME_DATA();
    auto omt = OmtFile::open(test::gameData()->open("resources/preferences.omt"), "preferences.omt");
    ASSERT_TRUE(omt);
    const auto* bg = omt->find(formats::chunk::kCanvas, "Preferences Background");
    ASSERT_TRUE(bg);
    EXPECT_EQ(bg->id, 0);
    EXPECT_TRUE(omt->find(formats::chunk::kWave, "click"));
    EXPECT_TRUE(omt->find(formats::chunk::kAnim, "prefscursor"));
}

} // namespace
} // namespace brc
