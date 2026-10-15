// Trail missions M5..M8 (0x442810–0x4445cf): the roses, the wishing well, the
// bell and the vines. TrailNode members the ride calls at fixed points (sound
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

void TrailNode::m5LoadSounds() {
    notPorted("M5_LoadSounds", 0x442810);
}

void TrailNode::m5ReleaseSounds() {
    notPorted("M5_ReleaseSounds", 0x4428c0);
}

void TrailNode::m5SetupStop() {
    notPorted("M5_SetupStop", 0x442960);
}

void TrailNode::m5CleanupStop() {
    notPorted("M5_CleanupStop", 0x442af0);
}

void TrailNode::m5RideTick() {
    notPorted("M5_RideTick", 0x442c10);
}

void TrailNode::m5MouseDown(const engine::Pick& pick) {
    (void)pick;
    notPorted("M5_MouseDown", 0x442d30);
}

void TrailNode::m5ScriptEnded(std::string_view script) {
    (void)script;
    notPorted("M5_ScriptEnded", 0x442de0);
}

void TrailNode::m6LoadSounds() {
    notPorted("M6_LoadSounds", 0x443000);
}

void TrailNode::m6ReleaseSounds() {
    notPorted("M6_ReleaseSounds", 0x4430a0);
}

void TrailNode::m6SetupStop() {
    notPorted("M6_SetupStop", 0x443130);
}

void TrailNode::m6CleanupStop() {
    notPorted("M6_CleanupStop", 0x4433d0);
}

void TrailNode::m6RideTick() {
    notPorted("M6_RideTick", 0x443510);
}

void TrailNode::m6MouseDown(const engine::Pick& pick) {
    (void)pick;
    notPorted("M6_MouseDown", 0x443710);
}

void TrailNode::m6ScriptEnded(std::string_view script) {
    (void)script;
    notPorted("M6_ScriptEnded", 0x443f50);
}

void TrailNode::m7SetupStop() {
    notPorted("M7_SetupStop", 0x444080);
}

void TrailNode::m7CleanupStop() {
    notPorted("M7_CleanupStop", 0x4441e0);
}

void TrailNode::m7MouseDown(const engine::Pick& pick) {
    (void)pick;
    notPorted("M7_MouseDown", 0x444260);
}

void TrailNode::m7RideTick() {
    notPorted("M7_RideTick", 0x4442d0);
}

void TrailNode::m7LoadSounds() {
    notPorted("M7_LoadSounds", 0x444340);
}

void TrailNode::m7ReleaseSounds() {
    notPorted("M7_ReleaseSounds", 0x444360);
}

void TrailNode::m8SetupStop() {
    notPorted("M8_SetupStop", 0x444380);
}

void TrailNode::m8CleanupStop() {
    notPorted("M8_CleanupStop", 0x444480);
}

void TrailNode::m8RideTick() {
    notPorted("M8_RideTick", 0x4444f0);
}

void TrailNode::m8MouseDown(const engine::Pick& pick) {
    (void)pick;
    notPorted("M8_MouseDown", 0x444570);
}

void TrailNode::m8LoadSounds() {
    notPorted("M8_LoadSounds", 0x444590);
}

void TrailNode::m8ReleaseSounds() {
    notPorted("M8_ReleaseSounds", 0x4445b0);
}

} // namespace brc::game
