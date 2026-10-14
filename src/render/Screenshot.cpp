#include "render/Screenshot.h"

#include "core/Png.h"
#include "core/StringUtil.h"

#include <SDL3/SDL_surface.h>

namespace brc::render {

bool saveCapture(const Capture& capture, const std::filesystem::path& path, std::string* error) {
    if (!capture.ok || capture.width <= 0 || capture.height <= 0 ||
        capture.rgba.size() <
            static_cast<std::size_t>(capture.width) * static_cast<std::size_t>(capture.height) * 4) {
        if (error)
            *error = "no image was captured";
        return false;
    }
    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), ec);

    if (str::iequals(str::fromPath(path.extension()), ".bmp")) {
        // SDL_PIXELFORMAT_RGBA32 is R, G, B, A in memory on every host.
        SDL_Surface* s =
            SDL_CreateSurfaceFrom(capture.width, capture.height, SDL_PIXELFORMAT_RGBA32,
                                  const_cast<std::uint8_t*>(capture.rgba.data()), capture.width * 4);
        bool ok = s && SDL_SaveBMP(s, str::fromPath(path).c_str());
        if (!ok && error)
            *error = SDL_GetError();
        if (s)
            SDL_DestroySurface(s);
        return ok;
    }
    auto png = png::encodeRgba(capture.rgba, capture.width, capture.height, capture.width * 4);
    if (!png::write(path, png)) {
        if (error)
            *error = "cannot write the file";
        return false;
    }
    return true;
}

} // namespace brc::render
