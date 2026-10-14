#pragma once

#include "vfs/Vfs.h"

namespace brc::test {

// The original game files from $OPENBRC_GAME_DATA (a disc image, the disc, or
// an installation; several may be given separated by the platform's path-list
// separator), or nullptr when unavailable. Mounted once and shared by all tests
// in the executable.
const vfs::Vfs* gameData();

} // namespace brc::test

// Skips the current test when the original game files are not available.
#define BRC_REQUIRE_GAME_DATA()                                                                        \
    do {                                                                                               \
        if (!::brc::test::gameData())                                                                  \
            GTEST_SKIP() << "set OPENBRC_GAME_DATA to a Barbie Riding Club disc image, disc or install"; \
    } while (0)
