#include "engine/Image8.h"

#include <cstring>

namespace brc::engine {

void Image8::fill(const Rect& r, std::uint8_t v) {
    const Rect c = r.intersect(bounds());
    for (int y = c.top; y < c.bottom; ++y)
        std::memset(&at(c.left, y), v, static_cast<std::size_t>(c.width()));
}

void blit(Image8& dst, const Image8& src, int x, int y, const Rect& clip, bool opaqueCopy) {
    blit(dst, src, src.bounds(), x, y, clip, opaqueCopy);
}

void blit(Image8& dst, const Image8& src, const Rect& srcRect, int x, int y, const Rect& clip, bool opaqueCopy) {
    const Rect s = srcRect.intersect(src.bounds());
    if (s.empty())
        return;
    // Destination area of the source rectangle, clipped.
    const int dx0 = x + (s.left - srcRect.left);
    const int dy0 = y + (s.top - srcRect.top);
    const Rect area = Rect{dx0, dy0, dx0 + s.width(), dy0 + s.height()}.intersect(clip).intersect(dst.bounds());
    if (area.empty())
        return;
    const int sx0 = s.left + (area.left - dx0);
    const int sy0 = s.top + (area.top - dy0);
    const auto w = static_cast<std::size_t>(area.width());
    for (int row = 0; row < area.height(); ++row) {
        const std::uint8_t* sp = src.pixels.data() + static_cast<std::size_t>(sy0 + row) * static_cast<std::size_t>(src.width) + static_cast<std::size_t>(sx0);
        std::uint8_t* dp = &dst.at(area.left, area.top + row);
        if (opaqueCopy || (src.key < 0 && src.opaque.empty())) {
            std::memcpy(dp, sp, w);
        } else if (!src.opaque.empty()) {
            const std::uint8_t* mp = &src.opaque[static_cast<std::size_t>(sy0 + row) * static_cast<std::size_t>(src.width) + static_cast<std::size_t>(sx0)];
            for (std::size_t i = 0; i < w; ++i)
                if (mp[i])
                    dp[i] = sp[i];
        } else {
            const auto key = static_cast<std::uint8_t>(src.key);
            for (std::size_t i = 0; i < w; ++i)
                if (sp[i] != key)
                    dp[i] = sp[i];
        }
    }
}

} // namespace brc::engine
