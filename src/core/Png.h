#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace brc::png {

// Encodes 8-bit RGBA (4 bytes per pixel, `pitch` bytes per row) as a PNG.
// The image data is zlib-compressed with stored (uncompressed) deflate
// blocks: no compression library is needed, at the cost of file size. It is
// meant for screenshots and developer dumps.
std::vector<std::uint8_t> encodeRgba(std::span<const std::uint8_t> rgba, int width, int height, int pitch);

// Encodes an 8-bit indexed image with a 256-entry palette (0x00RRGGBB) as RGBA.
std::vector<std::uint8_t> encodeIndexed(std::span<const std::uint8_t> pixels, int width, int height, int pitch,
                                        std::span<const std::uint32_t, 256> palette, int transparentIndex = -1);

bool write(const std::filesystem::path& path, std::span<const std::uint8_t> png);

} // namespace brc::png
