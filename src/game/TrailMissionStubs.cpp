// The trail missions, certificates and rescues (0x43eca0–0x4457f0): TrailNode
// members the ride calls at fixed points (sound loading, stop setup and
// cleanup, each new frame, clicks, script ends, the camera). They are not
// ported yet; each entry point logs once that it was reached. The names are
// the original's (M1..M8 missions, C1..C4 certificates, R1..R3 rescues, the
// riding lesson). See docs/engine/nodes.md "TrailNode mission, certificate and
// rescue code".
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
