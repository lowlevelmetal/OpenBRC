#pragma once

#include "vfs/GameSource.h"

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace brc::tool {

// Shared state for subcommands: the game data, mounted on first use from
// --source (repeatable) or $OPENBRC_GAME_DATA.
struct Context {
    std::vector<std::string> sourceArgs;
    const vfs::Vfs& data(); // exits with a message when no usable source is given

private:
    std::unique_ptr<vfs::GameData> m_data;
};

struct Command {
    std::string_view name;
    std::string_view usage;
    std::string_view help;
    std::function<int(Context&, std::span<const std::string>)> run;
};

// Subcommands register themselves at static-initialisation time.
std::vector<Command>& registry();
struct Register {
    explicit Register(Command c) { registry().push_back(std::move(c)); }
};

} // namespace brc::tool
