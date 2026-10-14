#pragma once

#include "core/File.h"

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace brc::formats {

// Four-character chunk type as the OMT tools wrote it: big-endian, so
// 'Canv' is 0x43616E76.
struct FourCC {
    std::uint32_t value = 0;

    constexpr FourCC() = default;
    constexpr explicit FourCC(std::uint32_t v) : value(v) {}
    constexpr FourCC(const char (&s)[5])
        : value(static_cast<std::uint32_t>(static_cast<unsigned char>(s[0])) << 24 |
                static_cast<std::uint32_t>(static_cast<unsigned char>(s[1])) << 16 |
                static_cast<std::uint32_t>(static_cast<unsigned char>(s[2])) << 8 |
                static_cast<std::uint32_t>(static_cast<unsigned char>(s[3]))) {}

    std::string str() const;
    constexpr bool operator==(const FourCC&) const = default;
};

namespace chunk {
inline constexpr FourCC kCanvas{"Canv"};      // OMT 1.x canvas (bitmap + palette)
inline constexpr FourCC kBlit{"BLIT"};        // pre-compiled sprite ('POSH')
inline constexpr FourCC kWave{"WAVE"};        // RIFF WAVE sound
inline constexpr FourCC kSmacker2{"SMK2"};    // embedded Smacker file
inline constexpr FourCC kSmacker{"SMAK"};     // embedded Smacker file (with SMKI info)
inline constexpr FourCC kSmackerInfo{"SMKI"}; // info for the SMAK chunk with the same id
inline constexpr FourCC kAnim{"HCAn"};        // Human Code 2D animation
inline constexpr FourCC kScript{"SCR "};      // compiled Human Code script
inline constexpr FourCC kData{"DATA"};        // raw data
inline constexpr FourCC kNames{"mCTy"};       // chunk name directory
inline constexpr FourCC kHeader{"0HDR"};      // the container's own directory
} // namespace chunk

struct OmtChunk {
    FourCC type;
    std::int32_t id = 0;
    std::uint32_t offset = 0; // from the start of the container
    std::uint32_t size = 0;
    std::string name;         // from the mCTy directory; empty when unnamed
};

// An OMT "formatted stream" (OMediaFormattedStream version 1, magic '0MFS'):
// chunks of data addressed by (type, id), with a directory at the end of the
// file. All header fields are big-endian. A ".omt.cache" file is the same
// container behind a 12-byte ASCII prefix. See docs/formats/omt-container.md.
class OmtFile {
public:
    // Parses the directory. The file stays open for chunk reads.
    static std::expected<OmtFile, std::string> open(std::shared_ptr<const RandomAccessFile> file,
                                                    std::string label);

    const std::string& label() const { return m_label; }
    const std::vector<OmtChunk>& chunks() const { return m_chunks; }

    const OmtChunk* find(FourCC type, std::int32_t id) const;
    // Case-insensitive, as the game's name lookups are.
    const OmtChunk* find(FourCC type, std::string_view name) const;
    std::vector<const OmtChunk*> ofType(FourCC type) const;

    std::vector<std::byte> read(const OmtChunk& chunk) const;
    // A window onto the chunk, for streaming (Smacker files are decoded in place).
    std::shared_ptr<RandomAccessFile> openChunk(const OmtChunk& chunk) const;

private:
    bool loadNames(std::string* error);

    std::shared_ptr<const RandomAccessFile> m_file;
    std::string m_label;
    std::uint32_t m_base = 0; // offset of '0MFS' in the file
    std::vector<OmtChunk> m_chunks;
};

} // namespace brc::formats
