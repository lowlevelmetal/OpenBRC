// RIFF WAVE parsing. The IMA ADPCM decoder is ported from the maintainer's
// BarbieRecomp runtime (runtime/audio.c, audio_decode_wav).
#include "formats/Wave.h"

#include "core/File.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>

namespace brc::formats {
namespace {

constexpr std::uint16_t kFormatPcm = 1;
constexpr std::uint16_t kFormatImaAdpcm = 0x11;
constexpr std::uint16_t kFormatExtensible = 0xfffe;

constexpr std::array<int, 89> kImaStep = {
    7,    8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,   28,
    31,   34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,  118,
    130,  143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,  494,
    544,  598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878, 2066,
    2272, 2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845, 8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
constexpr std::array<int, 16> kImaIndex = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

std::uint32_t u32(std::span<const std::byte> d, std::size_t o) {
    return loadLE<std::uint32_t>(d.data() + o);
}
std::uint16_t u16(std::span<const std::byte> d, std::size_t o) {
    return loadLE<std::uint16_t>(d.data() + o);
}

bool fourcc(std::span<const std::byte> d, std::size_t o, const char (&tag)[5]) {
    return std::memcmp(d.data() + o, tag, 4) == 0;
}

void decodePcm(std::span<const std::byte> data, int bits, int channels, std::vector<std::int16_t>& out) {
    const std::size_t width = static_cast<std::size_t>(bits / 8);
    const std::size_t frameBytes = width * static_cast<std::size_t>(channels);
    const std::size_t count = data.size() / frameBytes * static_cast<std::size_t>(channels);
    out.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::byte* p = data.data() + i * width;
        if (width == 1) {
            out[i] = static_cast<std::int16_t>((std::to_integer<int>(*p) - 128) * 256);
        } else {
            // Little-endian: the top 16 bits are the last two bytes.
            out[i] = static_cast<std::int16_t>(std::to_integer<std::uint16_t>(p[width - 2]) |
                                               std::to_integer<std::uint16_t>(p[width - 1]) << 8);
        }
    }
}

// IMA ADPCM, Microsoft/Intel layout: per block and channel a 4-byte header
// (initial sample, step index), then groups of 4 bytes (8 nibbles) per
// channel, interleaved.
bool decodeIma(std::span<const std::byte> data, int channels, std::size_t blockAlign,
               std::vector<std::int16_t>& out, std::string* error) {
    const std::size_t ch = static_cast<std::size_t>(channels);
    if (blockAlign < 4 * ch) {
        if (error)
            *error = std::format("IMA ADPCM block size {} too small", blockAlign);
        return false;
    }
    const std::size_t perBlock = (blockAlign - 4 * ch) * 2 / ch + 1;
    const std::size_t blocks = (data.size() + blockAlign - 1) / blockAlign;
    out.clear();
    out.reserve(blocks * perBlock * ch);
    for (std::size_t b = 0; b < blocks; ++b) {
        const std::size_t start = b * blockAlign;
        const std::size_t avail = std::min(blockAlign, data.size() - start);
        if (avail < 4 * ch)
            break;
        const std::byte* p = data.data() + start;
        std::array<int, 2> pred{}, index{};
        for (std::size_t c = 0; c < ch; ++c) {
            pred[c] = static_cast<std::int16_t>(loadLE<std::uint16_t>(p + c * 4));
            index[c] = std::min(std::to_integer<int>(p[c * 4 + 2]), 88);
            out.push_back(static_cast<std::int16_t>(pred[c]));
        }
        std::size_t q = 4 * ch;
        while (q + 4 * ch <= avail) {
            std::array<std::array<std::int16_t, 8>, 2> tmp{};
            for (std::size_t c = 0; c < ch; ++c) {
                for (std::size_t k = 0; k < 8; ++k) {
                    const int nibble = (std::to_integer<int>(p[q + c * 4 + k / 2]) >> ((k & 1) * 4)) & 15;
                    const int step = kImaStep[static_cast<std::size_t>(index[c])];
                    int diff = step >> 3;
                    if (nibble & 1)
                        diff += step >> 2;
                    if (nibble & 2)
                        diff += step >> 1;
                    if (nibble & 4)
                        diff += step;
                    if (nibble & 8)
                        diff = -diff;
                    pred[c] = std::clamp(pred[c] + diff, -32768, 32767);
                    index[c] = std::clamp(index[c] + kImaIndex[static_cast<std::size_t>(nibble)], 0, 88);
                    tmp[c][k] = static_cast<std::int16_t>(pred[c]);
                }
            }
            for (std::size_t k = 0; k < 8; ++k)
                for (std::size_t c = 0; c < ch; ++c)
                    out.push_back(tmp[c][k]);
            q += 4 * ch;
        }
    }
    return true;
}

} // namespace

std::expected<WaveSound, std::string> parseWave(std::span<const std::byte> data) {
    if (data.size() < 12 || !fourcc(data, 0, "RIFF") || !fourcc(data, 8, "WAVE"))
        return std::unexpected("not a RIFF WAVE file");
    const std::uint64_t riffEnd = std::uint64_t{u32(data, 4)} + 8;
    const std::size_t end = static_cast<std::size_t>(std::min<std::uint64_t>(riffEnd, data.size()));

    std::span<const std::byte> fmt, payload;
    bool haveFmt = false, haveData = false;
    std::size_t o = 12;
    while (o + 8 <= end && !(haveFmt && haveData)) {
        const std::uint32_t len = u32(data, o + 4);
        const std::size_t body = o + 8;
        if (fourcc(data, o, "fmt ") && !haveFmt) {
            fmt = data.subspan(body, std::min<std::size_t>(len, data.size() - body));
            haveFmt = true;
        } else if (fourcc(data, o, "data") && !haveData) {
            payload = data.subspan(body, std::min<std::size_t>(len, data.size() - body));
            haveData = true;
        }
        const std::uint64_t next = std::uint64_t{body} + len + (len & 1);
        if (next > end)
            break;
        o = static_cast<std::size_t>(next);
    }
    if (!haveFmt)
        return std::unexpected("no 'fmt ' chunk");
    if (!haveData)
        return std::unexpected("no 'data' chunk");
    if (fmt.size() < 14)
        return std::unexpected(std::format("'fmt ' chunk too short ({} bytes)", fmt.size()));

    WaveSound w;
    w.formatTag = u16(fmt, 0);
    w.channels = u16(fmt, 2);
    w.sampleRate = u32(fmt, 4);
    const std::uint16_t blockAlign = u16(fmt, 12);
    w.sourceBits = fmt.size() >= 16 ? u16(fmt, 14) : 8;
    if (w.formatTag == kFormatExtensible) {
        if (fmt.size() < 26)
            return std::unexpected("WAVE_FORMAT_EXTENSIBLE 'fmt ' chunk too short");
        w.formatTag = u16(fmt, 24); // first two bytes of the sub-format GUID
    }
    if (w.channels < 1 || w.channels > 2)
        return std::unexpected(std::format("{} channels not supported", w.channels));
    if (w.sampleRate == 0 || w.sampleRate > 384000)
        return std::unexpected(std::format("bad sample rate {}", w.sampleRate));

    switch (w.formatTag) {
    case kFormatPcm:
        if (w.sourceBits != 8 && w.sourceBits != 16 && w.sourceBits != 24 && w.sourceBits != 32)
            return std::unexpected(std::format("{}-bit PCM not supported", w.sourceBits));
        decodePcm(payload, w.sourceBits, w.channels, w.samples);
        return w;
    case kFormatImaAdpcm: {
        std::string error;
        if (!decodeIma(payload, w.channels, blockAlign, w.samples, &error))
            return std::unexpected(error);
        return w;
    }
    default:
        return std::unexpected(std::format("format tag {:#x} not supported", w.formatTag));
    }
}

} // namespace brc::formats
