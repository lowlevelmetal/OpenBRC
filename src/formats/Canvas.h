#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace brc::formats {

// A 256-colour palette as stored by OMT ('OPa2'): 16-bit guns (every value on
// the disc is an 8-bit value x 257) and a 4:4:4 RGB -> index inverse table.
struct Palette {
    std::array<std::uint32_t, 256> rgb{}; // 0x00RRGGBB (guns >> 8)
    std::vector<std::uint8_t> inverse;     // 4096 entries, index (r>>4)<<8 | (g>>4)<<4 | (b>>4); may be empty

    // Entry through the stored 4:4:4 inverse table (or nearest() without one).
    std::uint8_t lookup(std::uint32_t rgb) const;
    // GDI GetNearestPaletteIndex: the first entry with the smallest squared
    // RGB distance. This is how canvases are remapped to the display palette
    // when they are loaded (verified against the original's sign-in screen).
    std::uint8_t nearest(std::uint32_t rgb) const;
    friend bool operator==(const Palette& a, const Palette& b) { return a.rgb == b.rgb; }
};

// An OMT 1.x canvas ('Canv' chunk: 'OmCv' header, 'OmGW' world, optional
// 'OPa2' palette, PackBits rows). See docs/formats/canvas.md.
struct Canvas {
    std::uint32_t attr = 0; // world capability mask (0x1A sprite, 0x22D/0x22F pixmap)
    int width = 0;
    int height = 0;
    int depth = 8;          // 1, 8 or 32 on the disc (4 and 16 also decoded)
    bool hasKey = false;
    std::uint32_t key = 0;  // palette index (<= 8 bpp) or packed pixel (16/32 bpp)
    std::optional<Palette> palette;
    // Pixels, row-major, `width` per row: palette indices for 1/4/8 bpp (1 bpp
    // as 0 = white, 1 = black), 0x00RRGGBB for 16/32 bpp (16 bpp expanded 5-5-5).
    std::vector<std::uint8_t> indices;
    std::vector<std::uint32_t> rgb;
};

std::expected<Canvas, std::string> parseCanvas(std::span<const std::byte> chunk);
// Only the palette of a canvas (HCNode reads its background's palette this way, 0x492ab0).
std::expected<std::optional<Palette>, std::string> parseCanvasPalette(std::span<const std::byte> chunk);

// A pre-compiled sprite ('BLIT' chunk, tag 'POSH'): an 8-bit image of raw
// display-palette indices with transparent runs. See docs/formats/blit.md.
struct Blit {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels; // width x height
    std::vector<std::uint8_t> opaque; // 1 where the program writes the pixel
    std::uint32_t keyRgb = 0x00FF00FF; // trailer key colour
};

std::expected<Blit, std::string> parseBlit(std::span<const std::byte> chunk);

} // namespace brc::formats
