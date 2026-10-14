// The three splash nodes: MattelLogoNode (logos.cpp, 0x420f60), HCLogoNode
// (HCLogoNode.cpp, 0x4194f0) and BarbieSoftLogoNode (0x40a080). They derive
// from HCNode directly, without the toolbar. See docs/engine/nodes.md.
#include "core/Log.h"
#include "engine/Elements.h"
#include "engine/Timer.h"
#include "game/Godiva.h"

#include <cmath>
#include <numbers>

namespace brc::game {
namespace {

constexpr int kLogoTimerMessage = 0x32E6;

// MattelLogoNode and BarbieSoftLogoNode: the background canvas for 3 s, or
// until a mouse button is released or a key pressed.
class TimedLogoNode final : public engine::Node {
public:
    TimedLogoNode(Godiva& app, const std::string& name, std::string next)
        : engine::Node(app, name), m_next(std::move(next)), m_timer(app, 3000, kLogoTimerMessage) {
        m_timer.addListener(this);
    }

    void start() override {
        // 0x421130.
        m_timer.start();
        engine::Node::start();
    }
    void onMouseUp(const engine::InputEvent&) override { gotoNext(); }   // 0x419ed0
    void onKeyDown(const engine::InputEvent&) override { gotoNext(); }   // 0x419ed0
    void onMessage(int message, const engine::MessageData&) override {   // 0x40a250
        if (message == kLogoTimerMessage) {
            m_timer.stop();
            gotoNext();
        }
    }

private:
    // 0x421190: the logos fade the sound too.
    void gotoNext() { engine().requestNode(m_next, 16, true); }

    std::string m_next;
    engine::Timer m_timer;
};

// HCLogoNode: the Human Code logo whose lower half ripples like a reflection
// in water while the "music" script plays; 8 s, the script's end, a click or
// a key go on.
class HCLogoNode final : public engine::Node {
public:
    HCLogoNode(Godiva& app, const std::string& name, std::string next)
        : engine::Node(app, name), m_next(std::move(next)), m_timer(app, 8000, kLogoTimerMessage) {
        auto logo = app.resources().image("hclogo", palette());
        if (!logo) {
            log::warn("HCLogoNode: unable to load canvas \"hclogo\"!  Moving to next node.");
            gotoNext();
            return;
        }
        const int w = logo->width, h = logo->height;
        const float x = static_cast<float>((640 - w) / 2), y = static_cast<float>((480 - h) / 2);
        m_logo = std::make_unique<engine::ImageElement>(logo);
        m_logo->link(this);
        m_logo->setPosition(x, y, 250);

        // Twelve ripple frames (0x4194f0): rows from h/2 down take source row y + d.
        m_ripple = std::make_unique<engine::FrameAnim>();
        const int band = h / 14;
        for (int k = 1; k <= 12; ++k) {
            auto frame = std::make_shared<engine::Image8>(*logo);
            const double phase = static_cast<double>(k) * 2.0 * std::numbers::pi / 12.0;
            for (int row = h / 2; row < h; ++row) {
                const double a = static_cast<double>((h - row) * band) / static_cast<double>(row + 1) + phase;
                const int d = static_cast<int>(std::sin(a) * static_cast<double>(row + 28) * band / h);
                const int src = row + d;
                if (src < 0 || src >= h)
                    continue; // the row keeps its own pixels
                std::copy_n(logo->pixels.begin() + static_cast<std::ptrdiff_t>(src) * w, w,
                            frame->pixels.begin() + static_cast<std::ptrdiff_t>(row) * w);
            }
            m_ripple->addFrame({frame, 0, 0, 1000.0f / 15.0f}); // SetRate(15)
        }
        m_ripple->setLoop(true);
        m_ripple->link(this);
        m_ripple->setPosition(x, y, 200);
        m_ripple->hide(true);

        runScript("music");
        m_timer.addListener(this);
    }

    ~HCLogoNode() override { engine().sounds().stopAll(true); }

    void start() override {
        // 0x419e10.
        if (m_logo)
            m_logo->hide(true);
        if (m_ripple)
            m_ripple->hide(false);
        setScriptVar("music", "faded", 1.0);
        m_timer.start();
        engine::Node::start();
    }
    void scriptEnded(engine::Script& script) override {
        // 0x419ea0.
        if (script.name() == "music")
            gotoNext();
        engine::Node::scriptEnded(script);
    }
    void onMouseUp(const engine::InputEvent&) override { gotoNext(); }
    void onKeyDown(const engine::InputEvent&) override { gotoNext(); }
    void onMessage(int message, const engine::MessageData&) override {
        // 0x419ef0.
        if (message == kLogoTimerMessage) {
            m_timer.stop();
            gotoNext();
        }
    }

private:
    void gotoNext() { engine().requestNode(m_next, 16, true); }

    std::string m_next;
    engine::Timer m_timer;
    std::unique_ptr<engine::ImageElement> m_logo;
    std::unique_ptr<engine::FrameAnim> m_ripple;
};

BRC_REGISTER_NODE("MattelLogoNode", [](Godiva& app, const std::string& name) {
    return std::make_unique<TimedLogoNode>(app, name, "HCLogoNode");
});
BRC_REGISTER_NODE("HCLogoNode", [](Godiva& app, const std::string& name) {
    return std::make_unique<HCLogoNode>(app, name, "BarbieSoftLogoNode");
});
BRC_REGISTER_NODE("BarbieSoftLogoNode", [](Godiva& app, const std::string& name) {
    return std::make_unique<TimedLogoNode>(app, name, "preferences");
});

} // namespace
} // namespace brc::game
