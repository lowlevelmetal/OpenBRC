#pragma once

#include "core/File.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace brc::formats {

// Smacker video (RAD Game Tools), signatures 'SMK2' and 'SMK4'. Barbie Riding
// Club plays Smacker 3.2 files: standalone movies (Resources/SMK/*.smk) and
// sprite animations embedded in .omt containers ('SMK2' and 'SMAK' chunks).
//
// Ported from the maintainer's decoder in BarbieRecomp
// (runtime/hle/smacker.c), extended with SMK4 full-block modes, the Y-doubled
// and Y-interlaced flags, ring frames, all seven audio tracks, raw PCM audio
// and keyframe seeking. The format is little-endian:
//
//   0x00 'SMK2'|'SMK4', width, height, frames, frame rate, flags,
//   0x18 audio size[7] (largest decompressed chunk per track),
//   0x34 trees size, mmap/mclr/full/type tree allocation sizes,
//   0x48 audio rate[7] (bit 31 compressed, 30 present, 29 16-bit, 28 stereo,
//        27 Bink audio, 26 Bink DCT, 0..23 sample rate), 0x64 reserved;
//   0x68 frame sizes[n] (bit 0 keyframe, bit 1 unused), frame types[n]
//        (bit 0 palette chunk, bit 1+t audio track t), Huffman trees;
//   then the frames: [palette][audio chunks...][video bitstream].
//
// n = frames + 1 when the file has a ring frame (flags bit 0): an extra frame
// that turns the last frame back into frame 0 so a loop does not need a full
// frame.

inline constexpr int kSmackerMaxTracks = 7;

// No Smacker file of the game sets these flags or uses SMK4; their handling
// follows the format descriptions (inferred, not checked against smackw32).
enum class SmackerYScale : std::uint8_t {
    None,
    Interlaced, // flags bit 1: every stored row is followed by a row the decoder leaves untouched
    Doubled,    // flags bit 2: every stored row is shown twice
};

struct SmackerAudioTrack {
    bool present = false;    // the header declares the track (rate bit 30)
    bool compressed = false; // Smacker Huffman DPCM (bit 31); otherwise raw PCM
    bool bink = false;       // Bink audio (bit 27): not supported, never decoded
    bool is16Bit = false;    // bit 29; otherwise 8-bit unsigned
    bool stereo = false;     // bit 28
    std::uint32_t sampleRate = 0;
    std::uint32_t maxChunkSize = 0; // header audio size: largest decompressed chunk, bytes

    int channels() const { return stereo ? 2 : 1; }
    bool decodable() const { return present && !bink && sampleRate != 0; }
};

// Area of the output frame (frame coordinates, before any target offset).
struct SmackerRect {
    int x = 0, y = 0, w = 0, h = 0;

    bool empty() const { return w <= 0 || h <= 0; }
};

struct SmackerFrameInfo {
    std::uint32_t frame = 0;   // 0-based frame now shown in the target
    SmackerRect dirty;         // pixels changed by this call (union over every frame decoded)
    std::uint32_t decoded = 0; // frames decoded by this call (more than 1 after a seek, 0 if unchanged)
    bool paletteChanged = false;
};

class Smacker {
public:
    // Parses the header, tables and Huffman trees. The data may be followed by
    // other bytes (an .omt chunk or a whole file read from an offset); byteSize()
    // tells where the Smacker data ends. A file shorter than its frame table
    // says opens, and the missing frames fail to decode.
    static std::expected<Smacker, std::string> open(std::shared_ptr<const RandomAccessFile> file);
    // Copies `data`.
    static std::expected<Smacker, std::string> open(std::span<const std::byte> data);

    Smacker(Smacker&&) noexcept;
    Smacker& operator=(Smacker&&) noexcept;
    ~Smacker();

    // --- Header -----------------------------------------------------------
    std::uint32_t version() const { return m_version; } // 2 or 4
    std::uint32_t width() const { return m_width; }
    // Output height: the stored height, doubled for Y-interlaced/Y-doubled files.
    std::uint32_t height() const;
    std::uint32_t storedHeight() const { return m_height; }
    std::uint32_t frameCount() const { return m_frames; } // without the ring frame
    bool hasRingFrame() const { return m_ring; }
    SmackerYScale yScale() const { return m_yScale; }
    std::uint32_t flags() const { return m_flags; }
    // Header frame rate field: > 0 ms per frame, < 0 units of 10 us, 0 = 10 fps.
    std::int32_t rawFrameRate() const { return m_rawRate; }
    std::uint64_t frameDurationUs() const;
    double framesPerSecond() const;
    const SmackerAudioTrack& audioTrack(int track) const;
    bool isKeyframe(std::uint32_t frame) const;
    // Bytes the Smacker data occupies: header, tables, trees and every frame.
    std::uint64_t byteSize() const { return m_byteSize; }
    // The data ends before byteSize() (frames past the end cannot be decoded).
    bool truncated() const { return m_byteSize > m_file->size(); }

    // --- Playback position --------------------------------------------------
    // Like RAD's Smack structure, the current frame is "loaded" before it is
    // decoded: its palette is available first (the game reads it before
    // SmackDoFrame). open() loads frame 0.
    std::uint32_t currentFrame() const { return m_frame; }
    // 256 RGB triples, 8 bits per component, for the current frame.
    const std::array<std::uint8_t, 768>& palette() const { return m_palette; }
    // The current frame carries a palette chunk.
    bool paletteChanged() const;
    // Advances to the next frame; after the last one, back to frame 0 (through
    // the ring frame when the file has one).
    void nextFrame();
    // Loads `frame` (0-based, clamped to the last frame). The next
    // decodeFrame() shows it, decoding forward from the closest keyframe, or
    // from the frame already in the target when that is on the way.
    void gotoFrame(std::uint32_t frame);
    // Forgets what the target holds: the next decodeFrame() rebuilds the
    // current frame from its keyframe (or frame 0).
    void invalidate() { m_decoded = -1; }

    // --- Decode target ------------------------------------------------------
    // Decode into a caller-owned 8-bit buffer at (x, y) with `pitch` bytes per
    // row. Returns false (and keeps the previous target) when the frame would
    // not fit. Like SmackToBuffer, retargeting keeps the decode state: frames
    // are deltas, so the new buffer should hold the previous frame (or call
    // invalidate()).
    bool setTarget(std::span<std::uint8_t> buffer, std::size_t pitch, std::uint32_t x = 0,
                   std::uint32_t y = 0);
    // Decode into the decoder's own zero-initialised width() x height() buffer.
    void useOwnBuffer();
    std::span<const std::uint8_t> frameBuffer() const { return m_ownBuffer; }

    // Decodes the current frame into the target (frames before it too, after a
    // seek), and the audio of the current frame for the enabled tracks. When
    // frames were skipped with nextFrame(), their audio is included too, so a
    // player that drops frames keeps continuous sound. Does nothing when the
    // target already shows the current frame. On error the target holds
    // whatever was decoded before the problem.
    std::expected<SmackerFrameInfo, std::string> decodeFrame();
    // Dirty area of the last decodeFrame() call.
    const SmackerRect& dirtyRect() const { return m_dirty; }

    // --- Audio ----------------------------------------------------------------
    // Bit t enables track t. Default: every decodable track.
    void setAudioTracks(std::uint8_t mask) { m_trackMask = mask; }
    std::uint8_t audioTracks() const { return m_trackMask; }
    // Interleaved int16 PCM decoded by the last decodeFrame() for `track`
    // (empty when the frame has none or the track is disabled).
    std::span<const std::int16_t> audio(int track) const;

private:
    Smacker();

    struct Tree;
    bool readFrame(std::uint32_t stored, std::vector<std::uint8_t>& out, std::string* error) const;
    bool decodeStored(std::uint32_t stored, bool withAudio, std::string* error);
    bool decodeVideo(std::span<const std::uint8_t> data, std::string* error);
    std::uint32_t storedFrames() const { return m_frames + (m_ring ? 1u : 0u); }
    void loadPalette();

    std::shared_ptr<const RandomAccessFile> m_file;
    std::uint32_t m_version = 2;
    std::uint32_t m_width = 0, m_height = 0, m_frames = 0, m_flags = 0;
    std::int32_t m_rawRate = 0;
    bool m_ring = false;
    SmackerYScale m_yScale = SmackerYScale::None;
    std::array<SmackerAudioTrack, kSmackerMaxTracks> m_tracks{};
    std::uint64_t m_byteSize = 0;

    std::vector<std::uint32_t> m_frameSize; // raw table values (flag bits included)
    std::vector<std::uint8_t> m_frameType;
    std::vector<std::uint64_t> m_frameOffset;
    std::unique_ptr<Tree[]> m_trees; // mmap, mclr, full, type

    // Palette of the current frame, built by applying the palette chunks of
    // every stored frame up to it (only a few files have more than one).
    std::vector<std::uint32_t> m_paletteFrames; // stored frames with a palette chunk
    std::size_t m_paletteApplied = 0;           // leading m_paletteFrames applied to m_palette
    std::array<std::uint8_t, 768> m_palette{};

    std::uint32_t m_frame = 0;   // logical frame (0..frames-1)
    std::uint32_t m_stored = 0;  // stored frame to show (the ring frame shows as frame 0)
    std::int64_t m_decoded = -1; // stored frame the target holds, -1 = unknown

    std::uint8_t* m_target = nullptr;
    std::size_t m_pitch = 0;
    std::vector<std::uint8_t> m_ownBuffer;

    SmackerRect m_dirty;
    std::uint8_t m_trackMask = 0;
    std::array<std::vector<std::int16_t>, kSmackerMaxTracks> m_audio;
    std::vector<std::uint8_t> m_frameData; // scratch
};

// Decodes one Smacker palette chunk (without its leading size byte) on top
// of `palette`, which holds the previous frame's palette. Entries are 8-bit.
bool decodeSmackerPalette(std::span<const std::uint8_t> chunk, std::array<std::uint8_t, 768>& palette,
                          std::string* error = nullptr);

// Decodes one audio chunk (without its leading length) of `track` and appends
// interleaved int16 samples to `out`. 8-bit sources become (x - 128) << 8.
bool decodeSmackerAudio(std::span<const std::uint8_t> chunk, const SmackerAudioTrack& track,
                        std::vector<std::int16_t>& out, std::string* error = nullptr);

} // namespace brc::formats
