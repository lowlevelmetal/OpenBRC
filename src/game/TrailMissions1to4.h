#pragma once

// TrailNode fields used only by missions M1..M4 (TrailMissions1to4.cpp). Field comments give
// the original node offsets. The TrailNode constructor (0x431420) zeroes them all.

#include <memory>

namespace brc::engine {
class Anim;
} // namespace brc::engine

namespace brc::game {

class ACRollover;

struct Missions1to4State {
    // M1, the locket.
    engine::Anim* knot = nullptr;                  // +0x6E8: `m1 knot` (click 0x2719 once the owl has flown)
    engine::Anim* locket = nullptr;                // +0x6EC: `m1 locket` (0x271A)
    std::unique_ptr<ACRollover> knotRollover;      // +0x6F4
    std::unique_ptr<ACRollover> locketRollover;    // +0x6F8
    bool said0025 = false;                         // +0x6FE: beachE ride voices
    bool said0017 = false;                         // +0x700: appleW
    bool said0009 = false;                         // +0x702: rabbitE
    bool said0567 = false;                         // +0x704: rabbitE
    bool said0568 = false;                         // +0x706: appleW
    bool said00261 = false;                        // +0x708: beachE
    bool said0570 = false;                         // +0x70A: beachE
    // (+0x70C, the trail of the last wrong-trail hint, is TrailNode::m_hintTrail.)

    // M2, the coin frog.
    engine::Anim* frog = nullptr;                  // +0x710: `coinFrog` (0x271B)
    std::unique_ptr<ACRollover> frogRollover;      // +0x714
    bool said03003 = false;                        // +0x718

    // M3, the mystery rider. The "seen" flags +0x728/+0x72A/+0x72C are
    // TrailNode::m_hoofPrintsShown (the ride also reads them).
    engine::Anim* rider = nullptr;                 // +0x71C: Christie, Teresa, the deer or the mystery horse
    bool hintSaid = false;                         // +0x720: db04098
    int sightings = 0;                             // +0x724: in this node

    // M4, the cottage. (The bush and gate are TrailNode::m_bush / m_gate.)
    engine::Anim* cottageGrass = nullptr;          // +0x734
    engine::Anim* windMill = nullptr;              // +0x738 (0x271E during the finale)
    engine::Anim* windMillOverlay = nullptr;       // +0x73C (0x271E)
    std::unique_ptr<ACRollover> overlayRollover;   // +0x744
    bool said04123 = false;                        // +0x748
    bool said0074 = false;                         // +0x74A
};

} // namespace brc::game
