#include "formats/OmtFile.h"

#include "core/StringUtil.h"
#include "formats/ByteReader.h"

#include <array>
#include <format>

namespace brc::formats {
namespace {

constexpr std::uint32_t kMagic = FourCC("0MFS").value;
constexpr std::uint32_t kMaxPrefix = 64; // ".omt.cache" files start with "<size>      \r\n"

void setError(std::string* error, std::string msg) {
    if (error)
        *error = std::move(msg);
}

} // namespace

std::string FourCC::str() const {
    std::string s(4, ' ');
    for (int i = 0; i < 4; ++i) {
        const auto c = static_cast<char>((value >> (24 - 8 * i)) & 0xFF);
        s[static_cast<std::size_t>(i)] = (c >= 0x20 && c < 0x7F) ? c : '?';
    }
    return s;
}

std::expected<OmtFile, std::string> OmtFile::open(std::shared_ptr<const RandomAccessFile> file, std::string label) {
    OmtFile omt;
    omt.m_file = std::move(file);
    omt.m_label = std::move(label);
    const std::uint64_t fileSize = omt.m_file->size();

    // Find the magic: at 0, or after the short text prefix of a cache file.
    std::array<std::byte, kMaxPrefix + 8> head{};
    const std::size_t got = omt.m_file->readAt(0, head);
    bool found = false;
    for (std::size_t i = 0; i + 8 <= got; ++i) {
        const auto v = std::to_integer<std::uint32_t>(head[i]) << 24 | std::to_integer<std::uint32_t>(head[i + 1]) << 16 |
                       std::to_integer<std::uint32_t>(head[i + 2]) << 8 | std::to_integer<std::uint32_t>(head[i + 3]);
        if (v == kMagic) {
            omt.m_base = static_cast<std::uint32_t>(i);
            found = true;
            break;
        }
    }
    if (!found)
        return std::unexpected(std::format("{}: not an OMT container (no '0MFS' signature)", omt.m_label));

    std::array<std::byte, 4> hdrOffBytes{};
    if (!omt.m_file->readExact(omt.m_base + 4, hdrOffBytes))
        return std::unexpected(std::format("{}: truncated header", omt.m_label));
    std::uint32_t hdrOffset = 0;
    BeReader(hdrOffBytes).u32(hdrOffset);
    const std::uint64_t dirStart = std::uint64_t{omt.m_base} + hdrOffset;
    if (dirStart >= fileSize)
        return std::unexpected(std::format("{}: directory offset {} beyond end of file", omt.m_label, hdrOffset));

    std::vector<std::byte> dir(static_cast<std::size_t>(fileSize - dirStart));
    if (!omt.m_file->readExact(dirStart, dir))
        return std::unexpected(std::format("{}: cannot read directory", omt.m_label));

    BeReader r(dir);
    std::uint32_t ntypes = 0;
    if (!r.u32(ntypes) || ntypes > 4096)
        return std::unexpected(std::format("{}: bad directory", omt.m_label));
    for (std::uint32_t t = 0; t < ntypes; ++t) {
        std::uint32_t type = 0, count = 0;
        if (!r.u32(type) || !r.u32(count) || count > 1'000'000)
            return std::unexpected(std::format("{}: truncated directory", omt.m_label));
        for (std::uint32_t i = 0; i < count; ++i) {
            OmtChunk c;
            c.type = FourCC(type);
            if (!r.i32(c.id) || !r.u32(c.offset) || !r.u32(c.size))
                return std::unexpected(std::format("{}: truncated directory", omt.m_label));
            if (std::uint64_t{omt.m_base} + c.offset + c.size > fileSize)
                return std::unexpected(std::format("{}: chunk {} {} outside the file", omt.m_label, c.type.str(), c.id));
            if (c.type == chunk::kHeader)
                continue;
            omt.m_chunks.push_back(std::move(c));
        }
    }

    std::string error;
    if (!omt.loadNames(&error))
        return std::unexpected(std::format("{}: {}", omt.m_label, error));
    return omt;
}

// The 'mCTy' chunk maps (type, id) to names: BE u32 type count, then per type
// its fourcc, a BE u16 flag (0 or 1, meaning unknown), a BE u32 entry count and
// entries of (BE i32 id, Pascal string). Some chunks have unused bytes at the
// end. Every name on the disc refers to an existing chunk.
bool OmtFile::loadNames(std::string* error) {
    const OmtChunk* names = nullptr;
    for (const auto& c : m_chunks)
        if (c.type == chunk::kNames)
            names = &c;
    if (!names)
        return true;
    const auto data = read(*names);
    BeReader r(data);
    std::uint32_t ntypes = 0;
    if (!r.u32(ntypes)) {
        setError(error, "truncated name directory");
        return false;
    }
    for (std::uint32_t t = 0; t < ntypes; ++t) {
        std::uint32_t type = 0, count = 0;
        std::uint16_t flag = 0;
        if (!r.u32(type) || !r.u16(flag) || !r.u32(count)) {
            setError(error, "truncated name directory");
            return false;
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            std::int32_t id = 0;
            std::string name;
            if (!r.i32(id) || !r.pascal(name)) {
                setError(error, "truncated name directory");
                return false;
            }
            for (auto& c : m_chunks)
                if (c.type.value == type && c.id == id)
                    c.name = name;
        }
    }
    return true;
}

const OmtChunk* OmtFile::find(FourCC type, std::int32_t id) const {
    for (const auto& c : m_chunks)
        if (c.type == type && c.id == id)
            return &c;
    return nullptr;
}

const OmtChunk* OmtFile::find(FourCC type, std::string_view name) const {
    for (const auto& c : m_chunks)
        if (c.type == type && str::iequals(c.name, name))
            return &c;
    return nullptr;
}

std::vector<const OmtChunk*> OmtFile::ofType(FourCC type) const {
    std::vector<const OmtChunk*> out;
    for (const auto& c : m_chunks)
        if (c.type == type)
            out.push_back(&c);
    return out;
}

std::vector<std::byte> OmtFile::read(const OmtChunk& c) const {
    std::vector<std::byte> out(c.size);
    out.resize(m_file->readAt(std::uint64_t{m_base} + c.offset, out));
    return out;
}

std::shared_ptr<RandomAccessFile> OmtFile::openChunk(const OmtChunk& c) const {
    return std::make_shared<SubFile>(m_file, std::uint64_t{m_base} + c.offset, c.size);
}

} // namespace brc::formats
