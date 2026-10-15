// Trail missions M1..M4 (0x440580–0x4427f0): the locket, the coin frog, the
// mystery rider and the cottage. TrailNode members the ride calls at fixed
// points (sound loading, stop setup and cleanup, each new frame, clicks,
// script ends). See docs/engine/nodes.md "TrailNode mission, certificate and
// rescue code" for the mission steps and the fields.
#include "game/TrailNode.h"

#include "core/StringUtil.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "engine/SoundManager.h"
#include "game/Godiva.h"
#include "game/MiniGameFlags.h"
#include "game/PlayerRecord.h"
#include "game/Popups.h"
#include "game/Widgets.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace brc::game {
namespace {

// [0x80a278] / [0x80a284]: ride frames on another trail before the M1 / M2
// reminder. The M2 counter is never reset, so its reminder plays once.
int s_m1WrongTrailFrames = 0;
int s_m2WrongTrailFrames = 0;
// [0x80a288] the M3 rider anims are up; [0x80a28c] the appleW rider: 0
// Christie, 1 Teresa.
bool s_m3Shown = false;
int s_m3Rider = 0;

// PlaySound(name, -1, 0, 0, loops, 0, [0x7f1abc] = 0x70, -1, 0, 0).
void sfx(engine::SoundManager& snd, std::string_view name, int loops = 1) {
    snd.playSound(name, -1, false, false, loops, false, 0x70, -1, 0, false);
}

void getSounds(engine::SoundManager& snd, std::span<const std::string_view> names) {
    for (auto n : names)
        snd.getSound(n, true, false); // 0x44c180(name, 1, 0)
}

void releaseSounds(engine::SoundManager& snd, std::span<const std::string_view> names) {
    for (auto n : names)
        snd.retain(n, false); // 0x44c8b0(name, 0)
}

int clickId(const engine::Pick& pick) { return pick.element ? pick.element->clickId() : 0; }

constexpr std::size_t kMissionDone = PlayerRecord::kMissionDone; // app+0x258: u32 per mission

} // namespace

// --- M1: the locket (rabbitE mailbox -> appleW knot -> beachE locket) ---------------------------------

namespace {
// The release list names db0015 where the load list has db0017 (as in the original).
constexpr std::array<std::string_view, 8> kM1Load = {"db04092", "db0009",  "db0567",  "db0017",
                                                     "db0568",  "db0025",  "db00261", "db0570"};
constexpr std::array<std::string_view, 8> kM1Release = {"db04092", "db0009",  "db0567",  "db0015",
                                                        "db0568",  "db0025",  "db00261", "db0570"};
} // namespace

void TrailNode::m1LoadSounds() {
    // M1_LoadSounds (0x440580).
    getSounds(engine().sounds(), kM1Load);
}

void TrailNode::m1ReleaseSounds() {
    // M1_ReleaseSounds (0x440630).
    releaseSounds(engine().sounds(), kM1Release);
}

void TrailNode::m1SetupStop() {
    // M1_SetupStop (0x4406d0): on the mission's current trail, the knot at
    // appleW's owl stop once the first note is in the bag, the mailbox stop on
    // rabbitE (its props are the trail's own) and the locket at beachE's
    // first stop.
    auto& m = m_m14;
    m_missionTrail = m_mapLocation;
    m.knot = nullptr;
    m.knotRollover.reset();
    m.locket = nullptr;
    m.locketRollover.reset();
    if (m_mapLocation != static_cast<int>(app().session().missionTrail)) {
        m_missionTrail = -1;
        return;
    }
    if (m_mapLocation == 0) {
        if (m_decision == 2 && hasBagItem(0xD)) {
            m_skipStopComment = true;
            m.knot = createAnimAtViewport("m1 knot");
        }
    } else if (m_mapLocation == 4) {
        if (m_decision == 0)
            m_skipStopComment = true;
    } else if (m_mapLocation == 5 && m_decision == 1) {
        m_skipStopComment = true;
        m.locket = createAnimAtViewport("m1 locket");
        if (m.locket) {
            m.locket->setClickId(0x271A);
            m.locketRollover = std::make_unique<ACRollover>(*this, m.locket, 7);
        }
    }
}

void TrailNode::m1CleanupStop() {
    // M1_CleanupStop (0x440870). Quirk: leaving the locket stop with the
    // locket still closed clears the mission without counting it.
    auto& m = m_m14;
    auto& s = app().session();
    auto& anims = engine().anims();
    if (m_mapLocation == m_missionTrail) {
        if (m_mapLocation == 0) {
            if (m_decision == 2) {
                if (m.knot)
                    anims.remove("m1 knot");
                m.knot = nullptr;
                m.knotRollover.reset();
            }
        } else if (m_mapLocation == 5 && m_decision == 1) {
            if (m.locket) {
                anims.remove("m1 locket");
                m.locket = nullptr;
                if (s.activeMission == 0) {
                    s.activeMission = 0x11;
                    s.missionTrail = 7;
                    engine().sounds().stopSound("musicbox");
                }
            }
            m.locketRollover.reset();
        }
    }
    // The ride voices of the legs still ahead are said again.
    if (!hasBagItem(0xD)) {
        m.said0567 = false;
        m.said0009 = false;
    } else if (!hasBagItem(0xE)) {
        m.said0009 = false;
        m.said0017 = false;
    } else if (!hasBagItem(0xF)) {
        m.said0025 = false;
        m.said00261 = false;
        m.said0570 = false;
    }
    m_hintTrail = 7;
}

void TrailNode::m1RideTick() {
    // M1_RideTick (0x4409e0): Barbie's remarks on the mission's trail, and on
    // the others, after 25 frames, the reminder to look in the mailbox.
    auto& m = m_m14;
    const auto f = static_cast<std::uint32_t>(trailFrame());
    const int missionTrail = static_cast<int>(app().session().missionTrail);
    if (m_mapLocation == missionTrail) {
        if (m_mapLocation == 0) {
            if (f < 0xBE)
                m.said0568 = false;
            if (!m.said0017 && f > 0x2D && f < 0xBE) {
                playVoice("db0017");
                m.said0017 = true;
                return;
            }
            if (!m.said0568 && f > 0xBE && f < 200) {
                playVoice("db0568");
                m.said0568 = true;
            }
        } else if (m_mapLocation == 4) {
            if (!m.said0009 && f > 0x5A && f < 0x5F) {
                playVoice("db0009");
                m.said0009 = true;
                return;
            }
            if (!m.said0567 && f > 0x7D && f < 0x82) {
                playVoice("db0567");
                m.said0567 = true;
            }
        } else if (m_mapLocation == 5) {
            if (!m.said0025 && f > 0x145) {
                playVoice("db0025");
                m.said0025 = true;
                return;
            }
            if (!m.said00261 && f > 0x1CC) {
                playVoice("db00261");
                m.said00261 = true;
                return;
            }
            if (!m.said0570 && f > 0x1EF) {
                playVoice("db0570");
                m.said0570 = true;
            }
        }
    } else if (missionTrail == 0 && m_hintTrail != m_mapLocation) {
        if (s_m1WrongTrailFrames == 0x19) {
            playVoice("db04092");
            s_m1WrongTrailFrames = 0;
            m_hintTrail = m_mapLocation;
            return;
        }
        ++s_m1WrongTrailFrames;
    }
}

void TrailNode::m1MouseDown(const engine::Pick& pick) {
    // M1_MouseDown (0x440bb0).
    auto& m = m_m14;
    auto& s = app().session();
    auto& snd = engine().sounds();
    switch (clickId(pick)) {
    case kMailboxClick:
        if (!m_mailbox)
            return;
        if (m_mailbox->sequence() == 0) {
            sfx(snd, "mailOpen");
            if (!hasBagItem(0xD) && !hasBagItem(0xE)) {
                // The note is inside.
                m_mailbox->setSequence(2, true);
                playVoice("db04090");
            } else {
                m_mailbox->setSequence(1, true);
            }
        } else if (m_mailbox->sequence() == 1) {
            sfx(snd, "mailClos");
            m_mailbox->setSequence(0, true);
        } else if (m_mailbox->sequence() == 2) {
            sfx(snd, "clue");
            playVoice("db04091");
            playVoiceAfterPause("db0013");
            playVoiceAfterPause("db04095");
            playVoiceAfterPause("db04092");
            m_pickup = createAnimAtViewport("locketNote1Out");
            if (m_pickup) {
                m_pickup->setClickId(kPickupClick);
                m_pickup->setSequence(1, true);
            }
            m_pickupItem = 0xD;
            m_mailbox->setSequence(1, true);
            s.missionTrail = 0;
        }
        return;
    case kPickupClick:
        // The locket popup closed (TrailNode hides it and adds the item).
        if (m_pickupItem == 0xF) {
            snd.stopSound("musicbox");
            sfx(snd, "locketClose");
            playVoice("db9023");
            m_pagerDelay = 8000;
            if (!checkAllMissionsDone())
                runScript("PagerDelay");
            if (s.activeMission == 0) {
                s.activeMission = 0x11;
                s.missionTrail = 7;
            }
        }
        return;
    case kOwlClick:
        // The owl flies off the knot.
        if (m.knot && !m.knotRollover) {
            m.knot->setClickId(0x2719);
            m.knotRollover = std::make_unique<ACRollover>(*this, m.knot, 7);
            playVoice("db04094");
        }
        return;
    case 0x2719:
        if (m.knot && m.knot->sequence() == 0) {
            sfx(snd, "clue");
            playVoice("db04004");
            m_pickup = createAnimAtViewport("locketNote2Out");
            if (m_pickup) {
                m_pickup->setClickId(kPickupClick);
                m_pickup->setSequence(1, true);
            }
            m_pickupItem = 0xE;
            m.knot->setSequence(1, true);
            s.missionTrail = 5;
            m.knotRollover.reset();
        }
        return;
    case 0x271A:
        if (m.locket && m.locket->hideCount() == 0) {
            sfx(snd, "locketOpen");
            sfx(snd, "musicbox", 0);
            playVoice(app().record().u16(PlayerRecord::kVoiceVariantC) == 0 ? "db04164" : "db04165");
            m_pickup = createAnimAtViewport("locketOut");
            if (m_pickup) {
                m_pickup->setClickId(kPickupClick);
                m_pickup->setSequence(1, true);
            }
            m_pickupItem = 0xF;
            deleteAnim(m.locket);
            m.locketRollover.reset();
            auto& rec = app().record();
            rec.setU32(kMissionDone, rec.u32(kMissionDone) + 1); // app+0x258
            app().saveGame();
        }
        return;
    default:
        return;
    }
}

void TrailNode::m1ScriptEnded(std::string_view script) {
    // M1_ScriptEnded (0x441170): the prompts at the owl and mailbox stops.
    if (m_mapLocation != static_cast<int>(app().session().missionTrail))
        return;
    const bool runOn = str::iequals(script, "runOn");
    const bool delay = str::iequals(script, "delay");
    if (m_mapLocation == 0) {
        if (runOn && m_decision == 2) {
            runDelay(500);
            return;
        }
        if (delay && m_decision == 2 && !hasBagItem(0xE))
            playVoice("db04093");
    } else if (m_mapLocation == 4) {
        if (runOn && m_decision == 0) {
            runDelay(500);
            return;
        }
        if (delay && m_decision == 0) {
            if (m_pickup && hasBagItem(0xD)) {
                if (m_pickup->sequence() == 1)
                    playVoice("db0013");
                return;
            }
            if (m_mailbox && m_mailbox->sequence() == 0)
                playVoiceAfterPause("db0010");
        }
    }
}

// --- M2: the coin frog (bridgeS) -------------------------------------------------------------------------

namespace {
constexpr std::array<std::string_view, 3> kM2Sounds = {"db04117", "croak", "db03003"};
} // namespace

void TrailNode::m2SetupStop() {
    // M2_SetupStop (0x4412e0), at every stop: the coin frog at the bridge's
    // second stop while the mission is on, and for good once it is done
    // (app+0x25C). Back from leapfrog with the coin, the mission ends here and
    // the saddlebag opens on it.
    auto& m = m_m14;
    auto& s = app().session();
    m_missionTrail = m_mapLocation;
    const bool active = (m_mapLocation == static_cast<int>(s.missionTrail) && s.activeMission == 1) ||
                        (app().record().u32(kMissionDone + 1 * 4) != 0 && m_trail == 2);
    if (!active) {
        m_missionTrail = -1;
        return;
    }
    if (m_mapLocation != 2 || m_decision != 1) // [0x7f398c] = 1
        return;
    m.frog = createAnimAtViewport("coinFrog");
    if (!m.frog)
        return;
    m.frog->setClickId(0x271B);
    m.frog->pause(true);
    m.frog->move(0.0f, 0.0f, 1000.0f);
    m.frogRollover = std::make_unique<ACRollover>(*this, m.frog, 7);
    if (!flags::leapfrogCoinTaken) {
        playVoiceNoFlaps("DB04118");
        return;
    }
    playVoice(engine().rand() % 2 == 0 ? "DB9023" : "db9022");
    m_pagerDelay = 9000;
    if (!checkAllMissionsDone() && s.activeMission == 1) {
        runScript("PagerDelay");
        s.activeMission = 0x11;
        s.missionTrail = 7;
    }
    flags::leapfrogCoinTaken = false;
    if (popupKind() == popup::kNone)
        openPopup(popup::kSaddlebag);
}

void TrailNode::m2CleanupStop() {
    // M2_CleanupStop (0x4414c0), at every stop.
    auto& m = m_m14;
    auto& s = app().session();
    m.said03003 = false;
    const bool active = (m_mapLocation == static_cast<int>(s.missionTrail) && s.activeMission == 1) ||
                        (app().record().u32(kMissionDone + 1 * 4) != 0 && m_trail == 2);
    if (!active || m_mapLocation != 2 || m_decision != 1 || !m.frogRollover)
        return;
    flags::leapfrogCoinTaken = false;
    m.frogRollover.reset();
    engine().anims().remove("coinFrog");
    m.frog = nullptr;
}

void TrailNode::m2MouseDown(const engine::Pick& pick) {
    // M2_MouseDown (0x441550): the frog's script leads to leapfrog.
    if (clickId(pick) == 0x271B)
        runScript("bridgeFrog");
}

void TrailNode::m2RideTick() {
    // M2_RideTick (0x441570): on the bridge trail a remark and the frog
    // croaking every 4 s near it; on the others, once, a reminder.
    auto& m = m_m14;
    const auto f = static_cast<std::uint32_t>(trailFrame());
    const int missionTrail = static_cast<int>(app().session().missionTrail);
    if (m_mapLocation == missionTrail) {
        if (m_mapLocation != 2)
            return;
        if (!m.said03003 && f > 0x4B && f < 0x73) {
            playVoice("db03003");
            m.said03003 = true;
        }
        if (f > 100 && f < 0x9B) {
            // [0x80a27c], set on the first pass here ([0x80a280] bit 0).
            static std::uint32_t lastCroak = engine().ticks();
            if (engine().ticks() - lastCroak > 4000) {
                lastCroak = engine().ticks();
                sfx(engine().sounds(), "Croak");
            }
        }
    } else if (missionTrail == 2) {
        if (s_m2WrongTrailFrames == 0x19) {
            playVoice("db04117");
            m_hintTrail = m_mapLocation;
        }
        ++s_m2WrongTrailFrames;
    }
}

void TrailNode::m2LoadSounds() {
    // M2_LoadSounds (0x441670).
    getSounds(engine().sounds(), kM2Sounds);
}

void TrailNode::m2ReleaseSounds() {
    // M2_ReleaseSounds (0x4416c0).
    releaseSounds(engine().sounds(), kM2Sounds);
}

// --- M3: the mystery rider (appleW, rabbitE, beachE) -----------------------------------------------------

void TrailNode::m3PlayMusic(std::string_view name) {
    if (app().session().musicOff)
        return;
    if (!m_music.empty())
        stopMusic();
    m_music = name;
    app().playMusic(m_music);
}

void TrailNode::m3SetupStop() {
    // M3_SetupStop (0x441700): a stop in the rider's window (or after the
    // hoof-prints insert) sets up the hidden rider: Christie or Teresa at
    // appleW, the deer at rabbitE, the mystery horse at beachE. Any other stop
    // says db03009.
    auto& m = m_m14;
    s_m3Shown = false;
    m_missionTrail = m_mapLocation;
    std::uint32_t f = 0;
    if (m_current == m_base.get())
        f = m_base->frameNum();
    else if (m_insert && m_current == m_insert.get())
        f = m_insert->frameNum();
    else
        return;
    m.rider = nullptr;
    const bool onBase = m_current == m_base.get();
    auto setUp = [&](std::string_view music, std::string_view overlay) {
        m3PlayMusic(music);
        ++m.sightings;
        m_skipStopComment = true;
        m.hintSaid = false;
        deleteDecisionAnim();
        showDecisionAnim("m3 bkgd");
        createAnimAtViewport(overlay);
    };
    if (m_mapLocation == 0) {
        if ((m_insert || (onBase && f > 0x294 && f < 0x2FD)) && m_decision == 5 && !m_hoofPrintsShown[1]) {
            setUp("trailMusic", "m3 aa overlay");
            // Christie for the palomino, Teresa for the white horse, either for the others.
            const auto horse = app().record().horse();
            s_m3Rider = horse != 0 && (horse == 1 || engine().rand() % 2 == 0) ? 1 : 0;
            m.rider = createAnimAtViewport(s_m3Rider == 1 ? "m3 aa teresa" : "m3 aa christie");
            forceHideAnim(m.rider);
            s_m3Shown = true;
            return;
        }
    } else if (m_mapLocation == 4) {
        if ((m_insert || (onBase && f > 0x27E && f < 0x2D0)) && m_decision == 5 && !m_hoofPrintsShown[0]) {
            setUp("trailMusic", "m3 rr overlay");
            m.rider = createAnimAtViewport("m3 rr deer");
            if (m.rider)
                m.rider->pause(true);
            s_m3Shown = true;
            return;
        }
    } else if (m_mapLocation == 5) {
        if (m_decision == 1 && !m_hoofPrintsShown[2]) { // [0x804608] = 1
            setUp("mysteryMusic", "m3 tr overlay");
            m.rider = createAnimAtViewport("m3 tr mystery");
            if (m.rider)
                m.rider->pause(true);
            s_m3Shown = true;
            return;
        }
    }
    playVoice("db03009");
    m_missionTrail = -1;
    m3PlayMusic("trailMusic");
}

void TrailNode::m3CleanupStop() {
    // M3_CleanupStop (0x441a70).
    auto& m = m_m14;
    if (m_current != m_base.get() && m_current != m_insert.get())
        return;
    m.hintSaid = false;
    auto& anims = engine().anims();
    if (m_mapLocation == m_missionTrail && s_m3Shown) {
        // (The original keeps +0x71C pointing at the deleted rider; it is
        // cleared here so a late script end cannot use it.)
        if (m_mapLocation == 0) {
            if (m_decision == 5) {
                anims.remove("m3 aa overlay");
                anims.remove("m3 aa teresa");
                anims.remove("m3 aa christie");
                m.rider = nullptr;
            }
        } else if (m_mapLocation == 4) {
            if (m_decision == 5) {
                anims.remove("m3 rr overlay");
                anims.remove("m3 rr deer");
                m.rider = nullptr;
            }
        } else if (m_mapLocation == 5 && m_decision == 1) {
            anims.remove("m3 tr overlay");
            anims.remove("m3 tr mystery");
            m.rider = nullptr;
        }
    }
    s_m3Shown = false;
}

void TrailNode::m3ScriptEnded(std::string_view script) {
    // M3_ScriptEnded (0x441b60): the rider appears when the stop's "delay"
    // ends. The third sighting in this node completes the mission; the
    // mystery horse at beachE completes it at once.
    auto& m = m_m14;
    if (!m.rider)
        return;
    auto& s = app().session();
    auto& rec = app().record();
    auto complete = [&] {
        rec.setU32(kMissionDone + 2 * 4, rec.u32(kMissionDone + 2 * 4) + 1); // app+0x260
    };
    if (str::iequals(script, "runOn")) {
        if (m_decision == 5 || m_decision == 1)
            runDelay(100);
        return;
    }
    if (!str::iequals(script, "delay"))
        return;
    if (m_decision == 5) {
        forceShowAnim(m.rider);
        if (m_mapLocation == 0) {
            m_hoofPrintsShown[1] = true; // +0x72A
            if (s_m3Rider == 0) {
                playVoice("dc0484");
                playVoiceNoFlaps("db04011a");
                playVoiceNoFlaps("db04011b");
            } else if (s_m3Rider == 1) {
                playVoice("dt0485");
                playVoiceNoFlaps("db04012");
                playVoiceNoFlaps("db04011b");
            }
        } else if (m_mapLocation == 4) {
            m_hoofPrintsShown[0] = true; // +0x728
            playVoice("db04013");
        } else {
            return;
        }
        if (m.sightings != 3)
            return;
        complete();
        if (s.activeMission == 2) {
            s.activeMission = 0x11;
            s.missionTrail = 7;
        }
        app().saveGame();
        m_pagerDelay = 13000;
        if (!checkAllMissionsDone())
            runScript("PagerDelay");
        return;
    }
    if (m_decision != 1 || m_mapLocation != 5)
        return;
    m_hoofPrintsShown[2] = true; // +0x72C
    forceShowAnim(m.rider);
    playHelpHint();
    complete();
    if (s.activeMission == 2) {
        s.activeMission = 0x11;
        s.missionTrail = 7;
        playVoiceNoFlaps("db9023");
    }
    app().saveGame();
    m_pagerDelay = 0x4330;
    if (!checkAllMissionsDone())
        runScript("PagerDelay");
}

void TrailNode::m3RideTick() {
    // M3_RideTick (0x441dd0): the hint once an insert (the hoof prints) has
    // run for a moment on a trail whose rider is still unseen.
    auto& m = m_m14;
    if (!m_insert)
        return;
    const std::uint32_t f = m_insert->frameNum();
    if (m.hintSaid)
        return;
    if ((m_trail == 0 && !m_hoofPrintsShown[1] && f > 0x14) || (m_trail == 4 && !m_hoofPrintsShown[0] && f > 0x14) ||
        (m_trail == 5 && !m_hoofPrintsShown[2] && f > 0xF)) {
        playVoice("db04098");
        m.hintSaid = true;
    }
}

void TrailNode::m3LoadSounds() {
    // M3_LoadSounds (0x441e60).
    engine().sounds().getSound("db04098", true, false);
}

void TrailNode::m3ReleaseSounds() {
    // M3_ReleaseSounds (0x441e80).
    engine().sounds().retain("db04098", false);
}

// --- M4: the cottage (rabbitE bush and gate -> cottageL) ------------------------------------------------

namespace {
constexpr std::array<std::string_view, 4> kM4Sounds = {"db04123", "db0074", "db03056", "db04163"};
} // namespace

void TrailNode::m4SetupStop() {
    // M4_SetupStop (0x441ea0), at every stop while mission 3 is on this trail
    // or once the gate is open: the bush and the gate at rabbitE's fifth
    // stop, the cottage and the windmill at cottageL's first (the windmill
    // leads to the finale once every mission is done).
    auto& m = m_m14;
    auto& s = app().session();
    auto& rec = app().record();
    m_missionTrail = m_mapLocation;
    if (!((m_mapLocation == static_cast<int>(s.missionTrail) && s.activeMission == 3) ||
          rec.u16(PlayerRecord::kCottageGateOpen) != 0)) {
        m_missionTrail = -1;
        return;
    }
    if (m_mapLocation == 1) {
        if (m_decision != 0) // [0x7f3984] = 0
            return;
        m_exitProp = createAnimAtViewport("m4 cottage");
        if (m_exitProp) {
            m_exitProp->setClickId(kExitPropClick);
            m_exitPropRollover = std::make_unique<ACRollover>(*this, m_exitProp, 7);
        }
        m.cottageGrass = createAnimAtViewport("cottageGrass");
        m.windMill = createAnimAtViewport("windMill");
        if (rec.u32(PlayerRecord::kFinaleState) == 1) { // app+0x2F8
            if (m.windMill)
                m.windMill->setClickId(0x271E);
            m.windMillOverlay = createAnimAtViewport("windMillOverlay");
            if (engine::Anim* o = m.windMillOverlay) {
                o->setPosition(o->x(), o->y(), (m.windMill ? m.windMill->z() : 0.0f) - 1.0f);
                o->setClickId(0x271E);
                m.overlayRollover = std::make_unique<ACRollover>(*this, o, 7);
            }
            playVoiceNoFlaps("DB04159");
            return;
        }
        if (flags::carouselVisited) {
            playVoiceNoFlaps("DB9022");
            return;
        }
        if (rec.u32(kMissionDone + 3 * 4) == 0) { // app+0x264
            playVoice("DB0078");
            playVoiceNoFlaps("DB9048");
        } else {
            playVoice("DB0519");
        }
        flags::carouselVisited = false;
    } else if (m_mapLocation == 4) {
        if (m_decision != 4) // [0x7f39a4] = 4
            return;
        // The bush is HorseStop's `m4 bush`.
        if (m_bush) {
            m_bush->setClickId(0x271C);
            m_bushRollover = std::make_unique<ACRollover>(*this, m_bush, 7);
        }
        m_gate = createAnimAtViewport("m4 gate");
        if (m_gate) {
            m_gate->setSequence(1, true);
            m_gate->setClickId(0x271D);
        }
        createAnimAtViewport("m4 sparkle");
        playVoiceNoFlaps("DB04124");
    }
}

void TrailNode::m4CleanupStop() {
    // M4_CleanupStop (0x4421c0), at every stop.
    auto& m = m_m14;
    auto& anims = engine().anims();
    m.said04123 = false;
    if (m_bushRollover) {
        m_bushRollover.reset();
        m_bush = nullptr;
        m_gateRollover.reset();
        m_gate = nullptr;
        anims.remove("m4 sparkle");
    }
    if (anims.find("m4 cottage")) {
        anims.remove("m4 cottage");
        m_exitPropRollover.reset();
        anims.remove("cottageGrass");
        anims.remove("windMill");
        m.overlayRollover.reset();
        anims.remove("windMillOverlay");
        // (The original only clears +0x73C; the others are left pointing at
        // the deleted anims and never read.)
        m.windMillOverlay = nullptr;
        m.windMill = nullptr;
        m.cottageGrass = nullptr;
        m_exitProp = nullptr;
    }
}

void TrailNode::m4RideTick() {
    // M4_RideTick (0x442290): rabbitE remarks near the bush.
    auto& m = m_m14;
    if (m_mapLocation != static_cast<int>(app().session().missionTrail))
        return;
    const auto f = static_cast<std::uint32_t>(trailFrame());
    if (m_mapLocation != 4)
        return;
    if (!m.said04123 && f > 0x35C) {
        playVoice("db04123");
        m.said04123 = true;
        return;
    }
    if (!m.said0074 && f > 900 && !voicePlaying()) {
        playVoice("db0074");
        m.said0074 = true;
    }
}

void TrailNode::m4LoadSounds() {
    // M4_LoadSounds (0x442320).
    getSounds(engine().sounds(), kM4Sounds);
}

void TrailNode::m4ReleaseSounds() {
    // M4_ReleaseSounds (0x442380).
    releaseSounds(engine().sounds(), kM4Sounds);
}

void TrailNode::m4MouseDown(const engine::Pick& pick) {
    // M4_MouseDown (0x4423d0).
    auto& s = app().session();
    auto& rec = app().record();
    auto rideThroughGate = [&] {
        // Mounted, the bush aside and the gate open: on to the cottage.
        if (m_mounted && m_gate && m_gate->sequence() == 0 && !isScriptRunning("RunLeft")) {
            m_nextTrail = 1;
            stopAllScripts();
            runScript("RunLeft");
        }
    };
    switch (clickId(pick)) {
    case kExitPropClick:
        m_exitPropNode = 12; // carousel
        return;
    case 0x271C:
        if (!m_bush)
            return;
        if (m_bush->sequence() == 0 && !isScriptRunning("runOn")) {
            // The bush is pushed aside: the gate behind it.
            m_bush->setSequence(1, true);
            m_bushRollover.reset();
            m_gateRollover = std::make_unique<ACRollover>(*this, m_gate, 7);
            sfx(engine().sounds(), "branch");
            playVoiceNoFlaps("DB04125");
            return;
        }
        if (m_bush->sequence() == 1)
            rideThroughGate();
        return;
    case 0x271D:
        if (!m_bush || !m_gate)
            return;
        if (m_bush->sequence() == 0 && !isScriptRunning("runOn"))
            return;
        if (m_gate->sequence() == 1 && !isScriptRunning("runOn")) {
            // The key opens the gate (for good).
            s.missionTrail = 1;
            rec.setU16(PlayerRecord::kCottageGateOpen, 1); // app+0x2AA
            app().saveGame();
            m_gate->setPosition(m_gate->x(), m_gate->y(), m_bush->z() + 100.0f);
            m_gate->setSequence(0, true);
            m_gate->setPerPixelHit(false);
            sfx(engine().sounds(), "gateopen");
            sfx(engine().sounds(), "key");
            engine().anims().remove("m4 sparkle");
            playVoiceNoFlaps("DB04126");
            if (m_gateRollover)
                m_gateRollover->setCursorSequence(8);
            return;
        }
        rideThroughGate();
        return;
    case 0x271E:
        rec.setU32(PlayerRecord::kFinaleState, 2); // app+0x2F8
        app().gotoNode("finalpayoff");
        return;
    default:
        return;
    }
}

void TrailNode::m4ScriptEnded(std::string_view script) {
    // M4_ScriptEnded (0x4427b0).
    if (m_mapLocation == static_cast<int>(app().session().missionTrail) && m_mapLocation == 4 && m_decision == 4 &&
        str::iequals(script, "runOn"))
        playVoice("db0075");
}

} // namespace brc::game
