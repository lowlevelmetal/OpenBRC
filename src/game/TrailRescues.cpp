// The trail rescues R1..R3 (0x4445d0–0x44533f): the bird, the horseshoe and the
// branches. TrailNode members the ride calls at fixed points (sound loading,
// stop setup and cleanup, each new frame, clicks, script ends). A rescue is
// offered by the pager (0x41df10) and accepted there (0x406690), which makes
// it the active mission on its trail; it is done at one stop of that trail
// and earns a badge (rec+0xD8 horseshoe, +0xDC bird, +0xE0 branches). See
// docs/engine/nodes.md "TrailNode mission, certificate and rescue code".
#include "game/TrailNode.h"

#include "core/StringUtil.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "engine/SoundManager.h"
#include "game/Godiva.h"
#include "game/PlayerRecord.h"
#include "game/Widgets.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace brc::game {
namespace {

constexpr int kHorseshoeClick = 0x2726;
constexpr int kBirdClick = 0x2727;
constexpr int kNestClick = 0x2728;
constexpr int kBranchClick = 0x2729;
constexpr std::uint32_t kNoMission = 0x11, kAnyTrail = 7;

// [0x80a2a0]: the cursor's name while Barbie carries the bird.
std::string g_cursorBeforeBird;

} // namespace

bool TrailNode::rescueStopFrame(std::uint32_t& frame) const {
    // R1 and R2 SetupStop read the trail position themselves: the trail
    // movie's frame, plus the insert's frame while an insert plays (unlike
    // GetTrailFrame, without taking the insert's length off). Anything else
    // (a refusal loop) ends the setup.
    if (!m_current || !m_base)
        return false;
    if (m_current == m_base.get()) {
        frame = m_base->frameNum();
        return true;
    }
    if (m_current == m_insert.get()) {
        frame = m_base->frameNum() + m_insert->frameNum();
        return true;
    }
    return false;
}

// --- R2: the horseshoe (rainbowS, mission 12) -------------------------------------------------------

void TrailNode::r2SetupStop() {
    // R2_SetupStop (0x4445d0): the horseshoe at rainbowS's generic stop, during
    // the HorseShoe insert or at trail frames 0x5B..0x9F.
    m_missionTrail = m_mapLocation;
    std::uint32_t f = 0;
    if (!rescueStopFrame(f))
        return;
    m_rescue.horseshoe = nullptr;
    m_rescue.horseshoeRollover.reset(); // (the original only clears the pointer)
    if (m_mapLocation != static_cast<int>(app().session().missionTrail)) {
        m_missionTrail = -1;
        return;
    }
    if (m_mapLocation != 3 || m_decision != 5)
        return;
    if (!m_insert && (m_current != m_base.get() || f <= 0x5A || f >= 0xA0))
        return;
    m_rescue.horseshoe = createAnimAtViewport("rescue2");
    if (!m_rescue.horseshoe)
        return;
    m_rescue.horseshoe->setClickId(kHorseshoeClick);
    m_rescue.horseshoeRollover = std::make_unique<ACRollover>(*this, m_rescue.horseshoe, 7);
    playVoiceNoFlaps("DB0253");
}

void TrailNode::r2CleanupStop() {
    // R2_CleanupStop (0x444740).
    m_rescue.db0252Said = false;
    if (m_mapLocation != m_missionTrail || m_mapLocation != 3 || m_decision != 5)
        return;
    if (m_rescue.horseshoe)
        engine().anims().remove(m_rescue.horseshoe->name());
    m_rescue.horseshoeRollover.reset();
    m_rescue.horseshoe = nullptr;
}

void TrailNode::r2RideTick() {
    // R2_RideTick (0x4447c0).
    if (m_mapLocation != static_cast<int>(app().session().missionTrail))
        return;
    const auto f = static_cast<std::uint32_t>(trailFrame());
    if (!m_rescue.db0252Said && f > 0x50) {
        playVoice("db0252");
        m_rescue.db0252Said = true;
    }
}

void TrailNode::r2LoadSounds() {
    // R2_LoadSounds (0x444810).
    engine().sounds().getSound("db0252", true, false);
}

void TrailNode::r2ReleaseSounds() {
    // R2_ReleaseSounds (0x444830).
    engine().sounds().retain("db0252", false);
}

void TrailNode::r2MouseDown(const engine::Pick& pick) {
    // R2_MouseDown (0x444850): picking up the horseshoe earns the badge. Unlike
    // the other two rescues it clears the mission whatever it is.
    if (!pick.element || pick.element->clickId() != kHorseshoeClick)
        return;
    playVoiceNoFlaps("DB0255");
    if (m_rescue.horseshoe)
        m_rescue.horseshoe->hide(true);
    m_rescue.horseshoeRollover.reset();
    auto& rec = app().record();
    rec.setU32(PlayerRecord::kBadge + 0, rec.u32(PlayerRecord::kBadge + 0) + 1); // app+0x288
    auto& s = app().session();
    s.activeMission = kNoMission;
    s.missionTrail = kAnyTrail;
    m_dropInsertOnRestart = true; // +0x7BE: a badge was shown at this stop
    m_jumping = true;             // +0x3EC (as coded)
    m_certificate = createAnimAtViewport("badgeswon_horseshoe");
    if (m_certificate)
        m_certificate->setClickId(kCertificateClick);
    app().saveGame();
}

// --- R1: the fallen bird (appleW, mission 13) ---------------------------------------------------------

void TrailNode::r1SetupStop() {
    // R1_SetupStop (0x444920): the bird on the ground and its nest at appleW's
    // generic stop, trail frames 0xEF..0x12B (the FallenBird insert).
    m_missionTrail = m_mapLocation;
    std::uint32_t f = 0;
    if (!rescueStopFrame(f))
        return;
    m_rescue.birdRollover.reset(); // (the original only clears the pointer)
    if (m_mapLocation != static_cast<int>(app().session().missionTrail) || f <= 0xEE || f >= 300) {
        m_missionTrail = -1;
        return;
    }
    if (m_mapLocation != 0 || m_decision != 5)
        return;
    deleteDecisionAnim();
    showDecisionAnim("rescue1Background");
    playVoice("DB0270");
    // Both keep the depth given here when their sequence changes (flag 8).
    m_rescue.bird = createAnimAtViewport("rescue1");
    if (m_rescue.bird) {
        m_rescue.bird->setPosition(m_rescue.bird->x(), m_rescue.bird->y(), 175.0f);
        m_rescue.bird->addHeaderFlags(8);
        m_rescue.bird->setClickId(kBirdClick);
        m_rescue.birdRollover = std::make_unique<ACRollover>(*this, m_rescue.bird, 7);
    }
    m_rescue.nest = createAnimAtViewport("rescue1", "nest");
    if (m_rescue.nest) {
        m_rescue.nest->setPosition(m_rescue.nest->x(), m_rescue.nest->y(), 174.0f);
        m_rescue.nest->addHeaderFlags(8);
        m_rescue.nest->setClickId(kNestClick);
        m_rescue.nest->setSequence(1, true);
    }
}

void TrailNode::r1CleanupStop() {
    // R1_CleanupStop (0x444b30).
    m_rescue.db9064Said = false;
    m_rescue.db0269Said = false;
    if (m_mapLocation != m_missionTrail || m_mapLocation != 0 || m_decision != 5 || !m_rescue.nest)
        return;
    m_rescue.birdRollover.reset();
    // Leaving the stop with the bird still in hand: the original deletes the
    // anim its cursor points at. Give the cursor back first (not in 0x444b30).
    if (m_rescue.bird && cursor() == m_rescue.bird) {
        setCursor(g_cursorBeforeBird);
        edgeRightAlt = true;
    }
    auto& anims = engine().anims();
    if (m_rescue.bird)
        anims.remove(m_rescue.bird->name());
    anims.remove(m_rescue.nest->name());
    m_rescue.nest = nullptr;
    m_rescue.bird = nullptr;
}

void TrailNode::r1RideTick() {
    // R1_RideTick (0x444bd0).
    if (m_mapLocation != static_cast<int>(app().session().missionTrail))
        return;
    const auto f = static_cast<std::uint32_t>(trailFrame());
    if (!m_rescue.db0269Said && f > 0xEE && f < 0x102) {
        playVoiceNoFlaps("DB0269");
        m_rescue.db0269Said = true;
        return;
    }
    if (!m_rescue.db9064Said && f > 0x10C && f < 300) {
        playVoiceNoFlaps("DB9064");
        m_rescue.db9064Said = true;
    }
}

void TrailNode::r1LoadSounds() {
    // R1_LoadSounds (0x444c50).
    auto& snd = engine().sounds();
    snd.getSound("db0269", true, false);
    snd.getSound("db9064", true, false);
}

void TrailNode::r1ReleaseSounds() {
    // R1_ReleaseSounds (0x444c80).
    auto& snd = engine().sounds();
    snd.retain("db0269", false);
    snd.retain("db9064", false);
}

void TrailNode::r1MouseDown(const engine::Pick& pick) {
    // R1_MouseDown (0x444cb0): the bird becomes the cursor; carried to the nest
    // it earns the badge.
    if (!pick.element)
        return;
    const int id = pick.element->clickId();
    if (id == kBirdClick) {
        playVoice("DB0272");
        g_cursorBeforeBird = cursor() ? cursor()->name() : std::string();
        m_rescue.birdRollover.reset();
        edgeRightAlt = false; // +0xC8
        setCursor("rescue1");
        if (cursor()) {
            cursor()->recenter();
            cursor()->move(0, 0, 1000.0f);
        }
    } else if (id == kNestClick) {
        if (!cursor() || cursor()->name() != "rescue1") // strcmp
            return;
        if (auto* nest = dynamic_cast<engine::Anim*>(pick.element))
            nest->setSequence(2, true);
        playVoice("DB0273");
        auto& rec = app().record();
        rec.setU32(PlayerRecord::kBadge + 4, rec.u32(PlayerRecord::kBadge + 4) + 1); // app+0x28C
        auto& s = app().session();
        if (s.activeMission == 0xD) {
            s.activeMission = kNoMission;
            s.missionTrail = kAnyTrail;
        }
        m_certificate = createAnimAtViewport("badgeswon_bird");
        if (m_certificate)
            m_certificate->setClickId(kCertificateClick);
        app().saveGame();
        m_dropInsertOnRestart = true; // +0x7BE
        setCursor(g_cursorBeforeBird);
        edgeRightAlt = true;
    }
}

// --- R3: the branches (rabbitE, mission 14) -----------------------------------------------------------

void TrailNode::r3SetupStop() {
    // R3_SetupStop (0x444e80): fallen branches across rabbitE at its generic
    // stop, trail frames 0x211..0x25A (the Branches insert).
    m_rescue.clearBackground = nullptr;
    m_missionTrail = m_mapLocation;
    m_rescue.branch = nullptr;
    m_rescue.branchRollover.reset(); // (the original only clears the pointer)
    const auto f = static_cast<std::uint32_t>(trailFrame());
    if (m_mapLocation != static_cast<int>(app().session().missionTrail)) {
        m_missionTrail = -1;
        return;
    }
    if (m_mapLocation != 4)
        return;
    if (m_decision != 5 || f <= 0x210 || f >= 0x25B) {
        m_rescue.db0286Said = false;
        return;
    }
    m_walkDisabled = true;    // +0x338
    m_skipStopComment = true; // +0x64C
    m_rescue.branch = createAnimAtViewport("r3branch");
    if (m_rescue.branch)
        m_rescue.branch->setClickId(kBranchClick);
    m_rescue.clearBackground = createAnimAtViewport("r3clearbg");
    if (m_rescue.branch)
        m_rescue.branchRollover = std::make_unique<ACRollover>(*this, m_rescue.branch, 7);
}

void TrailNode::r3CleanupStop() {
    // R3_CleanupStop (0x444fe0).
    if (m_mapLocation == m_missionTrail && m_mapLocation == 4 && m_decision == 5 && m_rescue.branch) {
        m_rescue.branchRollover.reset();
        auto& anims = engine().anims();
        anims.remove(m_rescue.branch->name());
        if (m_rescue.clearBackground)
            anims.remove(m_rescue.clearBackground->name());
        m_rescue.clearBackground = nullptr;
        m_rescue.branch = nullptr;
    }
    m_walkDisabled = false;
}

void TrailNode::r3MouseDown(const engine::Pick& pick) {
    // R3_MouseDown (0x445080): each click moves the branches one step (both
    // anims to the next sequence); the third clears the trail.
    if (!pick.element || pick.element->clickId() != kBranchClick || !m_rescue.branch)
        return;
    const int next = m_rescue.branch->sequence() + 1; // [0x80a320]
    engine().sounds().playSound("branch", -1, false, false, 1, false, 0x70, -1, 0, false);
    if (next != 3) {
        m_rescue.branch->setSequence(next, true);
        if (m_rescue.clearBackground)
            m_rescue.clearBackground->setSequence(next, true);
        return;
    }
    playVoice("db0289");
    auto& rec = app().record();
    rec.setU32(PlayerRecord::kBadge + 8, rec.u32(PlayerRecord::kBadge + 8) + 1); // app+0x290
    auto& s = app().session();
    if (s.activeMission == 0xE) {
        s.activeMission = kNoMission;
        s.missionTrail = kAnyTrail;
    }
    m_certificate = createAnimAtViewport("badgeswon_branches");
    if (m_certificate)
        m_certificate->setClickId(kCertificateClick);
    m_dropInsertOnRestart = true; // +0x7BE
    app().saveGame();
    m_rescue.branch->hide(true);
    m_rescue.branchRollover.reset();
}

void TrailNode::r3ScriptEnded(std::string_view script) {
    // R3_ScriptEnded (0x445200): at the stop during the Branches insert
    // (insert frames 8..0x31), "runOn" waits half a second, then Barbie talks.
    if (!m_insert || m_current != m_insert.get() || m_mapLocation != 4)
        return;
    const std::uint32_t f = m_insert->frameNum();
    if (f <= 7 || f >= 0x32)
        return;
    if (str::iequals(script, "runOn") && m_decision == 5) {
        runDelay(500);
        return;
    }
    if (str::iequals(script, "delay") && m_decision == 5)
        playVoice("db0287");
}

void TrailNode::r3RideTick() {
    // R3_RideTick (0x445290): during the Branches insert.
    if (!m_insert || m_current != m_insert.get())
        return;
    const std::uint32_t f = m_insert->frameNum();
    if (m_mapLocation == static_cast<int>(app().session().missionTrail) && !m_rescue.db0286Said && f > 7 &&
        f < 0x32) {
        playVoice("db0286");
        m_rescue.db0286Said = true;
    }
}

void TrailNode::r3LoadSounds() {
    // R3_LoadSounds (0x445300).
    engine().sounds().getSound("db0286", true, false);
}

void TrailNode::r3ReleaseSounds() {
    // R3_ReleaseSounds (0x445320).
    engine().sounds().retain("db0286", false);
}

} // namespace brc::game
