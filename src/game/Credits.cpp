// The credits (credits.cpp, class Credits, ctor 0x410d20), reached from the
// exit dialog: the "credits" script pages through the names over the streamed
// "credits" music; a click or a key turns the page, the exit button or Escape
// ends it, and the end of the script quits the game. See docs/engine/nodes.md
// "Credits".
#include "core/StringUtil.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "engine/Scripts.h"
#include "engine/SoundManager.h"
#include "game/Godiva.h"
#include "game/ToolbarNode.h"
#include "game/Widgets.h"

#include <memory>

namespace brc::game {
namespace {

class Credits final : public ToolbarNode {
public:
    Credits(Godiva& app, const std::string& name) : ToolbarNode(app, name) {
        // 0x410d20.
        setCursor("cursorIn");
        m_exit = std::make_unique<RolloverButton>(*this, false);
        m_exit->init("exit");
        if (m_exit->anim())
            m_exit->anim()->setPerPixelHit(false);
        engine().sounds().playSound("credits", -1, false, false, 1, false, -1, -1, 0, true);
        runScript("credits");
    }

    ~Credits() override { m_exit.reset(); } // 0x410e40

    void onPress(const engine::Pick& pick) override {
        // MouseDown (0x410ec0).
        toolbarMouseDown(pick);
        if (m_exit && pick.element == m_exit->anim())
            setScriptVar("credits", "done", 1.0);
        else
            setScriptVar("credits", "nextPage", 1.0);
    }

    void onKeyDown(const engine::InputEvent& e) override {
        // KeyDown (0x410f20).
        if (e.key == engine::key::kEscape) {
            setScriptVar("credits", "done", 1.0);
            return;
        }
        setScriptVar("credits", "nextPage", 1.0);
        ToolbarNode::onKeyDown(e);
    }

    void scriptEnded(engine::Script& script) override {
        // ScriptEnded (0x410f70).
        ToolbarNode::scriptEnded(script);
        if (str::iequals(script.name(), "credits"))
            quitGame();
    }

private:
    std::unique_ptr<RolloverButton> m_exit; // +0x1C4
};

BRC_REGISTER_NODE("credits", [](Godiva& app, const std::string& name) {
    return std::make_unique<Credits>(app, name);
});

} // namespace
} // namespace brc::game
