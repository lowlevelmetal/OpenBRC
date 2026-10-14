#pragma once

#include "engine/Engine.h"
#include "engine/Timer.h"
#include "game/PlayerRecord.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace brc::game {

class BarbieMap;
class Godiva;

// Scenes register themselves with REGISTER_NODE so the factory (and the
// porting work) does not need one central list.
using NodeCreator = std::function<std::unique_ptr<engine::Node>(Godiva&, const std::string& name)>;
struct NodeRegistration {
    NodeRegistration(std::string_view name, NodeCreator creator);
};
#define BRC_REGISTER_NODE(NAME, CREATOR) static const ::brc::game::NodeRegistration BRC_CONCAT(s_node_, __LINE__)(NAME, CREATOR)
#define BRC_CONCAT_(a, b) a##b
#define BRC_CONCAT(a, b) BRC_CONCAT_(a, b)

// Unsaved session state (app+0x300..+0x34F, savegame.md §2).
struct Session {
    std::uint16_t tutorialActive = 0;      // 0x300
    std::uint32_t lastRaceResult = 0;      // 0x304
    std::uint32_t raceCourse = 0;          // 0x308: 1 Christie, 2 Teresa
    std::uint16_t wasRiding = 0;           // 0x30C
    std::uint32_t activeMission = 0x11;    // 0x310
    std::uint32_t missionTrail = 7;        // 0x314
    std::uint32_t savedMission = 0;        // 0x318
    std::uint32_t savedMissionTrail = 0;   // 0x31C
    std::uint16_t trailCertPending = 0;    // 0x320
    std::array<std::uint16_t, 7> c4Progress{}; // 0x322
    std::uint32_t c4LastTrail = 7;         // 0x330
    std::uint16_t lowEnd = 0;              // 0x334
    std::uint16_t debugMode = 0;           // 0x336
    std::uint16_t keepTrailResources = 0;  // 0x338
    std::uint16_t arriveByMap = 0;         // 0x33A
    std::uint16_t arriveFromMiniGame = 0;  // 0x33C
    std::uint16_t arriveFromRace = 0;      // 0x33E
    std::uint16_t raceFinished = 0;        // 0x340
    std::uint16_t mapRabbit2 = 0;          // 0x342
    std::uint16_t musicOff = 0;            // 0x344
    std::uint16_t mouseControl = 1;        // 0x346
    std::uint16_t desktopHorse = 1;        // 0x348
    std::uint16_t saveOnQuit = 0;          // 0x34A
    std::uint16_t field34C = 0;            // 0x34C: cleared by GotoNode
    // Not in the app object: the global [0x809b00], set by FinalPayoff
    // (0x411810); the next tack room greets with DB9025 and clears it (0x42c0b0).
    std::uint16_t finaleGreeting = 0;
};

// The game's application object (Godiva.cpp: Godiva, ctor 0x411bd0): the node
// factory, node music, the player record and session state, the idle timer.
// See docs/engine/classes.md "Godiva".
class Godiva final : public engine::Engine, private engine::Listener {
public:
    Godiva(const vfs::Vfs& data, engine::Host& host, audio::Mixer* mixer);
    ~Godiva() override;

    // Godiva::Start (0x411c70): opens the common databases, sets up the
    // record and goes to the first node (`startNode` replaces it, like /node).
    void start(std::string startNode = {});

    std::unique_ptr<engine::Node> createNode(const std::string& name) override; // 0x412140
    void gotoNode(std::string_view name) override;                               // 0x413370
    void playMusic(std::string_view name);                                       // 0x4133c0
    void selectNodeMusic(std::string_view nextNode);                             // 0x4133e0
    void quitGame(bool save);                                                    // 0x412db0
    void startRace(int message);                                                 // 0x412fe0

    PlayerRecord& record() { return m_record; } // app+0x1B0
    Session& session() { return m_session; }
    int saveSlot() const { return m_slot; }       // [0x809ac0]
    void setSaveSlot(int slot) { m_slot = slot; }
    bool saveGame() { return saves::save(m_record, m_slot); }

    std::uint32_t controlMode = 0;   // app+0x1A8: 1 keyboard, 0 mouse buttons, 2 mouse motion
    std::uint32_t popupRequest = 0;  // app+0x1AC: 1 pager, 3 map, 4 bag requested from a mini-game
    engine::Timer& idleTimer() { return *m_idleTimer; } // app+0x19C
    BarbieMap& map() { return *m_map; }                 // app+0x188: the trail map popup
    std::uint32_t idleInterval = 20000; // app+0x1A4

protected:
    void handleInput(const engine::InputEvent& e) override; // 0x413020
    void firstNode(std::string& name) override;

private:
    void listen(int message, const engine::MessageData& data) override;

    PlayerRecord m_record;
    Session m_session;
    int m_slot = 0;
    std::unique_ptr<engine::Timer> m_idleTimer;
    std::unique_ptr<BarbieMap> m_map;
    std::string m_startOverride;
};

// The node-name table at 0x7f1f50 (GotoNodeById, mini-game return locations).
std::string_view nodeName(int index);

} // namespace brc::game
