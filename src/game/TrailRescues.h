#pragma once

#include <memory>

namespace brc::engine {
class Anim;
} // namespace brc::engine

// TrailNode fields used only by the rescues R1..R3 (TrailRescues.cpp). Field comments give
// the original node offsets.

namespace brc::game {

class ACRollover;

struct RescueState {
    // R2, the horseshoe (rainbowS).
    engine::Anim* horseshoe = nullptr;             // +0x7C0: `rescue2` (click 0x2726)
    std::unique_ptr<ACRollover> horseshoeRollover; // +0x7C4
    bool db0252Said = false;                       // +0x7C8
    // R1, the fallen bird (appleW).
    engine::Anim* bird = nullptr;             // +0x7CC: `rescue1` (click 0x2727), later the cursor
    std::unique_ptr<ACRollover> birdRollover; // +0x7D0
    engine::Anim* nest = nullptr;             // +0x7D4: `rescue1` as "nest" (click 0x2728)
    bool db0269Said = false;                  // +0x7D8
    bool db9064Said = false;                  // +0x7DA
    // R3, the branches (rabbitE).
    engine::Anim* branch = nullptr;             // +0x7DC: `r3branch` (click 0x2729)
    std::unique_ptr<ACRollover> branchRollover; // +0x7E0
    engine::Anim* clearBackground = nullptr;    // +0x7E4: `r3clearbg`
    bool db0286Said = false;                    // +0x7E8
};

} // namespace brc::game
