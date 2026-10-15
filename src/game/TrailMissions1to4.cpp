// Trail missions M1..M4 (0x440580–0x4427f0): the locket, the coin frog, the
// mystery rider and the cottage. TrailNode members the ride calls at fixed points (sound
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

void TrailNode::m1LoadSounds() {
    notPorted("M1_LoadSounds", 0x440580);
}

void TrailNode::m1ReleaseSounds() {
    notPorted("M1_ReleaseSounds", 0x440630);
}

void TrailNode::m1SetupStop() {
    notPorted("M1_SetupStop", 0x4406d0);
}

void TrailNode::m1CleanupStop() {
    notPorted("M1_CleanupStop", 0x440870);
}

void TrailNode::m1RideTick() {
    notPorted("M1_RideTick", 0x4409e0);
}

void TrailNode::m1MouseDown(const engine::Pick& pick) {
    (void)pick;
    notPorted("M1_MouseDown", 0x440bb0);
}

void TrailNode::m1ScriptEnded(std::string_view script) {
    (void)script;
    notPorted("M1_ScriptEnded", 0x441170);
}

void TrailNode::m2SetupStop() {
    notPorted("M2_SetupStop", 0x4412e0);
}

void TrailNode::m2CleanupStop() {
    notPorted("M2_CleanupStop", 0x4414c0);
}

void TrailNode::m2MouseDown(const engine::Pick& pick) {
    (void)pick;
    notPorted("M2_MouseDown", 0x441550);
}

void TrailNode::m2RideTick() {
    notPorted("M2_RideTick", 0x441570);
}

void TrailNode::m2LoadSounds() {
    notPorted("M2_LoadSounds", 0x441670);
}

void TrailNode::m2ReleaseSounds() {
    notPorted("M2_ReleaseSounds", 0x4416c0);
}

void TrailNode::m3SetupStop() {
    notPorted("M3_SetupStop", 0x441700);
}

void TrailNode::m3CleanupStop() {
    notPorted("M3_CleanupStop", 0x441a70);
}

void TrailNode::m3ScriptEnded(std::string_view script) {
    (void)script;
    notPorted("M3_ScriptEnded", 0x441b60);
}

void TrailNode::m3RideTick() {
    notPorted("M3_RideTick", 0x441dd0);
}

void TrailNode::m3LoadSounds() {
    notPorted("M3_LoadSounds", 0x441e60);
}

void TrailNode::m3ReleaseSounds() {
    notPorted("M3_ReleaseSounds", 0x441e80);
}

void TrailNode::m4SetupStop() {
    notPorted("M4_SetupStop", 0x441ea0);
}

void TrailNode::m4CleanupStop() {
    notPorted("M4_CleanupStop", 0x4421c0);
}

void TrailNode::m4RideTick() {
    notPorted("M4_RideTick", 0x442290);
}

void TrailNode::m4LoadSounds() {
    notPorted("M4_LoadSounds", 0x442320);
}

void TrailNode::m4ReleaseSounds() {
    notPorted("M4_ReleaseSounds", 0x442380);
}

void TrailNode::m4MouseDown(const engine::Pick& pick) {
    (void)pick;
    notPorted("M4_MouseDown", 0x4423d0);
}

void TrailNode::m4ScriptEnded(std::string_view script) {
    (void)script;
    notPorted("M4_ScriptEnded", 0x4427b0);
}

} // namespace brc::game
