#include "Command.h"

#include "core/Png.h"
#include "core/StringUtil.h"
#include "formats/Canvas.h"
#include "formats/OmtFile.h"

#include <print>

namespace brc::tool {
namespace {

// Writes a Canv or BLIT chunk as PNG. BLITs have no palette: the palette of
// the same-name Canv is used, else that of the file's "<file> background".
int run(Context& ctx, std::span<const std::string> args) {
    if (args.size() < 3)
        return 1;
    std::string path = str::lower(args[0]);
    if (!path.contains('/'))
        path = "resources/" + path;
    auto omt = formats::OmtFile::open(ctx.data().open(path), path);
    if (!omt) {
        std::println(stderr, "brctool: {}", omt ? "" : omt.error());
        return 1;
    }
    const std::string& name = args[1];
    std::array<std::uint32_t, 256> pal{};
    auto paletteOf = [&](std::string_view canvasName) -> bool {
        const auto* c = omt->find(formats::chunk::kCanvas, canvasName);
        if (!c)
            return false;
        auto p = formats::parseCanvasPalette(omt->read(*c));
        if (!p || !*p)
            return false;
        pal = (*p)->rgb;
        return true;
    };
    std::vector<std::uint8_t> out;
    if (const auto* c = omt->find(formats::chunk::kCanvas, name)) {
        auto cv = formats::parseCanvas(omt->read(*c));
        if (!cv) {
            std::println(stderr, "brctool: {}", cv.error());
            return 1;
        }
        if (cv->depth <= 8) {
            if (cv->palette)
                pal = cv->palette->rgb;
            else
                for (int i = 0; i < 256; ++i)
                    pal[static_cast<std::size_t>(i)] = i == 1 ? 0 : 0xFFFFFF;
            out = png::encodeIndexed(cv->indices, cv->width, cv->height, cv->width, pal,
                                     cv->hasKey ? static_cast<int>(cv->key) : -1);
        } else {
            std::vector<std::uint8_t> rgba;
            for (std::uint32_t v : cv->rgb)
                rgba.insert(rgba.end(), {static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 8),
                                         static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(cv->hasKey && v == cv->key ? 0 : 255)});
            out = png::encodeRgba(rgba, cv->width, cv->height, cv->width * 4);
        }
    } else if (const auto* b = omt->find(formats::chunk::kBlit, name)) {
        auto bl = formats::parseBlit(omt->read(*b));
        if (!bl) {
            std::println(stderr, "brctool: {}", bl.error());
            return 1;
        }
        std::string stem = name;
        if (auto us = stem.rfind('_'); us != std::string::npos)
            stem.resize(us);
        if (!paletteOf(name) && !paletteOf(stem)) {
            std::string file = str::lower(path.substr(path.rfind('/') + 1));
            file.resize(file.find('.'));
            paletteOf(file + " background");
        }
        std::vector<std::uint8_t> rgba;
        for (std::size_t i = 0; i < bl->pixels.size(); ++i) {
            const std::uint32_t v = pal[bl->pixels[i]];
            rgba.insert(rgba.end(), {static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 8),
                                     static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(bl->opaque[i] ? 255 : 0)});
        }
        out = png::encodeRgba(rgba, bl->width, bl->height, bl->width * 4);
    } else {
        std::println(stderr, "brctool: no Canv or BLIT named '{}'", name);
        return 1;
    }
    if (!png::write(str::toPath(args[2]), out))
        return 1;
    std::println("-> {}", args[2]);
    return 0;
}

const Register reg{{"png", "<file.omt> <name> <out.png>", "Convert a Canv or BLIT image to PNG.", run}};

} // namespace
} // namespace brc::tool
