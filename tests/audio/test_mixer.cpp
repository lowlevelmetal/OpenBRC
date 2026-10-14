#include "audio/Device.h"
#include "audio/Mixer.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <thread>

namespace brc {
namespace {

using audio::Mixer;
using audio::VoiceId;
using audio::VoiceState;

constexpr float kEps = 1e-5f;

std::vector<float> render(Mixer& m, std::size_t frames) {
    std::vector<float> out(frames * 2);
    m.render(out);
    return out;
}

audio::SamplePtr constant(std::size_t frames, std::int16_t value, int channels = 1,
                          std::uint32_t rate = 22050) {
    return audio::makeSample(std::vector<std::int16_t>(frames * static_cast<std::size_t>(channels), value),
                             channels, rate);
}

// Sample rate equal to the mixer's: positions advance exactly one frame per
// output frame, and the cubic interpolation returns the samples themselves.
TEST(Mixer, PlaysSampleOnceBitExact) {
    Mixer m(22050);
    std::vector<std::int16_t> pcm(100);
    for (std::size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = static_cast<std::int16_t>(i * 300 - 15000);
    pcm.back() = 0;
    const VoiceId v = m.createVoice(audio::makeSample(pcm, 1, 22050));
    ASSERT_NE(v, VoiceId::None);
    EXPECT_EQ(m.state(v), VoiceState::Stopped);
    EXPECT_EQ(m.lengthMs(v), 4u); // 100 / 22050 s
    m.play(v);
    EXPECT_TRUE(m.isPlaying(v));
    const auto out = render(m, 150);
    for (std::size_t i = 0; i < 100; ++i) {
        EXPECT_NEAR(out[i * 2], pcm[i] / 32768.0f, kEps) << i;
        EXPECT_NEAR(out[i * 2 + 1], pcm[i] / 32768.0f, kEps) << i;
    }
    for (std::size_t i = 100; i < 150; ++i)
        EXPECT_EQ(out[i * 2], 0.0f);
    EXPECT_EQ(m.state(v), VoiceState::Done);
    EXPECT_TRUE(m.isDone(v));
    EXPECT_EQ(m.positionMs(v), 0u);

    // Playing again restarts from the beginning.
    m.play(v);
    const auto again = render(m, 10);
    EXPECT_NEAR(again[0], pcm[0] / 32768.0f, kEps);
}

TEST(Mixer, LoopCounts) {
    Mixer m(22050);
    const VoiceId v = m.createVoice(constant(50, 8192));
    m.play(v, 3);
    EXPECT_EQ(m.loopsRemaining(v), 3);
    auto out = render(m, 149);
    EXPECT_TRUE(m.isPlaying(v));
    EXPECT_EQ(m.loopsRemaining(v), 1);
    EXPECT_NEAR(out[148 * 2], 0.25f, kEps);
    out = render(m, 1);
    EXPECT_EQ(m.state(v), VoiceState::Done);

    // 0 = forever.
    m.play(v, 0);
    out = render(m, 50 * 40 + 7);
    EXPECT_TRUE(m.isPlaying(v));
    EXPECT_EQ(m.loopsRemaining(v), 0);
    EXPECT_NEAR(out.back(), 0.25f, kEps);
    EXPECT_EQ(m.positionMs(v), 7u * 1000 / 22050);
    m.stop(v);
    EXPECT_EQ(m.state(v), VoiceState::Stopped);
    EXPECT_EQ(m.positionMs(v), 0u);
}

TEST(Mixer, LoopsAreSeamless) {
    // A sine with a whole number of periods loops without a discontinuity,
    // even with resampling (the interpolation reads across the loop point).
    Mixer m(48000);
    std::vector<std::int16_t> pcm(220);
    for (std::size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = static_cast<std::int16_t>(20000 *
                                           std::sin(2 * std::numbers::pi * 2 * static_cast<double>(i) / 220));
    const VoiceId v = m.createVoice(audio::makeSample(pcm, 1, 11025));
    m.play(v, 0);
    const auto out = render(m, 48000 / 10);
    float maxStep = 0;
    // (The first frames interpolate from the silence before the start.)
    for (std::size_t i = 8; i < out.size() / 2; ++i)
        maxStep = std::max(maxStep, std::abs(out[i * 2] - out[(i - 1) * 2]));
    // 100.2 Hz at amplitude 0.61 changes by at most 2*pi*f*A/48000 per frame.
    EXPECT_LT(maxStep, 0.0085f);
}

TEST(Mixer, VolumeAndPan) {
    Mixer m(22050);
    const VoiceId v = m.createVoice(constant(1000, 16384));
    m.setVolume(v, 0.5f);
    m.play(v);
    auto out = render(m, 4);
    EXPECT_NEAR(out[0], 0.25f, kEps);
    EXPECT_NEAR(out[1], 0.25f, kEps);
    EXPECT_FLOAT_EQ(m.volume(v), 0.5f);

    // Changes while playing ramp over a few milliseconds.
    m.setPan(v, -1.0f);
    out = render(m, 200);
    for (std::size_t i = 1; i < 200; ++i)
        EXPECT_LE(out[i * 2 + 1], out[(i - 1) * 2 + 1] + kEps);
    EXPECT_NEAR(out[199 * 2], 0.25f, kEps);
    EXPECT_NEAR(out[199 * 2 + 1], 0.0f, kEps);

    m.setPan(v, 0.5f);
    out = render(m, 200);
    EXPECT_NEAR(out[199 * 2], 0.125f, kEps);
    EXPECT_NEAR(out[199 * 2 + 1], 0.25f, kEps);
    EXPECT_FLOAT_EQ(m.pan(v), 0.5f);

    m.setVolume(v, 7.0f); // clamped
    EXPECT_FLOAT_EQ(m.volume(v), 1.0f);
    m.setPan(v, -3.0f);
    EXPECT_FLOAT_EQ(m.pan(v), -1.0f);
}

TEST(Mixer, StereoSamplesKeepTheirChannels) {
    Mixer m(22050);
    std::vector<std::int16_t> pcm;
    for (int i = 0; i < 100; ++i) {
        pcm.push_back(8192);
        pcm.push_back(-16384);
    }
    const VoiceId v = m.createVoice(audio::makeSample(pcm, 2, 22050));
    m.setPan(v, 0.5f);
    m.play(v);
    const auto out = render(m, 10);
    EXPECT_NEAR(out[0], 0.25f * 0.5f, kEps);
    EXPECT_NEAR(out[1], -0.5f, kEps);
}

TEST(Mixer, MasterAndUserVolumeRamp) {
    Mixer m(48000);
    const VoiceId v = m.createVoice(constant(48000, 16384, 1, 48000));
    m.play(v);
    render(m, 10);
    m.setMasterVolume(0.5f, 10); // 480 frames
    auto out = render(m, 600);
    for (std::size_t i = 1; i < 600; ++i) {
        EXPECT_LE(out[i * 2], out[(i - 1) * 2] + kEps);
        EXPECT_LT(out[(i - 1) * 2] - out[i * 2], 0.001f); // no jump
    }
    EXPECT_NEAR(out[599 * 2], 0.25f, kEps);
    EXPECT_FLOAT_EQ(m.masterVolume(), 0.5f);
    m.setUserVolume(0.5f);
    out = render(m, 2000);
    EXPECT_NEAR(out[1999 * 2], 0.125f, kEps);
    EXPECT_FLOAT_EQ(m.userVolume(), 0.5f);
    m.setMasterVolume(1.0f, 0); // immediate
    m.setUserVolume(1.0f);
    out = render(m, 2000);
    EXPECT_NEAR(out[0], 0.25f, 0.001f); // the user volume ramps, the master jumped
    EXPECT_NEAR(out[1999 * 2], 0.5f, kEps);
}

TEST(Mixer, GlobalPauseFadesAndFreezes) {
    Mixer m(48000);
    const VoiceId v = m.createVoice(constant(48000, 16384, 1, 48000));
    m.play(v);
    render(m, 4800); // 100 ms
    EXPECT_EQ(m.positionMs(v), 100u);
    m.setGlobalPause(true);
    EXPECT_TRUE(m.globalPaused());
    auto out = render(m, 4800);
    for (std::size_t i = 1; i < 4800; ++i)
        EXPECT_LE(out[i * 2], out[(i - 1) * 2] + kEps);
    EXPECT_EQ(out[4799 * 2], 0.0f);
    const auto frozen = m.positionMs(v);
    EXPECT_LE(frozen, 116u); // the fade lasts 10 ms, checked every 256 frames
    render(m, 48000);
    EXPECT_EQ(m.positionMs(v), frozen);
    EXPECT_TRUE(m.isPlaying(v));
    m.setGlobalPause(false);
    out = render(m, 4800);
    EXPECT_NEAR(out[4799 * 2], 0.5f, kEps);
    EXPECT_EQ(m.positionMs(v), frozen + 100);
}

TEST(Mixer, VoicePauseAndPosition) {
    Mixer m(22050);
    const VoiceId v = m.createVoice(constant(22050, 1000)); // 1 s
    EXPECT_EQ(m.lengthMs(v), 1000u);
    m.setPositionMs(v, 500);
    EXPECT_EQ(m.positionMs(v), 500u);
    m.play(v);
    render(m, 2205);
    EXPECT_EQ(m.positionMs(v), 600u);
    m.setPaused(v, true);
    EXPECT_EQ(m.state(v), VoiceState::Paused);
    render(m, 2205);
    EXPECT_EQ(m.positionMs(v), 600u);
    m.setPaused(v, false);
    render(m, 2205);
    EXPECT_EQ(m.positionMs(v), 700u);
    m.setPositionMs(v, 1500); // wraps
    EXPECT_EQ(m.positionMs(v), 500u);
    m.stop(v);
    EXPECT_EQ(m.positionMs(v), 0u);
    m.setPositionMs(v, 200); // kept for the next play
    m.play(v);
    render(m, 2205);
    EXPECT_EQ(m.positionMs(v), 300u);
}

TEST(Mixer, ResamplesWithLowError) {
    for (std::uint32_t rate : {11025u, 22050u}) {
        SCOPED_TRACE(rate);
        Mixer m(48000);
        const double freq = 441.0;
        std::vector<std::int16_t> pcm(std::size_t{rate} * 2);
        for (std::size_t i = 0; i < pcm.size(); ++i)
            pcm[i] = static_cast<std::int16_t>(
                std::lround(16000 * std::sin(2 * std::numbers::pi * freq * static_cast<double>(i) / rate)));
        const VoiceId v = m.createVoice(audio::makeSample(pcm, 1, rate));
        m.play(v);
        const auto out = render(m, 48000);
        double err = 0, sig = 0;
        for (std::size_t i = 10; i < 48000; ++i) {
            const double want =
                16000.0 / 32768.0 * std::sin(2 * std::numbers::pi * freq * static_cast<double>(i) / 48000);
            err += (out[i * 2] - want) * (out[i * 2] - want);
            sig += want * want;
        }
        EXPECT_LT(std::sqrt(err / sig), 0.003) << "relative RMS error";
        EXPECT_EQ(m.positionMs(v), 1000u);
    }
}

TEST(Mixer, StreamUnderrunHoldsPosition) {
    Mixer m(22050);
    const VoiceId s = m.createStream(1, 22050);
    ASSERT_NE(s, VoiceId::None);
    std::vector<std::int16_t> chunk(1000, 8192);
    EXPECT_TRUE(m.queue(s, chunk));
    EXPECT_EQ(m.queuedFrames(s), 1000u);
    m.play(s, 5); // loop counts do not apply to streams
    render(m, 500);
    EXPECT_EQ(m.playedFrames(s), 500u);
    EXPECT_EQ(m.underruns(s), 0u);

    // Runs dry: silence (after a short fade), still playing, position held.
    auto out = render(m, 1000);
    EXPECT_TRUE(m.isPlaying(s));
    EXPECT_EQ(m.underruns(s), 1u);
    EXPECT_EQ(m.playedFrames(s), 998u); // keeps two frames for interpolation until more arrive or the end
    EXPECT_EQ(m.queuedFrames(s), 2u);
    EXPECT_NEAR(out[400 * 2], 0.25f, kEps);
    for (std::size_t i = 498; i < 1000; ++i)
        EXPECT_LE(std::abs(out[i * 2]), std::abs(out[(i - 1) * 2]) + kEps);
    EXPECT_EQ(out[999 * 2], 0.0f);
    render(m, 100);
    EXPECT_EQ(m.underruns(s), 1u); // one episode

    // More data resumes it (fading in), then the end of the stream finishes it.
    EXPECT_TRUE(m.queue(s, chunk));
    out = render(m, 500);
    EXPECT_NEAR(out[499 * 2], 0.25f, kEps);
    m.endStream(s);
    EXPECT_FALSE(m.queue(s, chunk));
    render(m, 2000);
    EXPECT_EQ(m.state(s), VoiceState::Done);
    EXPECT_EQ(m.playedFrames(s), 2000u);
    EXPECT_EQ(m.lengthMs(s), 2000u * 1000 / 22050);

    // stop() resets the stream for reuse.
    m.stop(s);
    EXPECT_EQ(m.playedFrames(s), 0u);
    EXPECT_TRUE(m.queue(s, chunk));
}

TEST(Mixer, LongStreamsStayAccurate) {
    // Data arriving in Smacker-sized chunks (one 15 fps frame of 22050 Hz
    // stereo) while the mixer consumes it at 48 kHz.
    Mixer m(48000);
    const VoiceId s = m.createStream(2, 22050);
    std::vector<std::int16_t> chunk(1470 * 2);
    for (std::size_t i = 0; i < chunk.size(); ++i)
        chunk[i] = static_cast<std::int16_t>(i % 2 ? -4000 : 4000);
    for (int i = 0; i < 3; ++i)
        m.queue(s, chunk);
    m.play(s);
    for (int frame = 0; frame < 300; ++frame) {
        m.queue(s, chunk);
        render(m, 3200); // 1/15 s
    }
    EXPECT_EQ(m.underruns(s), 0u);
    EXPECT_EQ(m.playedFrames(s), 300u * 1470);
    EXPECT_EQ(m.positionMs(s), 20000u);
}

TEST(Mixer, StoppingDoesNotClick) {
    Mixer m(48000);
    const VoiceId v = m.createVoice(constant(48000, 16384, 1, 48000));
    m.play(v);
    render(m, 100);
    m.stop(v);
    const auto out = render(m, 400);
    EXPECT_GT(out[0], 0.4f);
    for (std::size_t i = 1; i < 400; ++i)
        EXPECT_LT(out[(i - 1) * 2] - out[i * 2], 0.01f);
    EXPECT_EQ(out[399 * 2], 0.0f);
}

TEST(Mixer, ClipsTheSum) {
    Mixer m(22050);
    const VoiceId a = m.createVoice(constant(100, 30000));
    const VoiceId b = m.createVoice(constant(100, 30000));
    m.play(a);
    m.play(b);
    const auto out = render(m, 10);
    EXPECT_FLOAT_EQ(out[0], 1.0f);
}

TEST(Mixer, StaleHandlesAreIgnored) {
    Mixer m;
    EXPECT_EQ(m.createVoice(nullptr), VoiceId::None);
    EXPECT_EQ(m.createVoice(constant(10, 0, 3)), VoiceId::None);
    EXPECT_EQ(m.createStream(1, 0), VoiceId::None);
    const VoiceId v = m.createVoice(constant(10, 100));
    EXPECT_EQ(m.voiceCount(), 1u);
    m.destroyVoice(v);
    EXPECT_EQ(m.voiceCount(), 0u);
    EXPECT_EQ(m.state(v), VoiceState::Invalid);
    EXPECT_TRUE(m.isDone(v));
    m.play(v);
    m.setVolume(v, 0.5f);
    EXPECT_FALSE(m.queue(v, std::vector<std::int16_t>(4)));
    const VoiceId w = m.createVoice(constant(10, 100));
    EXPECT_NE(w, v); // same slot, new generation
    EXPECT_EQ(m.state(v), VoiceState::Invalid);
    EXPECT_EQ(m.state(w), VoiceState::Stopped);
    EXPECT_FALSE(m.queue(w, std::vector<std::int16_t>(4))); // not a stream

    // An empty sample finishes at once.
    const VoiceId e = m.createVoice(audio::makeSample({}, 1, 22050));
    m.play(e);
    EXPECT_TRUE(m.isDone(e));
}

TEST(Mixer, NullOutputAdvancesInRealTime) {
    Mixer m(48000);
    const VoiceId v = m.createVoice(constant(2205, 1000)); // 100 ms
    {
        audio::NullOutput out(m, 5);
        m.play(v);
        // The game thread keeps working with voices while the device renders.
        std::vector<VoiceId> extra;
        for (int i = 0; i < 50; ++i) {
            const VoiceId x = m.createVoice(constant(100, 500));
            m.play(x, 2);
            m.setPan(x, static_cast<float>(i % 3) - 1.0f);
            extra.push_back(x);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (i % 4 == 0)
                m.destroyVoice(extra[static_cast<std::size_t>(i) / 2]);
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!m.isDone(v) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        EXPECT_TRUE(m.isDone(v));
    }
}

TEST(Mixer, SdlOutputPlaysThroughTheDummyDriver) {
    // SDL's "dummy" driver consumes audio in real time without hardware.
#ifdef _WIN32
    _putenv_s("SDL_AUDIO_DRIVER", "dummy");
#else
    setenv("SDL_AUDIO_DRIVER", "dummy", 1);
#endif
    Mixer m;
    auto out = audio::SdlOutput::open(m);
    if (!out)
        GTEST_SKIP() << out.error();
    EXPECT_FALSE((*out)->name().empty());
    const VoiceId v = m.createVoice(constant(2205, 1000)); // 100 ms
    m.play(v);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!m.isDone(v) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_TRUE(m.isDone(v));
}

} // namespace
} // namespace brc
