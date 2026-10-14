#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace brc::audio {

// Decoded PCM shared, immutable, by any number of voices.
struct Sample {
    std::vector<std::int16_t> pcm; // interleaved
    int channels = 1;              // 1 or 2
    std::uint32_t rate = 22050;    // Hz

    std::size_t frames() const { return channels > 0 ? pcm.size() / static_cast<std::size_t>(channels) : 0; }
};
using SamplePtr = std::shared_ptr<const Sample>;

inline SamplePtr makeSample(std::vector<std::int16_t> pcm, int channels, std::uint32_t rate) {
    return std::make_shared<const Sample>(Sample{std::move(pcm), channels, rate});
}

// Handle of a voice. Stale handles (destroyed voices) are detected: every
// call on them is a no-op and queries report VoiceState::Invalid.
enum class VoiceId : std::uint32_t { None = 0 };

enum class VoiceState : std::uint8_t {
    Invalid, // no such voice
    Stopped, // created, or stopped with stop()
    Playing,
    Paused, // setPaused(id, true)
    Done,   // played every loop, or a stream that ended and ran dry
};

// Software mixer: mixes voices (samples or streams, any rate, mono or
// stereo) into interleaved stereo float at outputRate(). An output device
// (Device.h) pulls audio with render() from its own thread while the game
// thread controls voices; every method is thread-safe.
//
// Resampling uses 4-point cubic (Catmull-Rom) interpolation. Volume, pan,
// master and user volume changes are ramped, and voices that stop
// mid-signal (stop, pause, destroy, stream underrun, position jumps) fade
// out over a few milliseconds instead of clicking.
//
// Adapted from the maintainer's mixer in BarbieRecomp (runtime/audio.c).
class Mixer {
public:
    static constexpr std::uint32_t kDefaultRate = 48000;
    static constexpr std::uint32_t kDefaultRampMs = 20; // master/user volume changes
    static constexpr std::size_t kMaxVoices = 1024;

    explicit Mixer(std::uint32_t outputRate = kDefaultRate);
    ~Mixer();
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    std::uint32_t outputRate() const { return m_rate; }

    // --- Voices -------------------------------------------------------------
    // A voice playing `sample`. Returns VoiceId::None if the sample is empty
    // of channels/rate or kMaxVoices voices exist.
    VoiceId createVoice(SamplePtr sample);
    // A voice fed with queue() (Smacker audio). Loop counts do not apply.
    VoiceId createStream(int channels, std::uint32_t rate);
    void destroyVoice(VoiceId id);

    // Starts playing from the current position (a paused voice resumes).
    // `loops` is how many times the sample plays, counting the current pass;
    // 0 = forever. A finished voice restarts from the beginning. On a voice
    // that is already playing, only the loop count changes.
    void play(VoiceId id, int loops = 1);
    // Halts and rewinds to the start (the position can be set again before
    // play()). A stream also discards its queued data and restarts its
    // counters, ready to be fed again.
    void stop(VoiceId id);
    // Pauses or resumes a playing voice without rewinding.
    void setPaused(VoiceId id, bool paused);
    void setVolume(VoiceId id, float volume); // 0..1, linear
    // -1 (left) .. 0 (centre) .. 1 (right). A balance control: the centre
    // plays both channels at full volume, and moving away from it attenuates
    // the opposite channel linearly.
    void setPan(VoiceId id, float pan);
    // Position within the sample. Positions past the end wrap (modulo the
    // length). For streams, the position is counted from the first queued
    // frame and can only move within the data still buffered.
    void setPositionMs(VoiceId id, std::uint32_t ms);

    VoiceState state(VoiceId id) const;
    bool isPlaying(VoiceId id) const { return state(id) == VoiceState::Playing; }
    // Finished, or never valid.
    bool isDone(VoiceId id) const;
    std::uint32_t positionMs(VoiceId id) const;
    std::uint32_t lengthMs(VoiceId id) const; // streams: everything queued so far
    int loopsRemaining(VoiceId id) const;     // including the current one; 0 = forever
    float volume(VoiceId id) const;
    float pan(VoiceId id) const;

    // --- Streams --------------------------------------------------------------
    // Appends interleaved frames. False for non-stream voices or after endStream().
    bool queue(VoiceId id, std::span<const std::int16_t> pcm);
    // No more data: the voice becomes Done once it has played everything.
    void endStream(VoiceId id);
    std::size_t queuedFrames(VoiceId id) const;   // not yet played
    std::uint64_t playedFrames(VoiceId id) const; // source frames consumed since the start
    // Times a playing stream ran out of data before endStream(). While
    // starved it stays Playing, outputs silence and keeps its position.
    std::uint32_t underruns(VoiceId id) const;

    // --- Global -----------------------------------------------------------------
    // The game's master volume (0..1), ramped linearly over `rampMs`.
    void setMasterVolume(float volume, std::uint32_t rampMs = kDefaultRampMs);
    float masterVolume() const;
    // The player's volume setting (0..1), applied on top of the master volume.
    void setUserVolume(float volume);
    float userVolume() const;
    // Fades everything out and freezes all voices (positions included), e.g.
    // while the window is inactive; resuming fades back in.
    void setGlobalPause(bool paused);
    bool globalPaused() const;
    std::size_t voiceCount() const;

    // --- Output -------------------------------------------------------------------
    // Mixes the next out.size() / 2 frames of interleaved stereo float.
    // Called by an output device, or directly for offline rendering.
    void render(std::span<float> out);

private:
    struct Ramp {
        float value = 1, target = 1, step = 0;

        void set(float t, std::uint32_t frames);
        void jump(float t) { value = target = t; }
        float next();
    };

    struct Voice {
        std::uint16_t generation = 0;
        bool used = false;
        bool stream = false;
        bool ended = false;   // stream: endStream() called
        bool starved = false; // stream: out of data
        VoiceState state = VoiceState::Stopped;
        SamplePtr sample;
        std::vector<std::int16_t> buffer; // stream data, interleaved
        std::uint64_t dropped = 0;        // stream frames discarded from the front of buffer
        std::uint64_t queuedTotal = 0;    // stream frames ever queued
        std::uint32_t underruns = 0;
        int channels = 1;
        std::uint32_t rate = 0;
        // Position: source frame (samples: within the current loop; streams:
        // within buffer) plus a phase in 1/outputRate() units, advanced by
        // `rate` per output frame, so rate conversion has no drift.
        std::uint64_t frame = 0;
        std::uint32_t phase = 0;
        int loops = 1; // plays remaining including the current; 0 = forever
        std::uint32_t iteration = 0;
        float volume = 1, pan = 0;
        Ramp gainL, gainR, fade;
        float lastL = 0, lastR = 0; // last output, for de-clicking
    };

    Voice* find(VoiceId id);
    const Voice* find(VoiceId id) const;
    VoiceId allocate();
    void updateGains(Voice& v, bool immediate);
    // Fades the voice's last output to silence, from frame `at` of the block
    // being rendered (`out`, `blockFrames` long) on into the tail buffer.
    // Outside render(), out is null and blockFrames 0.
    void declick(Voice& v, float* out, std::size_t at, std::size_t blockFrames);
    void fadeIn(Voice& v);
    void finish(Voice& v, float* out, std::size_t at, std::size_t blockFrames);
    void mixVoice(Voice& v, float* out, std::size_t blockFrames);
    void renderBlock(float* out, std::size_t blockFrames);
    std::size_t frames(const Voice& v) const; // sample frames, or buffered stream frames

    const std::uint32_t m_rate;
    const std::uint32_t m_declickFrames;
    mutable std::mutex m_mutex;
    std::vector<Voice> m_voices;
    std::vector<std::uint32_t> m_free; // free slot indices
    Ramp m_master, m_user, m_pause;
    bool m_paused = false;
    std::vector<float> m_tail; // stereo ring of fading-out signal
    std::size_t m_tailPos = 0;
};

} // namespace brc::audio
