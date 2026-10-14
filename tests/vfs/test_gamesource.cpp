#include "TestData.h"

#include "vfs/DiscManifest.h"
#include "vfs/GameSource.h"

#include <gtest/gtest.h>

namespace brc {
namespace {

TEST(GameSource, RejectsMissingPath) {
    std::string error;
    EXPECT_FALSE(vfs::probeGameSource("/nonexistent/openbrc/path", &error));
    EXPECT_FALSE(error.empty());
}

TEST(GameSource, RejectsUnrelatedDirectory) {
    std::string error;
    EXPECT_FALSE(vfs::probeGameSource(std::filesystem::temp_directory_path(), &error));
}

TEST(GameSource, EmptySourceListIsUnusable) {
    const auto data = vfs::assembleGameData({});
    EXPECT_FALSE(data.usable());
    EXPECT_EQ(data.missing.size(), vfs::kDiscManifest.size());
}

TEST(GameSource, GameDataHasEveryManifestFile) {
    BRC_REQUIRE_GAME_DATA();
    for (const auto& e : vfs::kDiscManifest) {
        auto f = test::gameData()->open(e.path);
        ASSERT_TRUE(f) << e.path;
        EXPECT_EQ(f->size(), e.size) << e.path;
    }
}

} // namespace
} // namespace brc
