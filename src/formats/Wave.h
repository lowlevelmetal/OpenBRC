#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace brc::formats {

// A decoded RIFF WAVE sound: interleaved signed 16-bit PCM.
struct WaveSound {
    std::uint16_t formatTag = 0; // 1 = PCM, 0x11 = IMA ADPCM
    int channels = 0;            // 1 or 2
    int sourceBits = 0;          // bits per sample in the file (4 for IMA ADPCM)
    std::uint32_t sampleRate = 0;
    std::vector<std::int16_t> samples;

    std::size_t frames() const { return channels ? samples.size() / static_cast<std::size_t>(channels) : 0; }
    // Length in milliseconds, rounded down.
    std::uint64_t durationMs() const {
        return sampleRate ? static_cast<std::uint64_t>(frames()) * 1000 / sampleRate : 0;
    }
};

// Parses a RIFF WAVE image: PCM (8-bit unsigned, 16/24/32-bit signed; also
// as WAVE_FORMAT_EXTENSIBLE) or IMA ADPCM (0x11), mono or stereo. 8-bit
// samples become (x - 128) << 8; wider ones keep their top 16 bits.
//
// The game's sounds are tolerated as they are: odd-sized data chunks whose
// pad byte the RIFF length leaves out, unknown chunks ('fact', 'LIST',
// 'smpl'), a 'fmt ' chunk of 18 bytes, and bytes after the end the RIFF
// header declares (several .omt WAVE chunks carry leftover sample data
// there, which looks like a chunk with a huge length). The walk stops at the
// RIFF end; a data chunk is clamped to the bytes actually present.
std::expected<WaveSound, std::string> parseWave(std::span<const std::byte> data);

} // namespace brc::formats
