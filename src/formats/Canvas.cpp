#include "formats/Canvas.h"

#include "formats/ByteReader.h"
#include "formats/OmtFile.h"

#include <algorithm>
#include <format>
#include <limits>

namespace brc::formats {
namespace {

constexpr std::uint32_t kTagCanvas = FourCC("OmCv").value;
constexpr std::uint32_t kTagCanvasAlt = FourCC("OmMC").value;
constexpr std::uint32_t kTagWorld = FourCC("OmGW").value;
constexpr std::uint32_t kTagPalette = FourCC("OPa2").value;
constexpr std::uint32_t kTagPaletteOld = FourCC("OmPa").value;
constexpr std::uint32_t kTagBlit = FourCC("POSH").value;

std::unexpected<std::string> fail(std::string_view what) { return std::unexpected(std::string(what)); }

// OMediaPalette::read (0x49c730).
std::expected<Palette, std::string> readPalette(BeReader& r) {
    std::uint32_t tag = 0, count = 0;
    std::string description;
    if (!r.u32(tag) || (tag != kTagPalette && tag != kTagPaletteOld))
        return fail("Canv: bad palette tag");
    if (!r.pascal(description) || !r.u32(count) || count > 256)
        return fail("Canv: bad palette header");
    Palette p;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint16_t red, green, blue;
        if (!r.u16(red) || !r.u16(green) || !r.u16(blue))
            return fail("Canv: truncated palette");
        p.rgb[i] = (std::uint32_t{red} >> 8) << 16 | (std::uint32_t{green} >> 8) << 8 | (std::uint32_t{blue} >> 8);
    }
    if (tag == kTagPalette) {
        p.inverse.resize(4096);
        if (!r.bytes(std::as_writable_bytes(std::span(p.inverse))))
            return fail("Canv: truncated inverse table");
    }
    return p;
}

struct Header {
    std::uint32_t attr = 0;
    std::int16_t width = 0, height = 0, depth = 0;
    std::uint8_t packMode = 0, version = 0;
    std::int16_t rowBytes = 0;
    bool hasKey = false;
    std::uint32_t key = 0;
    std::uint16_t keyRgb[3] = {};
    bool hasPalette = false;
};

std::expected<Header, std::string> readHeader(BeReader& r) {
    Header h;
    std::uint32_t tag = 0, world = 0;
    std::int16_t hasKey = 0;
    if (!r.u32(tag) || (tag != kTagCanvas && tag != kTagCanvasAlt))
        return fail("Canv: not an OMT canvas");
    if (!r.u32(h.attr) || !r.u32(world) || world != kTagWorld)
        return fail("Canv: missing graphic world");
    if (!r.i16(h.width) || !r.i16(h.height) || !r.i16(h.depth) || !r.u8(h.packMode) || !r.u8(h.version) ||
        !r.i16(h.rowBytes) || !r.i16(hasKey))
        return fail("Canv: truncated header");
    if (h.width <= 0 || h.height <= 0 || h.rowBytes <= 0 || h.packMode > 2)
        return fail("Canv: bad dimensions");
    if (h.depth != 1 && h.depth != 4 && h.depth != 8 && h.depth != 16 && h.depth != 32)
        return fail(std::format("Canv: unsupported depth {}", h.depth));
    h.hasKey = hasKey != 0;
    if (h.version == 0) {
        if (!r.u16(h.keyRgb[0]) || !r.u16(h.keyRgb[1]) || !r.u16(h.keyRgb[2]))
            return fail("Canv: truncated key colour");
    } else if (h.hasKey && !r.u32(h.key)) {
        return fail("Canv: truncated key");
    }
    std::int16_t hasPalette = 0;
    if (!r.i16(hasPalette))
        return fail("Canv: truncated header");
    h.hasPalette = hasPalette != 0;
    return h;
}

// PackBits with 1-, 2- or 4-byte units (0x49bdd0). Control units of 16/32-bit
// data are little-endian (packed on a PC); only their low 16 bits count.
bool unpackRow(std::span<const std::byte> src, std::size_t& pos, std::size_t srcEnd, std::span<std::byte> dst,
               int unit) {
    std::size_t out = 0;
    auto readControl = [&](int& n) {
        if (pos + static_cast<std::size_t>(unit) > srcEnd)
            return false;
        if (unit == 1) {
            n = static_cast<std::int8_t>(std::to_integer<std::uint8_t>(src[pos]));
        } else {
            n = static_cast<std::int16_t>(std::to_integer<unsigned>(src[pos]) |
                                          std::to_integer<unsigned>(src[pos + 1]) << 8);
        }
        pos += static_cast<std::size_t>(unit);
        return true;
    };
    while (out < dst.size()) {
        int n = 0;
        if (!readControl(n))
            return false;
        if (n >= 0) {
            const std::size_t bytes = static_cast<std::size_t>(n + 1) * static_cast<std::size_t>(unit);
            if (pos + bytes > srcEnd || out + bytes > dst.size())
                return false;
            std::copy_n(src.begin() + static_cast<std::ptrdiff_t>(pos), bytes, dst.begin() + static_cast<std::ptrdiff_t>(out));
            pos += bytes;
            out += bytes;
        } else if (n != -128) {
            const std::size_t reps = static_cast<std::size_t>(1 - n);
            if (pos + static_cast<std::size_t>(unit) > srcEnd || out + reps * static_cast<std::size_t>(unit) > dst.size())
                return false;
            for (std::size_t i = 0; i < reps; ++i)
                for (int b = 0; b < unit; ++b)
                    dst[out++] = src[pos + static_cast<std::size_t>(b)];
            pos += static_cast<std::size_t>(unit);
        }
    }
    return true;
}

} // namespace

std::uint8_t Palette::lookup(std::uint32_t c) const {
    const unsigned r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    if (inverse.size() == 4096)
        return inverse[(r >> 4) << 8 | (g >> 4) << 4 | (b >> 4)];
    int best = 0;
    long bestDist = std::numeric_limits<long>::max();
    for (int i = 0; i < 256; ++i) {
        const long dr = long((rgb[static_cast<std::size_t>(i)] >> 16) & 0xFF) - long(r);
        const long dg = long((rgb[static_cast<std::size_t>(i)] >> 8) & 0xFF) - long(g);
        const long db = long(rgb[static_cast<std::size_t>(i)] & 0xFF) - long(b);
        const long d = dr * dr + dg * dg + db * db;
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return static_cast<std::uint8_t>(best);
}

std::expected<Canvas, std::string> parseCanvas(std::span<const std::byte> chunk) {
    // OMediaCanvas::read (0x45f8a0) and OMediaGfxWorld::read (0x471e60).
    BeReader r(chunk);
    auto h = readHeader(r);
    if (!h)
        return std::unexpected(h.error());
    Canvas c;
    c.attr = h->attr;
    c.width = h->width;
    c.height = h->height;
    c.depth = h->depth;
    c.hasKey = h->hasKey;
    c.key = h->key;
    if (h->hasPalette) {
        auto p = readPalette(r);
        if (!p)
            return std::unexpected(p.error());
        c.palette = std::move(*p);
    }
    if (h->version == 0 && c.hasKey && c.palette && c.depth <= 8) {
        // Version 0 stores the key as a colour; find its palette index.
        const std::uint32_t want = (std::uint32_t{h->keyRgb[0]} >> 8) << 16 | (std::uint32_t{h->keyRgb[1]} >> 8) << 8 |
                                   (std::uint32_t{h->keyRgb[2]} >> 8);
        const auto it = std::ranges::find(c.palette->rgb, want);
        c.key = it == c.palette->rgb.end() ? 0xFFFFFFFFu : static_cast<std::uint32_t>(it - c.palette->rgb.begin());
    }

    std::uint32_t dataSize = 0;
    if (!r.u32(dataSize) || dataSize > r.remaining())
        return fail("Canv: truncated pixel data");
    const auto data = r.rest().first(dataSize);
    const int unit = 1 << h->packMode;
    const auto rowBytes = static_cast<std::size_t>(h->rowBytes);
    if (rowBytes % static_cast<std::size_t>(unit) != 0)
        return fail("Canv: row size not a multiple of the pack unit");
    std::vector<std::byte> row(rowBytes);
    std::size_t pos = 0;

    const auto w = static_cast<std::size_t>(c.width);
    if (c.depth <= 8)
        c.indices.resize(w * static_cast<std::size_t>(c.height));
    else
        c.rgb.resize(w * static_cast<std::size_t>(c.height));

    for (int y = 0; y < c.height; ++y) {
        if (pos + 2 > data.size())
            return fail("Canv: truncated row");
        const std::size_t packed = std::to_integer<std::size_t>(data[pos]) << 8 | std::to_integer<std::size_t>(data[pos + 1]);
        pos += 2;
        const std::size_t end = std::min(pos + packed, data.size());
        if (!unpackRow(data, pos, end, row, unit))
            return fail(std::format("Canv: bad packed row {}", y));
        pos = std::max(pos, end); // `packedLen` bounds the row; the disc's rows use exactly that much
        const std::size_t base = static_cast<std::size_t>(y) * w;
        auto byteAt = [&](std::size_t i) { return i < row.size() ? std::to_integer<std::uint32_t>(row[i]) : 0u; };
        for (std::size_t x = 0; x < w; ++x) {
            switch (c.depth) {
            case 1:
                c.indices[base + x] = static_cast<std::uint8_t>((byteAt(x >> 3) >> (7 - (x & 7))) & 1);
                break;
            case 4:
                c.indices[base + x] = static_cast<std::uint8_t>(((x & 1) ? byteAt(x >> 1) >> 4 : byteAt(x >> 1)) & 15);
                break;
            case 8:
                c.indices[base + x] = static_cast<std::uint8_t>(byteAt(x));
                break;
            case 16: {
                const std::uint32_t v = byteAt(2 * x) << 8 | byteAt(2 * x + 1); // 5-5-5, big-endian
                const std::uint32_t r5 = (v >> 10) & 31, g5 = (v >> 5) & 31, b5 = v & 31;
                c.rgb[base + x] = (r5 << 3 | r5 >> 2) << 16 | (g5 << 3 | g5 >> 2) << 8 | (b5 << 3 | b5 >> 2);
                break;
            }
            case 32:
                c.rgb[base + x] = (byteAt(4 * x + 1) << 16 | byteAt(4 * x + 2) << 8 | byteAt(4 * x + 3));
                break;
            }
        }
    }
    return c;
}

std::expected<std::optional<Palette>, std::string> parseCanvasPalette(std::span<const std::byte> chunk) {
    BeReader r(chunk);
    auto h = readHeader(r);
    if (!h)
        return std::unexpected(h.error());
    if (!h->hasPalette)
        return std::optional<Palette>{};
    auto p = readPalette(r);
    if (!p)
        return std::unexpected(p.error());
    return std::optional<Palette>(std::move(*p));
}

std::expected<Blit, std::string> parseBlit(std::span<const std::byte> chunk) {
    // Canvas read 0x450200, world read 0x450630; program interpreter 0x482e50.
    BeReader r(chunk);
    std::uint32_t tag = 0, attr = 0;
    std::int32_t programSize = 0;
    if (!r.u32(tag) || tag != kTagBlit || !r.u32(attr) || !r.i32(programSize) || programSize < 16 ||
        static_cast<std::size_t>(programSize) > r.remaining())
        return fail("BLIT: bad header");
    const auto program = r.rest().first(static_cast<std::size_t>(programSize));
    r.skip(static_cast<std::size_t>(programSize));
    std::int16_t tw = 0, th = 0, tdepth = 0, flag = 0;
    std::uint8_t kr = 0, kg = 0, kb = 0;
    if (!r.i16(tw) || !r.i16(th) || !r.i16(tdepth) || !r.u8(kr) || !r.u8(kg) || !r.u8(kb) || !r.i16(flag))
        return fail("BLIT: truncated trailer");

    LeReader p(program);
    std::uint16_t height = 0, width = 0, pitch = 0, depth = 0;
    std::uint32_t pad = 0;
    if (!p.u16(height) || !p.u16(width) || !p.u16(pitch) || !p.u16(depth) || !p.u32(pad) || !p.skip(pad))
        return fail("BLIT: truncated program header");
    if (depth != 8)
        return fail(std::format("BLIT: unsupported depth {}", depth));
    if (width != tw || height != th)
        return fail("BLIT: program size does not match the trailer");

    Blit b;
    b.width = width;
    b.height = height;
    b.keyRgb = std::uint32_t{kr} << 16 | std::uint32_t{kg} << 8 | kb;
    b.pixels.assign(std::size_t{width} * height, 0);
    b.opaque.assign(std::size_t{width} * height, 0);

    std::vector<std::uint32_t> rows(height);
    for (auto& o : rows)
        if (!p.u32(o))
            return fail("BLIT: truncated row table");

    for (std::size_t y = 0; y < height; ++y) {
        LeReader ops(program);
        if (!ops.seek(rows[y]))
            return fail("BLIT: row offset outside the program");
        std::size_t x = 0;
        auto put = [&](std::uint8_t v) {
            if (x < width) {
                b.pixels[y * width + x] = v;
                b.opaque[y * width + x] = 1;
            }
            ++x;
        };
        for (int guard = 0;; ++guard) {
            std::uint32_t n = 0;
            std::int16_t op = 0, count = 0;
            if (guard > 65536 || !ops.u32(n) || !ops.skip(n) || !ops.i16(op))
                return fail(std::format("BLIT: truncated row {}", y));
            if (op == 4)
                break;
            if (!ops.i16(count) || count < 0)
                return fail(std::format("BLIT: bad op in row {}", y));
            const auto cnt = static_cast<std::size_t>(count);
            switch (op) {
            case 0: // SKIP
                x += cnt;
                break;
            case 1: { // FILL: a replicated dword holds the value
                std::uint32_t v = 0;
                if (!ops.u32(v))
                    return fail("BLIT: truncated fill");
                for (std::size_t i = 0; i < cnt; ++i)
                    put(static_cast<std::uint8_t>(v));
                break;
            }
            case 2: { // COPY
                const auto src = ops.rest();
                if (src.size() < cnt)
                    return fail("BLIT: truncated copy");
                for (std::size_t i = 0; i < cnt; ++i)
                    put(std::to_integer<std::uint8_t>(src[i]));
                ops.skip(cnt);
                break;
            }
            case 3: { // MASKED: dst = (dst & mask) | src
                std::uint32_t k = 0;
                if (!ops.u32(k))
                    return fail("BLIT: truncated masked run");
                const auto data = ops.rest();
                const std::size_t maskAt = 4 * std::size_t{k};
                if (data.size() < maskAt + cnt || data.size() < cnt)
                    return fail("BLIT: truncated masked run");
                for (std::size_t i = 0; i < cnt; ++i) {
                    const auto src = std::to_integer<std::uint8_t>(data[i]);
                    const auto mask = std::to_integer<std::uint8_t>(data[maskAt + i]);
                    if (mask == 0x00)
                        put(src);
                    else
                        ++x; // 0xFF keeps the destination (the disc has no partial masks)
                }
                ops.skip(2 * cnt); // the interpreter steps 8 + 2n
                break;
            }
            default:
                return fail(std::format("BLIT: unknown op {} in row {}", op, y));
            }
        }
    }
    return b;
}

} // namespace brc::formats
