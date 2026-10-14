// Software mixer, adapted from the maintainer's BarbieRecomp mixer
// (runtime/audio.c): same voice model (samples and streams, loop counts with
// 0 = forever, balance pan), with fixed-point positions, cubic resampling and
// de-clicking added.
#include "audio/Mixer.h"

#include <algorithm>
#include <cmath>

namespace brc::audio {
namespace {

constexpr float kFromInt16 = 1.0f / 32768.0f;
constexpr std::uint64_t kStreamHistory = 4;    // frames kept before a stream's read position
constexpr std::uint64_t kStreamCompact = 8192; // drop consumed stream data past this many frames
constexpr std::size_t kBlockFrames = 256;      // global pause is checked this often

std::uint32_t slotOf(VoiceId id) {
    return (static_cast<std::uint32_t>(id) & 0xffffu) - 1;
}
std::uint16_t generationOf(VoiceId id) {
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(id) >> 16);
}

// 4-point Catmull-Rom spline through p1 (t = 0) and p2 (t = 1).
float cubic(float p0, float p1, float p2, float p3, float t) {
    return p1 +
           0.5f * t *
               (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
}

float clamp01(float v) {
    return std::isnan(v) ? 0.0f : std::clamp(v, 0.0f, 1.0f);
}

} // namespace

void Mixer::Ramp::set(float t, std::uint32_t frames) {
    target = t;
    step = frames ? std::abs(t - value) / static_cast<float>(frames) : 0.0f;
    if (step <= 0.0f)
        value = t;
}

float Mixer::Ramp::next() {
    if (value != target)
        value = value < target ? std::min(target, value + step) : std::max(target, value - step);
    return value;
}

Mixer::Mixer(std::uint32_t outputRate)
    : m_rate(std::max<std::uint32_t>(outputRate, 1000)),
      m_declickFrames(std::max<std::uint32_t>(m_rate / 200, 1)),
      m_tail(std::size_t{m_declickFrames} * 2, 0.0f) {}

Mixer::~Mixer() = default;

Mixer::Voice* Mixer::find(VoiceId id) {
    const std::uint32_t slot = slotOf(id);
    if (id == VoiceId::None || slot >= m_voices.size())
        return nullptr;
    Voice& v = m_voices[slot];
    return v.used && v.generation == generationOf(id) ? &v : nullptr;
}

const Mixer::Voice* Mixer::find(VoiceId id) const {
    return const_cast<Mixer*>(this)->find(id);
}

VoiceId Mixer::allocate() {
    std::uint32_t slot;
    if (!m_free.empty()) {
        slot = m_free.back();
        m_free.pop_back();
    } else if (m_voices.size() < kMaxVoices) {
        slot = static_cast<std::uint32_t>(m_voices.size());
        m_voices.emplace_back();
    } else {
        return VoiceId::None;
    }
    Voice& v = m_voices[slot];
    std::uint16_t generation = static_cast<std::uint16_t>(v.generation + 1);
    if (generation == 0)
        generation = 1;
    v = Voice{};
    v.generation = generation;
    v.used = true;
    return static_cast<VoiceId>(std::uint32_t{generation} << 16 | (slot + 1));
}

std::size_t Mixer::frames(const Voice& v) const {
    if (v.stream)
        return v.buffer.size() / static_cast<std::size_t>(v.channels);
    return v.sample->frames();
}

void Mixer::updateGains(Voice& v, bool immediate) {
    const float l = v.volume * (v.pan > 0 ? 1.0f - v.pan : 1.0f);
    const float r = v.volume * (v.pan < 0 ? 1.0f + v.pan : 1.0f);
    v.gainL.set(l, immediate ? 0 : m_declickFrames);
    v.gainR.set(r, immediate ? 0 : m_declickFrames);
}

void Mixer::declick(Voice& v, float* out, std::size_t at, std::size_t blockFrames) {
    if (std::abs(v.lastL) + std::abs(v.lastR) > 1e-6f) {
        const std::size_t n = m_declickFrames;
        for (std::size_t k = 0; k < n; ++k) {
            const float w = 1.0f - static_cast<float>(k + 1) / static_cast<float>(n);
            const std::size_t f = at + k;
            float* dst = f < blockFrames ? out + f * 2 : &m_tail[((m_tailPos + f - blockFrames) % n) * 2];
            dst[0] += v.lastL * w;
            dst[1] += v.lastR * w;
        }
    }
    v.lastL = v.lastR = 0;
}

void Mixer::fadeIn(Voice& v) {
    v.fade.value = 0;
    v.fade.set(1, m_declickFrames);
}

void Mixer::finish(Voice& v, float* out, std::size_t at, std::size_t blockFrames) {
    v.state = VoiceState::Done;
    declick(v, out, at, blockFrames);
    if (!v.stream) {
        v.frame = 0;
        v.phase = 0;
        v.iteration = 0;
    }
}

VoiceId Mixer::createVoice(SamplePtr sample) {
    if (!sample || sample->channels < 1 || sample->channels > 2 || sample->rate == 0)
        return VoiceId::None;
    std::lock_guard lock(m_mutex);
    const VoiceId id = allocate();
    if (Voice* v = find(id)) {
        v->channels = sample->channels;
        v->rate = sample->rate;
        v->sample = std::move(sample);
        updateGains(*v, true);
    }
    return id;
}

VoiceId Mixer::createStream(int channels, std::uint32_t rate) {
    if (channels < 1 || channels > 2 || rate == 0)
        return VoiceId::None;
    std::lock_guard lock(m_mutex);
    const VoiceId id = allocate();
    if (Voice* v = find(id)) {
        v->stream = true;
        v->channels = channels;
        v->rate = rate;
        updateGains(*v, true);
    }
    return id;
}

void Mixer::destroyVoice(VoiceId id) {
    std::lock_guard lock(m_mutex);
    Voice* v = find(id);
    if (!v)
        return;
    if (v->state == VoiceState::Playing)
        declick(*v, nullptr, 0, 0);
    const std::uint16_t generation = v->generation;
    *v = Voice{};
    v->generation = generation;
    m_free.push_back(slotOf(id));
}

void Mixer::play(VoiceId id, int loops) {
    std::lock_guard lock(m_mutex);
    Voice* v = find(id);
    if (!v)
        return;
    v->loops = std::max(loops, 0);
    if (v->state == VoiceState::Playing)
        return;
    if (v->state == VoiceState::Done && !v->stream) {
        v->frame = 0;
        v->phase = 0;
        v->iteration = 0;
    }
    updateGains(*v, true);
    if (v->frame != 0 || v->phase != 0)
        fadeIn(*v);
    else
        v->fade.jump(1);
    v->state = VoiceState::Playing;
    if (!v->stream && frames(*v) == 0)
        v->state = VoiceState::Done;
}

void Mixer::stop(VoiceId id) {
    std::lock_guard lock(m_mutex);
    Voice* v = find(id);
    if (!v)
        return;
    if (v->state == VoiceState::Playing)
        declick(*v, nullptr, 0, 0);
    v->state = VoiceState::Stopped;
    v->frame = 0;
    v->phase = 0;
    v->iteration = 0;
    if (v->stream) {
        v->buffer.clear();
        v->dropped = v->queuedTotal = 0;
        v->ended = v->starved = false;
    }
}

void Mixer::setPaused(VoiceId id, bool paused) {
    std::lock_guard lock(m_mutex);
    Voice* v = find(id);
    if (!v)
        return;
    if (paused && v->state == VoiceState::Playing) {
        declick(*v, nullptr, 0, 0);
        v->state = VoiceState::Paused;
    } else if (!paused && v->state == VoiceState::Paused) {
        fadeIn(*v);
        v->state = VoiceState::Playing;
    }
}

void Mixer::setVolume(VoiceId id, float volume) {
    std::lock_guard lock(m_mutex);
    if (Voice* v = find(id)) {
        v->volume = clamp01(volume);
        updateGains(*v, v->state != VoiceState::Playing);
    }
}

void Mixer::setPan(VoiceId id, float pan) {
    std::lock_guard lock(m_mutex);
    if (Voice* v = find(id)) {
        v->pan = std::isnan(pan) ? 0.0f : std::clamp(pan, -1.0f, 1.0f);
        updateGains(*v, v->state != VoiceState::Playing);
    }
}

void Mixer::setPositionMs(VoiceId id, std::uint32_t ms) {
    std::lock_guard lock(m_mutex);
    Voice* v = find(id);
    if (!v)
        return;
    std::uint64_t frame = std::uint64_t{ms} * v->rate / 1000;
    const std::uint64_t n = frames(*v);
    if (!v->stream) {
        frame = n ? frame % n : 0;
    } else {
        frame = std::clamp(frame, v->dropped, v->dropped + n) - v->dropped;
    }
    if (v->state == VoiceState::Playing && (frame != v->frame || v->phase != 0)) {
        declick(*v, nullptr, 0, 0);
        fadeIn(*v);
    }
    v->frame = frame;
    v->phase = 0;
}

VoiceState Mixer::state(VoiceId id) const {
    std::lock_guard lock(m_mutex);
    const Voice* v = find(id);
    return v ? v->state : VoiceState::Invalid;
}

bool Mixer::isDone(VoiceId id) const {
    const VoiceState s = state(id);
    return s == VoiceState::Done || s == VoiceState::Invalid;
}

std::uint32_t Mixer::positionMs(VoiceId id) const {
    std::lock_guard lock(m_mutex);
    const Voice* v = find(id);
    if (!v)
        return 0;
    const std::uint64_t frame = (v->stream ? v->dropped : 0) + v->frame;
    return static_cast<std::uint32_t>(frame * 1000 / v->rate);
}

std::uint32_t Mixer::lengthMs(VoiceId id) const {
    std::lock_guard lock(m_mutex);
    const Voice* v = find(id);
    if (!v)
        return 0;
    const std::uint64_t n = v->stream ? v->queuedTotal : frames(*v);
    return static_cast<std::uint32_t>(n * 1000 / v->rate);
}

int Mixer::loopsRemaining(VoiceId id) const {
    std::lock_guard lock(m_mutex);
    const Voice* v = find(id);
    return v ? v->loops : 0;
}

float Mixer::volume(VoiceId id) const {
    std::lock_guard lock(m_mutex);
    const Voice* v = find(id);
    return v ? v->volume : 0.0f;
}

float Mixer::pan(VoiceId id) const {
    std::lock_guard lock(m_mutex);
    const Voice* v = find(id);
    return v ? v->pan : 0.0f;
}

bool Mixer::queue(VoiceId id, std::span<const std::int16_t> pcm) {
    std::lock_guard lock(m_mutex);
    Voice* v = find(id);
    if (!v || !v->stream || v->ended)
        return false;
    const std::size_t ch = static_cast<std::size_t>(v->channels);
    const std::size_t n = pcm.size() / ch;
    v->buffer.insert(v->buffer.end(), pcm.begin(), pcm.begin() + static_cast<std::ptrdiff_t>(n * ch));
    v->queuedTotal += n;
    return true;
}

void Mixer::endStream(VoiceId id) {
    std::lock_guard lock(m_mutex);
    if (Voice* v = find(id); v && v->stream)
        v->ended = true;
}

std::size_t Mixer::queuedFrames(VoiceId id) const {
    std::lock_guard lock(m_mutex);
    const Voice* v = find(id);
    if (!v || !v->stream)
        return 0;
    const std::uint64_t n = frames(*v);
    return static_cast<std::size_t>(n > v->frame ? n - v->frame : 0);
}

std::uint64_t Mixer::playedFrames(VoiceId id) const {
    std::lock_guard lock(m_mutex);
    const Voice* v = find(id);
    if (!v)
        return 0;
    return (v->stream ? v->dropped : 0) + v->frame;
}

std::uint32_t Mixer::underruns(VoiceId id) const {
    std::lock_guard lock(m_mutex);
    const Voice* v = find(id);
    return v ? v->underruns : 0;
}

void Mixer::setMasterVolume(float volume, std::uint32_t rampMs) {
    std::lock_guard lock(m_mutex);
    m_master.set(clamp01(volume), static_cast<std::uint32_t>(std::uint64_t{rampMs} * m_rate / 1000));
}

float Mixer::masterVolume() const {
    std::lock_guard lock(m_mutex);
    return m_master.target;
}

void Mixer::setUserVolume(float volume) {
    std::lock_guard lock(m_mutex);
    m_user.set(clamp01(volume), kDefaultRampMs * m_rate / 1000);
}

float Mixer::userVolume() const {
    std::lock_guard lock(m_mutex);
    return m_user.target;
}

void Mixer::setGlobalPause(bool paused) {
    std::lock_guard lock(m_mutex);
    m_paused = paused;
    m_pause.set(paused ? 0.0f : 1.0f, m_declickFrames * 2);
}

bool Mixer::globalPaused() const {
    std::lock_guard lock(m_mutex);
    return m_paused;
}

std::size_t Mixer::voiceCount() const {
    std::lock_guard lock(m_mutex);
    return m_voices.size() - m_free.size();
}

void Mixer::mixVoice(Voice& v, float* out, std::size_t blockFrames) {
    const std::size_t ch = static_cast<std::size_t>(v.channels);
    const std::int16_t* pcm = v.stream ? v.buffer.data() : v.sample->pcm.data();
    const auto total = static_cast<std::int64_t>(frames(v));
    const float invRate = 1.0f / static_cast<float>(m_rate);

    // Source sample `i` (relative to the current loop or stream buffer) of channel `c`.
    auto at = [&](std::int64_t i, std::size_t c) -> float {
        if (i < 0) {
            if (v.stream || v.iteration == 0)
                return 0.0f;
            i += total; // the end of the previous loop
            if (i < 0)
                return 0.0f;
        } else if (i >= total) {
            if (v.stream || v.loops == 1)
                return 0.0f;
            i %= total; // the start of the next loop
        }
        return static_cast<float>(pcm[static_cast<std::size_t>(i) * ch + c]);
    };

    for (std::size_t n = 0; n < blockFrames; ++n) {
        const auto i = static_cast<std::int64_t>(v.frame);
        if (v.stream) {
            if (!v.ended && i + 2 >= total) {
                if (!v.starved) {
                    v.starved = true;
                    if (v.dropped + static_cast<std::uint64_t>(i) > 0)
                        ++v.underruns;
                    declick(v, out, n, blockFrames);
                }
                break;
            }
            if (v.ended && i >= total) {
                finish(v, out, n, blockFrames);
                break;
            }
            if (v.starved) {
                v.starved = false;
                fadeIn(v);
            }
        }
        const float t = static_cast<float>(v.phase) * invRate;
        const float l = cubic(at(i - 1, 0), at(i, 0), at(i + 1, 0), at(i + 2, 0), t);
        const float r = ch == 2 ? cubic(at(i - 1, 1), at(i, 1), at(i + 1, 1), at(i + 2, 1), t) : l;
        const float fade = v.fade.next() * kFromInt16;
        v.lastL = l * v.gainL.next() * fade;
        v.lastR = r * v.gainR.next() * fade;
        out[n * 2] += v.lastL;
        out[n * 2 + 1] += v.lastR;
        v.phase += v.rate;
        if (v.phase >= m_rate) {
            v.frame += v.phase / m_rate;
            v.phase %= m_rate;
        }
        if (!v.stream) {
            while (static_cast<std::int64_t>(v.frame) >= total) {
                if (v.loops == 1) {
                    finish(v, out, n + 1, blockFrames);
                    return;
                }
                if (v.loops > 1)
                    --v.loops;
                ++v.iteration;
                v.frame -= static_cast<std::uint64_t>(total);
            }
        }
    }

    if (v.stream) {
        if (v.frame > kStreamCompact + kStreamHistory) {
            const std::uint64_t drop = v.frame - kStreamHistory;
            v.buffer.erase(v.buffer.begin(), v.buffer.begin() + static_cast<std::ptrdiff_t>(drop * ch));
            v.frame -= drop;
            v.dropped += drop;
        }
    }
}

void Mixer::render(std::span<float> out) {
    std::lock_guard lock(m_mutex);
    const std::size_t total = out.size() / 2;
    for (std::size_t done = 0; done < total; done += kBlockFrames)
        renderBlock(out.data() + done * 2, std::min(kBlockFrames, total - done));
    if (out.size() % 2)
        out.back() = 0.0f;
}

void Mixer::renderBlock(float* out, std::size_t blockFrames) {
    std::fill(out, out + blockFrames * 2, 0.0f);
    if (m_paused && m_pause.value <= 0.0f) {
        std::fill(m_tail.begin(), m_tail.end(), 0.0f);
        return; // frozen: voices keep their positions
    }

    // Fading tails queued since the last block.
    const std::size_t n = m_declickFrames;
    for (std::size_t f = 0; f < std::min(blockFrames, n); ++f) {
        float* t = &m_tail[((m_tailPos + f) % n) * 2];
        out[f * 2] += t[0];
        out[f * 2 + 1] += t[1];
        t[0] = t[1] = 0.0f;
    }
    m_tailPos = (m_tailPos + blockFrames) % n;

    for (Voice& v : m_voices)
        if (v.used && v.state == VoiceState::Playing)
            mixVoice(v, out, blockFrames);

    for (std::size_t f = 0; f < blockFrames; ++f) {
        const float g = m_master.next() * m_user.next() * m_pause.next();
        out[f * 2] = std::clamp(out[f * 2] * g, -1.0f, 1.0f);
        out[f * 2 + 1] = std::clamp(out[f * 2 + 1] * g, -1.0f, 1.0f);
    }
}

} // namespace brc::audio
