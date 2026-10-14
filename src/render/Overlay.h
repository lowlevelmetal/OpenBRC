#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace brc::render {

// The toast overlay texture: one byte of coverage per texel, fixed size.
inline constexpr int kOverlayTexW = 256;
inline constexpr int kOverlayTexH = 16;

struct OverlayBitmap {
    std::array<std::uint8_t, kOverlayTexW * kOverlayTexH> pixels{};
    int width = 0; // used part, texels from the top-left
    int height = 0;
};

// Renders one line of text with the 8x8 font and a 4-texel margin. UTF-8 input; code
// points outside printable ASCII show as '?'. Text that does not fit is cut off.
OverlayBitmap rasterizeToast(std::string_view text);

} // namespace brc::render
