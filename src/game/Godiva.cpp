#include "game/Godiva.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "game/Popups.h"

#include <map>

namespace brc::game {
namespace {

std::map<std::string, NodeCreator>& registry() {
    static std::map<std::string, NodeCreator> nodes; // key: lower-case name
    return nodes;
}

// A scene that is not ported yet: shows its background so the rest of the game
// can still be reached during development.
class PlaceholderNode final : public engine::Node {
public:
    PlaceholderNode(Godiva& app, const std::string& name) : engine::Node(app, name) {}
};

constexpr std::array<std::string_view, 32> kNodeNames = {
    "applesort", "bulletinboard", "intro",    "signin",   "leapfrog",    "stable",   "race",     "tackroom",
    "yard",      "vinegame",      "preferences", "grooming", "carousel", "bellgame", "opendoor", "finalpayoff",
    "intromovie", "appleW",       "cottageL", "bridgeS",  "rabbitE",     "rainbowS", "beachE",   "wishingL",
    "credits",   "EXIT",          "BOOK",     "CAMERA",   "MAP",         "BAG",      "HELP",     "PRINT"};

} // namespace

NodeRegistration::NodeRegistration(std::string_view name, NodeCreator creator) {
    registry()[str::lower(name)] = std::move(creator);
}

std::string_view nodeName(int index) {
    return index >= 0 && index < static_cast<int>(kNodeNames.size()) ? kNodeNames[static_cast<std::size_t>(index)] : "";
}

Godiva::Godiva(const vfs::Vfs& data, engine::Host& host, audio::Mixer* mixer)
    : engine::Engine(data, host, mixer), m_map(std::make_unique<BarbieMap>()) {}

Godiva::~Godiva() {
    deleteCurrentNode(); // toolbar nodes close their popup (possibly the map) when deleted
    resources().close("common.omt");
    resources().close("commonhd.omt");
}

void Godiva::start(std::string startNode) {
    // Godiva::Start (0x411c70).
    resources().open("commonhd.omt");
    resources().open("common.omt");
    m_idleTimer = std::make_unique<engine::Timer>(*this, idleInterval, engine::msg::kInactivity);
    m_idleTimer->addListener(this);
    m_record.setSessionDefaults();
    m_session = Session{};
    srand(ticks());
    m_startOverride = std::move(startNode);
    gotoNode("MattelLogoNode");
}

bool Godiva::loadSlot(int slot) {
    auto rec = saves::load(slot);
    if (!rec)
        return false;
    m_record = *rec;
    m_slot = slot;
    return true;
}

std::string Godiva::applyDevState(std::string_view spec) {
    for (auto item : str::split(spec, ',')) {
        item = str::trim(item);
        if (item.empty())
            continue;
        const auto eq = item.find('=');
        if (eq == std::string_view::npos)
            return std::string("dev state: expected key=value, got '") + std::string(item) + "'";
        const std::string key = str::lower(str::trim(item.substr(0, eq)));
        const auto value = str::parseInt(str::trim(item.substr(eq + 1)));
        if (!value)
            return "dev state: bad value for " + key;
        const auto v = static_cast<std::uint32_t>(*value);
        auto indexed = [&](char prefix, std::size_t base, int count) -> bool {
            if (key.size() == 2 && key[0] == prefix && key[1] >= '1' && key[1] < '1' + count) {
                m_record.setU32(base + 4 * static_cast<std::size_t>(key[1] - '1'), v);
                return true;
            }
            return false;
        };
        if (key == "horse")
            m_record.setU32(PlayerRecord::kHorse, v);
        else if (key == "saddle")
            m_record.setU32(PlayerRecord::kSaddle, v);
        else if (key == "firstride")
            m_record.setU16(PlayerRecord::kFirstRideDone, static_cast<std::uint16_t>(v));
        else if (key == "finale")
            m_record.setU32(PlayerRecord::kFinaleState, v);
        else if (key == "course")
            m_session.raceCourse = v;
        else if (key == "control")
            controlMode = v;
        else if (key == "mission")
            m_session.activeMission = v;
        else if (key == "trail")
            m_session.missionTrail = v;
        else if (key == "debug")
            m_session.debugMode = static_cast<std::uint16_t>(v);
        else if (!indexed('m', PlayerRecord::kMissionDone, 8) && !indexed('c', PlayerRecord::kCertificate, 4) &&
                 !indexed('b', PlayerRecord::kBadge, 3))
            return "dev state: unknown key " + key;
    }
    return {};
}

void Godiva::firstNode(std::string& name) {
    // The first switch takes the /node argument instead (application.md §2.2).
    if (!m_startOverride.empty())
        name = m_startOverride;
}

std::unique_ptr<engine::Node> Godiva::createNode(const std::string& name) {
    // Godiva::CreateNewNode (0x412140): case-insensitive; unknown names make a yard.
    auto& reg = registry();
    auto it = reg.find(str::lower(name));
    if (it == reg.end()) {
        log::warn("Godiva::CreateNewNode( no match ): {}", name);
        it = reg.find("yard");
        if (it == reg.end())
            return std::make_unique<PlaceholderNode>(*this, name);
    }
    log::info("Godiva::CreateNewNode( {} )", name);
    return it->second(*this, name);
}

void Godiva::gotoNode(std::string_view name) {
    // 0x413370: (the saved master volume is never read) clears +0x34C.
    m_session.field34C = 0;
    requestNode(name, 16, false);
}

void Godiva::playMusic(std::string_view name) {
    // 0x4133c0: channel 13, preempt, loop forever, volume 0x30, streamed.
    sounds().playSound(name, 13, false, true, 0, false, 0x30, -1, 0, true);
}

void Godiva::selectNodeMusic(std::string_view next) {
    // 0x4133e0, called by HCToolbarNode::Exit with the next node.
    sounds().stopAll(true);
    sounds().service(true);
    sounds().stopChannel(13);
    if (m_session.musicOff)
        return;
    auto is = [&](std::initializer_list<std::string_view> names) {
        for (auto n : names)
            if (str::iequals(next, n))
                return true;
        return false;
    };
    if (is({"signin"}))
        playMusic("sign-in");
    else if (is({"yard", "appleW", "rainbowS", "beachE", "bridgeS", "race"}))
        playMusic("trailMusic");
    else if (is({"stable", "grooming", "tackroom", "bulletinboard"}))
        playMusic("stablemusic");
    else if (is({"rabbitE"}))
        playMusic(m_record.u16(PlayerRecord::kFirstRideDone) ? "trailMusic" : "nonmissionMusic");
    else if (is({"applesort", "vinegame", "leapfrog"}))
        playMusic("gamemusic");
    else if (is({"wishingL", "cottageL"}))
        playMusic("hiddenmusic");
}

void Godiva::quitGame(bool save) {
    // 0x412db0.
    if (save)
        saveGame();
    quit();
}

void Godiva::startRace(int message) {
    // 0x412fe0, from the pager.
    if (message == 0x3415)
        m_session.raceCourse = 1;
    else if (message == 0x3416)
        m_session.raceCourse = 2;
    gotoNode("race");
}

void Godiva::handleInput(const engine::InputEvent& e) {
    // 0x413020: mouse buttons restart the idle timer.
    if (e.type >= engine::InputEvent::LeftDown && e.type <= engine::InputEvent::RightUp && m_idleTimer)
        m_idleTimer->restartIfRunning();
    engine::Engine::handleInput(e);
}

void Godiva::listen(int message, const engine::MessageData& data) {
    // The idle timer fired: tell the current node (its listener at +0x18).
    if (message == engine::msg::kInactivity)
        if (engine::Node* node = currentNode())
            node->listen(message, data);
}

} // namespace brc::game
