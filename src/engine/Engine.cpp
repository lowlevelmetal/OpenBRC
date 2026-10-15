#include "engine/Engine.h"

#include "core/Log.h"

#include <algorithm>

namespace brc::engine {

// The event manager's idle task (0x45dd60): at most one input event per pass.
class Engine::EventTask final : public Task {
public:
    explicit EventTask(Engine& e) : Task(e.tasks()), m_engine(e) {}
    void spendTime() override {
        if (m_engine.m_input.empty())
            return;
        const InputEvent ev = m_engine.m_input.front();
        m_engine.m_input.pop_front();
        m_engine.handleInput(ev);
    }

private:
    Engine& m_engine;
};

// The application's periodical (0x412060).
class Engine::AppTask final : public Task {
public:
    explicit AppTask(Engine& e) : Task(e.tasks()), m_engine(e) {}
    void spendTime() override { m_engine.applicationTask(); }

private:
    Engine& m_engine;
};

Engine::Engine(const vfs::Vfs& data, Host& host, audio::Mixer* mixer)
    : m_host(host), m_mixer(mixer), m_resources(data), m_sounds(*this, mixer), m_scripts(*this), m_anims(*this) {
    // The event manager is created by the OMediaApplication constructor, the
    // application's own periodical by HCApplication's: both before any node.
    m_eventTask = std::make_unique<EventTask>(*this);
    m_appTask = std::make_unique<AppTask>(*this);
    for (int i = 0; i < 256; ++i)
        m_gamePalette.rgb[static_cast<std::size_t>(i)] = i == 255 ? 0xFFFFFFu : 0u;
    m_screenPalette = m_gamePalette.rgb;
}

Engine::~Engine() {
    deleteCurrentNode();
    m_anims.deleteAll();
    m_sounds.stopAll(true);
}

void Engine::deleteCurrentNode() {
    m_scripts.deleteAll();
    m_ownedNode.reset();
    m_node = nullptr;
}

int Engine::rand() {
    // MSVC CRT rand (0x7dc460).
    m_seed = m_seed * 214013u + 2531011u;
    return static_cast<int>((m_seed >> 16) & 0x7FFF);
}

// --- screen ------------------------------------------------------------------------------

void Engine::setScreenPalette(const std::array<std::uint32_t, 256>& rgb) {
    m_screenPalette = rgb;
    present();
}

void Engine::present() {
    if (!m_suppressPresent)
        m_host.present(m_screen, m_screenPalette);
}

// --- node switching ------------------------------------------------------------------------

void Engine::requestNode(std::string_view name, int fadeSteps, bool fadeSound) {
    // 0x44de80: only queued; several requests before the next pass overwrite each other.
    m_pendingNode = name;
    m_fadeSteps = std::min(fadeSteps, 16);
    m_fadeSound = fadeSound;
    m_switchPending = true;
}

void Engine::gotoNode(std::string_view name) { requestNode(name, 16, false); }

void Engine::applicationTask() {
    if (m_switchPending)
        switchNode();
    m_sounds.service(true);
}

void Engine::switchNode() {
    // 0x44b4e0.
    const int vol = m_sounds.enabled() ? m_sounds.masterVolume() : 0;
    if (m_node) {
        m_node->pause(true);
        fade(true, m_fadeSound ? 0 : vol);
        m_node->leaving(m_pendingNode);
        const std::string oldFile = m_node->name() + ".omt";
        m_node = nullptr;
        m_ownedNode.reset();
        m_resources.close(oldFile);
        m_resources.purge();
        afterNodeDeleted();
    } else if (m_firstSwitch) {
        firstNode(m_pendingNode);
    }
    m_firstSwitch = false;

    const std::string name = m_pendingNode;
    if (!m_resources.open(name + ".omt"))
        log::warn("node {}: no {}.omt", name, name);
    beforeNodeCreated();
    const std::uint32_t t0 = ticks();
    m_ownedNode = createNode(name);
    m_node = m_ownedNode.get();
    log::info("Node \"{}\" created in {} milliseconds.", name, ticks() - t0);
    if (m_node) {
        fade(false, vol);
        // Input given during the switch is lost (0x44b3c0).
        m_input.clear();
        m_host.flushInput();
        m_node->start();
    }
    m_switchPending = false;
}

void Engine::fade(bool out, int target) {
    // 0x44df20 -> 0x44df60. 50 steps of at least 12 ms; entries 0 and 255 stay.
    if (m_fadeSteps == 0)
        return;
    int vol = m_sounds.enabled() ? m_sounds.masterVolume() : 0;
    const int volStep = vol == target ? 0 : (target - vol) / m_fadeSteps + 1;

    Image8 c(640, 480, 0);
    if (out) {
        c = m_screen; // what is visible now
    } else if (m_node) {
        // Render the new node's first frame off-screen, then pause it.
        m_node->restartClock();
        const std::uint32_t wait = m_node->frameInterval() + 1;
        const std::uint32_t t = ticks();
        while (ticks() - t < wait)
            m_host.keepAlive();
        const bool wasModal = m_modal;
        m_modal = false;
        Image8 saved = m_screen;
        // Draw into the screen buffer without showing it (the view points at C).
        m_suppressPresent = true;
        m_node->worldIdle();
        m_suppressPresent = false;
        c = m_screen;
        m_screen = saved;
        m_modal = wasModal;
        m_node->pause(true);
    }

    const auto palette = m_screenPalette;
    // `white`: the last pure-white entry; `bright`: the brightest other one.
    int white = 255;
    for (int i = 0; i < 256; ++i)
        if ((palette[static_cast<std::size_t>(i)] & 0xFFFFFF) == 0xFFFFFF)
            white = i;
    int bright = 0;
    std::uint32_t best = UINT32_MAX;
    for (int i = 0; i < 256; ++i) {
        if (i == white)
            continue;
        const std::uint32_t c32 = palette[static_cast<std::size_t>(i)];
        const std::uint32_t sum = ((c32 >> 16) & 0xFF) * 257 + ((c32 >> 8) & 0xFF) * 257 + (c32 & 0xFF) * 257;
        const std::uint32_t d = 3 * 0xFFFFu - sum;
        if (d < best) {
            best = d;
            bright = i;
        }
    }
    for (auto& p : c.pixels)
        if (p == white)
            p = static_cast<std::uint8_t>(bright);

    auto faded = [&](int level) {
        std::array<std::uint32_t, 256> f{};
        for (int j = 0; j < 256; ++j) {
            const std::uint32_t src = palette[static_cast<std::size_t>(j)];
            std::uint32_t out32 = 0;
            for (int shift = 16; shift >= 0; shift -= 8) {
                const std::uint32_t gun16 = ((src >> shift) & 0xFF) * 257;
                out32 |= (((gun16 * static_cast<std::uint32_t>(level)) / 50) >> 8) << shift;
            }
            f[static_cast<std::size_t>(j)] = out32;
        }
        f[0] = 0;
        f[255] = 0xFFFFFF;
        return f;
    };
    auto stepWait = [&](std::uint32_t t0) {
        while (ticks() - t0 < 12)
            m_host.keepAlive();
    };
    auto setVolume = [&](bool fadingOut) {
        vol += volStep;
        if (fadingOut ? vol < target : vol > target)
            vol = target;
        if (m_sounds.enabled())
            m_sounds.setMasterVolume(std::clamp(vol, 0, 127));
    };

    m_screen = out ? c : Image8(640, 480, 0);
    present();
    if (out) {
        for (int i = 49; i >= 0; --i) {
            const std::uint32_t t0 = ticks();
            m_screenPalette = faded(i);
            present();
            setVolume(true);
            stepWait(t0);
        }
        m_screen.fill(0);
        present();
    } else {
        for (int i = 0; i < 50; ++i) {
            const std::uint32_t t0 = ticks();
            m_screenPalette = faded(i);
            present();
            setVolume(false);
            if (i == 0) {
                m_screen = c;
                present();
            }
            stepWait(t0);
        }
        m_screenPalette = palette;
        if (m_node) {
            m_node->pause(false);
            m_node->updateFrame(true);
        }
        present();
    }
}

// --- main loop and input ---------------------------------------------------------------------

void Engine::runPass() { m_tasks.runPass(); }

void Engine::quit() {
    // 0x44b2e0.
    m_modal = true;
    fade(true, 0);
    if (m_node)
        m_node->quitting();
    m_quit = true;
}

void Engine::postInput(const InputEvent& e) { m_input.push_back(e); }

void Engine::setActive(bool active) {
    // WM_ACTIVATEAPP (0x44c000).
    if (!active && !m_inactive) {
        m_inactive = true;
        m_modal = true;
        if (m_node && m_node->pauseCount() == 0)
            m_node->pause(true);
        m_sounds.pauseAll();
    } else if (active && m_inactive) {
        m_inactive = false;
        m_modal = false;
        if (m_node && m_node->pauseCount() > 0)
            m_node->pause(false);
        m_sounds.resumeAll();
        m_screen.fill(0);
        if (m_node)
            m_node->updateFrame(true);
        present();
    }
}

void Engine::handleInput(const InputEvent& e) {
    // 0x44be50 and the mouse/key broadcasters (input.md §1).
    Node* node = m_node;
    switch (e.type) {
    case InputEvent::LeftDown:
    case InputEvent::RightDown:
    case InputEvent::LeftUp:
    case InputEvent::RightUp: {
        const bool down = e.type == InputEvent::LeftDown || e.type == InputEvent::RightDown;
        if (node) {
            if (down)
                node->onMouseDown(e);
            else
                node->onMouseUp(e);
        }
        if (m_node != node || !node)
            break; // the handler switched nodes synchronously (it never does in the original)
        // The node's click tracker (0x492db0): pick, then -600 / -601 on its relay.
        Pick p = node->pick(e.x, e.y);
        p.modifiers = e.modifiers;
        p.notLeftPress = e.type != InputEvent::LeftDown;
        p.eventType = e.type;
        if (p.result != Pick::None || !down)
            node->mouseRelay().broadcast(down ? Node::kMousePress : Node::kMouseRelease, {&p});
        if (m_node != node)
            break;
        m_mouseListeners.broadcast(down ? -102 : -103, {&e});
        break;
    }
    case InputEvent::KeyDown:
    case InputEvent::KeyUp:
        if (node) {
            if (e.type == InputEvent::KeyDown)
                node->onKeyDown(e);
            else
                node->onKeyUp(e);
        }
        m_keyListeners.broadcast(e.type == InputEvent::KeyDown ? -104 : -105, {&e});
        break;
    default:
        break;
    }
}

} // namespace brc::engine
