#pragma once

// Session-wide flags that the mini-games and the cottage interior set when
// they end, and that the trail rides read (and clear) when the player comes
// back, to play the right voices and music. They are plain globals in the
// original executable; the addresses are given so ports of the readers can be
// matched to them.
namespace brc::game::flags {

// [0x80a160] AppleSort won (set by ~AppleSort 0x401bd0). The trails treat it
// as the orchard's "mystery mode" (mystery music, no critters or nags) and
// clear it when that is over.
inline bool appleSortWon = false;
// ([0x80a04c], set by Leapfrog's constructor, is trailflags::bridgeArrival.)
// [0x80a168] Leapfrog coin taken (Leapfrog::MouseDown 0x41fcd0).
inline bool leapfrogCoinTaken = false;
// [0x809ad4] BellGame entered (BellGame ctor 0x40a980).
inline bool bellGameVisited = false;
// [0x809afc] Carousel (cottage interior) entered (Carousel ctor 0x40f860).
inline bool carouselVisited = false;

} // namespace brc::game::flags
