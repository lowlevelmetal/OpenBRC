#include "TestData.h"
#include "formats/OmtFile.h"
#include "formats/Wave.h"
#include "vfs/DiscManifest.h"

#include <gtest/gtest.h>

#include <random>

namespace brc {
namespace {

using formats::parseWave;

struct Builder {
    std::vector<std::byte> d;

    void bytes(std::initializer_list<int> v) {
        for (int b : v)
            d.push_back(static_cast<std::byte>(b));
    }
    void tag(const char (&t)[5]) {
        for (int i = 0; i < 4; ++i)
            d.push_back(static_cast<std::byte>(t[i]));
    }
    void u16(std::uint32_t v) { bytes({static_cast<int>(v & 0xff), static_cast<int>((v >> 8) & 0xff)}); }
    void u32(std::uint32_t v) {
        u16(v & 0xffff);
        u16(v >> 16);
    }
    void fmt(std::uint16_t format, std::uint16_t channels, std::uint32_t rate, std::uint16_t bits,
             std::uint16_t align, std::uint32_t size = 16) {
        tag("fmt ");
        u32(size);
        u16(format);
        u16(channels);
        u32(rate);
        u32(rate * align);
        u16(align);
        u16(bits);
        for (std::uint32_t i = 16; i < size; ++i)
            bytes({0});
    }
    void chunk(const char (&t)[5], std::vector<int> body, bool pad = true) {
        tag(t);
        u32(static_cast<std::uint32_t>(body.size()));
        for (int b : body)
            bytes({b});
        if (pad && body.size() % 2)
            bytes({0});
    }
    // RIFF header with the length of everything that follows (minus `shortBy`).
    std::vector<std::byte> riff(std::size_t shortBy = 0) const {
        Builder r;
        r.tag("RIFF");
        r.u32(static_cast<std::uint32_t>(d.size() + 4 - shortBy));
        r.tag("WAVE");
        r.d.insert(r.d.end(), d.begin(), d.end());
        return r.d;
    }
};

TEST(Wave, Pcm8Mono) {
    Builder b;
    b.fmt(1, 1, 11025, 8, 1);
    b.chunk("data", {0x80, 0xFF, 0x00, 0x81});
    const auto w = parseWave(b.riff());
    ASSERT_TRUE(w) << w.error();
    EXPECT_EQ(w->formatTag, 1);
    EXPECT_EQ(w->channels, 1);
    EXPECT_EQ(w->sampleRate, 11025u);
    EXPECT_EQ(w->sourceBits, 8);
    EXPECT_EQ(w->samples, (std::vector<std::int16_t>{0, 127 * 256, -32768, 256}));
    EXPECT_EQ(w->frames(), 4u);
}

TEST(Wave, Pcm16StereoAnd24Bit) {
    Builder b;
    b.fmt(1, 2, 22050, 16, 4);
    b.chunk("data", {0x01, 0x00, 0xFF, 0xFF, 0x00, 0x80, 0xFF, 0x7F});
    auto w = parseWave(b.riff());
    ASSERT_TRUE(w);
    EXPECT_EQ(w->channels, 2);
    EXPECT_EQ(w->samples, (std::vector<std::int16_t>{1, -1, -32768, 32767}));
    EXPECT_EQ(w->frames(), 2u);

    Builder c;
    c.fmt(1, 1, 44100, 24, 3);
    c.chunk("data", {0x12, 0x34, 0x56, 0xAB, 0xCD, 0xEF});
    w = parseWave(c.riff());
    ASSERT_TRUE(w);
    EXPECT_EQ(w->samples, (std::vector<std::int16_t>{0x5634, static_cast<std::int16_t>(0xEFCD)}));
}

TEST(Wave, ToleratesTheGamesQuirks) {
    // An 18-byte 'fmt ', 'fact' before the data, an odd data chunk whose pad
    // byte the RIFF length leaves out, a LIST after it, and leftover samples
    // after the RIFF end that look like a chunk with a huge length.
    Builder b;
    b.fmt(1, 1, 22050, 8, 1, 18);
    b.chunk("fact", {3, 0, 0, 0});
    b.chunk("data", {0x80, 0x90, 0xA0});
    b.chunk("LIST", {'I', 'N', 'F', 'O'});
    auto bytes = b.riff();
    for (int i = 0; i < 64; ++i)
        bytes.push_back(std::byte{0x80});
    auto w = parseWave(bytes);
    ASSERT_TRUE(w) << w.error();
    EXPECT_EQ(w->frames(), 3u);

    // RIFF length one short (the pad byte), nothing after the data.
    Builder c;
    c.fmt(1, 1, 22050, 8, 1);
    c.chunk("data", {0x80, 0x90, 0xA0});
    w = parseWave(c.riff(1));
    ASSERT_TRUE(w);
    EXPECT_EQ(w->frames(), 3u);

    // Data chunk longer than the file: clamped to what is there.
    Builder d;
    d.fmt(1, 2, 22050, 16, 4);
    d.tag("data");
    d.u32(1000);
    d.bytes({1, 0, 2, 0, 3, 0, 4, 0, 5, 0});
    w = parseWave(d.riff());
    ASSERT_TRUE(w);
    EXPECT_EQ(w->frames(), 2u); // the half frame is dropped
}

TEST(Wave, ImaAdpcm) {
    Builder b;
    b.fmt(0x11, 1, 22050, 4, 8);
    // Block: initial sample 0, step index 0, then 8 nibbles of 7.
    b.chunk("data", {0, 0, 0, 0, 0x77, 0x77, 0x77, 0x77});
    const auto w = parseWave(b.riff());
    ASSERT_TRUE(w) << w.error();
    EXPECT_EQ(w->formatTag, 0x11);
    ASSERT_EQ(w->frames(), 9u);
    // step 7 -> diff 0+1+3+7 = 11, index 8 (step 16) -> diff 2+4+8+16 = 30, index 16 (step 34) -> 72
    EXPECT_EQ(w->samples[0], 0);
    EXPECT_EQ(w->samples[1], 11);
    EXPECT_EQ(w->samples[2], 41);
    EXPECT_EQ(w->samples[3], 41 + 4 + 8 + 17 + 34);
}

TEST(Wave, RejectsWhatItCannotPlay) {
    EXPECT_FALSE(parseWave({}));
    Builder notWave;
    notWave.tag("RIFF");
    notWave.u32(4);
    notWave.tag("AVI ");
    EXPECT_FALSE(parseWave(notWave.d));

    Builder noFmt;
    noFmt.chunk("data", {1, 2});
    EXPECT_FALSE(parseWave(noFmt.riff()));

    Builder noData;
    noData.fmt(1, 1, 22050, 8, 1);
    EXPECT_FALSE(parseWave(noData.riff()));

    for (auto [format, channels, rate, bits] :
         {std::tuple{0x55, 1, 22050u, 16}, std::tuple{1, 0, 22050u, 8}, std::tuple{1, 3, 22050u, 8},
          std::tuple{1, 1, 0u, 8}, std::tuple{1, 1, 22050u, 12}}) {
        Builder b;
        b.fmt(static_cast<std::uint16_t>(format), static_cast<std::uint16_t>(channels), rate,
              static_cast<std::uint16_t>(bits), 2);
        b.chunk("data", {1, 2, 3, 4});
        const auto w = parseWave(b.riff());
        EXPECT_FALSE(w) << format << " " << channels << " " << rate << " " << bits;
        if (!w) {
            EXPECT_FALSE(w.error().empty());
        }
    }
}

TEST(Wave, SurvivesRandomMutations) {
    Builder b;
    b.fmt(0x11, 2, 22050, 4, 64);
    std::vector<int> body(200);
    for (std::size_t i = 0; i < body.size(); ++i)
        body[i] = static_cast<int>((i * 37) & 0xff);
    b.chunk("data", body);
    const auto good = b.riff();
    std::mt19937 rng(42);
    for (int i = 0; i < 3000; ++i) {
        auto d = good;
        for (int e = 0; e < 4; ++e)
            d[rng() % d.size()] = static_cast<std::byte>(rng());
        if (rng() % 8 == 0)
            d.resize(rng() % d.size());
        const auto w = parseWave(d);
        if (w) {
            EXPECT_LE(w->samples.size(), std::size_t{1} << 20);
        }
    }
}

TEST(WaveDisc, EveryWaveChunkParses) {
    BRC_REQUIRE_GAME_DATA();
    int chunks = 0;
    std::uint64_t frames = 0;
    for (const auto& e : vfs::kDiscManifest) {
        if (!e.path.ends_with(".omt") && !e.path.ends_with(".cache"))
            continue;
        auto omt = formats::OmtFile::open(test::gameData()->open(e.path), std::string(e.path));
        ASSERT_TRUE(omt) << omt.error();
        for (const auto* c : omt->ofType(formats::chunk::kWave)) {
            const auto w = parseWave(omt->read(*c));
            ASSERT_TRUE(w) << e.path << " " << c->id << ": " << w.error();
            EXPECT_EQ(w->formatTag, 1);
            EXPECT_TRUE(w->sampleRate == 11025 || w->sampleRate == 22050 || w->sampleRate == 44100);
            frames += w->frames();
            ++chunks;
        }
    }
    EXPECT_EQ(chunks, 991);
    EXPECT_EQ(frames, 56877003u);
}

} // namespace
} // namespace brc
