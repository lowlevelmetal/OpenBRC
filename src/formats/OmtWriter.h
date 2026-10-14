#pragma once

#include "formats/Canvas.h"
#include "formats/OmtFile.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace brc::formats {

// Builds an OMT "formatted stream" ('0MFS', see docs/formats/omt-container.md):
// chunks addressed by (type, id), written back to back after the 8-byte
// header, then the directory. The game's own writer (0x462110/0x462170) also
// lists the directory as the pseudo-chunk ('0HDR', '0HID'); this one does the
// same and writes an empty free list (the whole file is rewritten each time).
// The game writes such databases for its camera photos (BarbieCamera::SavePhoto
// 0x4042d0). No 'mCTy' name directory is written: those databases have none.
class OmtWriter {
public:
    // Every chunk of an existing container (the '0HDR' pseudo-chunk excluded).
    static std::expected<OmtWriter, std::string> from(const OmtFile& file);

    // Adds or replaces the chunk (type, id).
    void put(FourCC type, std::int32_t id, std::vector<std::byte> data);
    bool remove(FourCC type, std::int32_t id);
    bool contains(FourCC type, std::int32_t id) const;
    std::size_t size() const { return m_chunks.size(); }

    // The container bytes: '0MFS', directory offset, payloads, directory.
    std::vector<std::byte> serialize() const;

private:
    struct Chunk {
        FourCC type;
        std::int32_t id = 0;
        std::vector<std::byte> data;
    };
    std::vector<Chunk> m_chunks; // types keep their first-insertion order
};

// Serialises a canvas as the body of a 'Canv' chunk (OMediaCanvas::write
// 0x45fa10 with OMediaGfxWorld::write 0x472ce0; layout in
// docs/formats/canvas.md): 8 bits per pixel with its 'OPa2' palette (the
// 4096-entry inverse table is computed when the palette has none). Rows are
// PackBits-packed in 8-bit units. Other depths are not supported.
std::expected<std::vector<std::byte>, std::string> writeCanvas(const Canvas& canvas);

// PackBits in 8-bit units (the inverse of the unpacker 0x49bdd0).
void packBits8(std::span<const std::uint8_t> row, std::vector<std::byte>& out);

} // namespace brc::formats
