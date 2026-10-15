#pragma once

#include "engine/Engine.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace brc::app {

// Scripted input for testing: a ';'-separated list of steps, each optionally
// prefixed with "+<ms>" (delay after the previous step):
//   wait <node>         until the current node has that name
//   click <x> <y> [ms]  move to game pixel (x, y), press the left button on the
//                       next pass (so rollovers see the move first, as when the
//                       original handles WM_MOUSEMOVE in a pass of its own) and
//                       release it after ms (default 150, like BarbieRecomp's autoplay)
//   rclick <x> <y> [ms] right button
//   move <x> <y>        move the pointer
//   key <code>          key press and release with an engine key code (1 Esc, 3 Enter, ...)
//   type <text>         typed characters
//   snap <name>         save the 640x480 game frame as <dir>/<name>.png
//   quit                leave the game
// The pointer and buttons autoplay drives instead of the real mouse.
struct VirtualMouse {
    std::pair<int, int> pos{320, 240};
    bool left = false;
};

class Autoplay {
public:
    static std::optional<Autoplay> parse(std::string_view script, std::filesystem::path snapDir, std::string* error);
    // Runs the steps that are due. Returns false after "quit".
    bool step(engine::Engine& app, std::uint32_t now, VirtualMouse& mouse);
    bool finished() const { return m_next >= m_steps.size(); }

private:
    struct Step {
        std::uint32_t delay = 0;
        std::string cmd;
        std::vector<std::string> args;
    };
    std::vector<Step> m_steps;
    std::size_t m_next = 0;
    std::uint32_t m_last = 0;
    bool m_started = false;
    bool m_moved = false;              // the current click step has moved the pointer
    int m_held = 0;                    // button being held: 0 none, 1 left, 2 right
    std::uint32_t m_releaseAt = 0;
    std::filesystem::path m_dir;
};

} // namespace brc::app
