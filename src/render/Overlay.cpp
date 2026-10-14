#include "render/Overlay.h"

#include "render/Font8x8.h"

namespace brc::render {

OverlayBitmap rasterizeToast(std::string_view text) {
    constexpr int kPad = 4;
    constexpr int kMaxChars = (kOverlayTexW - 2 * kPad) / 8;
    OverlayBitmap bmp;
    int n = 0;
    for (std::size_t i = 0; i < text.size() && n < kMaxChars; ++i) {
        auto c = static_cast<unsigned char>(text[i]);
        if ((c & 0xc0) == 0x80)
            continue; // UTF-8 continuation byte: the lead byte already drew a '?'
        int glyph = c >= 32 && c < 127 ? c - 32 : '?' - 32;
        for (int row = 0; row < 8; ++row)
            for (int col = 0; col < 8; ++col)
                if ((kFont8x8[glyph][row] >> col) & 1)
                    bmp.pixels[static_cast<std::size_t>((kPad + row) * kOverlayTexW + kPad + n * 8 + col)] =
                        255;
        ++n;
    }
    bmp.width = n * 8 + 2 * kPad;
    bmp.height = 8 + 2 * kPad;
    return bmp;
}

} // namespace brc::render
