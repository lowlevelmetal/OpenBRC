// Smacker decoder. Ported from the maintainer's C decoder in BarbieRecomp
// (runtime/hle/smacker.c); the SMK4 full-block modes, Y scaling, ring frames,
// raw PCM and multi-track audio follow the format as other open decoders
// (FFmpeg, libsmacker) describe it.
#include "formats/Smacker.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <limits>

namespace brc::formats {
namespace {

constexpr std::size_t kHeaderSize = 0x68;
constexpr std::uint32_t kMaxDimension = 0x4000;
constexpr std::uint32_t kMaxAudioChunk = 1u << 24; // decompressed bytes
constexpr int kSmallTreeMaxDepth = 32;
constexpr int kBigTreeMaxDepth = 512;
constexpr std::uint32_t kNode = 0x80000000u;
constexpr int kPeekBits = 10;

// Frame type bits.
constexpr std::uint8_t kFramePalette = 0x01;

// Audio rate bits.
constexpr std::uint32_t kAudioCompressed = 0x80000000u;
constexpr std::uint32_t kAudioPresent = 0x40000000u;
constexpr std::uint32_t kAudio16Bit = 0x20000000u;
constexpr std::uint32_t kAudioStereo = 0x10000000u;
constexpr std::uint32_t kAudioBink = 0x08000000u;
constexpr std::uint32_t kAudioRateMask = 0x00ffffffu;

// Run lengths of the block type codes.
constexpr std::array<std::uint16_t, 64> kBlockRuns = {
    1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,  17,  18,  19,   20,  21, 22,
    23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,  39,  40,  41,   42,  43, 44,
    45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 128, 256, 512, 1024, 2048};

// 6-bit palette components to 8 bits (smackw32 table at 0x100142e0).
constexpr std::array<std::uint8_t, 64> kPalette6to8 = {
    0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C,
    0x41, 0x45, 0x49, 0x4D, 0x51, 0x55, 0x59, 0x5D, 0x61, 0x65, 0x69, 0x6D, 0x71, 0x75, 0x79, 0x7D,
    0x82, 0x86, 0x8A, 0x8E, 0x92, 0x96, 0x9A, 0x9E, 0xA2, 0xA6, 0xAA, 0xAE, 0xB2, 0xB6, 0xBA, 0xBE,
    0xC3, 0xC7, 0xCB, 0xCF, 0xD3, 0xD7, 0xDB, 0xDF, 0xE3, 0xE7, 0xEB, 0xEF, 0xF3, 0xF7, 0xFB, 0xFF};

std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

void setError(std::string* error, std::string msg) {
    if (error)
        *error = std::move(msg);
}

// LSB-first bit reader. Reads past the end return zero bits and are reported
// by overrun().
class BitReader {
public:
    explicit BitReader(std::span<const std::uint8_t> data)
        : m_data(data.data()), m_size(data.size()), m_bits(static_cast<std::uint64_t>(data.size()) * 8) {}

    unsigned bit() {
        if (m_pos >= m_bits) {
            ++m_pos;
            return 0;
        }
        const unsigned v = (m_data[m_pos >> 3] >> (m_pos & 7)) & 1u;
        ++m_pos;
        return v;
    }

    // Next `n` (<= 32) bits without consuming them.
    std::uint32_t peek(int n) const {
        const std::uint64_t byte = m_pos >> 3;
        std::uint64_t w = 0;
        if (byte + 8 <= m_size) {
            w = loadLE<std::uint64_t>(reinterpret_cast<const std::byte*>(m_data + byte));
        } else {
            for (std::uint64_t i = 0; i < 8 && byte + i < m_size; ++i)
                w |= static_cast<std::uint64_t>(m_data[byte + i]) << (8 * i);
        }
        w >>= (m_pos & 7);
        return static_cast<std::uint32_t>(w & ((std::uint64_t{1} << n) - 1));
    }

    void skip(unsigned n) { m_pos += n; }

    std::uint32_t bits(int n) {
        const std::uint32_t v = peek(n);
        skip(static_cast<unsigned>(n));
        return v;
    }

    bool overrun() const { return m_pos > m_bits; }
    std::uint64_t remaining() const { return m_pos >= m_bits ? 0 : m_bits - m_pos; }

private:
    const std::uint8_t* m_data;
    std::uint64_t m_size;
    std::uint64_t m_bits;
    std::uint64_t m_pos = 0;
};

// Huffman tree of 8-bit values, as a flat array: an internal node is
// kNode | (entries in its left subtree), followed by the left subtree, then
// the right one. An absent tree decodes as 0 without reading bits.
class SmallTree {
public:
    bool read(BitReader& br) {
        m_n = 0;
        if (!br.bit())
            return true;
        if (!readNode(br, 0))
            return false;
        br.bit(); // terminating 0
        return !br.overrun();
    }

    std::uint32_t get(BitReader& br) const {
        if (m_n == 0)
            return 0;
        std::uint32_t i = 0;
        while (m_v[i] & kNode)
            i = br.bit() ? i + 1 + (m_v[i] & ~kNode) : i + 1;
        return m_v[i];
    }

private:
    bool readNode(BitReader& br, int depth) {
        if (m_n >= m_v.size() || depth > kSmallTreeMaxDepth || br.overrun())
            return false;
        if (!br.bit()) {
            m_v[m_n++] = br.bits(8);
            return true;
        }
        const std::uint32_t idx = m_n++;
        if (!readNode(br, depth + 1))
            return false;
        m_v[idx] = kNode | (m_n - idx - 1);
        return readNode(br, depth + 1);
    }

    std::array<std::uint32_t, 511> m_v{};
    std::uint32_t m_n = 0;
};

} // namespace

// Huffman tree of 16-bit values whose leaves are coded with two 8-bit trees.
// Three escape values mark leaves that act as a most-recently-used cache:
// they decode to the last three distinct values and are cleared every frame.
struct Smacker::Tree {
    struct Peek {
        std::uint32_t index = 0;
        std::uint8_t length = 0;
    };

    std::vector<std::uint32_t> v;
    std::array<std::uint32_t, 3> last{};
    std::vector<Peek> peek;

    bool read(BitReader& br, std::uint32_t allocSize, std::string* error) {
        v.clear();
        if (!br.bit()) {
            v.assign(4, 0);
            last = {1, 1, 1};
            buildPeek();
            return true;
        }
        SmallTree lo, hi;
        if (!lo.read(br) || !hi.read(br)) {
            setError(error, "bad byte tree");
            return false;
        }
        for (auto& e : m_escape)
            e = br.bits(16);
        constexpr std::uint32_t kUnset = std::numeric_limits<std::uint32_t>::max();
        last = {kUnset, kUnset, kUnset};
        // Each entry costs at least one bit, so the remaining tree data bounds
        // the size as well as the header's allocation size does.
        m_cap =
            std::min<std::uint64_t>((static_cast<std::uint64_t>(allocSize) + 3) / 4 + 8, br.remaining() + 4);
        m_lo = &lo;
        m_hi = &hi;
        const bool ok = readNode(br, 0);
        m_lo = m_hi = nullptr;
        if (!ok) {
            setError(error, br.overrun() ? "tree data ends early" : "tree too large or too deep");
            return false;
        }
        br.bit(); // terminating 0
        for (auto& l : last) {
            if (l == kUnset) {
                l = static_cast<std::uint32_t>(v.size());
                v.push_back(0);
            }
        }
        buildPeek();
        return true;
    }

    void reset() { v[last[0]] = v[last[1]] = v[last[2]] = 0; }

    std::uint32_t get(BitReader& br) {
        const Peek& p = peek[br.peek(kPeekBits)];
        br.skip(p.length);
        std::uint32_t i = p.index;
        while (v[i] & kNode)
            i = br.bit() ? i + 1 + (v[i] & ~kNode) : i + 1;
        const std::uint32_t value = v[i];
        if (value != v[last[0]]) {
            v[last[2]] = v[last[1]];
            v[last[1]] = v[last[0]];
            v[last[0]] = value;
        }
        return value;
    }

private:
    bool readNode(BitReader& br, int depth) {
        if (v.size() >= m_cap || depth > kBigTreeMaxDepth || br.overrun())
            return false;
        const auto idx = static_cast<std::uint32_t>(v.size());
        if (!br.bit()) {
            std::uint32_t value = m_lo->get(br) | m_hi->get(br) << 8;
            for (int k = 0; k < 3; ++k) {
                if (value == m_escape[k]) {
                    last[k] = idx;
                    value = 0;
                    break;
                }
            }
            v.push_back(value);
            return true;
        }
        v.push_back(0);
        if (!readNode(br, depth + 1))
            return false;
        v[idx] = kNode | static_cast<std::uint32_t>(v.size() - idx - 1);
        return readNode(br, depth + 1);
    }

    // For every combination of the next kPeekBits bits: the leaf they reach
    // and how many bits that takes, or the node reached after all of them.
    void buildPeek() {
        peek.assign(std::size_t{1} << kPeekBits, Peek{});
        for (std::uint32_t bits = 0; bits < peek.size(); ++bits) {
            std::uint32_t i = 0;
            std::uint8_t n = 0;
            while ((v[i] & kNode) && n < kPeekBits) {
                i = ((bits >> n) & 1u) ? i + 1 + (v[i] & ~kNode) : i + 1;
                ++n;
            }
            peek[bits] = {i, n};
        }
    }

    std::array<std::uint32_t, 3> m_escape{};
    std::uint64_t m_cap = 0;
    const SmallTree* m_lo = nullptr;
    const SmallTree* m_hi = nullptr;
};

namespace {
enum TreeIndex { kTreeMMap, kTreeMClr, kTreeFull, kTreeType };
} // namespace

Smacker::Smacker() = default;
Smacker::Smacker(Smacker&&) noexcept = default;
Smacker& Smacker::operator=(Smacker&&) noexcept = default;
Smacker::~Smacker() = default;

std::expected<Smacker, std::string> Smacker::open(std::span<const std::byte> data) {
    return open(std::make_shared<MemoryFile>(std::vector<std::byte>(data.begin(), data.end())));
}

std::expected<Smacker, std::string> Smacker::open(std::shared_ptr<const RandomAccessFile> file) {
    if (!file)
        return std::unexpected("no file");
    const std::uint64_t fileSize = file->size();
    std::array<std::uint8_t, kHeaderSize> h{};
    if (fileSize < kHeaderSize || !file->readExact(0, std::as_writable_bytes(std::span(h))))
        return std::unexpected("too short for a Smacker header");

    Smacker s;
    s.m_file = std::move(file);
    if (std::memcmp(h.data(), "SMK2", 4) == 0)
        s.m_version = 2;
    else if (std::memcmp(h.data(), "SMK4", 4) == 0)
        s.m_version = 4;
    else
        return std::unexpected("not a Smacker file (no SMK2/SMK4 signature)");

    s.m_width = le32(&h[4]);
    s.m_height = le32(&h[8]);
    s.m_frames = le32(&h[12]);
    s.m_rawRate = static_cast<std::int32_t>(le32(&h[16]));
    s.m_flags = le32(&h[20]);
    if (s.m_width == 0 || s.m_height == 0 || s.m_width > kMaxDimension || s.m_height > kMaxDimension)
        return std::unexpected(std::format("bad dimensions {}x{}", s.m_width, s.m_height));
    if (s.m_frames == 0)
        return std::unexpected("no frames");
    s.m_ring = (s.m_flags & 1) != 0;
    if (s.m_flags & 4)
        s.m_yScale = SmackerYScale::Doubled;
    else if (s.m_flags & 2)
        s.m_yScale = SmackerYScale::Interlaced;

    for (int t = 0; t < kSmackerMaxTracks; ++t) {
        const std::uint32_t rate = le32(&h[0x48 + 4 * t]);
        auto& track = s.m_tracks[static_cast<std::size_t>(t)];
        track.present = (rate & kAudioPresent) != 0;
        track.compressed = (rate & kAudioCompressed) != 0;
        track.bink = (rate & kAudioBink) != 0;
        track.is16Bit = (rate & kAudio16Bit) != 0;
        track.stereo = (rate & kAudioStereo) != 0;
        track.sampleRate = rate & kAudioRateMask;
        track.maxChunkSize = le32(&h[0x18 + 4 * t]);
        if (track.decodable())
            s.m_trackMask |= static_cast<std::uint8_t>(1u << t);
    }

    const std::uint32_t treesSize = le32(&h[0x34]);
    const std::uint64_t stored = s.storedFrames();
    const std::uint64_t tablesEnd = kHeaderSize + stored * 5;
    if (tablesEnd > fileSize)
        return std::unexpected(std::format("frame tables ({} frames) run past the end of the data", stored));
    if (tablesEnd + treesSize > fileSize)
        return std::unexpected("Huffman trees run past the end of the data");

    std::vector<std::uint8_t> tables(static_cast<std::size_t>(stored * 5));
    if (!s.m_file->readExact(kHeaderSize, std::as_writable_bytes(std::span(tables))))
        return std::unexpected("cannot read the frame tables");
    s.m_frameSize.resize(stored);
    s.m_frameType.assign(tables.begin() + static_cast<std::ptrdiff_t>(stored * 4), tables.end());
    for (std::size_t i = 0; i < stored; ++i)
        s.m_frameSize[i] = le32(&tables[i * 4]);

    std::vector<std::uint8_t> treeData(treesSize);
    if (!s.m_file->readExact(tablesEnd, std::as_writable_bytes(std::span(treeData))))
        return std::unexpected("cannot read the Huffman trees");
    BitReader br(treeData);
    s.m_trees = std::make_unique<Tree[]>(4);
    static constexpr const char* kTreeNames[4] = {"mono map", "mono colour", "full block", "block type"};
    for (int t = 0; t < 4; ++t) {
        std::string err;
        if (!s.m_trees[static_cast<std::size_t>(t)].read(br, le32(&h[0x38 + 4 * t]), &err))
            return std::unexpected(std::format("{} tree: {}", kTreeNames[t], err));
    }
    if (br.overrun())
        return std::unexpected("Huffman trees run past their declared size");

    std::uint64_t offset = tablesEnd + treesSize;
    s.m_frameOffset.resize(stored);
    for (std::size_t i = 0; i < stored; ++i) {
        s.m_frameOffset[i] = offset;
        offset += s.m_frameSize[i] & ~3u;
    }
    s.m_byteSize = offset;

    for (std::uint32_t f = 0; f < stored; ++f)
        if (s.m_frameType[f] & kFramePalette)
            s.m_paletteFrames.push_back(f);
    s.loadPalette();
    return s;
}

void Smacker::loadPalette() {
    const auto target = static_cast<std::size_t>(
        std::upper_bound(m_paletteFrames.begin(), m_paletteFrames.end(), m_stored) - m_paletteFrames.begin());
    if (target < m_paletteApplied) {
        m_palette.fill(0);
        m_paletteApplied = 0;
    }
    std::vector<std::uint8_t> chunk;
    for (; m_paletteApplied < target; ++m_paletteApplied) {
        const std::uint32_t f = m_paletteFrames[m_paletteApplied];
        const std::uint64_t frameSize = m_frameSize[f] & ~3u;
        std::uint8_t lenByte = 0;
        if (frameSize < 1 ||
            !m_file->readExact(m_frameOffset[f], std::as_writable_bytes(std::span(&lenByte, 1))))
            continue; // reported when the frame is decoded
        const std::size_t len = std::size_t{lenByte} * 4;
        if (len < 1 || len > frameSize)
            continue;
        chunk.resize(len - 1);
        if (!m_file->readExact(m_frameOffset[f] + 1, std::as_writable_bytes(std::span(chunk))))
            continue;
        // Errors are reported again by decodeFrame(); keep what was decoded.
        decodeSmackerPalette(chunk, m_palette);
    }
}

std::uint32_t Smacker::height() const {
    return m_yScale == SmackerYScale::None ? m_height : m_height * 2;
}

std::uint64_t Smacker::frameDurationUs() const {
    if (m_rawRate > 0)
        return static_cast<std::uint64_t>(m_rawRate) * 1000;
    if (m_rawRate < 0)
        return static_cast<std::uint64_t>(-static_cast<std::int64_t>(m_rawRate)) * 10;
    return 100000;
}

double Smacker::framesPerSecond() const {
    return 1e6 / static_cast<double>(frameDurationUs());
}

const SmackerAudioTrack& Smacker::audioTrack(int track) const {
    static const SmackerAudioTrack kNone{};
    if (track < 0 || track >= kSmackerMaxTracks)
        return kNone;
    return m_tracks[static_cast<std::size_t>(track)];
}

bool Smacker::isKeyframe(std::uint32_t frame) const {
    return frame < m_frameSize.size() && (m_frameSize[frame] & 1);
}

bool Smacker::paletteChanged() const {
    return (m_frameType[m_stored] & kFramePalette) != 0;
}

void Smacker::nextFrame() {
    if (m_frame + 1 < m_frames) {
        ++m_frame;
        m_stored = m_frame;
        loadPalette();
        return;
    }
    m_frame = 0;
    if (m_frames == 1) {
        m_stored = 0;
        m_decoded = -1; // a one-frame loop still plays the frame (and its audio) again
        loadPalette();
        return;
    }
    // The ring frame is a delta from the last frame; use it only when the
    // target will hold that frame.
    const bool lastShown = m_decoded == static_cast<std::int64_t>(m_frames - 1) && m_stored == m_frames - 1;
    m_stored = m_ring && lastShown ? m_frames : 0;
    loadPalette();
}

void Smacker::gotoFrame(std::uint32_t frame) {
    m_frame = std::min(frame, m_frames - 1);
    m_stored = m_frame;
    loadPalette();
}

bool Smacker::setTarget(std::span<std::uint8_t> buffer, std::size_t pitch, std::uint32_t x, std::uint32_t y) {
    const std::uint64_t w = m_width, hgt = height();
    if (pitch < w || x > pitch - w)
        return false;
    const std::uint64_t need = (static_cast<std::uint64_t>(y) + hgt - 1) * pitch + x + w;
    if (need > buffer.size())
        return false;
    m_target = buffer.data() + static_cast<std::size_t>(y) * pitch + x;
    m_pitch = pitch;
    return true;
}

void Smacker::useOwnBuffer() {
    m_target = nullptr;
    if (m_ownBuffer.empty())
        m_ownBuffer.assign(static_cast<std::size_t>(m_width) * height(), 0);
}

std::span<const std::int16_t> Smacker::audio(int track) const {
    if (track < 0 || track >= kSmackerMaxTracks)
        return {};
    return m_audio[static_cast<std::size_t>(track)];
}

bool Smacker::readFrame(std::uint32_t stored, std::vector<std::uint8_t>& out, std::string* error) const {
    const std::uint64_t size = m_frameSize[stored] & ~3u;
    const std::uint64_t offset = m_frameOffset[stored];
    if (offset + size > m_file->size()) {
        setError(error, std::format("frame {} lies past the end of the data (truncated file)", stored));
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    if (!m_file->readExact(offset, std::as_writable_bytes(std::span(out)))) {
        setError(error, std::format("cannot read frame {}", stored));
        return false;
    }
    return true;
}

std::expected<SmackerFrameInfo, std::string> Smacker::decodeFrame() {
    for (auto& a : m_audio)
        a.clear();
    m_dirty = {};
    SmackerFrameInfo info;
    info.frame = m_frame;
    info.paletteChanged = paletteChanged();
    if (!m_target && m_ownBuffer.empty())
        useOwnBuffer();

    // Logical frame the target holds (the ring frame shows frame 0), or -1.
    const std::uint32_t ring = m_frames; // stored index of the ring frame
    const std::int64_t held = m_decoded >= 0 && m_ring && m_decoded == ring ? 0 : m_decoded;
    if (held >= 0 && held == m_frame)
        return info;

    const std::uint32_t s = m_stored;
    std::uint32_t start;
    bool catchUp = false;
    if (held >= 0 && held + 1 == s) {
        start = s; // the next frame (or the ring frame after the last one)
        catchUp = true;
    } else {
        std::uint32_t key = s;
        while (key > 0 && !(m_frameSize[key] & 1))
            --key;
        if (held >= key && held < s) {
            start = static_cast<std::uint32_t>(held) + 1;
            catchUp = true;
        } else {
            start = key;
        }
    }

    std::string error;
    for (std::uint32_t f = start;; ++f) {
        // Audio of frames skipped by a catch-up is kept (so audio has no gaps
        // when the caller drops frames); after a seek only the shown frame's.
        const bool withAudio = catchUp || f == s;
        const bool ok = decodeStored(f, withAudio, &error);
        ++info.decoded;
        m_decoded = f;
        if (!ok) {
            info.dirty = m_dirty;
            return std::unexpected(std::format("frame {}: {}", f, error));
        }
        if (f == s)
            break;
    }
    info.dirty = m_dirty;
    return info;
}

bool Smacker::decodeStored(std::uint32_t stored, bool withAudio, std::string* error) {
    if (!readFrame(stored, m_frameData, error))
        return false;
    const std::span<const std::uint8_t> data = m_frameData;
    const std::uint8_t type = m_frameType[stored];
    std::size_t o = 0;
    std::string later; // problems that do not stop the video from being decoded
    if (type & kFramePalette) {
        const std::size_t len = data.empty() ? 0 : std::size_t{data[0]} * 4;
        if (len == 0 || len > data.size()) {
            setError(error, "bad palette chunk size");
            return false;
        }
        // The palette itself was applied when the frame was loaded; check it here.
        std::array<std::uint8_t, 768> scratch{};
        std::string err;
        if (!decodeSmackerPalette(data.subspan(1, len - 1), scratch, &err))
            later = "palette: " + err;
        o = len;
    }
    for (int t = 0; t < kSmackerMaxTracks; ++t) {
        if (!(type & (2u << t)))
            continue;
        if (o + 4 > data.size()) {
            setError(error, std::format("audio track {} chunk header past the frame end", t));
            return false;
        }
        const std::uint32_t len = le32(&data[o]);
        if (len < 4 || len > data.size() - o) {
            setError(error, std::format("bad audio track {} chunk size {}", t, len));
            return false;
        }
        const auto& track = m_tracks[static_cast<std::size_t>(t)];
        if (withAudio && (m_trackMask & (1u << t)) && track.decodable()) {
            std::string err;
            if (!decodeSmackerAudio(data.subspan(o + 4, len - 4), track, m_audio[static_cast<std::size_t>(t)],
                                    &err) &&
                later.empty())
                later = std::format("audio track {}: {}", t, err);
        }
        o += len;
    }
    if (!decodeVideo(data.subspan(o), error))
        return false;
    if (!later.empty()) {
        setError(error, std::move(later));
        return false;
    }
    return true;
}

bool Smacker::decodeVideo(std::span<const std::uint8_t> data, std::string* error) {
    BitReader br(data);
    Tree& mmap = m_trees[kTreeMMap];
    Tree& mclr = m_trees[kTreeMClr];
    Tree& full = m_trees[kTreeFull];
    Tree& types = m_trees[kTreeType];
    mmap.reset();
    mclr.reset();
    full.reset();
    types.reset();

    std::uint8_t* const base = m_target ? m_target : m_ownBuffer.data();
    const std::size_t pitch = m_target ? m_pitch : m_width;
    const std::size_t rowScale = m_yScale == SmackerYScale::None ? 1 : 2;
    const bool doubled = m_yScale == SmackerYScale::Doubled;
    const std::size_t step = pitch * rowScale; // between stored rows
    const std::uint32_t bw = m_width / 4, bh = m_height / 4;
    const std::uint32_t blocks = bw * bh;

    std::uint32_t x0 = std::numeric_limits<std::uint32_t>::max(), y0 = x0, x1 = 0, y1 = 0;
    bool any = false;
    auto finishRow = [&](std::uint8_t* row) {
        if (doubled)
            std::memcpy(row + pitch, row, 4);
    };
    auto put16 = [](std::uint8_t* p, std::uint32_t v) {
        p[0] = static_cast<std::uint8_t>(v);
        p[1] = static_cast<std::uint8_t>(v >> 8);
    };

    std::uint32_t blk = 0;
    bool ok = true;
    while (blk < blocks) {
        const std::uint32_t type = types.get(br);
        std::uint32_t run = kBlockRuns[(type >> 2) & 0x3f];
        const std::uint32_t kind = type & 3;
        int mode = 0;
        if (kind == 1 && m_version == 4) {
            // SMK4 full blocks: 0 = 16 pixels, 1 = 2x2 pixel doubled, 2 = rows doubled.
            if (br.bit())
                mode = 1;
            else if (br.bit())
                mode = 2;
        }
        if (kind == 2) { // void: the previous frame shows through
            blk = std::min(blocks, blk + run);
            if (br.overrun()) {
                ok = false;
                break;
            }
            continue;
        }
        for (; run > 0 && blk < blocks; --run, ++blk) {
            const std::uint32_t bx = blk % bw, by = blk / bw;
            x0 = std::min(x0, bx);
            x1 = std::max(x1, bx);
            y0 = std::min(y0, by);
            y1 = std::max(y1, by);
            any = true;
            std::uint8_t* out = base + by * 4 * step + std::size_t{bx} * 4;
            switch (kind) {
            case 0: { // mono: two colours and a 16-bit map
                const std::uint32_t clr = mclr.get(br);
                std::uint32_t map = mmap.get(br);
                const auto hi = static_cast<std::uint8_t>(clr >> 8), lo = static_cast<std::uint8_t>(clr);
                for (int i = 0; i < 4; ++i) {
                    out[0] = (map & 1) ? hi : lo;
                    out[1] = (map & 2) ? hi : lo;
                    out[2] = (map & 4) ? hi : lo;
                    out[3] = (map & 8) ? hi : lo;
                    finishRow(out);
                    map >>= 4;
                    out += step;
                }
                break;
            }
            case 1: // full
                if (mode == 0) {
                    for (int i = 0; i < 4; ++i) {
                        put16(out + 2, full.get(br));
                        put16(out, full.get(br));
                        finishRow(out);
                        out += step;
                    }
                } else if (mode == 1) {
                    for (int half = 0; half < 2; ++half) {
                        const std::uint32_t p = full.get(br);
                        for (int i = 0; i < 2; ++i) {
                            out[0] = out[1] = static_cast<std::uint8_t>(p);
                            out[2] = out[3] = static_cast<std::uint8_t>(p >> 8);
                            finishRow(out);
                            out += step;
                        }
                    }
                } else {
                    for (int half = 0; half < 2; ++half) {
                        const std::uint32_t right = full.get(br);
                        const std::uint32_t left = full.get(br);
                        for (int i = 0; i < 2; ++i) {
                            put16(out, left);
                            put16(out + 2, right);
                            finishRow(out);
                            out += step;
                        }
                    }
                }
                break;
            default: { // solid
                const auto c = static_cast<std::uint8_t>(type >> 8);
                for (int i = 0; i < 4; ++i) {
                    std::memset(out, c, 4);
                    finishRow(out);
                    out += step;
                }
                break;
            }
            }
        }
        if (br.overrun()) {
            ok = false;
            break;
        }
    }

    if (any) {
        const int scale = static_cast<int>(rowScale);
        const SmackerRect r{static_cast<int>(x0) * 4, static_cast<int>(y0) * 4 * scale,
                            static_cast<int>(x1 - x0 + 1) * 4, static_cast<int>(y1 - y0 + 1) * 4 * scale};
        if (m_dirty.empty()) {
            m_dirty = r;
        } else {
            const int rx1 = std::max(m_dirty.x + m_dirty.w, r.x + r.w);
            const int ry1 = std::max(m_dirty.y + m_dirty.h, r.y + r.h);
            m_dirty.x = std::min(m_dirty.x, r.x);
            m_dirty.y = std::min(m_dirty.y, r.y);
            m_dirty.w = rx1 - m_dirty.x;
            m_dirty.h = ry1 - m_dirty.y;
        }
    }
    if (!ok)
        setError(error, "video bitstream ends early");
    return ok;
}

bool decodeSmackerPalette(std::span<const std::uint8_t> chunk, std::array<std::uint8_t, 768>& palette,
                          std::string* error) {
    const std::array<std::uint8_t, 768> old = palette;
    std::size_t i = 0;
    std::uint32_t entry = 0;
    while (entry < 256) {
        if (i >= chunk.size()) {
            setError(error, std::format("palette chunk ends at entry {}", entry));
            return false;
        }
        const std::uint8_t t = chunk[i++];
        if (t & 0x80) { // skip: keep entries
            entry += (t & 0x7fu) + 1;
        } else if (t & 0x40) { // copy from the previous palette
            if (i >= chunk.size()) {
                setError(error, "palette copy command ends early");
                return false;
            }
            std::uint32_t src = chunk[i++];
            std::uint32_t n = (t & 0x3fu) + 1;
            if (src + n > 256) {
                setError(error, std::format("palette copy {}+{} past entry 255", src, n));
                return false;
            }
            for (; n > 0 && entry < 256; --n, ++src, ++entry)
                std::memcpy(&palette[entry * 3], &old[src * 3], 3);
        } else { // new entry, 6 bits per component
            if (i + 2 > chunk.size()) {
                setError(error, "palette entry ends early");
                return false;
            }
            palette[entry * 3] = kPalette6to8[t & 0x3f];
            palette[entry * 3 + 1] = kPalette6to8[chunk[i++] & 0x3f];
            palette[entry * 3 + 2] = kPalette6to8[chunk[i++] & 0x3f];
            ++entry;
        }
    }
    return true;
}

bool decodeSmackerAudio(std::span<const std::uint8_t> chunk, const SmackerAudioTrack& track,
                        std::vector<std::int16_t>& out, std::string* error) {
    if (track.bink) {
        setError(error, "Bink audio is not supported");
        return false;
    }
    const std::size_t channels = track.stereo ? 2 : 1;
    if (!track.compressed) {
        const std::size_t width = track.is16Bit ? 2 : 1;
        const std::size_t count = chunk.size() / (width * channels) * channels;
        const std::size_t base = out.size();
        out.resize(base + count);
        for (std::size_t i = 0; i < count; ++i) {
            out[base + i] = track.is16Bit
                                ? static_cast<std::int16_t>(chunk[i * 2] | chunk[i * 2 + 1] << 8)
                                : static_cast<std::int16_t>((static_cast<int>(chunk[i]) - 128) * 256);
        }
        return true;
    }

    if (chunk.size() < 4) {
        setError(error, "compressed audio chunk too short");
        return false;
    }
    const std::uint32_t unpacked = le32(chunk.data());
    if (unpacked > kMaxAudioChunk) {
        setError(error, std::format("decompressed size {} too large", unpacked));
        return false;
    }
    BitReader br(chunk.subspan(4));
    if (!br.bit())
        return true; // no sound in this frame
    const bool stereo = br.bit() != 0;
    const bool is16 = br.bit() != 0;
    if (stereo != track.stereo || is16 != track.is16Bit) {
        setError(error, "chunk format differs from the header");
        return false;
    }
    std::array<SmallTree, 4> trees;
    const int ntrees = 1 << ((is16 ? 1 : 0) + (stereo ? 1 : 0));
    for (int i = 0; i < ntrees; ++i) {
        if (!trees[static_cast<std::size_t>(i)].read(br)) {
            setError(error, "bad audio Huffman tree");
            return false;
        }
    }
    const std::size_t samples = (is16 ? unpacked / 2 : unpacked) / channels * channels;
    const std::size_t base = out.size();
    out.resize(base + samples);
    std::int16_t* pcm = out.data() + base;
    const std::size_t st = stereo ? 1 : 0;

    // DPCM with wrap-around (no clipping), as the encoder expects.
    if (is16) {
        std::array<std::int16_t, 2> pred{};
        for (std::size_t c = channels; c-- > 0;) {
            const std::uint32_t v = br.bits(16);
            pred[c] = static_cast<std::int16_t>(static_cast<std::uint16_t>((v & 0xff) << 8 | v >> 8));
        }
        std::size_t i = 0;
        for (; i < channels && i < samples; ++i)
            pcm[i] = pred[i];
        for (; i < samples; ++i) {
            const std::size_t c = i & st;
            const std::uint32_t lo = trees[c * 2].get(br), hi = trees[c * 2 + 1].get(br);
            const auto delta = static_cast<std::int16_t>(static_cast<std::uint16_t>(lo | hi << 8));
            pred[c] = static_cast<std::int16_t>(static_cast<std::uint16_t>(pred[c] + delta));
            pcm[i] = pred[c];
        }
    } else {
        std::array<std::uint8_t, 2> pred{};
        for (std::size_t c = channels; c-- > 0;)
            pred[c] = static_cast<std::uint8_t>(br.bits(8));
        std::size_t i = 0;
        for (; i < channels && i < samples; ++i)
            pcm[i] = static_cast<std::int16_t>((pred[i] - 128) * 256);
        for (; i < samples; ++i) {
            const std::size_t c = i & st;
            pred[c] = static_cast<std::uint8_t>(pred[c] + static_cast<std::int8_t>(trees[c].get(br)));
            pcm[i] = static_cast<std::int16_t>((pred[c] - 128) * 256);
        }
    }
    if (br.overrun()) {
        setError(error, "audio bitstream ends early");
        return false;
    }
    return true;
}

} // namespace brc::formats
