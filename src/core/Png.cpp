#include "core/Png.h"

#include "core/File.h"

#include <array>

namespace brc::png {
namespace {

const std::array<std::uint32_t, 256>& crcTable() {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    return table;
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void chunk(std::vector<std::uint8_t>& out, const char* type, std::span<const std::uint8_t> data) {
    put32(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = start; i < out.size(); ++i)
        crc = crcTable()[(crc ^ out[i]) & 0xFF] ^ (crc >> 8);
    put32(out, crc ^ 0xFFFFFFFFu);
}

} // namespace

std::vector<std::uint8_t> encodeRgba(std::span<const std::uint8_t> rgba, int width, int height, int pitch) {
    // Raw scanlines, each prefixed with filter type 0.
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (static_cast<std::size_t>(width) * 4 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const auto* row = rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(pitch);
        raw.insert(raw.end(), row, row + static_cast<std::size_t>(width) * 4);
    }

    // zlib stream of stored deflate blocks.
    std::vector<std::uint8_t> z = {0x78, 0x01};
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    std::size_t pos = 0;
    do {
        const std::size_t n = std::min<std::size_t>(raw.size() - pos, 65535);
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n));
        z.push_back(static_cast<std::uint8_t>(~n >> 8));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                 raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
    } while (pos < raw.size());
    put32(z, (b << 16) | a);

    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> ihdr;
    put32(ihdr, static_cast<std::uint32_t>(width));
    put32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0}); // 8-bit RGBA, deflate, no filter, no interlace
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

std::vector<std::uint8_t> encodeIndexed(std::span<const std::uint8_t> pixels, int width, int height, int pitch,
                                        std::span<const std::uint32_t, 256> palette, int transparentIndex) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint8_t i = pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(pitch) +
                                          static_cast<std::size_t>(x)];
            const std::uint32_t c = palette[i];
            auto* p = &rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(x)) * 4];
            p[0] = static_cast<std::uint8_t>(c >> 16);
            p[1] = static_cast<std::uint8_t>(c >> 8);
            p[2] = static_cast<std::uint8_t>(c);
            p[3] = (i == transparentIndex) ? 0 : 255;
        }
    }
    return encodeRgba(rgba, width, height, width * 4);
}

bool write(const std::filesystem::path& path, std::span<const std::uint8_t> png) {
    return file::writeAtomic(path, std::as_bytes(png));
}

} // namespace brc::png
