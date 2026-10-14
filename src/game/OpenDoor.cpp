// The stable-door movie (opendoor.cpp, class OpenDoor, ctor 0x421200):
// smk\OpenDoor.smk with the "dooropen" sound, then the carousel. Nothing in
// the game goes to this node (the carousel plays the same movie itself); it is
// reachable through the node-name table. See docs/engine/nodes.md "OpenDoor".
#include "core/Log.h"
#include "engine/Engine.h"
#include "engine/Movie.h"
#include "engine/SoundManager.h"
#include "game/Godiva.h"
#include "game/ToolbarNode.h"

#include <memory>

namespace brc::game {
namespace {

class OpenDoor final : public ToolbarNode {
public:
    OpenDoor(Godiva& app, const std::string& name) : ToolbarNode(app, name) {
        // 0x421200: saves the game first.
        app.saveGame();
        listenToMouseButtons();
        m_movie = std::make_unique<engine::SmackMovie>(engine(), 12, true);
        if (!m_movie->open("smk\\OpenDoor.smk", true)) {
            log::warn("Smacker File Failed to Load!");
            return;
        }
        m_movie->link(this);
        m_movie->setLoop(false);
        m_movie->setHideCount(0);
        m_movie->setPosition(static_cast<float>(viewRect().left), 0.0f, 5000.0f);
        m_movie->gotoFrame(1);
        m_movie->setPaused(false);
        engine().sounds().playSound("dooropen", -1, false, false, 1, false, 0x70, -1, 0, false);
        edgeScroll = false;
    }

    ~OpenDoor() override {
        // 0x421440.
        m_movie->setPaused(true);
        m_movie->hide(true);
        m_movie.reset();
    }

    void tick() override {
        // SpendTime (0x4214e0). The original does not run the toolbar's (or the
        // node's) idle here, so its world never redraws; the node's idle still
        // runs here so that the movie shows.
        if (m_movie && m_movie->done())
            app().gotoNode("carousel");
        engine::Node::tick();
    }

    void listen(int message, const engine::MessageData&) override {
        // 0x421510: a mouse button release skips; nothing else is handled.
        if (message == -103)
            app().gotoNode("carousel");
    }

private:
    std::unique_ptr<engine::SmackMovie> m_movie; // +0x1C0
};

BRC_REGISTER_NODE("opendoor", [](Godiva& app, const std::string& name) {
    return std::make_unique<OpenDoor>(app, name);
});

} // namespace
} // namespace brc::game
