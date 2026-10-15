#pragma once

#include <memory>

namespace brc::engine {
class Anim;
} // namespace brc::engine

namespace brc::game {

class ACRollover;

// TrailNode fields used only by missions M5..M8 (TrailMissions5to8.cpp). Field comments give
// the original node offsets. (M5's ride-voice flags are TrailNode::m_field74C, +0x74C..+0x752;
// the wishing well, the bush and the bell are TrailNode members shared with the stop setup.)
struct Missions5to8State {
    // The exit-prop rollover (+0x6D4) M5_SetupStop drops; see there.
    std::unique_ptr<ACRollover> droppedRollover;
    engine::Anim* handle = nullptr;                    // +0x75C: `m6 hidden handle` (click 0x2720)
    engine::Anim* squirrel = nullptr;                  // +0x760: `well squirrel` (click 0x2722)
    std::unique_ptr<ACRollover> handleRollover;        // +0x76C
    std::unique_ptr<ACRollover> squirrelRollover;      // +0x770
    // M6 ride voices already said.
    bool saidDb0132 = false;                           // +0x776
    bool saidDb0572 = false;                           // +0x778
    bool saidWellVoice = false;                        // +0x77A: db02006, db04055 or db04056
    bool saidDb02022 = false;                          // +0x77C
    bool saidDb04053 = false;                          // +0x77E
    bool saidBellVoice = false;                        // +0x78C: db0147N or db02025
    bool saidDb0176 = false;                           // +0x78E
    engine::Anim* vines = nullptr;                     // +0x790: `vines_shoe` / `vines_noshoe` (0x2711)
    std::unique_ptr<ACRollover> vinesRollover;         // +0x794
};

} // namespace brc::game
