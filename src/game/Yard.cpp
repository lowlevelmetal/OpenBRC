// The barn yard (yard.cpp, class Yard, ctor 0x43cda0): mount and dismount the
// horse, the two trail signs (rabbitE / beachE), the left edge to the tack
// room, and on the very first visit (no horse chosen yet) Barbie's intro.
// Also the factory's fallback for unknown node names and home of the
// developers' cheat keys. See docs/engine/nodes.md "Yard".
#include "core/Log.h"
#include "core/StringUtil.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "engine/Font.h"
#include "engine/Scripts.h"
#include "engine/SoundManager.h"
#include "game/Popups.h"
#include "game/ToolbarNode.h"
#include "game/TrailFlags.h"
#include "game/Widgets.h"

#include <array>
#include <charconv>
#include <memory>

namespace brc::game {
namespace {

// Click ids (anim +0x11c).
constexpr int kHorseClick = 10000;
constexpr int kRabbitSignClick = 0x2711;
constexpr int kTurtleSignClick = 0x2712;
constexpr int kCertificateClick = 0x2713;

// Node-name table indices of the two trails the signs lead to.
constexpr int kRabbitE = 0x14;
constexpr int kBeachE = 0x16;

// Per-horse resource names (horse 0 palomino, 1 white, 2 paint, 3 dapple).
constexpr std::array<std::string_view, 4> kTrailOmt = {"trailPal.omt", "trailWht.omt", "trailPnt.omt",
                                                       "trailDpl.omt"};                                // 0x8027d0
constexpr std::array<std::string_view, 4> kMountRun = {"palMountRun", "whtMountRun", "pntMountRun",
                                                       "dplMountRun"};                                 // 0x803878
constexpr std::array<std::string_view, 4> kAmbient = {"ambientPal", "ambientWht", "ambientPnt", "ambientDpl"}; // 0x803868

// [0x804030]: the yard shows Barbie's intro (the player has no horse yet).
bool s_introMode = true;
// [0x80a1e0]: what the player has typed in the yard (the cheat console).
std::array<char, 101> s_typed{};

// ForceHideAnim / ForceShowAnim (0x43baa0 / 0x43bb30): call Hide / Pause
// until the anim is hidden and paused, or Show / Unpause until it is visible
// and running.
void forceHide(engine::Anim* a) {
    if (!a)
        return;
    while (a->hideCount() == 0)
        a->hide(true);
    while (a->motionPauseCount() == 0)
        a->pause(true);
}

void forceShow(engine::Anim* a) {
    if (!a)
        return;
    while (a->hideCount() != 0)
        a->hide(false);
    while (a->motionPauseCount() != 0)
        a->pause(false);
}

std::size_t horseIndex(const PlayerRecord& r) { return std::min<std::size_t>(r.horse(), 3); }

class Yard final : public ToolbarNode {
public:
    Yard(Godiva& app, const std::string& name) : ToolbarNode(app, name) {
        // 0x43cda0.
        engine().anims().preloadCache("yard.omt.cache");
        listenToMouseButtons();               // [0x80a5a0]+0xC
        engine().keyListeners().addListener(this); // [0x80a5a0]+0x18
        setCursor("cursorOut");
        edgeLeft = true; // +0xCA: the left edge leads on (message 0x32C9)
        setSkin(kOut);
        PlayerRecord& rec = app.record();
        s_introMode = rec.horse() == PlayerRecord::kNoHorse;
        if (!s_introMode) {
            addToolbarButton("tbQuitOut", kQuit);
            addToolbarButton("tbPagerOut", kPager);
            addToolbarButton("tbCameraOut", kCamera);
            addToolbarButton("tbMapOut", kMap);
            addToolbarButton("tbBagOut", kBag);
            addToolbarButton("tbHelpOut", kHelp);
            addToolbarButton("tbHomeOut", kHome);
        }
        app.map().setYouAreHere(0x16); // map vt+0x14 (0x4055f0)
        createAnimAtViewport("signpost");
        // GlobalMemoryStatus: more than ~18 MB of RAM enables the ambient anims.
        m_highMemory = true;

        if (!s_introMode) {
            delayVoice("db9010", 500);
            if (rec.horse() == PlayerRecord::kNoHorse) // cannot happen here; kept from the original
                rec.setU32(PlayerRecord::kHorse, 2);
            if (rec.u32(PlayerRecord::kSaddle) == 2)
                rec.setU32(PlayerRecord::kSaddle, 0);
            m_fancy = rec.u32(PlayerRecord::kSaddle) == 1;

            // The trail databases stay open when the yard hands over to a ride.
            if (!engine().resources().isOpen("trails.omt"))
                engine().resources().open("trails.omt");
            if (!engine().resources().isOpen("trlAnims.omt"))
                engine().resources().open("trlAnims.omt");
            engine().anims().preloadCache("trlAnims.omt.cache");
            m_barbie = createAnimAtViewport("bMountRun");
            m_barbie->setPosition(0, 25, m_barbie->z());
            const std::size_t h = horseIndex(rec);
            engine().resources().open(kTrailOmt[h]);
            engine().anims().preloadCache(std::string(kTrailOmt[h]) + ".cache");
            m_horse = createAnimAtViewport(kMountRun[h]);
            m_horse->setPosition(0, 25, m_horse->z());
            if (m_fancy) {
                m_fancyAnim = createAnimAtViewport("fancyMountRun");
                m_fancyAnim->setPosition(0, 25, m_fancyAnim->z());
                m_fancyAnim->pause(true);
                m_fancyAnim->setClickId(kHorseClick);
            }
            m_barbie->pause(true);
            m_horse->pause(true);
            m_barbie->setClickId(kHorseClick);
            m_horse->setClickId(kHorseClick);
            if (m_highMemory) {
                trailflags::highMemoryAmbient = true;
                m_ambientBarbie = createAnimAtViewport("ambientBarbie");
                m_ambientHorse = createAnimAtViewport(kAmbient[h]);
                m_ambientBarbie->setClickId(kHorseClick);
                m_ambientHorse->setClickId(kHorseClick);
                m_ambientBarbie->setPosition(0, 25, m_ambientBarbie->z());
                m_ambientHorse->setPosition(0, 25, m_ambientBarbie->z());
                forceHide(m_ambientBarbie);
                forceHide(m_ambientHorse);
                if (m_fancy) {
                    m_ambientFancy = createAnimAtViewport("ambientFancy");
                    m_ambientFancy->setClickId(kHorseClick);
                    m_ambientFancy->setPosition(0, 25, m_ambientBarbie->z());
                    forceHide(m_ambientFancy);
                }
            }
            m_rabbitSign = createAnimAtViewport("sign rabbit");
            m_rabbitSign->setClickId(kRabbitSignClick);
            m_rabbitHotspot = std::make_unique<ACRollover>(*this, m_rabbitSign, 7);
            m_turtleSign = createAnimAtViewport("sign turtle");
            m_turtleSign->setClickId(kTurtleSignClick);
            m_turtleHotspot = std::make_unique<ACRollover>(*this, m_turtleSign, 7);

            // Back from a ride: the horse is still saddled and mounted.
            if (app.session().wasRiding) {
                m_mounted = true;
                m_barbie->setSequence(1, true);
                m_horse->setSequence(1, true);
                if (m_fancy)
                    m_fancyAnim->setSequence(1, true);
                if (trailflags::highMemoryAmbient) {
                    forceHide(m_barbie);
                    forceHide(m_horse);
                    runScript("Ambient");
                }
            }
            if (app.session().trailCertPending == 1) {
                app.session().trailCertPending = 0;
                m_certificate = createAnimAtViewport("certificatewon_trail");
                showCertificateName();
                m_certificate->setClickId(kCertificateClick);
            }
        } else {
            // First visit: Barbie rides in and introduces herself.
            engine().anims().preloadCache("yardintro.omt.cache");
            m_barbie = createAnimAtViewport("BarbieIntro");
            m_barbie->setSequence(0, true);
            m_barbie->setPosition(0, 0, 200);
            forceHide(m_barbie);
            runScript("Intro");
            playVoiceNoFlaps("spause");
            playVoiceNoFlaps("pause");
            playVoiceNoFlaps("pause");
            playVoiceNoFlaps("DB04072");
            playVoiceNoFlaps("DB04036");
        }
        if (!ambientLoop().empty())
            releaseAmbient();
        playAmbientLoop("exterior", 0x28);
        m_typedLength = 0;
    }

    ~Yard() override {
        // 0x43d660.
        auto& anims = engine().anims();
        anims.remove("signpost");
        anims.remove("sign rabbit");
        anims.remove("sign turtle");
        anims.remove("BarbieIntro");
        anims.remove("introFlaps");
        releaseAmbient();
        if (!s_introMode) {
            if (!m_ridingOff) {
                engine().resources().close("trails.omt");
                engine().resources().close("trlAnims.omt");
                engine().resources().close(kTrailOmt[horseIndex(app().record())]);
            } else {
                app().session().keepTrailResources = 1;
            }
            m_rabbitHotspot.reset();
            m_turtleHotspot.reset();
        } else {
            stopScript("Intro");
            stopVoice();
        }
        stopVoice();
        s_introMode = false;
        app().session().wasRiding = 1; // the next yard shows the horse mounted
        edgeLeft = false;
        m_certificateText.reset();
    }

    // SpendTime (0x43e740): the toolbar's tick, then the intro's sounds.
    void tick() override {
        ToolbarNode::tick();
        if (!s_introMode || !m_barbie)
            return;
        forceShow(m_barbie);
        const int frame = m_barbie->frameIndex();
        if (m_barbie->sequence() == 0) {
            if (!m_playedRoad10y && frame >= 0) {
                playEffect("road10y");
                m_playedRoad10y = true;
            } else if (!m_playedRoad && frame > 10) {
                playEffect("road");
                m_playedRoad = true;
            } else if (frame > 0x22) {
                m_barbie->setSequence(2, true);
            }
        } else if (m_barbie->sequence() == 2) {
            if (!m_playedBreath && frame > 5) {
                playEffect("breath");
                m_playedBreath = true;
            } else if (!m_playedBlow && frame > 0x1C) {
                playEffect("blow");
                m_playedBlow = true;
            }
        }
    }

    // ListenToMessage (0x43d800).
    void listen(int message, const engine::MessageData& data) override {
        if (m_tutorialRedirect)
            return;
        if (message == -102) { // mouse button down (event manager)
            const auto* e = static_cast<const engine::InputEvent*>(data.ptr);
            if (e && e->type == engine::InputEvent::LeftDown && s_introMode && !exitDialogShowing()) {
                gotoNode("stable"); // a click skips the intro
                return;
            }
        } else if (message == kMsgEdgeLeft) {
            gotoNode(s_introMode ? "stable" : "tackroom");
            return;
        }
        ToolbarNode::listen(message, data);
    }

    // MouseDown (0x43e160).
    void onPress(const engine::Pick& pick) override {
        if (m_tutorialRedirect)
            return;
        const int id = pick.element ? pick.element->clickId() : 0;
        if (id >= 0x32CF) {
            ToolbarNode::onPress(pick);
            return;
        }
        switch (id) {
        case kHelpClickId:
            playVoice("db04106");
            return;
        case kHorseClick:
            if (s_introMode || isScriptRunning("dismount") || isScriptRunning("mount") || isScriptRunning("runOff"))
                return;
            if (!m_mounted) {
                runScript("mount");
                m_mounted = true;
                return;
            }
            forceShow(m_barbie);
            forceShow(m_horse);
            forceHide(m_ambientBarbie);
            forceHide(m_ambientHorse);
            forceHide(m_ambientFancy);
            stopAllScripts();
            runScript("dismount");
            m_mounted = false;
            return;
        case kRabbitSignClick:
        case kTurtleSignClick:
            if (s_introMode)
                return;
            if (app().record().u16(PlayerRecord::kFirstRideDone) == 0) {
                // The first ride is the tutorial on rabbitE (db9011 ends -> rabbitE).
                m_tutorialRedirect = true;
                playVoice("db9011");
                return;
            }
            if (isScriptRunning("dismount") || isScriptRunning("mount") || isScriptRunning("runOff"))
                return;
            m_destination = id == kRabbitSignClick ? kRabbitE : kBeachE;
            engine().sounds().playSound("trailsign", -1, false, false, 1, false, 0x70, -1, 0, false);
            if (!m_mounted) {
                m_runOffAfterMount = true;
                m_mounted = true;
                runScript("mount");
                return;
            }
            if (!isScriptRunning("RunOff")) {
                stopAllScripts();
                forceShow(m_barbie);
                forceShow(m_horse);
                forceHide(m_ambientBarbie);
                forceHide(m_ambientHorse);
                forceHide(m_ambientFancy);
                runScript("runOff");
            }
            return;
        case kCertificateClick:
            playVoice("db0197");
            if (m_certificate)
                engine().anims().remove(m_certificate);
            m_certificate = nullptr;
            if (m_certificateText) {
                m_certificateText.reset();
                m_certificateFont.reset();
            }
            return;
        default:
            ToolbarNode::onPress(pick);
            return;
        }
    }

    // KeyDown (0x43d8a0): Esc, and the developers' console.
    void onKeyDown(const engine::InputEvent& e) override {
        if (m_typedLength > 100)
            m_typedLength = 0;
        if (e.key == 0) {
            if (e.ch == 0) {
                ToolbarNode::onKeyDown(e);
                return;
            }
            if (e.ch != '\r')
                s_typed[static_cast<std::size_t>(m_typedLength++)] = static_cast<char>(e.ch);
            s_typed[static_cast<std::size_t>(m_typedLength)] = 0;
            log::debug("yard: typed \"{}\"", s_typed.data());
            if (debugKey(static_cast<char>(e.ch)))
                return;
            ToolbarNode::onKeyDown(e);
            return;
        }
        if (e.key == engine::key::kEscape) {
            if (!s_introMode)
                showExitDialog(true);
            return;
        }
        if (e.key == engine::key::kEnter) {
            s_typed[static_cast<std::size_t>(m_typedLength)] = 0;
            m_typedLength = 0;
            const std::string_view typed(s_typed.data());
            Session& session = app().session();
            if (str::iequals(typed, "sevanne")) {
                playEffect("trash");
                session.debugMode = 1;
                return;
            }
            if (str::iequals(typed, "leinad")) {
                playEffect("camera");
                session.debugMode = 0;
                return;
            }
            if (!session.debugMode || typed.size() < 2 || !str::iequals(typed.substr(0, 2), "fr"))
                return;
            // "fr<N>": the trail frame rate, 1..19 (atoi).
            int fps = 0;
            const auto digits = typed.substr(2);
            std::from_chars(digits.data(), digits.data() + digits.size(), fps);
            if (fps < 1 || fps > 0x13)
                return;
            trailflags::frameRate = fps;
            log::info("framerate = {}", typed);
            return;
        }
        ToolbarNode::onKeyDown(e);
    }

    // SoundEnded (0x43e6f0).
    void soundDone(const std::string& name, const void* sound, bool completed) override {
        if (str::iequals(name, "db9011") && m_tutorialRedirect)
            gotoNode("rabbitE");
        ToolbarNode::soundDone(name, sound, completed);
    }

    // ScriptEnded (0x43e5f0).
    void scriptEnded(engine::Script& script) override {
        const std::string& name = script.name();
        if (str::iequals(name, "runOff")) {
            m_ridingOff = true;
            gotoNode(nodeName(m_destination));
        } else if (str::iequals(name, "mount")) {
            if (m_runOffAfterMount) {
                m_runOffAfterMount = false;
                runScript("runOff");
            } else {
                if (m_highMemory) {
                    forceHide(m_barbie);
                    forceHide(m_horse);
                    forceShow(m_ambientBarbie);
                    forceShow(m_ambientFancy);
                    forceShow(m_ambientHorse);
                }
                if (trailflags::highMemoryAmbient)
                    runScript("Ambient");
            }
        }
        ToolbarNode::scriptEnded(script);
    }

    // ScriptCommand (0x43e530): the horse-specific anim names for the scripts.
    void scriptMessage(engine::Script& script, std::string_view message, int counter) override {
        const std::size_t h = horseIndex(app().record());
        bool handled = false;
        if (str::iequals(message, "initAmbient")) {
            setScriptVar(script.name(), "hAmbient", kAmbient[h]);
            handled = true;
        } else if (str::iequals(message, "initHMountRun")) {
            setScriptVar(script.name(), "hMountRun", kMountRun[h]);
            handled = true;
        }
        if (handled && m_fancy)
            setScriptVar(script.name(), "fancy", 1.0);
        ToolbarNode::scriptMessage(script, message, counter);
    }

private:
    void playEffect(std::string_view name) {
        engine().sounds().playSound(name, -1, false, false, 1, false, 0x70, -1, 0, false);
    }

    // The developers' single-key toggles; true when the key was used.
    bool debugKey(char c) {
        Session& session = app().session();
        if (c == '?') {
            trailflags::narrowSpritePalette = true;
            return true;
        }
        if (!session.debugMode)
            return false;
        auto say = [](std::string_view what) { log::info("YARD :: {}", what); };
        switch (c) {
        case 'A': trailflags::ambient = true; say("Ambient on"); return true;
        case 'a': trailflags::ambient = false; say("Ambient off"); return true;
        case 'B': trailflags::backups = true; say("backups on"); return true;
        case 'b': trailflags::backups = false; say("backups off"); return true;
        case 'D': trailflags::dialogue = true; say("Dialog on"); return true;
        case 'd': trailflags::dialogue = false; say("Dialog off"); return true;
        case 'F': trailflags::butterflies = true; say("butterflies on"); return true;
        case 'f': trailflags::butterflies = false; say("butterflies off"); return true;
        case 'H': trailflags::horseHeads = true; say("Horse Heads on"); return true;
        case 'h': trailflags::horseHeads = false; say("Horse Heads off"); return true;
        case 'M': trailflags::music = true; say("Music on"); return true;
        case 'm': trailflags::music = false; say("Music off"); return true;
        case 'O': trailflags::smallMovies = true; say("NewBridge on"); return true;
        case 'o': trailflags::smallMovies = false; say("OldBridge off"); return true;
        case 'P':
            trailflags::programmaticLetterbox = true;
            trailflags::videoLetterbox = false;
            say("Programmatic Letter Box on");
            return true;
        case 'p':
            trailflags::programmaticLetterbox = false;
            trailflags::videoLetterbox = false;
            say("Programmatic Letter Box off");
            return true;
        case 'V':
            trailflags::programmaticLetterbox = true;
            trailflags::videoLetterbox = true;
            say("Video Letter Box on");
            return true;
        case 'v':
            trailflags::programmaticLetterbox = false;
            trailflags::videoLetterbox = false;
            say("Video Letter Box off");
            return true;
        case 'L':
            app().record().setU16(PlayerRecord::kFirstRideDone, 0);
            session.tutorialActive = 1;
            say("Tutorial on");
            return true;
        case 'l':
            app().record().setU16(PlayerRecord::kFirstRideDone, 1);
            say("Tutorial off");
            return true;
        case 'R':
        case 'r':
            gotoNode("rabbitE");
            return true;
        default:
            return false;
        }
    }

    // ShowCertificateName (0x43e930, also TrailNode 0x434610): the player's
    // name on the trail certificate, font 18, centred at y 270, at most 300
    // pixels wide, just above the certificate.
    void showCertificateName() {
        if (!m_certificate)
            return;
        m_certificateFont = engine::Font::bureau18(engine().resources(), palette());
        if (!m_certificateFont)
            return;
        m_certificateText = std::make_unique<engine::TextElement>(m_certificateFont);
        m_certificateText->link(this);
        m_certificateText->setText(app().record().name());
        const engine::Image8* img = m_certificateText->image();
        // The element is as wide as its text (margins and border are 0, inferred), at most 300.
        const int w = std::min(img ? img->width : 0, 300);
        m_certificateText->setPosition(static_cast<float>(320 - w / 2), 270.0f, m_certificate->z() + 3.0f);
    }

    bool m_highMemory = false;      // +0x1F4
    bool m_fancy = false;           // +0x202
    bool m_mounted = false;         // +0x1FE
    bool m_runOffAfterMount = false; // +0x200
    bool m_ridingOff = false;       // +0x1FC
    bool m_tutorialRedirect = false; // +0x204
    int m_destination = kRabbitE;   // +0x1F8
    int m_typedLength = 0;          // +0x210
    bool m_playedRoad10y = false, m_playedRoad = false, m_playedBreath = false, m_playedBlow = false; // +0x206..

    engine::Anim* m_barbie = nullptr;      // +0x1DC: bMountRun, or BarbieIntro
    engine::Anim* m_horse = nullptr;       // +0x1E0
    engine::Anim* m_fancyAnim = nullptr;   // +0x1E4
    engine::Anim* m_ambientBarbie = nullptr; // +0x1E8
    engine::Anim* m_ambientHorse = nullptr;  // +0x1EC
    engine::Anim* m_ambientFancy = nullptr;  // +0x1F0
    engine::Anim* m_rabbitSign = nullptr;  // +0x1C0
    engine::Anim* m_turtleSign = nullptr;  // +0x1C4
    std::unique_ptr<ACRollover> m_rabbitHotspot, m_turtleHotspot; // +0x1C8 / +0x1CC
    engine::Anim* m_certificate = nullptr; // +0x1D0
    std::shared_ptr<engine::Font> m_certificateFont;            // +0x1D4
    std::unique_ptr<engine::TextElement> m_certificateText;     // +0x1D8
};

BRC_REGISTER_NODE("yard", [](Godiva& app, const std::string& name) { return std::make_unique<Yard>(app, name); });

} // namespace
} // namespace brc::game
