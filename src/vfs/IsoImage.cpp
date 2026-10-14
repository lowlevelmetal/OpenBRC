#include "vfs/IsoImage.h"

#include "core/Log.h"
#include "core/StringUtil.h"

#include <array>
#include <cstring>
#include <format>
#include <fstream>

namespace brc::vfs {
namespace {

constexpr std::uint32_t kSector = 2048;
constexpr std::array<std::uint8_t, 12> kSync = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

void setError(std::string* error, std::string msg) {
    if (error)
        *error = std::move(msg);
}

bool hasSyncAt(const RandomAccessFile& f, std::uint64_t offset) {
    std::array<std::byte, 12> buf{};
    return f.readExact(offset, buf) && std::memcmp(buf.data(), kSync.data(), 12) == 0;
}

// Presents a raw-sector image as a sequence of 2048-byte logical sectors.
class RawSectorFile final : public RandomAccessFile {
public:
    RawSectorFile(std::shared_ptr<const RandomAccessFile> raw, std::uint32_t sectorSize, std::uint32_t dataOffset)
        : m_raw(std::move(raw)), m_sectorSize(sectorSize), m_dataOffset(dataOffset) {}

    std::uint64_t size() const override { return (m_raw->size() / m_sectorSize) * kSector; }

    std::size_t readAt(std::uint64_t offset, std::span<std::byte> out) const override {
        std::size_t total = 0;
        while (total < out.size()) {
            const std::uint64_t pos = offset + total;
            const std::uint64_t sector = pos / kSector;
            const std::uint32_t within = static_cast<std::uint32_t>(pos % kSector);
            const std::size_t chunk = std::min<std::size_t>(out.size() - total, kSector - within);
            const std::uint64_t rawPos = sector * m_sectorSize + m_dataOffset + within;
            const std::size_t got = m_raw->readAt(rawPos, out.subspan(total, chunk));
            total += got;
            if (got != chunk)
                break;
        }
        return total;
    }

private:
    std::shared_ptr<const RandomAccessFile> m_raw;
    std::uint32_t m_sectorSize;
    std::uint32_t m_dataOffset;
};

// Decodes a Joliet (UCS-2 big-endian) identifier to UTF-8.
std::string decodeUcs2(const std::byte* p, std::size_t len) {
    std::string out;
    for (std::size_t i = 0; i + 1 < len; i += 2) {
        const char32_t c = (static_cast<char32_t>(std::to_integer<unsigned>(p[i])) << 8) |
                           std::to_integer<unsigned>(p[i + 1]);
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

// Strips the ISO 9660 ";1" version suffix and a trailing '.'.
std::string cleanIdentifier(std::string id) {
    if (const auto semi = id.find(';'); semi != std::string::npos)
        id.erase(semi);
    if (!id.empty() && id.back() == '.')
        id.pop_back();
    return id;
}

// Resolves the data file referenced by a .cue sheet (first FILE line).
std::optional<std::filesystem::path> cueDataFile(const std::filesystem::path& cue) {
    std::ifstream in(cue);
    std::string line;
    while (std::getline(in, line)) {
        const auto t = str::trim(line);
        if (!str::istartsWith(t, "FILE "))
            continue;
        auto rest = t.substr(5);
        const auto lastSpace = rest.rfind(' ');
        if (lastSpace != std::string_view::npos)
            rest = rest.substr(0, lastSpace); // drop BINARY/WAVE type
        rest = str::trim(rest);
        if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"')
            rest = rest.substr(1, rest.size() - 2);
        auto p = str::toPath(rest);
        if (p.is_relative())
            p = cue.parent_path() / p;
        return p;
    }
    return std::nullopt;
}

} // namespace

std::shared_ptr<IsoImage> IsoImage::open(const std::filesystem::path& path, std::string* error) {
    std::filesystem::path target = path;
    if (str::iequals(str::fromPath(path.extension()), ".cue")) {
        auto data = cueDataFile(path);
        if (!data) {
            setError(error, "cue sheet has no FILE entry");
            return nullptr;
        }
        target = *data;
    }
    auto file = OsFile::open(target);
    if (!file) {
        setError(error, std::format("cannot open '{}'", str::fromPath(target)));
        return nullptr;
    }
    return open(std::move(file), str::fromPath(target), error);
}

std::shared_ptr<IsoImage> IsoImage::open(std::shared_ptr<const RandomAccessFile> image, std::string label,
                                         std::string* error) {
    std::shared_ptr<IsoImage> iso(new IsoImage());
    iso->m_raw = std::move(image);
    iso->m_label = std::move(label);
    if (!iso->load(error))
        return nullptr;
    return iso;
}

bool IsoImage::load(std::string* error) {
    // Detect the sector layout by looking for the raw-sector sync pattern at
    // the start of the image (sector 0 of every raw dump starts with it).
    struct Layout {
        SectorFormat format;
        std::uint32_t sectorSize;
        std::uint32_t dataOffset;
    };
    constexpr Layout layouts[] = {
        {SectorFormat::Cooked2048, 2048, 0},
        {SectorFormat::RawMode1, 2352, 16},
        {SectorFormat::RawMode2Form1, 2352, 24},
        {SectorFormat::RawMode1Sub, 2448, 16},
        {SectorFormat::RawMode2Form1Sub, 2448, 24},
    };

    bool found = false;
    for (const auto& l : layouts) {
        if (l.sectorSize != 2048 && !(hasSyncAt(*m_raw, 0) && hasSyncAt(*m_raw, l.sectorSize)))
            continue;
        std::shared_ptr<const RandomAccessFile> logical =
            l.sectorSize == 2048 ? m_raw
                                 : std::make_shared<RawSectorFile>(m_raw, l.sectorSize, l.dataOffset);

        // Primary Volume Descriptor lives at logical sector 16.
        std::array<std::byte, 7> hdr{};
        if (!logical->readExact(16 * kSector, hdr))
            continue;
        if (std::to_integer<int>(hdr[0]) == 1 && std::memcmp(hdr.data() + 1, "CD001", 5) == 0) {
            m_format = l.format;
            m_logical = std::move(logical);
            found = true;
            break;
        }
    }
    if (!found) {
        setError(error, "not an ISO 9660 image (no primary volume descriptor)");
        return false;
    }

    // Walk the volume descriptor set: PVD (type 1) and, if present, a Joliet
    // SVD (type 2 with escape sequence %/@, %/C or %/E).
    std::uint32_t rootLba = 0, rootSize = 0;
    std::uint32_t jolietLba = 0, jolietSize = 0;
    for (std::uint32_t sector = 16; sector < 16 + 64; ++sector) {
        std::array<std::byte, kSector> vd{};
        if (!m_logical->readExact(static_cast<std::uint64_t>(sector) * kSector, vd))
            break;
        const int type = std::to_integer<int>(vd[0]);
        if (std::memcmp(vd.data() + 1, "CD001", 5) != 0 || type == 255)
            break;
        const std::byte* root = vd.data() + 156; // root directory record
        const std::uint32_t lba = loadLE<std::uint32_t>(root + 2);
        const std::uint32_t size = loadLE<std::uint32_t>(root + 10);
        if (type == 1) {
            rootLba = lba;
            rootSize = size;
            std::string volId(reinterpret_cast<const char*>(vd.data() + 40), 32);
            m_volumeId = std::string(str::trim(volId));
        } else if (type == 2) {
            const std::byte* esc = vd.data() + 88;
            if (std::to_integer<int>(esc[0]) == 0x25 && std::to_integer<int>(esc[1]) == 0x2F &&
                (std::to_integer<int>(esc[2]) == 0x40 || std::to_integer<int>(esc[2]) == 0x43 ||
                 std::to_integer<int>(esc[2]) == 0x45)) {
                jolietLba = lba;
                jolietSize = size;
            }
        }
    }

    if (jolietLba && readDirectory(jolietLba, jolietSize, "", true, 0)) {
        m_joliet = true;
    } else {
        m_files.clear();
        if (!rootLba || !readDirectory(rootLba, rootSize, "", false, 0)) {
            setError(error, "unreadable root directory");
            return false;
        }
    }
    log::debug("iso: '{}' volume '{}' {} files ({}, {})", m_label, m_volumeId, m_files.size(),
               m_joliet ? "joliet" : "iso9660", static_cast<int>(m_format) == 0 ? "cooked" : "raw");
    return true;
}

bool IsoImage::readDirectory(std::uint32_t lba, std::uint32_t size, const std::string& prefix, bool joliet,
                             int depth) {
    if (depth > 16 || size == 0 || size > (64u << 20))
        return false;
    std::vector<std::byte> dir(size);
    if (!m_logical->readExact(static_cast<std::uint64_t>(lba) * kSector, dir))
        return false;

    std::size_t pos = 0;
    while (pos < dir.size()) {
        const std::uint8_t recLen = std::to_integer<std::uint8_t>(dir[pos]);
        if (recLen == 0) {
            // Records never span sectors; skip padding to the next one.
            pos = (pos / kSector + 1) * kSector;
            continue;
        }
        if (pos + recLen > dir.size() || recLen < 33)
            break;
        const std::byte* rec = dir.data() + pos;
        const std::uint32_t extent = loadLE<std::uint32_t>(rec + 2);
        const std::uint32_t length = loadLE<std::uint32_t>(rec + 10);
        const std::uint8_t flags = std::to_integer<std::uint8_t>(rec[25]);
        const std::uint8_t nameLen = std::to_integer<std::uint8_t>(rec[32]);
        const std::byte* nameBytes = rec + 33;
        pos += recLen;

        // "." and ".." are encoded as single bytes 0 and 1.
        if (nameLen == 1 && (std::to_integer<int>(nameBytes[0]) == 0 || std::to_integer<int>(nameBytes[0]) == 1))
            continue;

        std::string name = joliet ? decodeUcs2(nameBytes, nameLen)
                                  : std::string(reinterpret_cast<const char*>(nameBytes), nameLen);
        name = cleanIdentifier(std::move(name));
        if (name.empty())
            continue;
        const std::string path = prefix.empty() ? name : prefix + "/" + name;

        if (flags & 0x02) {
            if (!readDirectory(extent, length, path, joliet, depth + 1))
                return false;
        } else {
            m_files.try_emplace(str::normalizeVirtualPath(path), FileEntry{extent, length});
        }
    }
    return true;
}

std::string IsoImage::describe() const {
    return std::format("disc image {} (volume '{}')", m_label, m_volumeId);
}

std::shared_ptr<RandomAccessFile> IsoImage::open(std::string_view path) const {
    const auto it = m_files.find(str::normalizeVirtualPath(path));
    if (it == m_files.end())
        return nullptr;
    return std::make_shared<SubFile>(m_logical, static_cast<std::uint64_t>(it->second.lba) * kSector,
                                     it->second.size);
}

bool IsoImage::exists(std::string_view path) const { return m_files.contains(str::normalizeVirtualPath(path)); }

void IsoImage::forEachFile(const std::function<void(const EntryInfo&)>& fn) const {
    for (const auto& [key, e] : m_files)
        fn(EntryInfo{key, e.size, false});
}

} // namespace brc::vfs
