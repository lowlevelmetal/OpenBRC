#include "SmackerWriter.h"
#include "TestData.h"
#include "formats/OmtFile.h"
#include "formats/Smacker.h"
#include "vfs/DiscManifest.h"

#include <gtest/gtest.h>

#include <random>

namespace brc {
namespace {

using formats::Smacker;
namespace smk = test::smk;

std::shared_ptr<MemoryFile> memFile(const std::vector<std::uint8_t>& d) {
    const auto* p = reinterpret_cast<const std::byte*>(d.data());
    return std::make_shared<MemoryFile>(std::vector<std::byte>(p, p + d.size()));
}

std::uint32_t crc32(std::span<const std::uint8_t> data, std::uint32_t crc = 0) {
    crc = ~crc;
    for (const auto b : data) {
        crc ^= b;
        for (int k = 0; k < 8; ++k)
            crc = (crc & 1) ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
    }
    return ~crc;
}

// Deterministic test images: per 4x4 block one of solid, two-colour, random,
// 2x2-pixel-doubled, row-doubled, or (after frame 0) unchanged.
std::vector<std::uint8_t> makeImage(std::uint32_t w, std::uint32_t h, std::uint32_t seed,
                                    const std::vector<std::uint8_t>& prev) {
    std::mt19937 rng(seed);
    std::vector<std::uint8_t> img(std::size_t{w} * h);
    for (std::uint32_t by = 0; by < h / 4; ++by) {
        for (std::uint32_t bx = 0; bx < w / 4; ++bx) {
            const int kind = static_cast<int>(rng() % (prev.empty() ? 5 : 6));
            const auto c0 = static_cast<std::uint8_t>(rng()), c1 = static_cast<std::uint8_t>(rng() | 1);
            for (std::uint32_t y = 0; y < 4; ++y) {
                for (std::uint32_t x = 0; x < 4; ++x) {
                    const std::size_t o = (by * 4 + y) * w + bx * 4 + x;
                    std::uint8_t v = 0;
                    switch (kind) {
                    case 0:
                        v = c0;
                        break;
                    case 1:
                        v = (rng() & 1) ? c0 : static_cast<std::uint8_t>(c0 ^ c1);
                        break;
                    case 2:
                        v = static_cast<std::uint8_t>(rng());
                        break;
                    case 3:
                        v = static_cast<std::uint8_t>(c0 + (y / 2) * 7 + (x / 2) * 3);
                        break;
                    case 4:
                        v = static_cast<std::uint8_t>(c0 + (y / 2) * 11 + x * 5);
                        break;
                    default:
                        v = prev[o];
                        break;
                    }
                    img[o] = v;
                }
            }
        }
    }
    return img;
}

std::array<std::uint8_t, 768> makePalette(std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::array<std::uint8_t, 768> p{};
    for (auto& v : p)
        v = smk::kPal6to8[rng() % 64];
    return p;
}

smk::Movie makeMovie(int version, std::uint32_t w, std::uint32_t h, int frames) {
    smk::Movie m;
    m.version = version;
    m.width = w;
    m.height = h;
    std::vector<std::uint8_t> prev;
    for (int i = 0; i < frames; ++i) {
        smk::Frame f;
        f.pixels = makeImage(w, h, static_cast<std::uint32_t>(i * 7 + version), prev);
        prev = f.pixels;
        m.frames.push_back(std::move(f));
    }
    m.frames[0].palette = makePalette(1);
    return m;
}

Smacker openMovie(const smk::Movie& m) {
    auto s = Smacker::open(memFile(smk::encode(m)));
    EXPECT_TRUE(s) << (s ? "" : s.error());
    return std::move(*s);
}

// Bounding box of the 4x4 blocks that differ between two images.
formats::SmackerRect changedBlocks(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b,
                                   std::uint32_t w, std::uint32_t h) {
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x)
            if (a.empty() || a[y * w + x] != b[y * w + x]) {
                x0 = std::min(x0, static_cast<int>(x / 4));
                x1 = std::max(x1, static_cast<int>(x / 4));
                y0 = std::min(y0, static_cast<int>(y / 4));
                y1 = std::max(y1, static_cast<int>(y / 4));
            }
    if (x1 < 0)
        return {};
    return {x0 * 4, y0 * 4, (x1 - x0 + 1) * 4, (y1 - y0 + 1) * 4};
}

void expectFrame(const Smacker& s, const std::vector<std::uint8_t>& expected) {
    const auto got = s.frameBuffer();
    ASSERT_EQ(got.size(), expected.size());
    EXPECT_TRUE(std::equal(got.begin(), got.end(), expected.begin())) << "frame " << s.currentFrame();
}

// ---------------------------------------------------------------- video

TEST(Smacker, RoundTripsEveryBlockType) {
    for (int version : {2, 4}) {
        SCOPED_TRACE(version);
        const auto m = makeMovie(version, 64, 32, 8);
        auto s = openMovie(m);
        EXPECT_EQ(s.version(), static_cast<std::uint32_t>(version));
        EXPECT_EQ(s.width(), 64u);
        EXPECT_EQ(s.height(), 32u);
        EXPECT_EQ(s.frameCount(), 8u);
        for (std::uint32_t f = 0; f < 8; ++f) {
            if (f > 0)
                s.nextFrame();
            const auto r = s.decodeFrame();
            ASSERT_TRUE(r) << r.error();
            EXPECT_EQ(r->frame, f);
            EXPECT_EQ(r->decoded, 1u);
            expectFrame(s, m.frames[f].pixels);
            const auto want = changedBlocks(f ? m.frames[f - 1].pixels : std::vector<std::uint8_t>{},
                                            m.frames[f].pixels, 64, 32);
            EXPECT_EQ(r->dirty.x, want.x);
            EXPECT_EQ(r->dirty.y, want.y);
            EXPECT_EQ(r->dirty.w, want.w);
            EXPECT_EQ(r->dirty.h, want.h);
        }
        // Decoding the same frame again changes nothing.
        const auto again = s.decodeFrame();
        ASSERT_TRUE(again);
        EXPECT_EQ(again->decoded, 0u);
        EXPECT_TRUE(again->dirty.empty());
    }
}

TEST(Smacker, LongRunsAndUnchangedFrames) {
    smk::Movie m;
    m.width = 400; // 100 blocks per row: runs up to 128/256/512 codes
    m.height = 40;
    m.frames.resize(3);
    m.frames[0].pixels.assign(400 * 40, 9);  // one solid run over 1000 blocks
    m.frames[1].pixels = m.frames[0].pixels; // all void
    m.frames[2].pixels = m.frames[0].pixels;
    m.frames[2].pixels[399 * 1 + 40 * 0] = 3; // one changed block near the right
    auto s = openMovie(m);
    ASSERT_TRUE(s.decodeFrame());
    expectFrame(s, m.frames[0].pixels);
    s.nextFrame();
    const auto r1 = s.decodeFrame();
    ASSERT_TRUE(r1);
    EXPECT_TRUE(r1->dirty.empty());
    s.nextFrame();
    const auto r2 = s.decodeFrame();
    ASSERT_TRUE(r2);
    expectFrame(s, m.frames[2].pixels);
    EXPECT_EQ(r2->dirty.x, 396);
    EXPECT_EQ(r2->dirty.y, 0);
    EXPECT_EQ(r2->dirty.w, 4);
    EXPECT_EQ(r2->dirty.h, 4);
}

TEST(Smacker, YDoubledRepeatsRows) {
    auto m = makeMovie(2, 16, 8, 2);
    m.flags = 4;
    auto s = openMovie(m);
    EXPECT_EQ(s.yScale(), formats::SmackerYScale::Doubled);
    EXPECT_EQ(s.storedHeight(), 8u);
    EXPECT_EQ(s.height(), 16u);
    for (int f = 0; f < 2; ++f) {
        if (f)
            s.nextFrame();
        const auto r = s.decodeFrame();
        ASSERT_TRUE(r);
        const auto out = s.frameBuffer();
        const auto& src = m.frames[static_cast<std::size_t>(f)].pixels;
        for (std::size_t y = 0; y < 16; ++y) {
            for (std::size_t x = 0; x < 16; ++x) {
                ASSERT_EQ(out[y * 16 + x], src[(y / 2) * 16 + x]) << y << "," << x;
            }
        }
        if (f == 0) {
            EXPECT_EQ(r->dirty.h, 16);
        }
    }
}

TEST(Smacker, YInterlacedLeavesOddRows) {
    auto m = makeMovie(2, 16, 8, 1);
    m.flags = 2;
    auto s = openMovie(m);
    EXPECT_EQ(s.yScale(), formats::SmackerYScale::Interlaced);
    std::vector<std::uint8_t> buf(16 * 16, 0xAA);
    ASSERT_TRUE(s.setTarget(buf, 16));
    ASSERT_TRUE(s.decodeFrame());
    for (std::size_t y = 0; y < 16; ++y) {
        for (std::size_t x = 0; x < 16; ++x) {
            ASSERT_EQ(buf[y * 16 + x], y % 2 ? 0xAA : m.frames[0].pixels[(y / 2) * 16 + x]);
        }
    }
}

TEST(Smacker, RingFrameLoopsBackToFrameZero) {
    auto m = makeMovie(2, 32, 16, 3);
    smk::Frame ring;
    ring.pixels = m.frames[0].pixels;
    m.frames.push_back(ring);
    m.flags = 1;
    auto s = openMovie(m);
    EXPECT_TRUE(s.hasRingFrame());
    EXPECT_EQ(s.frameCount(), 3u);
    for (int f = 0; f < 3; ++f) {
        if (f)
            s.nextFrame();
        ASSERT_TRUE(s.decodeFrame());
    }
    s.nextFrame();
    EXPECT_EQ(s.currentFrame(), 0u);
    auto r = s.decodeFrame();
    ASSERT_TRUE(r) << r.error();
    EXPECT_EQ(r->decoded, 1u); // just the ring frame
    expectFrame(s, m.frames[0].pixels);
    s.nextFrame();
    r = s.decodeFrame();
    ASSERT_TRUE(r);
    EXPECT_EQ(r->decoded, 1u);
    expectFrame(s, m.frames[1].pixels);

    // Wrapping without the last frame in the target restarts at frame 0.
    s.gotoFrame(1);
    ASSERT_TRUE(s.decodeFrame());
    s.nextFrame();
    s.nextFrame();
    EXPECT_EQ(s.currentFrame(), 0u);
    ASSERT_TRUE(s.decodeFrame());
    expectFrame(s, m.frames[0].pixels);
}

TEST(Smacker, LoopsWithoutRingFrame) {
    const auto m = makeMovie(2, 16, 8, 3);
    auto s = openMovie(m);
    for (int i = 0; i < 7; ++i) {
        if (i)
            s.nextFrame();
        ASSERT_TRUE(s.decodeFrame());
        EXPECT_EQ(s.currentFrame(), static_cast<std::uint32_t>(i % 3));
        expectFrame(s, m.frames[static_cast<std::size_t>(i % 3)].pixels);
    }
}

TEST(Smacker, SeeksFromKeyframes) {
    auto m = makeMovie(2, 32, 16, 10);
    m.frames[0].keyframe = true;
    m.frames[5].keyframe = true;
    // A keyframe must not depend on earlier frames: make frame 5 change every block.
    m.frames[5].pixels = makeImage(32, 16, 999, {});
    m.frames[6].pixels = makeImage(32, 16, 1000, m.frames[5].pixels);
    auto s = openMovie(m);
    EXPECT_TRUE(s.isKeyframe(5));
    EXPECT_FALSE(s.isKeyframe(6));

    s.gotoFrame(7);
    auto r = s.decodeFrame();
    ASSERT_TRUE(r);
    EXPECT_EQ(r->decoded, 3u); // 5, 6, 7
    expectFrame(s, m.frames[7].pixels);

    s.gotoFrame(3);
    r = s.decodeFrame();
    ASSERT_TRUE(r);
    EXPECT_EQ(r->decoded, 4u); // 0..3
    expectFrame(s, m.frames[3].pixels);

    s.gotoFrame(4);
    r = s.decodeFrame();
    ASSERT_TRUE(r);
    EXPECT_EQ(r->decoded, 1u);
    expectFrame(s, m.frames[4].pixels);

    s.gotoFrame(100); // clamped
    EXPECT_EQ(s.currentFrame(), 9u);
    r = s.decodeFrame();
    ASSERT_TRUE(r);
    EXPECT_EQ(r->decoded, 5u); // 5..9
    expectFrame(s, m.frames[9].pixels);

    s.invalidate();
    r = s.decodeFrame();
    ASSERT_TRUE(r);
    EXPECT_EQ(r->decoded, 5u);
}

TEST(Smacker, DecodesIntoCallerBuffer) {
    const auto m = makeMovie(2, 16, 8, 2);
    auto s = openMovie(m);
    std::vector<std::uint8_t> small(16 * 8 - 1);
    EXPECT_FALSE(s.setTarget(small, 16));
    std::vector<std::uint8_t> buf(40 * 20, 0xEE);
    EXPECT_FALSE(s.setTarget(buf, 40, 25, 0)); // past the right edge
    EXPECT_FALSE(s.setTarget(buf, 40, 0, 13)); // past the bottom
    ASSERT_TRUE(s.setTarget(buf, 40, 10, 5));
    for (int f = 0; f < 2; ++f) {
        if (f)
            s.nextFrame();
        ASSERT_TRUE(s.decodeFrame());
        for (std::size_t y = 0; y < 20; ++y) {
            for (std::size_t x = 0; x < 40; ++x) {
                const bool inside = x >= 10 && x < 26 && y >= 5 && y < 13;
                const std::uint8_t want =
                    inside ? m.frames[static_cast<std::size_t>(f)].pixels[(y - 5) * 16 + (x - 10)] : 0xEE;
                ASSERT_EQ(buf[y * 40 + x], want) << x << "," << y;
            }
        }
    }
}

// ---------------------------------------------------------------- palette and timing

TEST(Smacker, PaletteChunksApplyWhenFramesLoad) {
    auto m = makeMovie(2, 16, 8, 4);
    const auto p0 = makePalette(1);
    auto p2 = p0;
    // Frame 2: unchanged entries (skip), entries moved from elsewhere (copy) and new ones.
    for (int i = 0; i < 10; ++i)
        std::memcpy(&p2[static_cast<std::size_t>(100 + i) * 3], &p0[static_cast<std::size_t>(20 + i) * 3], 3);
    for (int i = 200; i < 256; ++i)
        p2[static_cast<std::size_t>(i) * 3] = smk::kPal6to8[static_cast<std::size_t>(i % 64)];
    m.frames[0].palette = p0;
    m.frames[2].palette = p2;
    auto s = openMovie(m);
    EXPECT_EQ(s.palette(), p0); // frame 0 is loaded by open()
    EXPECT_TRUE(s.paletteChanged());
    s.nextFrame();
    EXPECT_EQ(s.palette(), p0);
    EXPECT_FALSE(s.paletteChanged());
    s.nextFrame();
    EXPECT_EQ(s.palette(), p2);
    EXPECT_TRUE(s.paletteChanged());
    const auto r = s.decodeFrame();
    ASSERT_TRUE(r);
    EXPECT_TRUE(r->paletteChanged);
    s.gotoFrame(1);
    EXPECT_EQ(s.palette(), p0);
    s.gotoFrame(3);
    EXPECT_EQ(s.palette(), p2);
}

TEST(Smacker, PaletteDecoderRejectsBadCommands) {
    std::array<std::uint8_t, 768> pal{};
    std::string error;
    const std::vector<std::uint8_t> copyPastEnd = {0x40 | 9, 250};
    EXPECT_FALSE(formats::decodeSmackerPalette(copyPastEnd, pal, &error));
    EXPECT_FALSE(error.empty());
    const std::vector<std::uint8_t> shortChunk = {0x80 | 127, 0x01, 0x02};
    EXPECT_FALSE(formats::decodeSmackerPalette(shortChunk, pal));
    const std::vector<std::uint8_t> skipAll = {0xFF, 0xFF};
    EXPECT_TRUE(formats::decodeSmackerPalette(skipAll, pal));
    const std::vector<std::uint8_t> one = {0x3F, 0x20, 0x00, 0x80 | 127, 0x80 | 126};
    EXPECT_TRUE(formats::decodeSmackerPalette(one, pal));
    EXPECT_EQ(pal[0], 0xFF);
    EXPECT_EQ(pal[1], 0x82);
    EXPECT_EQ(pal[2], 0x00);
}

TEST(Smacker, FrameRateUnits) {
    auto m = makeMovie(2, 8, 8, 1);
    m.rate = 100; // ms per frame
    EXPECT_EQ(openMovie(m).frameDurationUs(), 100000u);
    m.rate = -6666; // 10 us units
    EXPECT_EQ(openMovie(m).frameDurationUs(), 66660u);
    m.rate = -10000;
    EXPECT_DOUBLE_EQ(openMovie(m).framesPerSecond(), 10.0);
    m.rate = 0; // default 10 fps
    EXPECT_EQ(openMovie(m).frameDurationUs(), 100000u);
}

// ---------------------------------------------------------------- audio

std::vector<std::int32_t> makeSamples(std::size_t n, bool is16, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::vector<std::int32_t> s(n);
    std::int32_t v = is16 ? 0 : 128;
    for (auto& x : s) {
        if (is16) {
            v += static_cast<std::int32_t>(rng() % 4001) - 2000;
            if (rng() % 50 == 0)
                v += 40000; // wraps: DPCM relies on modular arithmetic
            v = static_cast<std::int16_t>(static_cast<std::uint16_t>(v));
        } else {
            v = static_cast<std::int32_t>((v + static_cast<std::int32_t>(rng() % 31) - 15) & 0xff);
        }
        x = v;
    }
    return s;
}

std::vector<std::int16_t> toPcm(const std::vector<std::int32_t>& s, bool is16) {
    std::vector<std::int16_t> out;
    for (auto v : s)
        out.push_back(is16 ? static_cast<std::int16_t>(v) : static_cast<std::int16_t>((v - 128) * 256));
    return out;
}

void checkAudio(bool compressed) {
    smk::Movie m = makeMovie(2, 8, 8, 3);
    const std::array<std::pair<bool, bool>, 4> layouts = {
        {{false, false}, {false, true}, {true, false}, {true, true}}};
    for (std::size_t t = 0; t < 4; ++t) {
        smk::AudioTrack a;
        a.compressed = compressed;
        a.is16 = layouts[t].first;
        a.stereo = layouts[t].second;
        a.rate = t % 2 ? 22050 : 11025;
        m.tracks[t * 2] = a; // tracks 0, 2, 4, 6
        for (std::size_t f = 0; f < 3; ++f)
            m.frames[f].audio[t * 2] = makeSamples((a.stereo ? 2 : 1) * (100 + 37 * f), a.is16,
                                                   static_cast<std::uint32_t>(t * 10 + f));
    }
    auto s = openMovie(m);
    EXPECT_EQ(s.audioTracks(), 0x55);
    for (std::size_t t = 0; t < 4; ++t) {
        const auto& info = s.audioTrack(static_cast<int>(t * 2));
        EXPECT_TRUE(info.present);
        EXPECT_EQ(info.compressed, compressed);
        EXPECT_EQ(info.is16Bit, layouts[t].first);
        EXPECT_EQ(info.channels(), layouts[t].second ? 2 : 1);
        EXPECT_EQ(info.sampleRate, t % 2 ? 22050u : 11025u);
    }
    for (std::size_t f = 0; f < 3; ++f) {
        if (f)
            s.nextFrame();
        const auto r = s.decodeFrame();
        ASSERT_TRUE(r) << r.error();
        expectFrame(s, m.frames[f].pixels);
        for (std::size_t t = 0; t < 4; ++t) {
            const auto want = toPcm(m.frames[f].audio[t * 2], layouts[t].first);
            const auto got = s.audio(static_cast<int>(t * 2));
            ASSERT_EQ(got.size(), want.size()) << "track " << t * 2 << " frame " << f;
            EXPECT_TRUE(std::equal(got.begin(), got.end(), want.begin()))
                << "track " << t * 2 << " frame " << f;
        }
        EXPECT_TRUE(s.audio(1).empty());
    }
    // Disabled tracks produce nothing; skipped frames keep their audio.
    s.setAudioTracks(0x01);
    s.gotoFrame(0);
    ASSERT_TRUE(s.decodeFrame());
    s.nextFrame();
    s.nextFrame();
    const auto r = s.decodeFrame();
    ASSERT_TRUE(r);
    EXPECT_EQ(r->decoded, 2u);
    EXPECT_TRUE(s.audio(2).empty());
    EXPECT_EQ(s.audio(0).size(), m.frames[1].audio[0].size() + m.frames[2].audio[0].size());
}

TEST(Smacker, RawPcmAudioOnEveryLayout) {
    checkAudio(false);
}

TEST(Smacker, CompressedAudioOnEveryLayout) {
    checkAudio(true);
}

TEST(Smacker, AudioChunkChecks) {
    formats::SmackerAudioTrack t;
    t.present = true;
    t.compressed = true;
    t.sampleRate = 22050;
    std::vector<std::int16_t> out;
    EXPECT_FALSE(formats::decodeSmackerAudio(std::vector<std::uint8_t>{1, 2}, t, out));
    // Declares stereo 16-bit while the track is mono 8-bit.
    const std::vector<std::uint8_t> mismatch = {4, 0, 0, 0, 0x07};
    std::string error;
    EXPECT_FALSE(formats::decodeSmackerAudio(mismatch, t, out, &error));
    EXPECT_FALSE(error.empty());
    // "No data" flag.
    EXPECT_TRUE(formats::decodeSmackerAudio(std::vector<std::uint8_t>{4, 0, 0, 0, 0}, t, out));
    EXPECT_TRUE(out.empty());
    // Huge decompressed size.
    EXPECT_FALSE(formats::decodeSmackerAudio(std::vector<std::uint8_t>{0, 0, 0, 0x40, 1}, t, out));
    t.bink = true;
    EXPECT_FALSE(formats::decodeSmackerAudio(std::vector<std::uint8_t>{4, 0, 0, 0, 1}, t, out));
}

// ---------------------------------------------------------------- containers and malformed data

TEST(Smacker, ReportsItsSizeWithTrailingData) {
    const auto m = makeMovie(2, 16, 8, 3);
    auto bytes = smk::encode(m);
    const std::size_t size = bytes.size();
    bytes.resize(size + 123, 0x5A);
    auto s = Smacker::open(memFile(bytes));
    ASSERT_TRUE(s);
    EXPECT_EQ(s->byteSize(), size);
    EXPECT_FALSE(s->truncated());

    // Byte-span overload (copies).
    auto s2 = Smacker::open(std::as_bytes(std::span(bytes)));
    ASSERT_TRUE(s2);
    EXPECT_EQ(s2->frameCount(), 3u);
}

TEST(Smacker, TruncatedFileDecodesUpToTheCut) {
    const auto m = makeMovie(2, 16, 8, 3);
    auto bytes = smk::encode(m);
    bytes.resize(bytes.size() - 4);
    auto s = Smacker::open(memFile(bytes));
    ASSERT_TRUE(s) << s.error();
    EXPECT_TRUE(s->truncated());
    EXPECT_TRUE(s->decodeFrame());
    s->nextFrame();
    EXPECT_TRUE(s->decodeFrame());
    s->nextFrame();
    const auto r = s->decodeFrame();
    ASSERT_FALSE(r);
    EXPECT_NE(r.error().find("truncated"), std::string::npos);
}

TEST(Smacker, RejectsMalformedHeaders) {
    const auto good = smk::encode(makeMovie(2, 16, 8, 2));
    auto patched = [&](std::size_t offset, std::uint32_t value) {
        auto b = good;
        for (int i = 0; i < 4; ++i)
            b[offset + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value >> (8 * i));
        return b;
    };
    EXPECT_FALSE(Smacker::open(memFile({})));
    EXPECT_FALSE(Smacker::open(memFile({'S', 'M', 'K', '2'})));
    EXPECT_FALSE(Smacker::open(memFile(std::vector<std::uint8_t>(0x68, 0))));
    EXPECT_FALSE(Smacker::open(memFile(patched(0, 0x334B4D53))));    // "SMK3"
    EXPECT_FALSE(Smacker::open(memFile(patched(4, 0))));             // width 0
    EXPECT_FALSE(Smacker::open(memFile(patched(8, 0x10000))));       // height too large
    EXPECT_FALSE(Smacker::open(memFile(patched(12, 0))));            // no frames
    EXPECT_FALSE(Smacker::open(memFile(patched(12, 0x7fffffff))));   // frame table past the end
    EXPECT_FALSE(Smacker::open(memFile(patched(0x34, 0x7fffffff)))); // trees past the end
    EXPECT_FALSE(Smacker::open(memFile(std::vector<std::uint8_t>(good.begin(), good.begin() + 0x70))));

    // Trees that never end (all ones: nodes all the way down).
    auto deep = good;
    const std::size_t treeStart = 0x68 + 2 * 5;
    const std::uint32_t treeSize = good[0x34] | good[0x35] << 8;
    std::fill(deep.begin() + static_cast<std::ptrdiff_t>(treeStart),
              deep.begin() + static_cast<std::ptrdiff_t>(treeStart + treeSize), 0xFF);
    auto r = Smacker::open(memFile(deep));
    EXPECT_FALSE(r);
}

TEST(Smacker, SurvivesRandomMutations) {
    smk::Movie m = makeMovie(4, 32, 16, 5);
    m.frames[2].palette = makePalette(5);
    m.frames[3].keyframe = true;
    smk::AudioTrack a;
    a.compressed = true;
    a.is16 = true;
    a.stereo = true;
    m.tracks[0] = a;
    smk::AudioTrack raw;
    m.tracks[3] = raw;
    for (std::size_t f = 0; f < m.frames.size(); ++f) {
        m.frames[f].audio[0] = makeSamples(64, true, static_cast<std::uint32_t>(f));
        m.frames[f].audio[3] = makeSamples(33, false, static_cast<std::uint32_t>(f));
    }
    const auto good = smk::encode(m);
    std::mt19937 rng(20260101);
    int opened = 0;
    for (int iter = 0; iter < 3000; ++iter) {
        auto b = good;
        const int edits = 1 + static_cast<int>(rng() % 8);
        for (int e = 0; e < edits; ++e) {
            const std::size_t pos = (rng() % 4 == 0) ? rng() % 0x80 : rng() % b.size(); // favour the header
            b[pos] = static_cast<std::uint8_t>(rng());
        }
        if (rng() % 10 == 0)
            b.resize(rng() % b.size());
        auto s = Smacker::open(memFile(b));
        if (!s)
            continue;
        ++opened;
        for (std::uint32_t f = 0; f < std::min(s->frameCount(), 64u); ++f) {
            if (f)
                s->nextFrame();
            (void)s->decodeFrame();
        }
        s->gotoFrame(rng() % s->frameCount());
        (void)s->decodeFrame();
        s->nextFrame();
        (void)s->decodeFrame();
    }
    EXPECT_GT(opened, 100);
}

TEST(Smacker, ChunkDecodersSurviveRandomInput) {
    std::mt19937 rng(7);
    std::array<std::uint8_t, 768> pal{};
    for (int iter = 0; iter < 2000; ++iter) {
        std::vector<std::uint8_t> chunk(rng() % 600);
        for (auto& c : chunk)
            c = static_cast<std::uint8_t>(rng());
        if (chunk.size() >= 4) {
            chunk[2] = 0; // keep the decompressed size reasonable
            chunk[3] = 0;
        }
        (void)formats::decodeSmackerPalette(chunk, pal);
        formats::SmackerAudioTrack t;
        t.present = true;
        t.compressed = (iter & 1) != 0;
        t.is16Bit = (iter & 2) != 0;
        t.stereo = (iter & 4) != 0;
        t.sampleRate = 22050;
        std::vector<std::int16_t> out;
        (void)formats::decodeSmackerAudio(chunk, t, out);
        EXPECT_LE(out.size(), std::size_t{1} << 24);
    }
}

// ---------------------------------------------------------------- the disc

TEST(SmackerDisc, EveryStreamDecodes) {
    BRC_REQUIRE_GAME_DATA();
    int files = 0, chunks = 0;
    std::uint64_t frames = 0;
    for (const auto& e : vfs::kDiscManifest) {
        auto f = test::gameData()->open(e.path);
        ASSERT_TRUE(f) << e.path;
        if (e.path.ends_with(".smk")) {
            // Standalone movies: header, palette and the first frames (all
            // frames of every stream are checked by tools/smkcheck).
            auto s = Smacker::open(f);
            ASSERT_TRUE(s) << e.path << ": " << s.error();
            EXPECT_EQ(s->byteSize(), e.size) << e.path;
            for (std::uint32_t i = 0; i < std::min(s->frameCount(), 3u); ++i) {
                if (i)
                    s->nextFrame();
                const auto r = s->decodeFrame();
                ASSERT_TRUE(r) << e.path << ": " << r.error();
            }
            frames += s->frameCount();
            ++files;
        } else if (e.path.ends_with(".omt") || e.path.ends_with(".cache")) {
            auto omt = formats::OmtFile::open(f, std::string(e.path));
            ASSERT_TRUE(omt) << omt.error();
            for (const auto& c : omt->chunks()) {
                if (c.type != formats::chunk::kSmacker && c.type != formats::chunk::kSmacker2)
                    continue;
                auto s = Smacker::open(omt->openChunk(c));
                ASSERT_TRUE(s) << e.path << " " << c.id << ": " << s.error();
                EXPECT_EQ(s->byteSize(), c.size) << e.path << " " << c.id; // no wrapper, no trailer
                for (std::uint32_t i = 0; i < s->frameCount(); ++i) {
                    if (i)
                        s->nextFrame();
                    const auto r = s->decodeFrame();
                    ASSERT_TRUE(r) << e.path << " " << c.id << ": " << r.error();
                }
                frames += s->frameCount();
                ++chunks;
            }
        }
    }
    EXPECT_EQ(files, 253);
    EXPECT_EQ(chunks, 714);
    EXPECT_EQ(frames, 27455u);
}

TEST(SmackerDisc, IntroMatchesReferenceDecoder) {
    BRC_REQUIRE_GAME_DATA();
    // Checksums of BarbieRecomp's decoder (and FFmpeg's) output.
    auto s = Smacker::open(test::gameData()->open("resources/smk/intro.smk"));
    ASSERT_TRUE(s) << s.error();
    EXPECT_EQ(s->width(), 512u);
    EXPECT_EQ(s->height(), 384u);
    EXPECT_EQ(s->frameCount(), 420u);
    EXPECT_EQ(s->frameDurationUs(), 100000u);
    const auto& a = s->audioTrack(0);
    EXPECT_TRUE(a.present && a.compressed && a.stereo && !a.is16Bit);
    EXPECT_EQ(a.sampleRate, 22050u);
    std::uint32_t video = 0, palette = 0, audio = 0;
    std::size_t samples = 0;
    for (std::uint32_t f = 0; f < s->frameCount(); ++f) {
        if (f)
            s->nextFrame();
        palette = crc32(s->palette(), palette);
        const auto r = s->decodeFrame();
        ASSERT_TRUE(r) << r.error();
        video = crc32(s->frameBuffer(), video);
        const auto pcm = s->audio(0);
        samples += pcm.size();
        std::vector<std::uint8_t> le;
        for (const auto v : pcm) {
            le.push_back(static_cast<std::uint8_t>(v));
            le.push_back(static_cast<std::uint8_t>(static_cast<std::uint16_t>(v) >> 8));
        }
        audio = crc32(le, audio);
    }
    EXPECT_EQ(samples, 1852200u);
    EXPECT_EQ(video, 0x7bfd29e4u) << std::hex << video;
    EXPECT_EQ(palette, 0x94248519u) << std::hex << palette;
    EXPECT_EQ(audio, 0xf9963f1bu) << std::hex << audio;
}

} // namespace
} // namespace brc
