#pragma once

// Global switches of the trail rides, shared by the yard (whose developer
// keys toggle them, yard.cpp 0x43d8a0) and the trail nodes (TrailNode sets
// them from the low-end flag in its constructor, 0x431420). Initial values
// are the executable's .data contents. See docs/engine/movies.md §4.1 and
// docs/engine/nodes.md "Yard" / "TrailNode".
namespace brc::game::trailflags {

inline int frameRate = 14;          // [0x802790] trail movie frame rate (12 on low-end machines); "fr<N>" key
inline bool horseHeads = true;      // [0x802794] horse-head overlays; H / h
inline bool music = true;           // [0x802798] trail music; M / m
inline bool ambient = true;         // [0x80279c] ambient loops; A / a
inline bool dialogue = true;        // [0x8027a0] ride voice hints; D / d
inline bool backups = true;         // [0x8027a4] preload the back loops of every trail; B / b
inline bool butterflies = true;     // [0x8027a8] butterfly overlays; F / f
inline bool smallMovies = false;    // [0x80a16c] use the `_L` trail/head movies ("new/old bridge"); O / o
inline bool programmaticLetterbox = false; // [0x80a170] 600x300 letterbox; P / p, V / v
inline bool videoLetterbox = false; // [0x80a174] letterbox without the ride's pan offsets; V / v
// [0x80a164] set by '?' in the yard: the trail palette takes only entries
// 0xFE..0xFF from the sprite palette instead of 0xC0..0xFF (0x431380).
inline bool narrowSpritePalette = false;
// [0x80a264] the yard found enough memory for the horse's ambient anims
// (always on modern machines); the "Ambient" script runs only then.
inline bool highMemoryAmbient = false;
// [0x80a04c] set by Leapfrog's constructor (0x41f1f0): the next arrival on
// the bridge trail stops at its second decision; HorseStop (0x432f30) clears it.
inline bool bridgeArrival = false;

} // namespace brc::game::trailflags
