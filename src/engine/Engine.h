#pragma once

#include "engine/Anim.h"
#include "engine/Broadcaster.h"
#include "engine/Image8.h"
#include "engine/Node.h"
#include "engine/Resources.h"
#include "engine/Scripts.h"
#include "engine/SoundManager.h"
#include "engine/Task.h"
#include "formats/Canvas.h"

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>

namespace brc::engine {

// What the engine needs from the platform: time, the mouse, presentation.
class Host {
public:
    virtual ~Host() = default;
    // Milliseconds since start (GetTickCount).
    virtual std::uint32_t ticks() = 0;
    // Shows a finished frame: 640x480 indices and the palette (0x00RRGGBB).
    virtual void present(const Image8& screen, const std::array<std::uint32_t, 256>& palette) = 0;
    // Pointer position in game pixels and the left button state (polled).
    virtual std::pair<int, int> mousePosition() = 0;
    virtual bool leftButtonDown() = 0;
    // SetCursorPos: warps the pointer (the riding nodes read relative motion).
    virtual void warpMouse(int x, int y) = 0;
    // Keep the window alive inside blocking loops (fades) without delivering input.
    virtual void keepAlive() {}
    // Drop the keyboard and mouse input still waiting to be delivered (the
    // PeekMessage(PM_REMOVE) loops of 0x44b3c0 after a node switch).
    virtual void flushInput() {}
    // Sleep a little when a main-loop pass had nothing to do.
    virtual void idleSleep() {}
};

// HCApplication: owns the subsystems, runs the idle tasks, switches nodes with
// the palette fade, dispatches input and quits. The game subclasses it to
// create its nodes (Godiva::CreateNewNode) and to hold the game state.
// See docs/engine/application.md.
class Engine {
public:
    Engine(const vfs::Vfs& data, Host& host, audio::Mixer* mixer);
    virtual ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // --- subsystems -------------------------------------------------------------------
    Host& host() { return m_host; }
    Resources& resources() { return m_resources; }
    AnimRegistry& anims() { return m_anims; }
    ScriptSystem& scripts() { return m_scripts; }
    SoundManager& sounds() { return m_sounds; }
    audio::Mixer* mixer() const { return m_mixer; } // null when sound is off
    TaskList& tasks() { return m_tasks; }
    Broadcaster& scriptEvents() { return m_scriptEvents; } // [0x80a43c]
    Broadcaster& mouseListeners() { return m_mouseListeners; } // event manager +0xc
    Broadcaster& keyListeners() { return m_keyListeners; } // event manager +0x18
    std::uint32_t ticks() { return m_host.ticks(); }
    // MSVC rand()/srand(): the whole game shares one sequence.
    int rand();
    void srand(std::uint32_t seed) { m_seed = seed; }

    // --- screen and palettes ---------------------------------------------------------------
    Image8& screen() { return m_screen; }
    // The palette of the current node's background ([0x80a3cc]); anims and
    // canvases loaded later are remapped against it.
    const formats::Palette& gamePalette() const { return m_gamePalette; }
    void setGamePalette(const formats::Palette& p) { m_gamePalette = p; }
    // The hardware palette (SetPalette): shown immediately.
    void setScreenPalette(const std::array<std::uint32_t, 256>& rgb);
    const std::array<std::uint32_t, 256>& screenPalette() const { return m_screenPalette; }
    void present();
    // Modal/inactive ([0x80a438]): nodes stop drawing.
    bool modal() const { return m_modal; }

    // --- nodes -------------------------------------------------------------------------
    Node* currentNode() const { return m_node; }
    void setCurrentNode(Node* node) { m_node = node; } // vt+0x24, called by Node's constructor
    // vt+0x1c RequestNode(name, fadeSteps, fadeSound): the switch happens in the next pass.
    void requestNode(std::string_view name, int fadeSteps, bool fadeSound);
    // vt+0x44 GotoNode: what game code calls (palette fade, no sound fade).
    virtual void gotoNode(std::string_view name);
    // vt+0x20 CreateNewNode.
    virtual std::unique_ptr<Node> createNode(const std::string& name) = 0;
    bool switchPending() const { return m_switchPending; }

    // --- main loop ----------------------------------------------------------------------
    // One pass of the main loop (0x45c130): every idle task once.
    void runPass();
    bool quitRequested() const { return m_quit; }
    virtual void quit(); // vt+0x3c (0x44b2e0)

    // --- input from the platform -------------------------------------------------------------
    void postInput(const InputEvent& e);
    // WM_ACTIVATEAPP (0x44c000).
    virtual void setActive(bool active);
    bool active() const { return !m_inactive; }
    std::pair<int, int> mousePosition() { return m_host.mousePosition(); }
    void warpMouse(int x, int y) { m_host.warpMouse(x, y); }

protected:
    // Application message handler (0x44be50; the game adds 0x413020).
    virtual void handleInput(const InputEvent& e);
    // The application's own task (0x412060): node switch, then the sound service.
    virtual void applicationTask();
    // Hooks around the switch (vt+0x28/+0x2c/+0x40).
    virtual void afterNodeDeleted() {}
    virtual void beforeNodeCreated() {}
    // Called once when the first node is about to be created (vt+0x40).
    virtual void firstNode(std::string&) {}
    void switchNode(); // 0x44b4e0
    // Deletes the current node now (the destructor does it too). A subclass
    // whose members the node still uses calls it first in its own destructor.
    void deleteCurrentNode();
    // 0x44df20 / 0x44df60: palette (and volume) fade around a switch.
    void fade(bool out, int targetVolume);

private:
    class EventTask;
    class AppTask;

    Host& m_host;
    audio::Mixer* m_mixer;
    TaskList m_tasks; // must outlive the tasks below
    Resources m_resources;
    Broadcaster m_scriptEvents;
    Broadcaster m_mouseListeners;
    Broadcaster m_keyListeners;
    SoundManager m_sounds;
    ScriptSystem m_scripts;
    AnimRegistry m_anims;
    std::unique_ptr<EventTask> m_eventTask;
    std::unique_ptr<AppTask> m_appTask;

    Image8 m_screen{640, 480, 0};
    std::array<std::uint32_t, 256> m_screenPalette{};
    formats::Palette m_gamePalette;
    std::uint32_t m_seed = 1;

    std::unique_ptr<Node> m_ownedNode;
    Node* m_node = nullptr;
    bool m_firstSwitch = true;
    std::string m_pendingNode;
    bool m_switchPending = false;
    int m_fadeSteps = 0;
    bool m_fadeSound = false;

    std::deque<InputEvent> m_input;
    bool m_quit = false;
    bool m_suppressPresent = false;
    bool m_modal = false;
    bool m_inactive = false;
};

} // namespace brc::engine
