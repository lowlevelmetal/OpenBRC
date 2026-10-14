#pragma once

#include "core/File.h"
#include "formats/OmtFile.h"

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace brc::formats {

// Human Code sprite animation description ('HCAn' chunk). The frames' pixels
// come from a Smacker file or still images found by the anim's name; see
// docs/formats/hcanim.md. All fields are big-endian.

namespace anim_flags {
// HCAn header flags (HC2DAnim v2 loader 0x4485b0)
inline constexpr std::uint32_t kTimeBased = 0x1;   // frames last `durationMs` (set on every anim on the disc)
inline constexpr std::uint32_t kUnknown2 = 0x2;    // copied to the element; no reader found
inline constexpr std::uint32_t kNoSources = 0x4;   // a Pascal string replaces the frame-source table
inline constexpr std::uint32_t kKeepDepth = 0x8;   // SetSequence does not take the sequence's z
// Sequence flags
inline constexpr std::uint32_t kSeqLoop = 0x4;
inline constexpr std::uint32_t kSeqHasZ = 0x8;
// Frame flags
inline constexpr std::uint32_t kFrameEvent = 0x1;      // tell the node the frame index
inline constexpr std::uint32_t kFrameStop = 0x2;       // stop on this frame, report the sequence ended
inline constexpr std::uint32_t kFrameDuration = 0x4;   // own duration follows
inline constexpr std::uint32_t kFrameGoto = 0x8;       // then continue with another sequence
inline constexpr std::uint32_t kFrameSound = 0x10;     // play a sound when shown
inline constexpr std::uint32_t kFrameSetSeq = 0x20;    // set another anim's sequence
inline constexpr std::uint32_t kFramePauseAnim = 0x40; // pause and hide another anim
inline constexpr std::uint32_t kFrameStartScript = 0x80;
inline constexpr std::uint32_t kFrameStopScript = 0x100;
} // namespace anim_flags

struct HcAnimFrame {
    std::uint16_t source = 1; // 1-based index into HcAnimDesc::sourceOffsets (= Smacker frame number)
    std::uint32_t flags = 0;
    std::uint16_t gotoSequence = 0;
    std::uint16_t durationMs = 0; // own duration or the anim's default
    std::string sound;            // WAVE name, ".wav" removed
    std::string setSeqAnim;       // flag 0x20
    std::uint16_t setSeqIndex = 0;
    std::string pauseAnim;        // flag 0x40
    std::string startScript;      // flag 0x80
    std::string stopScript;       // flag 0x100
};

struct HcAnimSequence {
    std::uint32_t flags = 0;
    std::uint16_t z = 0; // own z (flag 8) or the anim's default
    std::vector<HcAnimFrame> frames;

    bool loops() const { return (flags & anim_flags::kSeqLoop) != 0; }
};

struct HcAnimOffset {
    std::int16_t x = 0;
    std::int16_t y = 0;
};

struct HcAnimDesc {
    int version = 2;
    std::uint16_t defaultFrameMs = 0;
    std::uint16_t defaultZ = 0;
    std::uint32_t flags = 0;
    std::vector<HcAnimOffset> sourceOffsets; // top-left of each source frame relative to the anim
    std::vector<HcAnimSequence> sequences;
};

// Parses a version 1 or 2 description. Version 1 (no instances on the disc)
// lists still images per sequence; its frames get 15 fps timing (66 ms) and the
// last frame of each sequence is a stop frame (loader 0x448320).
std::expected<HcAnimDesc, std::string> parseHcAnim(std::span<const std::byte> data);

// 'SMKI' chunk: the content size of each frame of a cropped ('SMAK') anim.
struct SmkiEntry {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
};
std::vector<SmkiEntry> parseSmki(std::span<const std::byte> data);

// A ".omt.cache" file: the HCAn descriptions of one scene in an OMT container,
// followed by one record per anim with its Smacker data, so the scene's anims
// can be created in one sequential read.
struct AnimCacheRecord {
    std::string name;
    bool cropped = false;       // 'SMK1': Smacker data then SMKI records; 'SMK2': full frames
    std::uint64_t offset = 0;   // of the record's data in the file
    std::uint32_t size = 0;
};

struct AnimCache {
    OmtFile container;
    std::vector<AnimCacheRecord> records;
};

std::expected<AnimCache, std::string> openAnimCache(std::shared_ptr<const RandomAccessFile> file, std::string label);

} // namespace brc::formats
