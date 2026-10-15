#pragma once

// The trail rides' constant tables (.rdata/.data of the executable), shared by
// TrailNode.cpp and TrailRide.cpp. Index conventions: trail 0..7 (aaW cwL obS
// rfS rrE trE wwE wwW), horse 0..3 (Pal Wht Pnt Dpl, app+0x1C0).
#include <array>
#include <cstdint>
#include <string_view>

namespace brc::game::trail {

using Names8 = std::array<std::string_view, 8>;
using Names4 = std::array<std::string_view, 4>;

inline constexpr Names8 kPrefix = {"aaW", "cwL", "obS", "rfS", "rrE", "trE", "wwE", "wwW"}; // 0x802f48
// 0x802f68 [trail * 2 + [0x80a16c]]
inline constexpr std::array<std::string_view, 16> kTrailMovie = {
    "AppleW.smk",   "AppleW_L.smk",  "CottageW.smk", "CottageW_L.smk", "Bridge.smk",   "Bridge_L.smk",
    "Rainbow.smk",  "Rainbow_L.smk", "RabbitE.smk",  "RabbitE_L.smk",  "TurtleE.smk",  "TurtleE_L.smk",
    "WishingN.smk", "WishingN_L.smk", "WishingS.smk", "WishingS_L.smk"};
inline constexpr Names8 kTrailPalette = {"aa_west.act",  "cw_loop.act", "ob_south.act", "rf_south.act",
                                         "rr_east.act",  "tr_east.act", "ww_north.act", "ww_south.act"}; // 0x802fa8
inline constexpr Names4 kHorsePalette = {"pal.act", "wht.act", "pnt.act", "dpl.act"};                     // 0x802fc8
inline constexpr Names4 kTrailOmt = {"trailPal.omt", "trailWht.omt", "trailPnt.omt", "trailDpl.omt"};     // 0x8027d0

// Horse heads [horse * 2 + [0x80a170]].
inline constexpr Names8 kHeadStraight = {"pal_straight_crop.smk", "pal_fwd_L.smk", "wht_straight_crop.smk",
                                         "wht_fwd_L.smk",         "pnt_straight_crop.smk", "pnt_fwd_L.smk",
                                         "dpl_straight_crop.smk", "dpl_fwd_L.smk"}; // 0x802fd8
inline constexpr Names8 kHeadLeft = {"pal_left_crop.smk", "pal_left_L.smk", "wht_left_crop.smk", "wht_left_L.smk",
                                     "pnt_left_crop.smk", "pnt_left_L.smk", "dpl_left_crop.smk", "dpl_left_L.smk"}; // 0x802ff8
inline constexpr Names8 kHeadRight = {"pal_right_crop.smk", "pal_right_L.smk", "wht_right_crop.smk",
                                      "wht_right_L.smk",    "pnt_right_crop.smk", "pnt_right_L.smk",
                                      "dpl_right_crop.smk", "dpl_right_L.smk"}; // 0x803018
inline constexpr Names8 kHeadJump = {"pal_jump_crop.smk", "pal_jump_L.smk", "wht_jump_crop.smk", "wht_jump_L.smk",
                                     "pnt_jump_crop.smk", "pnt_jump_L.smk", "dpl_jump_crop.smk", "dpl_jump_L.smk"}; // 0x803038
inline constexpr Names8 kHeadReverse = {"pal_rev_crop.smk", "pal_rev_L.smk", "wht_rev_crop.smk", "wht_rev_L.smk",
                                        "pnt_rev_crop.smk", "pnt_rev_L.smk", "dpl_rev_crop.smk", "dpl_rev_L.smk"}; // 0x803058

inline constexpr std::array<std::string_view, 5> kGaitSuffix = {"2", "8", "10", "12", "15"};                // 0x803078
inline constexpr std::array<std::string_view, 5> kAmbientLoop = {"wind", "Xaat001", "Xrrt001", "Xtrr001",
                                                                 "Xwwt001"};                               // 0x803090
inline constexpr Names4 kSurfaceName = {"grass", "road", "rocks", "sand"};                                // 0x8030b0
inline constexpr std::array<int, 8> kSurface = {1, 0, 2, 2, 1, 3, 0, 0};                                   // 0x7f3df0

inline constexpr Names8 kButterflyPrefix = {"aa_", "cw_", "ob_", "rf_", "rr_", "tr_", "ww_", "ww_"}; // 0x8030c0
// 0x8030e0 [trail * 3 + species]; "" = none.
inline constexpr std::array<std::string_view, 24> kButterflySpecies = {
    "admiral", "frit",    "yellow", "admiral", "Baton", "",       "Baton", "Swallow",
    "Yellow",  "Apollo",  "Baton",  "",        "Apollo", "Baton", "Yellow", "Baton",
    "Swallow", "Yellow",  "frit",   "Swallow", "",      "frit",   "Swallow", ""};
inline constexpr std::array<std::string_view, 3> kButterflySize = {"_20", "_50", "_80"}; // 0x803140

// Per-horse anims.
inline constexpr Names4 kAmbientH = {"ambientPal", "ambientWht", "ambientPnt", "ambientDpl"};          // 0x803868
inline constexpr Names4 kMountRunH = {"palMountRun", "whtMountRun", "pntMountRun", "dplMountRun"};     // 0x803878
inline constexpr Names4 kLookLeftH = {"PalLookLeft", "WhtLookLeft", "PntLookLeft", "DplLookLeft"};     // 0x803888
inline constexpr Names4 kTurnLeftH = {"PalTurnLeft", "WhtTurnLeft", "PntTurnLeft", "DplTurnLeft"};     // 0x803898
inline constexpr Names4 kTurnRightH = {"PalTurnRight", "WhtTurnRight", "PntTurnRight", "DplTurnRight"}; // 0x8038a8
inline constexpr Names4 kGeneralAmbH = {"GeneralAmbPal", "GeneralAmbWht", "GeneralAmbPnt", "GeneralAmbDpl"}; // 0x8038b8
inline constexpr Names4 kSubtleAmbH = {"SubtleAmbPal", "SubtleAmbWht", "SubtleAmbPnt", "SubtleAmbDpl"}; // 0x8038c8
inline constexpr Names4 kTMomentH = {"hTMoment2 Pal", "hTMoment2 Wht", "hTMoment2 Pnt", "hTMoment2 Dpl"}; // 0x8038d8
inline constexpr Names4 kEatAppleH = {"PalEatApple", "WhtEatApple", "PntEatApple", "DplEatApple"};     // 0x8038e8
inline constexpr std::array<std::string_view, 14> kHorseStateName = {
    "MOUNT_RUN", "AMBIENT", "TURN_LEFT", "TURN_RIGHT", "LOOK_LEFT", "EXCITED_LOOK_LEFT", "SUBTLE_AMBIENT",
    "GENERAL_AMBIENT", "WALK", "WALK_AMBIENT", "TENDER_MOMENT", "FEED_APPLE", "HIDE_ALL", "NO_STATE"}; // 0x8038f8

// 0x7f3830: decision windows [trail][decision] = {first, last} trail frame; 9999 = none.
struct Window {
    std::uint32_t lo, hi;
};
inline constexpr std::array<std::array<Window, 5>, 8> kDecision = {{
    {{{730, 770}, {555, 650}, {47, 245}, {300, 345}, {365, 555}}},
    {{{60, 145}, {9999, 9999}, {9999, 9999}, {9999, 9999}, {9999, 9999}}},
    {{{40, 136}, {137, 175}, {0, 40}, {9999, 9999}, {9999, 9999}}},
    {{{40, 145}, {9999, 9999}, {9999, 9999}, {9999, 9999}, {9999, 9999}}},
    {{{60, 180}, {300, 430}, {475, 550}, {770, 820}, {842, 975}}},
    {{{80, 140}, {450, 545}, {9999, 9999}, {9999, 9999}, {9999, 9999}}},
    {{{170, 200}, {9999, 9999}, {9999, 9999}, {9999, 9999}, {9999, 9999}}},
    {{{0, 25}, {9999, 9999}, {9999, 9999}, {9999, 9999}, {9999, 9999}}},
}};

// Obstacles [trail * 3 + i]: window start (0x7f3ae0), end (0x7f3b40) and the
// base-movie frame the refusal seeks to (0x7f3ba0), in SmackGoto numbers.
inline constexpr std::array<int, 24> kObstacleStart = {510, 565, 626, 0, 0, 0, 0, 0, 0, 85, 140, 227,
                                                       108, 192, 302, 170, 270, 320, 0, 0, 0, 0, 0, 0};
inline constexpr std::array<int, 24> kObstacleEnd = {525, 612, 665, 0, 0, 0, 0, 0, 0, 121, 199, 259,
                                                     156, 266, 383, 215, 302, 361, 0, 0, 0, 0, 0, 0};
inline constexpr std::array<int, 24> kObstacleRefusal = {513, 600, 653, 0, 0, 0, 0, 0, 0, 111, 187, 248,
                                                         144, 254, 370, 203, 290, 350, 0, 0, 0, 0, 0, 0};

inline constexpr std::array<int, 8> kNextTrail = {4, 4, 0, 0, 3, 0, 4, 4};    // 0x7f3c10
inline constexpr std::array<int, 8> kMusicId = {2, 3, 2, 2, 2, 2, 3, 3};      // 0x7f3c30
inline constexpr std::array<int, 8> kAmbientIndex = {1, 4, 2, 2, 2, 3, 4, 4}; // 0x7f3c50
inline constexpr std::array<int, 8> kMapLocation = {0, 1, 2, 3, 4, 5, 6, 6};  // 0x8027b0

// 0x7f3c70: frame ranges [trail][face][4] = {lo, hi} (exclusive) where the
// head may not face that way (SetHorseFace's dormant check).
struct Range {
    std::int16_t lo, hi;
};
inline constexpr std::array<std::array<std::array<Range, 4>, 3>, 8> kInvalidFace = {{
    {{{{{612, 660}, {0, 0}, {0, 0}, {0, 0}}}, {{{0, 0}, {0, 0}, {0, 0}, {0, 0}}}, {{{341, 381}, {760, 780}, {0, 0}, {0, 0}}}}},
    {{{{{60, 137}, {0, 0}, {0, 0}, {0, 0}}}, {{{0, 0}, {0, 0}, {0, 0}, {0, 0}}}, {{{15, 30}, {170, 180}, {0, 0}, {0, 0}}}}},
    {{{{{10, 16}, {164, 170}, {221, 289}, {0, 0}}}, {{{0, 0}, {0, 0}, {0, 0}, {0, 0}}}, {{{0, 0}, {0, 0}, {0, 0}, {0, 0}}}}},
    {{{{{133, 162}, {217, 250}, {0, 0}, {0, 0}}}, {{{0, 0}, {0, 0}, {0, 0}, {0, 0}}}, {{{201, 215}, {256, 289}, {0, 0}, {0, 0}}}}},
    {{{{{151, 175}, {355, 375}, {656, 725}, {910, 944}}}, {{{0, 0}, {0, 0}, {0, 0}, {0, 0}}}, {{{245, 285}, {772, 878}, {0, 0}, {0, 0}}}}},
    {{{{{0, 0}, {0, 0}, {0, 0}, {0, 0}}}, {{{0, 0}, {0, 0}, {0, 0}, {0, 0}}}, {{{80, 100}, {480, 495}, {600, 608}, {0, 0}}}}},
    {{{{{115, 170}, {0, 0}, {0, 0}, {0, 0}}}, {{{0, 0}, {0, 0}, {0, 0}, {0, 0}}}, {{{0, 0}, {0, 0}, {0, 0}, {0, 0}}}}},
    {{{{{5, 25}, {0, 0}, {0, 0}, {0, 0}}}, {{{80, 100}, {0, 0}, {0, 0}, {0, 0}}}, {{{155, 175}, {0, 0}, {0, 0}, {0, 0}}}}},
}};

// Critters (SpawnCritter 0x42fda0): anims 0x7f3770 and comments 0x7f37b0.
inline constexpr int kNoCritter = 0x10;
inline constexpr std::array<std::string_view, 16> kCritterAnim = {
    "owl",           "critter_turtle",   "critter_rabbit",    "critter_rabbit2",
    "critter_bluebird", "critter_redbird", "critter_crab",    "critter_crab2",
    "critter_frog",  "critter_turtle2",  "m3 rr deer",        "critter_pecker",
    "critter_bluebird2", "critter_bluebird3", "critter_redbird2", "critter_redbirdfly"};
inline constexpr std::array<std::string_view, 16> kCritterVoice = {
    "db0618", "db04049", "", "", "db04047", "db04048", "db03068", "db03068",
    "db0626", "db04049", "db04052", "db0607", "db0602", "db0602", "db0606", "db0606"};

} // namespace brc::game::trail
