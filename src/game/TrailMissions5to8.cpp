// Trail missions M5..M8 (0x442810–0x4445cf): the roses, the wishing well, the
// bell and the vines. TrailNode members the ride calls at fixed points (sound
// loading, stop setup and cleanup, each new frame, clicks, script ends). The
// mission ids (app+0x310) are 4..7; the map locations (+0x7EC) are 0 appleW,
// 2 bridgeS, 3 rainbowS, 4 rabbitE, 6 wishingL. See docs/engine/nodes.md
// "TrailNode mission, certificate and rescue code".
#include "game/TrailNode.h"

#include "core/StringUtil.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "engine/Movie.h"
#include "engine/SoundManager.h"
#include "engine/Timer.h"
#include "game/MiniGameFlags.h"
#include "game/Widgets.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace brc::game {
namespace {

constexpr std::size_t kM5Done = PlayerRecord::kMissionDone + 4 * 4; // app+0x268
constexpr std::size_t kM6Done = PlayerRecord::kMissionDone + 5 * 4; // app+0x26C
constexpr std::size_t kM7Done = PlayerRecord::kMissionDone + 6 * 4; // app+0x270
constexpr std::size_t kM8Done = PlayerRecord::kMissionDone + 7 * 4; // app+0x274

constexpr int kHorseshoeItem = 0;  // VineGame's reward
constexpr int kBottleItem = 0x17;  // the note in the bottle (`bottleOut`)
constexpr int kCoinItem = 0x18;    // Leapfrog's coin
constexpr int kBagCoinClick = 0x36B9;

// 0x803888 [horse]
constexpr std::array<std::string_view, 4> kLookLeftAnim = {"PalLookLeft", "WhtLookLeft", "PntLookLeft", "DplLookLeft"};
// 0x804858 [horse]
constexpr std::array<std::string_view, 4> kDrinkMovie = {"DrinkPal.smk", "DrinkWht.smk", "DrinkPnt.smk",
                                                         "DrinkDpl.smk"};

int s_m5HintTicks = 0;    // [0x80a290]: new frames off the mission trail before db04148
int s_m5PayoffEnded = 0;  // [0x80a294]: ExcitedLook / M5Payoff scripts that have ended

std::size_t horseIndex(const PlayerRecord& r) { return std::min<std::size_t>(r.horse(), 3); }

void playEffect(engine::Engine& e, std::string_view name) {
    e.sounds().playSound(name, -1, false, false, 1, false, 0x70, -1, 0, false);
}

// HCNode's "show" idiom: a hidden anim gets one hide count, then Hide(false).
void show(engine::Anim* a) {
    if (a && a->hideCount() != 0) {
        a->setHideCount(1);
        a->hide(false);
    }
}

} // namespace

void TrailNode::m58SwitchMusic(std::string_view name) {
    // Inlined in 0x442af0, 0x442d30 and 0x443130: StopMusic, then (music on)
    // StopMusic again if a music is set (+0x174) and PlayMusic.
    stopMusic();
    if (app().session().musicOff)
        return;
    if (!m_music.empty())
        stopMusic();
    m_music = name;
    app().playMusic(name);
}

// --- M5: the roses (appleW stop 1, the applesort game) ----------------------------------------------

void TrailNode::m5LoadSounds() {
    // M5_LoadSounds (0x442810).
    auto& snd = engine().sounds();
    for (auto name : {"thunder", "thunder2", "db04148", "db0104", "db0100", "db04149", "db0106", "db03014"})
        snd.getSound(name, true, false);
}

void TrailNode::m5ReleaseSounds() {
    // M5_ReleaseSounds (0x4428c0).
    auto& snd = engine().sounds();
    for (auto name : {"thunder", "thunder2", "db04148", "db0104", "db0100", "db04149", "db0106", "db03014"})
        snd.retain(name, false);
}

void TrailNode::m5SetupStop() {
    // M5_SetupStop (0x442960): at appleW stop 1 the orchard background, the
    // applesort entrance and the roses; after a won applesort also the overlay
    // and the horse looking left (hidden until the payoff scripts show it).
    m_missionTrail = m_mapLocation;
    // +0x6D4 = 0 without deleting it: a rollover M4_SetupStop just made for
    // the cottage keeps its hand cursor. It is deleted with the stop here.
    m_m58.droppedRollover = std::move(m_exitPropRollover);
    if (m_mapLocation != static_cast<int>(app().session().missionTrail)) {
        m_missionTrail = -1;
        return;
    }
    if (m_mapLocation != 0 || m_decision != 1)
        return;
    m_skipStopComment = true;
    deleteDecisionAnim();
    showDecisionAnim("m5 bkgd");
    m_exitProp = createAnimAtViewport("m5 game");
    if (m_exitProp) {
        m_exitProp->setClickId(kExitPropClick);
        m_exitPropRollover = std::make_unique<ACRollover>(*this, m_exitProp, 7);
    }
    createAnimAtViewport("m5 roses");
    if (!flags::appleSortWon)
        return;
    createAnimAtViewport("m5 overlay");
    engine::Anim* look = createAnimAtViewport(kLookLeftAnim[horseIndex(app().record())]);
    forceHideAnim(look);
    if (!m_highMemory)
        show(look);
    runDelay(250);
}

void TrailNode::m5CleanupStop() {
    // M5_CleanupStop (0x442af0).
    m_m58.droppedRollover.reset();
    if (m_mapLocation != m_missionTrail || m_mapLocation != 0 || m_decision != 1)
        return;
    auto& anims = engine().anims();
    if (flags::appleSortWon) {
        // These names never match a running script (they have no ".scr"), so
        // the payoff scripts keep running (docs/engine/scripts.md, quirks).
        stopScript("ExcitedLook.scr");
        stopScript("LookLeft.scr");
        stopScript("M5Payoff.scr");
        anims.remove("m5 myster horse");
        m58SwitchMusic("trailMusic");
    }
    anims.remove("m5 game");
    m_exitProp = nullptr;
    m_exitPropRollover.reset();
    anims.remove("m5 roses");
    anims.remove("m5 overlay");
    anims.remove(kLookLeftAnim[horseIndex(app().record())]);
    flags::appleSortWon = false;
}

void TrailNode::m5RideTick() {
    // M5_RideTick (0x442c10): ride voices on the way to the roses; elsewhere
    // the wrong-trail hint after 25 new frames, once per trail.
    const auto f = static_cast<std::uint32_t>(trailFrame());
    if (m_mapLocation == static_cast<int>(app().session().missionTrail)) {
        if (!m_field74C[0] && f > 0xFA) {
            playVoice(engine().rand() % 2 == 0 ? "db0100" : "db0104");
            m_field74C[0] = true;
        } else if (!m_field74C[3] && f > 400) {
            playVoice("db04149");
            m_field74C[3] = true;
        } else if (!m_field74C[1] && f > 0x23F) {
            playVoice("db0106");
            m_field74C[1] = true;
        } else if (!m_field74C[2] && f > 0x271) {
            playVoice("db03014");
            m_field74C[2] = true;
        }
    } else if (m_hintTrail != m_mapLocation) {
        if (s_m5HintTicks == 0x19) {
            playVoice("db04148");
            s_m5HintTicks = 0;
            m_hintTrail = m_mapLocation;
        } else {
            ++s_m5HintTicks;
        }
    }
}

void TrailNode::m5MouseDown(const engine::Pick& pick) {
    // M5_MouseDown (0x442d30): the applesort entrance ends the mystery mode
    // and sets the node to go to (applesort).
    if (!pick.element || pick.element->clickId() != kExitPropClick)
        return;
    if (flags::appleSortWon) {
        m58SwitchMusic("trailMusic");
        flags::appleSortWon = false;
        stopScript("ExcitedLook.scr"); // no-ops, as in M5_CleanupStop
        stopScript("LookLeft.scr");
        stopScript("M5Payoff.scr");
    }
    m_exitPropNode = 0;
}

void TrailNode::m5ScriptEnded(std::string_view script) {
    // M5_ScriptEnded (0x442de0). After a won applesort the stop's Delay starts
    // the payoff (ExcitedLook, M5Payoff, the thunder); when both scripts have
    // ended, the next Delay offers a pager message. Otherwise "runOn" says the
    // roses line.
    if (m_mapLocation != static_cast<int>(app().session().missionTrail))
        return;
    if (m_mapLocation == 0) {
        if (flags::appleSortWon) {
            if (str::iequals(script, "delay") && m_decision == 1) {
                if (s_m5PayoffEnded == 2) {
                    choosePagerMessage();
                    s_m5PayoffEnded = 0;
                } else {
                    s_m5PayoffEnded = 0;
                    m_ambientPreemptToggle = false;
                    stopAllScripts();
                    runScript("ExcitedLook");
                    runScript("M5Payoff");
                    const auto& rec = app().record();
                    if (rec.u16(PlayerRecord::kVoiceVariantB) == 0)
                        playVoice("db04018");
                    else if (hasBagItem(kHorseshoeItem))
                        playVoice("db04154");
                    else
                        playVoice("db0116"); // (a db0119 branch after it is unreachable)
                    playVoiceNoFlaps("thunder2");
                    playVoiceNoFlaps("db04019");
                }
            } else if (str::iequals(script, "ExcitedLook") && m_decision == 1) {
                ++s_m5PayoffEnded;
                runScript("LookLeft");
            } else if (str::iequals(script, "M5Payoff") && m_decision == 1) {
                ++s_m5PayoffEnded;
            }
        } else if (str::iequals(script, "runOn") && m_decision == 1) {
            const bool won = app().record().u32(kM5Done) != 0;
            playVoice(won && engine().rand() % 2 == 0 ? "db02020" : "db0107");
            playVoiceAfterPause("db0108a");
        }
    }
    if (s_m5PayoffEnded == 2)
        runDelay(15000);
}

// --- M6: the wishing well (rabbitE stop 1 bush, wishingL stop 0 well) ---------------------------------

void TrailNode::m6LoadSounds() {
    // M6_LoadSounds (0x443000).
    auto& snd = engine().sounds();
    for (auto name : {"db02022", "db04053", "db0132", "db0572", "db04055", "db04056", "db02006"})
        snd.getSound(name, true, false);
}

void TrailNode::m6ReleaseSounds() {
    // M6_ReleaseSounds (0x4430a0). Three names are misspelt in the original
    // ("db4055", "db4056", "db2006"), so those stay loaded.
    auto& snd = engine().sounds();
    for (auto name : {"db02022", "db04053", "db0132", "db0572", "db4055", "db4056", "db2006"})
        snd.retain(name, false);
}

void TrailNode::m6SetupStop() {
    // M6_SetupStop (0x443130): the bush with the rabbit at rabbitE stop 1; at
    // the well the game music, the hidden handle and the squirrel.
    m_missionTrail = m_mapLocation;
    m_m58.handle = nullptr;
    m_m58.handleRollover.reset(); // (the original drops the pointers)
    m_m58.squirrel = nullptr;
    m_m58.squirrelRollover.reset();
    if (m_mapLocation != static_cast<int>(app().session().missionTrail)) {
        m_missionTrail = -1;
        return;
    }
    if (m_mapLocation == 4) {
        if (m_decision != 1)
            return;
        m_skipStopComment = true;
        m_bush = createAnimAtViewport("m6 bush");
        if (m_bush) {
            m_bush->setSequence(0, true);
            m_bush->setClickId(kBushClick);
            m_bushRollover = std::make_unique<ACRollover>(*this, m_bush, 7);
        }
        createAnimAtViewport("m6 rabbit");
    } else if (m_mapLocation == 6 && m_decision == 0) {
        if (hasBagItem(kBottleItem))
            removeBagItem(kBottleItem);
        m_skipStopComment = true;
        m58SwitchMusic("gameMusic");
        m_m58.handle = createAnimAtViewport("m6 hidden handle");
        if (m_m58.handle) {
            m_m58.handle->setSequence(engine().rand() % 3, true);
            m_m58.handle->setClickId(0x2720);
            m_m58.handleRollover = std::make_unique<ACRollover>(*this, m_m58.handle, 7);
        }
        m_m58.squirrel = createAnimAtViewport("well squirrel");
        if (m_m58.squirrel) {
            m_m58.squirrel->setClickId(0x2722);
            m_m58.squirrelRollover = std::make_unique<ACRollover>(*this, m_m58.squirrel, 7);
        }
        runScript("wellSquirrel");
    }
}

void TrailNode::m6CleanupStop() {
    // M6_CleanupStop (0x4433d0).
    if (m_mapLocation != m_missionTrail)
        return;
    auto& anims = engine().anims();
    if (m_mapLocation == 4) {
        if (m_decision != 1)
            return;
        anims.remove("m6 bush");
        m_bush = nullptr;
        m_bushRollover.reset();
        anims.remove("m6 rabbit");
    } else if (m_mapLocation == 6 && m_decision == 0) {
        m_wellRollover.reset();
        if (anims.find("m6 mouse handle")) {
            anims.remove("m6 mouse handle");
            setCursor("cursorOut");
            edgeRightAlt = true;
        }
        if (m_m58.handle)
            anims.remove("m6 hidden handle");
        m_m58.handle = nullptr;
        m_m58.handleRollover.reset();
        anims.remove("m6 hot spot");
        anims.remove("well squirrel");
        m_m58.squirrel = nullptr;
        m_m58.squirrelRollover.reset();
    }
}

void TrailNode::m6RideTick() {
    // M6_RideTick (0x443510): the "wrong way" line when riding another trail
    // (once), and the ride voices on rabbitE and to the well.
    if (!m_riding)
        return;
    static int lastTrail = m_trail; // [0x80a298], set on the first call ([0x80a29c] bit 0)
    const auto f = static_cast<std::uint32_t>(trailFrame());
    const int missionTrail = static_cast<int>(app().session().missionTrail);
    if (missionTrail != 7 && lastTrail != m_trail) {
        if (m_trail == 4) {
            lastTrail = 4;
        } else if (m_trail != 6 && m_trail != 7 && f > 0x32 && !m_m58.saidDb02022) {
            playVoiceNoFlaps("db02022");
            lastTrail = m_trail;
            m_m58.saidDb02022 = true;
        }
    }
    if (m_mapLocation != missionTrail)
        return;
    if (m_mapLocation == 4) {
        if (!m_m58.saidDb04053 && f > 0xAF && f < 200) {
            playVoiceNoFlaps("db04053");
            m_m58.saidDb04053 = true;
        } else if (!m_m58.saidDb0132 && f > 0x145 && f < 0x15E) {
            playVoiceNoFlaps("db0132");
            m_m58.saidDb0132 = true;
        } else if (!m_m58.saidDb0572 && f > 0x168 && f < 0x177) {
            playVoiceNoFlaps("db0572");
            m_m58.saidDb0572 = true;
        }
    } else if (m_mapLocation == 6 && m_trail == 6 && !m_m58.saidWellVoice && f > 0x8C && f < 0xA0) {
        if (!hasBagItem(kCoinItem))
            playVoiceNoFlaps("db02006");
        else
            playVoiceNoFlaps(engine().rand() % 2 != 0 ? "db04055" : "db04056");
        m_m58.saidWellVoice = true;
    }
}

void TrailNode::m6MouseDown(const engine::Pick& pick) {
    // M6_MouseDown (0x443710). Nothing while the drink movie plays.
    if (m_modal || !pick.element)
        return;
    auto& anims = engine().anims();
    auto& rec = app().record();
    auto& s = app().session();
    switch (pick.element->clickId()) {
    case kBushClick:
        // The bush: the first click opens the path to the well (and starts the
        // mission there); then it closes again, or Barbie on the horse rides
        // through to wishingL.
        if (!m_bush)
            return;
        if (m_bush->sequence() == 0 && !isScriptRunning("runOn")) {
            s.missionTrail = 6;
            rec.setU16(PlayerRecord::kWellPathOpen, 1);
            m_bush->setSequence(1, true);
            const int r = engine().rand() % 3;
            playVoice(r == 0 ? "db0482" : r == 1 ? "db0135" : "db0134");
            if (m_bushRollover)
                m_bushRollover->setCursorSequence(8);
            return;
        }
        if (!m_mounted || m_bush->sequence() == 0) {
            m_bush->setSequence(0, true);
            if (m_bushRollover)
                m_bushRollover->setCursorSequence(7);
            return;
        }
        if (!isScriptRunning("RunLeft")) {
            m_nextTrail = 6;
            stopAllScripts();
            runScript("RunLeft");
        }
        return;
    case kPickupClick: {
        // The bottle's note was put in the bag.
        playVoiceNoFlaps("db04063");
        std::string_view line = "db03024";
        if (hasBagItem(kCoinItem)) {
            playVoiceAfterPause("db02024");
            line = engine().rand() % 2 == 0 ? "db03023" : "db02023";
        }
        playVoiceAfterPause(line);
        m_pagerDelay = 30000;
        if (!checkAllMissionsDone())
            runScript("PagerDelay");
        m_walkDisabled = false;
        return;
    }
    case kBagCoinClick:
        // The coin in the saddlebag becomes the cursor at the well.
        if (m_mapLocation != 6 || m_decision != 0)
            return;
        removeBagItem(kCoinItem);
        closePopup();
        edgeRightAlt = false;
        setCursor("m6 coin cursor");
        m_walkDisabled = true;
        return;
    case 0x2720:
        // The hidden handle becomes the cursor; the hot spot on the well takes it.
        playVoice("db0138");
        m_m58.handleRollover.reset();
        anims.remove("m6 hidden handle");
        m_m58.handle = nullptr; // (the original keeps the pointer; its cleanup only tests it)
        edgeRightAlt = false;
        setCursor("m6 mouse handle");
        if (engine::Anim* spot = createAnimAtViewport("m6 hot spot"))
            spot->setClickId(kWellClick);
        m_walkDisabled = true;
        return;
    case kWellClick: {
        if (!m_well)
            return;
        engine::Anim* handle = anims.find("m6 mouse handle");
        if (handle && cursor() == handle) {
            playEffect(engine(), "handle");
            playVoice("db04059");
            anims.remove("m6 mouse handle");
            setCursor("cursorOut");
            edgeRightAlt = true;
            m_well->setSequence(1, true);
            if (!m_wellRollover)
                m_wellRollover = std::make_unique<ACRollover>(*this, m_well, 7);
            return;
        }
        engine::Anim* coin = anims.find("m6 coin cursor");
        if (coin && cursor() == coin) {
            setCursor("cursorOut");
            edgeRightAlt = true;
            if (engine::Anim* toss = createAnimAtViewport("m6 coin toss"))
                toss->setSequence(0, true);
            playVoice("db04155");
            static constexpr std::array<std::string_view, 4> kWish = {"db02010", "db02011", "db02012", "db02013"};
            delayVoice(kWish[static_cast<std::size_t>(engine().rand() % 4)], 2500);
            m_walkDisabled = false;
            return;
        }
        const int seq = m_well->sequence();
        if (seq == 1) {
            playEffect(engine(), "clue");
            playEffect(engine(), "well");
            delayVoice("db04061", 2000);
            m_well->setSequence(2, true);
        } else if (seq == 2 && m_well->stopped()) {
            // The bottle comes up: mission done.
            playEffect(engine(), "clue");
            m_well->setSequence(3, true);
            playVoiceNoFlaps("pause");
            playVoiceNoFlaps("db04062");
            playVoiceNoFlaps("pause");
            if (rec.u16(PlayerRecord::kVoiceVariantA) != 0)
                playVoiceNoFlaps(rec.u16(PlayerRecord::kVoiceVariantB) == 0 ? "db0142" : "db0143");
            playVoiceNoFlaps("pause");
            m_pickup = createAnimAtViewport("bottleOut");
            if (m_pickup) {
                m_pickup->setClickId(kPickupClick);
                m_pickup->setSequence(1, true);
            }
            m_pickupItem = kBottleItem;
            rec.setU32(kM6Done, rec.u32(kM6Done) + 1);
            s.missionTrail = 7;
            app().saveGame();
        } else if (seq == 3 && !isScriptRunning("runOff")) {
            // A drink from the well: a full-screen movie; the ride loop deletes
            // it when it is done (+0x780, 0x435980).
            struct DrinkMovie final : ModalPopup {
                DrinkMovie(engine::Engine& e, engine::Node& owner) : movie(e, owner) {}
                bool finished() const override { return movie.done(); }
                engine::HcMovie movie;
            };
            stopVoice();
            auto drink = std::make_unique<DrinkMovie>(engine(), *this);
            drink->movie.open(kDrinkMovie[horseIndex(rec)], false, false, false, false, 0, true);
            drink->movie.setPosition(0.0f, 0.0f, 9999.0f);
            drink->movie.link(this);
            drink->movie.setPaused(false);
            m_modal = std::move(drink);
            playEffect(engine(), "drink");
        }
        return;
    }
    case 0x2722:
        // The squirrel by the well.
        if (m_m58.squirrel)
            m_m58.squirrel->setSequence(3, true);
        m_m58.squirrelRollover.reset();
        return;
    default:
        return;
    }
}

void TrailNode::m6ScriptEnded(std::string_view script) {
    // M6_ScriptEnded (0x443f50): arriving at the bush or the well.
    if (m_mapLocation != static_cast<int>(app().session().missionTrail) || !str::iequals(script, "runOn"))
        return;
    if (m_mapLocation == 4) {
        if (m_decision == 1) {
            playVoiceNoFlaps("db04054");
            runScript("M6_Rabbit");
        }
    } else if (m_mapLocation == 6 && m_decision == 0 && m_well) {
        if (m_well->sequence() == 0) {
            app().idleTimer().start(); // the idle timer (app+0x19C) restarts from zero
            playVoice("db04057");
        } else if (m_well->sequence() == 3) {
            playVoice("db03024");
        }
    }
}

// --- M7: the bell (bridgeS stop 0, the bell game) -------------------------------------------------------

void TrailNode::m7SetupStop() {
    // M7_SetupStop (0x444080).
    m_missionTrail = m_mapLocation;
    if (m_mapLocation != static_cast<int>(app().session().missionTrail)) {
        m_missionTrail = -1;
        return;
    }
    if (m_mapLocation != 2 || m_decision != 0)
        return;
    const bool won = app().record().u32(kM7Done) != 0;
    if (!flags::bellGameVisited)
        playVoiceNoFlaps("DB9018");
    else if (won)
        playVoice(engine().rand() % 2 == 0 ? "DB9022" : "DB9023");
    flags::bellGameVisited = false;
    m_skipStopComment = true;
    m_bellAnim = createAnimAtViewport(won ? "M7 bell" : "M7 nobell");
    if (m_bellAnim) {
        m_bellAnim->setClickId(0x2723);
        m_bellRollover = std::make_unique<ACRollover>(*this, m_bellAnim, 7);
    }
}

void TrailNode::m7CleanupStop() {
    // M7_CleanupStop (0x4441e0).
    m_m58.saidBellVoice = false;
    if (m_mapLocation != m_missionTrail || m_mapLocation != 2 || m_decision != 0)
        return;
    m_bellRollover.reset();
    if (m_bellAnim)
        engine().anims().remove(m_bellAnim->name());
    m_bellAnim = nullptr;
}

void TrailNode::m7MouseDown(const engine::Pick& pick) {
    // M7_MouseDown (0x444260): the bell leads to the bell game.
    if (!pick.element || pick.element->clickId() != 0x2723)
        return;
    app().gotoNode("bellgame");
    m_bellRollover.reset();
    if (m_bellAnim)
        engine().anims().remove(m_bellAnim->name());
    m_bellAnim = nullptr; // (the original keeps the pointer; the cleanup only removes it by name again)
}

void TrailNode::m7RideTick() {
    // M7_RideTick (0x4442d0): on bridgeS, whatever the mission's trail.
    const auto f = static_cast<std::uint32_t>(trailFrame());
    if (m_mapLocation != 2 || m_m58.saidBellVoice || f <= 0x14 || f >= 0x69)
        return;
    playVoice(app().record().u32(kM7Done) != 0 ? "db0147N" : "db02025");
    m_m58.saidBellVoice = true;
}

void TrailNode::m7LoadSounds() {
    // M7_LoadSounds (0x444340).
    engine().sounds().getSound("db02025", true, false);
}

void TrailNode::m7ReleaseSounds() {
    // M7_ReleaseSounds (0x444360).
    engine().sounds().retain("db02025", false);
}

// --- M8: the vines (rainbowS stop 0, the vine game) -----------------------------------------------------

void TrailNode::m8SetupStop() {
    // M8_SetupStop (0x444380): the vines, with the horseshoe until it is won.
    m_missionTrail = m_mapLocation;
    if (m_mapLocation != static_cast<int>(app().session().missionTrail)) {
        m_missionTrail = -1;
        return;
    }
    if (m_decision != 0)
        return;
    m_m58.vines = createAnimAtViewport(app().record().u32(kM8Done) == 0 ? "vines_shoe" : "vines_noshoe");
    if (m_m58.vines) {
        m_m58.vines->setClickId(kExitPropClick);
        m_m58.vinesRollover = std::make_unique<ACRollover>(*this, m_m58.vines, 7);
    }
}

void TrailNode::m8CleanupStop() {
    // M8_CleanupStop (0x444480).
    m_m58.saidDb0176 = false;
    if (m_mapLocation != m_missionTrail)
        return;
    m_m58.vinesRollover.reset();
    if (m_m58.vines)
        engine().anims().remove(m_m58.vines->name());
    m_m58.vines = nullptr;
}

void TrailNode::m8RideTick() {
    // M8_RideTick (0x4444f0): the frame of the trail movie or the insert
    // playing (not GetTrailFrame).
    if (m_mapLocation != static_cast<int>(app().session().missionTrail))
        return;
    std::uint32_t f = 0;
    if (m_current == m_base.get())
        f = m_base->frameNum();
    else if (m_current == m_insert.get())
        f = m_insert->frameNum();
    else
        return;
    if (!m_m58.saidDb0176 && f > 0x28 && f < 0x2D) {
        playVoiceNoFlaps("db0176");
        m_m58.saidDb0176 = true;
    }
}

void TrailNode::m8MouseDown(const engine::Pick& pick) {
    // M8_MouseDown (0x444570): the vines lead to the vine game.
    if (pick.element && pick.element->clickId() == kExitPropClick)
        m_exitPropNode = 9;
}

void TrailNode::m8LoadSounds() {
    // M8_LoadSounds (0x444590).
    engine().sounds().getSound("db0176", true, false);
}

void TrailNode::m8ReleaseSounds() {
    // M8_ReleaseSounds (0x4445b0).
    engine().sounds().retain("db0176", false);
}

} // namespace brc::game
