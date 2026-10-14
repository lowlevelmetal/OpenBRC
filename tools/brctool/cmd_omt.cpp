#include "Command.h"

#include "core/File.h"
#include "core/StringUtil.h"
#include "formats/OmtFile.h"

#include <charconv>
#include <print>

namespace brc::tool {
namespace {

std::optional<formats::OmtFile> openOmt(Context& ctx, const std::string& name) {
    std::string path = str::lower(name);
    if (!path.contains('/'))
        path = "resources/" + path;
    auto f = ctx.data().open(path);
    if (!f) {
        std::println(stderr, "brctool: no file '{}' in the game data", path);
        return std::nullopt;
    }
    auto omt = formats::OmtFile::open(f, path);
    if (!omt) {
        std::println(stderr, "brctool: {}", omt.error());
        return std::nullopt;
    }
    return std::move(*omt);
}

formats::FourCC parseType(std::string_view t) {
    char buf[5] = {' ', ' ', ' ', ' ', 0};
    for (std::size_t i = 0; i < 4 && i < t.size(); ++i)
        buf[i] = t[i];
    return formats::FourCC(buf);
}

const formats::OmtChunk* findChunk(const formats::OmtFile& omt, std::string_view type, std::string_view key) {
    const auto t = parseType(type);
    int id = 0;
    const auto [p, ec] = std::from_chars(key.data(), key.data() + key.size(), id);
    if (ec == std::errc{} && p == key.data() + key.size())
        if (const auto* c = omt.find(t, id))
            return c;
    return omt.find(t, key);
}

const Register ls{{
    "ls",
    "<file.omt> [type]",
    "List the chunks of an OMT container (type, id, size, name).",
    [](Context& ctx, std::span<const std::string> args) {
        if (args.empty())
            return 1;
        auto omt = openOmt(ctx, args[0]);
        if (!omt)
            return 1;
        for (const auto& c : omt->chunks()) {
            if (args.size() > 1 && c.type != parseType(args[1]))
                continue;
            std::println("{} {:>6} {:>10}  {}", c.type.str(), c.id, c.size, c.name);
        }
        return 0;
    },
}};

const Register extract{{
    "extract",
    "<file.omt> <type> <id|name> <out>",
    "Write one chunk's raw bytes to a file.",
    [](Context& ctx, std::span<const std::string> args) {
        if (args.size() < 4)
            return 1;
        auto omt = openOmt(ctx, args[0]);
        if (!omt)
            return 1;
        const auto* c = findChunk(*omt, args[1], args[2]);
        if (!c) {
            std::println(stderr, "brctool: no {} chunk '{}'", args[1], args[2]);
            return 1;
        }
        const auto bytes = omt->read(*c);
        if (!file::writeAtomic(str::toPath(args[3]), bytes))
            return 1;
        std::println("{} bytes -> {}", bytes.size(), args[3]);
        return 0;
    },
}};

} // namespace
} // namespace brc::tool
