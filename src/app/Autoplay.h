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
//   click <x> <y>       left press and release at game pixel (x, y)
//   rclick <x> <y>      right button
//   move <x> <y>        move the pointer
//   key <code>          key press and release with an engine key code (1 Esc, 3 Enter, ...)
//   type <text>         typed characters
//   snap <name>         save the 640x480 game frame as <dir>/<name>.png
//   quit                leave the game
class Autoplay {
public:
    static std::optional<Autoplay> parse(std::string_view script, std::filesystem::path snapDir, std::string* error);
    // Runs the steps that are due. Returns false after "quit".
    bool step(engine::Engine& app, std::uint32_t now, std::pair<int, int>& pointer);
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
    std::filesystem::path m_dir;
};

} // namespace brc::app
