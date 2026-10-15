// The trail rescues R1..R3 (0x4445d0–0x44533f): the bird, the horseshoe and the
// branches. TrailNode members the ride calls at fixed points (sound
// loading, stop setup and cleanup, each new frame, clicks, script ends, the
// camera). Entry points not ported yet log once that they were reached. See
// docs/engine/nodes.md "TrailNode mission, certificate and rescue code".
#include "game/TrailNode.h"

#include "core/Log.h"

#include <set>
#include <string_view>

namespace brc::game {
namespace {

void notPorted(std::string_view name, unsigned address) {
    static std::set<std::string_view> logged;
    if (logged.insert(name).second)
        log::info("TrailNode::{} ({:#x}) is not ported yet", name, address);
}

} // namespace

void TrailNode::r2SetupStop() {
    notPorted("R2_SetupStop", 0x4445d0);
}

void TrailNode::r2CleanupStop() {
    notPorted("R2_CleanupStop", 0x444740);
}

void TrailNode::r2RideTick() {
    notPorted("R2_RideTick", 0x4447c0);
}

void TrailNode::r2LoadSounds() {
    notPorted("R2_LoadSounds", 0x444810);
}

void TrailNode::r2ReleaseSounds() {
    notPorted("R2_ReleaseSounds", 0x444830);
}

void TrailNode::r2MouseDown(const engine::Pick& pick) {
    (void)pick;
    notPorted("R2_MouseDown", 0x444850);
}

void TrailNode::r1SetupStop() {
    notPorted("R1_SetupStop", 0x444920);
}

void TrailNode::r1CleanupStop() {
    notPorted("R1_CleanupStop", 0x444b30);
}

void TrailNode::r1RideTick() {
    notPorted("R1_RideTick", 0x444bd0);
}

void TrailNode::r1LoadSounds() {
    notPorted("R1_LoadSounds", 0x444c50);
}

void TrailNode::r1ReleaseSounds() {
    notPorted("R1_ReleaseSounds", 0x444c80);
}

void TrailNode::r1MouseDown(const engine::Pick& pick) {
    (void)pick;
    notPorted("R1_MouseDown", 0x444cb0);
}

void TrailNode::r3SetupStop() {
    notPorted("R3_SetupStop", 0x444e80);
}

void TrailNode::r3CleanupStop() {
    notPorted("R3_CleanupStop", 0x444fe0);
}

void TrailNode::r3MouseDown(const engine::Pick& pick) {
    (void)pick;
    notPorted("R3_MouseDown", 0x445080);
}

void TrailNode::r3ScriptEnded(std::string_view script) {
    (void)script;
    notPorted("R3_ScriptEnded", 0x445200);
}

void TrailNode::r3RideTick() {
    notPorted("R3_RideTick", 0x445290);
}

void TrailNode::r3LoadSounds() {
    notPorted("R3_LoadSounds", 0x445300);
}

void TrailNode::r3ReleaseSounds() {
    notPorted("R3_ReleaseSounds", 0x445320);
}

} // namespace brc::game
