// The ending (finalpayoff.cpp, class FinalPayoff, ctor 0x411810): the
// smk\mhp.smk movie once every mission is done; the movie's end or a click
// returns to the tack room. See docs/engine/nodes.md "FinalPayoff".
#include "core/Log.h"
#include "engine/Engine.h"
#include "engine/Movie.h"
#include "game/FinalPayoff.h"
#include "game/Godiva.h"
#include "game/PlayerRecord.h"
#include "game/ToolbarNode.h"

#include <memory>

namespace brc::game {
namespace {

class FinalPayoff final : public ToolbarNode {
public:
    FinalPayoff(Godiva& app, const std::string& name) : ToolbarNode(app, name) {
        // 0x411810. FinalPayoffBackground (vtable 0x7f11a0) replaces the world's
        // background with an empty draw, so the background canvas never shows.
        setBackground(nullptr);
        setFrameInterval(100);
        setUpdateMode(0);
        flags::finalPayoffShown = true;
        app.record().setU16(PlayerRecord::kPayoffSeen, 1); // app+0x2A4
        app.saveGame();
        listenToMouseButtons();
        edgeScroll = false;
    }

    ~FinalPayoff() override {
        // 0x411980.
        setUpdateMode(2);
        if (m_movie) {
            m_movie->setPaused(true);
            m_movie->hide(true);
            m_movie.reset();
        }
    }

    void tick() override {
        // SpendTime (0x411a40): the movie starts on the first pass.
        if (!m_movie) {
            m_movie = std::make_unique<engine::HcMovie>(engine(), *this);
            if (!m_movie->open("smk\\mhp.smk", false, false, true, true, 0, true))
                log::warn("Smacker File Failed to Load!");
            m_movie->setPosition(64.0f, 48.0f, 9000.0f);
            m_movie->link(this);
            m_movie->setPaused(false);
        } else if (m_movie->done()) {
            app().gotoNode("tackroom");
        }
        ToolbarNode::tick();
    }

    void listen(int message, const engine::MessageData& data) override {
        // 0x411b50: any mouse button release leaves.
        if (message == -103)
            app().gotoNode("tackroom");
        ToolbarNode::listen(message, data);
    }

private:
    std::unique_ptr<engine::HcMovie> m_movie; // +0x1C0
};

BRC_REGISTER_NODE("finalpayoff", [](Godiva& app, const std::string& name) {
    return std::make_unique<FinalPayoff>(app, name);
});

} // namespace
} // namespace brc::game
