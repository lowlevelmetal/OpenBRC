#include "formats/OmtWriter.h"

#include <algorithm>
#include <format>
#include <map>

namespace brc::formats {
namespace {

constexpr std::uint32_t kMagic = FourCC("0MFS").value;
constexpr FourCC kHeaderId{"0HID"}; // the directory's own id ('0HDR', '0HID')

// Big-endian stream primitives (the OMT stream with its swap flag set).
class BeWriter {
public:
    explicit BeWriter(std::vector<std::byte>& out) : m_out(out) {}
    void u8(std::uint8_t v) { m_out.push_back(std::byte{v}); }
    void u16(std::uint16_t v) {
        u8(static_cast<std::uint8_t>(v >> 8));
        u8(static_cast<std::uint8_t>(v));
    }
    void i16(std::int16_t v) { u16(static_cast<std::uint16_t>(v)); }
    void u32(std::uint32_t v) {
        u16(static_cast<std::uint16_t>(v >> 16));
        u16(static_cast<std::uint16_t>(v));
    }
    void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
    void pascal(std::string_view s) {
        const auto n = static_cast<std::uint8_t>(std::min<std::size_t>(s.size(), 255));
        u8(n);
        for (std::size_t i = 0; i < n; ++i)
            u8(static_cast<std::uint8_t>(s[i]));
    }
    void bytes(std::span<const std::byte> b) { m_out.insert(m_out.end(), b.begin(), b.end()); }

private:
    std::vector<std::byte>& m_out;
};

} // namespace

std::expected<OmtWriter, std::string> OmtWriter::from(const OmtFile& file) {
    OmtWriter w;
    for (const auto& c : file.chunks()) {
        auto data = file.read(c);
        if (data.size() != c.size)
            return std::unexpected(std::format("{}: cannot read {} {}", file.label(), c.type.str(), c.id));
        w.put(c.type, c.id, std::move(data));
    }
    return w;
}

void OmtWriter::put(FourCC type, std::int32_t id, std::vector<std::byte> data) {
    for (auto& c : m_chunks) {
        if (c.type == type && c.id == id) {
            c.data = std::move(data);
            return;
        }
    }
    m_chunks.push_back({type, id, std::move(data)});
}

bool OmtWriter::remove(FourCC type, std::int32_t id) {
    return std::erase_if(m_chunks, [&](const Chunk& c) { return c.type == type && c.id == id; }) != 0;
}

bool OmtWriter::contains(FourCC type, std::int32_t id) const {
    return std::ranges::any_of(m_chunks, [&](const Chunk& c) { return c.type == type && c.id == id; });
}

std::vector<std::byte> OmtWriter::serialize() const {
    // Directory order: types in first-insertion order, ids ascending, then the
    // directory's own entry under its own type.
    std::vector<FourCC> types;
    for (const auto& c : m_chunks)
        if (std::ranges::find(types, c.type) == types.end())
            types.push_back(c.type);

    std::vector<std::byte> out;
    BeWriter w(out);
    w.u32(kMagic);
    w.u32(0); // directory offset, patched below
    struct Entry {
        std::int32_t id;
        std::uint32_t offset, size;
    };
    std::map<std::uint32_t, std::vector<Entry>> entries;
    for (const auto& c : m_chunks) {
        entries[c.type.value].push_back({c.id, static_cast<std::uint32_t>(out.size()),
                                         static_cast<std::uint32_t>(c.data.size())});
        w.bytes(c.data);
    }
    const auto dirOffset = static_cast<std::uint32_t>(out.size());
    std::uint32_t dirSize = 4 + 4; // type count, free count
    for (const auto& t : types)
        dirSize += 8 + 12 * static_cast<std::uint32_t>(entries[t.value].size());
    dirSize += 8 + 12; // '0HDR' with its one entry

    w.u32(static_cast<std::uint32_t>(types.size() + 1));
    for (const auto& t : types) {
        auto& list = entries[t.value];
        std::ranges::sort(list, {}, &Entry::id);
        w.u32(t.value);
        w.u32(static_cast<std::uint32_t>(list.size()));
        for (const auto& e : list) {
            w.i32(e.id);
            w.u32(e.offset);
            w.u32(e.size);
        }
    }
    w.u32(chunk::kHeader.value);
    w.u32(1);
    w.i32(static_cast<std::int32_t>(kHeaderId.value));
    w.u32(dirOffset);
    w.u32(dirSize);
    w.u32(0); // no free ranges

    for (int i = 0; i < 4; ++i)
        out[4 + static_cast<std::size_t>(i)] = std::byte{static_cast<std::uint8_t>(dirOffset >> (24 - 8 * i))};
    return out;
}

void packBits8(std::span<const std::uint8_t> row, std::vector<std::byte>& out) {
    // Runs of three or more equal bytes become repeat units (control 1 - n),
    // everything else literal runs (control n - 1), at most 128 bytes each.
    const std::size_t n = row.size();
    std::size_t i = 0;
    while (i < n) {
        std::size_t run = 1;
        while (i + run < n && run < 128 && row[i + run] == row[i])
            ++run;
        if (run >= 3) {
            out.push_back(std::byte{static_cast<std::uint8_t>(static_cast<std::int8_t>(1 - static_cast<int>(run)))});
            out.push_back(std::byte{row[i]});
            i += run;
            continue;
        }
        std::size_t lit = 0;
        while (i + lit < n && lit < 128) {
            if (i + lit + 2 < n && row[i + lit] == row[i + lit + 1] && row[i + lit] == row[i + lit + 2])
                break;
            ++lit;
        }
        out.push_back(std::byte{static_cast<std::uint8_t>(lit - 1)});
        for (std::size_t k = 0; k < lit; ++k)
            out.push_back(std::byte{row[i + k]});
        i += lit;
    }
}

std::expected<std::vector<std::byte>, std::string> writeCanvas(const Canvas& canvas) {
    if (canvas.depth != 8 || !canvas.palette)
        return std::unexpected(std::format("writeCanvas: unsupported canvas (depth {}, palette {})", canvas.depth,
                                           canvas.palette.has_value()));
    if (canvas.width <= 0 || canvas.height <= 0 || canvas.width > 0x7FFF || canvas.height > 0x7FFF ||
        canvas.indices.size() != static_cast<std::size_t>(canvas.width) * static_cast<std::size_t>(canvas.height))
        return std::unexpected(std::format("writeCanvas: bad size {}x{}", canvas.width, canvas.height));

    std::vector<std::byte> out;
    BeWriter w(out);
    w.u32(FourCC("OmCv").value);
    w.u32(canvas.attr);
    w.u32(FourCC("OmGW").value);
    w.i16(static_cast<std::int16_t>(canvas.width));
    w.i16(static_cast<std::int16_t>(canvas.height));
    w.i16(8);                                        // depth
    w.u8(0);                                         // pack mode: 8-bit units
    w.u8(1);                                         // version
    w.i16(static_cast<std::int16_t>(canvas.width)); // row bytes
    w.i16(canvas.hasKey ? 1 : 0);
    if (canvas.hasKey)
        w.u32(canvas.key);
    w.i16(1); // has palette

    // 'OPa2': description, count, 16-bit guns, inverse table.
    const Palette& pal = *canvas.palette;
    w.u32(FourCC("OPa2").value);
    w.pascal("");
    w.u32(256);
    for (std::uint32_t rgb : pal.rgb) {
        w.u16(static_cast<std::uint16_t>(((rgb >> 16) & 0xFF) * 257));
        w.u16(static_cast<std::uint16_t>(((rgb >> 8) & 0xFF) * 257));
        w.u16(static_cast<std::uint16_t>((rgb & 0xFF) * 257));
    }
    if (pal.inverse.size() == 4096) {
        for (std::uint8_t v : pal.inverse)
            w.u8(v);
    } else {
        // No stored table: nearest colour to each cell's centre.
        Palette search;
        search.rgb = pal.rgb;
        for (std::uint32_t i = 0; i < 4096; ++i) {
            const std::uint32_t r = ((i >> 8) & 0xF) * 17, g = ((i >> 4) & 0xF) * 17, b = (i & 0xF) * 17;
            w.u8(search.lookup(r << 16 | g << 8 | b));
        }
    }

    std::vector<std::byte> rows;
    std::vector<std::byte> packed;
    for (int y = 0; y < canvas.height; ++y) {
        packed.clear();
        packBits8(std::span(canvas.indices).subspan(static_cast<std::size_t>(y) * static_cast<std::size_t>(canvas.width),
                                                    static_cast<std::size_t>(canvas.width)),
                  packed);
        rows.push_back(std::byte{static_cast<std::uint8_t>(packed.size() >> 8)});
        rows.push_back(std::byte{static_cast<std::uint8_t>(packed.size())});
        rows.insert(rows.end(), packed.begin(), packed.end());
    }
    w.u32(static_cast<std::uint32_t>(rows.size()));
    w.bytes(rows);
    return out;
}

} // namespace brc::formats
