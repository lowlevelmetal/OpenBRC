#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace brc::engine {

// Rectangle with exclusive right/bottom edges.
struct Rect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    int width() const { return right - left; }
    int height() const { return bottom - top; }
    bool empty() const { return right <= left || bottom <= top; }
    bool contains(int x, int y) const { return x >= left && x < right && y >= top && y < bottom; }
    Rect intersect(const Rect& o) const {
        return {std::max(left, o.left), std::max(top, o.top), std::min(right, o.right), std::min(bottom, o.bottom)};
    }
    Rect offset(int dx, int dy) const { return {left + dx, top + dy, right + dx, bottom + dy}; }
    friend bool operator==(const Rect&, const Rect&) = default;
};

// An 8-bit image: palette indices, with transparency given either by a colour
// key (canvases, Smacker anims: index 0xFE) or by a per-pixel mask (compiled
// BLIT sprites). Also the type of the 640x480 screen buffer.
struct Image8 {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels; // row-major, `width` bytes per row
    std::vector<std::uint8_t> opaque; // per-pixel 1 = drawn; empty: use `key`
    int key = -1;                     // transparent index, -1 = none

    Image8() = default;
    Image8(int w, int h, std::uint8_t fill = 0)
        : width(w), height(h), pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), fill) {}

    Rect bounds() const { return {0, 0, width, height}; }
    std::uint8_t at(int x, int y) const { return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)]; }
    std::uint8_t& at(int x, int y) { return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)]; }
    bool transparentAt(int x, int y) const {
        if (!opaque.empty())
            return opaque[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] == 0;
        return key >= 0 && at(x, y) == key;
    }
    void fill(std::uint8_t v) { std::ranges::fill(pixels, v); }
    void fill(const Rect& r, std::uint8_t v);
};

// Copies `src` (or its `srcRect`) to `dst` with its top-left at (x, y),
// clipped to `clip` (destination coordinates) and to `dst`. Transparent
// pixels are skipped unless `opaqueCopy`.
void blit(Image8& dst, const Image8& src, int x, int y, const Rect& clip, bool opaqueCopy = false);
void blit(Image8& dst, const Image8& src, const Rect& srcRect, int x, int y, const Rect& clip, bool opaqueCopy = false);

} // namespace brc::engine
