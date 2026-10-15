// The trail certificates C1..C4 (0x43eca0–0x440580) and the riding lesson
// (0x445340–0x4457f0). TrailNode members the ride calls at fixed points (sound
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

void TrailNode::c1LoadSounds() {
    notPorted("C1_LoadSounds", 0x43eca0);
}

void TrailNode::c1ReleaseSounds() {
    notPorted("C1_ReleaseSounds", 0x43ecd0);
}

void TrailNode::c1SetupStop() {
    notPorted("C1_SetupStop", 0x43ed00);
}

void TrailNode::c1CleanupStop() {
    notPorted("C1_CleanupStop", 0x43ee10);
}

void TrailNode::c1RideTick() {
    notPorted("C1_RideTick", 0x43eeb0);
}

void TrailNode::c1MouseDown(const engine::Pick& pick) {
    (void)pick;
    notPorted("C1_MouseDown", 0x43efc0);
}

void TrailNode::c1CameraClick() {
    notPorted("C1_CameraClick", 0x43f0b0);
}

void TrailNode::c2SetupStop() {
    notPorted("C2_SetupStop", 0x43f2d0);
}

void TrailNode::c2CleanupStop() {
    notPorted("C2_CleanupStop", 0x43f460);
}

void TrailNode::c2ScriptEnded(std::string_view script) {
    (void)script;
    notPorted("C2_ScriptEnded", 0x43f510);
}

void TrailNode::c2MouseDown(const engine::Pick& pick) {
    (void)pick;
    notPorted("C2_MouseDown", 0x43f5a0);
}

void TrailNode::c2CameraClick() {
    notPorted("C2_CameraClick", 0x43f6b0);
}

void TrailNode::c2RideTick() {
    notPorted("C2_RideTick", 0x43f8e0);
}

void TrailNode::c2LoadSounds() {
    notPorted("C2_LoadSounds", 0x43f9d0);
}

void TrailNode::c2ReleaseSounds() {
    notPorted("C2_ReleaseSounds", 0x43fa50);
}

void TrailNode::c3SetupStop() {
    notPorted("C3_SetupStop", 0x43fad0);
}

void TrailNode::c3CleanupStop() {
    notPorted("C3_CleanupStop", 0x43fb30);
}

void TrailNode::c3MouseDown(const engine::Pick& pick) {
    (void)pick;
    notPorted("C3_MouseDown", 0x43fb60);
}

void TrailNode::c3CameraClick() {
    notPorted("C3_CameraClick", 0x43fc20);
}

void TrailNode::c3RideTick() {
    notPorted("C3_RideTick", 0x43fd50);
}

void TrailNode::c3LoadSounds() {
    notPorted("C3_LoadSounds", 0x43fdb0);
}

void TrailNode::c3ReleaseSounds() {
    notPorted("C3_ReleaseSounds", 0x43fdd0);
}

void TrailNode::c4LoadSounds() {
    notPorted("C4_LoadSounds", 0x43fdf0);
}

void TrailNode::c4ReleaseSounds() {
    notPorted("C4_ReleaseSounds", 0x43fe60);
}

void TrailNode::c4SetupStop() {
    notPorted("C4_SetupStop", 0x43fec0);
}

void TrailNode::c4UpdateProgress() {
    notPorted("C4_UpdateProgress", 0x43ff10);
}

void TrailNode::c4LoadTrail(int arg) {
    (void)arg;
    notPorted("C4_LoadTrail", 0x440040);
}

void TrailNode::c4UnloadTrail() {
    notPorted("C4_UnloadTrail", 0x440340);
}

void TrailNode::lessonLoadSounds() {
    notPorted("Lesson_LoadSounds", 0x445340);
}

void TrailNode::lessonReleaseSounds() {
    notPorted("Lesson_ReleaseSounds", 0x4453b0);
}

void TrailNode::lessonRideTick() {
    notPorted("Lesson_RideTick", 0x445420);
}

void TrailNode::lessonIdleHint() {
    notPorted("Lesson_IdleHint", 0x4457f0);
}

} // namespace brc::game
