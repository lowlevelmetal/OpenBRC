#include "engine/SoundManager.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "engine/Engine.h"
#include "engine/Node.h"
#include "formats/Wave.h"

#include <algorithm>
#include <cstring>

namespace brc::engine {
namespace {

float milesVolume(int v) { return static_cast<float>(std::clamp(v, 0, 127)) / 127.0f; }
float milesPan(int p) { return std::clamp((static_cast<float>(std::clamp(p, 0, 127)) - 64.0f) / 63.0f, -1.0f, 1.0f); }

std::uint32_t le32(const std::vector<std::byte>& d, std::size_t at) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v |= std::to_integer<std::uint32_t>(d[at + static_cast<std::size_t>(i)]) << (8 * i);
    return v;
}

void put32(std::vector<std::byte>& d, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        d[at + static_cast<std::size_t>(i)] = static_cast<std::byte>(v >> (8 * i));
}

bool tagAt(const std::vector<std::byte>& d, std::size_t at, const char* tag) {
    return d.size() >= at + 4 && std::memcmp(d.data() + at, tag, 4) == 0;
}

} // namespace

// --- Sound -----------------------------------------------------------------------------

bool Sound::isDone() const { return m_mixer && m_mixer->state(m_voice) == audio::VoiceState::Done; }

bool Sound::isPlaying() const { return (m_flags & kPlaying) && !isDone() && !(m_flags & kPaused); }

int Sound::volume() const {
    return m_mixer ? static_cast<int>(m_mixer->volume(m_voice) * 127.0f + 0.5f) : -1;
}

void Sound::setVolume(int v) {
    if (m_mixer)
        m_mixer->setVolume(m_voice, milesVolume(v));
}

void Sound::setVolumePan(int volume, int pan) {
    if (!m_mixer)
        return;
    m_mixer->setVolume(m_voice, milesVolume(volume));
    m_mixer->setPan(m_voice, milesPan(pan));
}

// --- SoundManager ------------------------------------------------------------------------

SoundManager::SoundManager(Engine& engine, audio::Mixer* mixer) : m_engine(engine), m_mixer(mixer) {}

SoundManager::~SoundManager() { stopAll(true); }

Sound* SoundManager::findLoaded(std::string_view name) const {
    for (const auto& s : m_loaded)
        if (str::iequals(s->m_name, name))
            return s.get();
    return nullptr;
}

bool SoundManager::isPlaying(std::string_view name) const {
    const Sound* s = findLoaded(name);
    return s && s->isPlaying();
}

bool SoundManager::convertSampleRate(std::vector<std::byte>& d) {
    // 0x44fd10: only 11 kHz 8-bit mono with the data chunk right after "fmt ".
    if (d.size() < 0x2C || !tagAt(d, 0, "RIFF") || !tagAt(d, 8, "WAVE") || !tagAt(d, 12, "fmt "))
        return false;
    if (le32(d, 0x18) == 22050)
        return true;
    const std::size_t dataTag = 20 + le32(d, 16);
    if (!tagAt(d, dataTag, "data") || dataTag + 8 != 0x2C)
        return false; // "no 'data' tag" (also the 13 voice lines with a 'fact' chunk)
    const std::uint32_t n = le32(d, 0x28);
    if (n == 0 || 0x2C + std::size_t{n} > d.size())
        return false;
    std::vector<std::byte> out(d.size() + n);
    std::memcpy(out.data(), d.data(), 0x2C);
    put32(out, 4, le32(d, 4) + n);
    put32(out, 0x18, 22050);
    put32(out, 0x1C, 22050);
    put32(out, 0x28, n * 2);
    const auto in = [&](std::size_t i) { return std::to_integer<unsigned>(d[0x2C + i]); };
    auto* o = out.data() + 0x2C;
    o[0] = d[0x2C];
    for (std::size_t i = 0; i + 1 < n; ++i) {
        o[2 * i + 1] = d[0x2C + i];
        o[2 * i + 2] = static_cast<std::byte>((in(i) + in(i + 1)) >> 1);
    }
    o[2 * std::size_t{n} - 1] = d[0x2C + n - 1];
    d = std::move(out);
    return true;
}

bool SoundManager::load(Sound& s) {
    // Memory sound 0x44f840 / stream 0x44fb40. Streams are read whole too: the
    // handle trick has no visible behaviour, and they are never converted.
    auto found = m_engine.resources().find(formats::chunk::kWave, s.m_name);
    if (!found) {
        log::warn("Can't find sound {}.", s.m_name);
        return false;
    }
    auto bytes = m_engine.resources().read(found);
    if (bytes.empty()) {
        log::warn("Size is 0 for sound {}.", s.m_name);
        return false;
    }
    if (!s.m_stream)
        convertSampleRate(bytes); // its result is ignored, as in the original
    auto wave = formats::parseWave(bytes);
    if (!wave) {
        log::warn("Load error for sound {}, reason: {}.", s.m_name, wave.error());
        return false;
    }
    auto sample = audio::makeSample(std::move(wave->samples), wave->channels, wave->sampleRate);
    s.m_voice = m_mixer->createVoice(std::move(sample));
    s.m_mixer = m_mixer;
    return s.m_voice != audio::VoiceId::None;
}

void SoundManager::unload(Sound* s) {
    for (auto& q : m_channels)
        std::erase(q, s);
    if (m_mixer && s->m_voice != audio::VoiceId::None)
        m_mixer->destroyVoice(s->m_voice);
    m_loaded.remove_if([&](const auto& p) { return p.get() == s; });
}

Sound* SoundManager::getSound(std::string_view name, bool keep, bool stream) {
    if (!m_mixer)
        return nullptr;
    if (Sound* s = findLoaded(name)) {
        if (keep)
            ++s->m_uses;
        return s;
    }
    auto s = std::make_unique<Sound>();
    s->m_name = name;
    s->m_stream = stream;
    if (keep)
        ++s->m_uses;
    if (!load(*s))
        return nullptr;
    m_loaded.push_back(std::move(s));
    return m_loaded.back().get();
}

bool SoundManager::play(Sound& s) {
    // Memory 0x44f700 / stream 0x44fa90.
    if (s.m_voice == audio::VoiceId::None)
        return false;
    if (s.m_stream) {
        if (s.m_nextVolume != -1)
            m_mixer->setVolume(s.m_voice, milesVolume(s.m_nextVolume));
        if (s.m_nextPan != -1)
            m_mixer->setPan(s.m_voice, milesPan(s.m_nextPan));
    } else if (s.m_nextVolume != -1) {
        s.setVolumePan(s.m_nextVolume, s.m_nextPan == -1 ? 63 : s.m_nextPan);
    }
    m_mixer->setPositionMs(s.m_voice, static_cast<std::uint32_t>(std::max<int>(0, s.m_startMs)));
    m_mixer->play(s.m_voice, s.m_loops);
    ++s.m_uses;
    s.m_flags = (s.m_flags & ~(Sound::kStopped | Sound::kNotified)) | Sound::kPlaying;
    return true;
}

void SoundManager::stop(Sound& s) {
    // 0x44f7b0: the first stop tells the current node (completed = it ran to its end).
    if (!(s.m_flags & Sound::kNotified)) {
        s.m_flags |= Sound::kNotified;
        const bool completed = (s.m_flags & Sound::kPlaying) && s.isDone();
        if (s.m_flags & Sound::kPlaying)
            --s.m_uses;
        if (Node* node = m_engine.currentNode())
            node->soundDone(s.m_name, &s, completed);
    }
    if (m_mixer && s.m_voice != audio::VoiceId::None)
        m_mixer->stop(s.m_voice);
    s.m_flags = (s.m_flags & ~Sound::kPlaying) | Sound::kStopped;
}

void SoundManager::pause(Sound& s) {
    if (s.m_flags & Sound::kPaused)
        return;
    s.m_flags |= Sound::kPaused;
    m_mixer->setPaused(s.m_voice, true);
}

void SoundManager::resume(Sound& s) {
    if (!(s.m_flags & Sound::kPaused))
        return;
    s.m_flags &= ~Sound::kPaused;
    m_mixer->setPaused(s.m_voice, false);
}

void SoundManager::update(Sound& s) {
    // vt+0x04 (0x44f6a0).
    if ((s.m_flags & Sound::kPlaying) && !(s.m_flags & Sound::kStopped) && s.isDone() &&
        !(s.m_flags & Sound::kPaused))
        stop(s);
}

bool SoundManager::playSound(std::string_view name, int channel, bool queue, bool preempt, int loops, bool keep,
                             int volume, int pan, int startMs, bool stream) {
    // 0x44c3c0.
    Sound* s = getSound(name, keep, stream);
    if (!s)
        return false;
    if (queue && preempt) {
        log::info("You can't both preempt AND queue a sound! Queuing sound ({})", name);
        preempt = false;
    }
    if (!queue && s->isPlaying()) {
        ++s->m_uses;
        stop(*s);
        service(true);
        --s->m_uses;
    }
    if (channel == -1) {
        for (int c = 0; c < 16; ++c) {
            if (m_channels[static_cast<std::size_t>(c)].empty()) {
                channel = c;
                break;
            }
        }
        if (channel == -1) {
            if (!queue && !preempt) {
                log::info("No free sound channels for sound {}. Try QueueSound or ForceSound to force playback.", name);
                return false;
            }
            channel = 15;
            log::info("No free sound channels to {} sound {}. Defaulting to channel {}", queue ? "queue" : "preempt",
                      name, channel);
        }
    }
    if (channel < 0 || channel > 15)
        return false;
    s->m_loops = loops;
    s->m_nextVolume = static_cast<std::int16_t>(volume);
    s->m_nextPan = static_cast<std::int16_t>(pan);
    s->m_startMs = static_cast<std::int16_t>(startMs);
    auto& q = m_channels[static_cast<std::size_t>(channel)];
    if (!queue && !q.empty()) {
        if (!preempt) {
            log::info("Channel {} not free for sound {}. Try QueueSound or ForceSound to force playback.", channel,
                      name);
            return false;
        }
        stopChannel(channel);
        q.push_front(s);
    } else {
        q.push_back(s);
    }
    ++s->m_uses;
    service(false);
    return true;
}

void SoundManager::service(bool full) {
    // 0x44c990.
    for (auto& q : m_channels) {
        if (q.empty())
            continue;
        Sound* head = q.front();
        if (!(head->m_flags & (Sound::kPlaying | Sound::kStopped))) {
            --head->m_uses;
            play(*head);
        }
        if (full) {
            update(*head);
            if (!q.empty() && q.front() == head && (head->m_flags & Sound::kStopped)) {
                head->m_flags &= ~(Sound::kPlaying | Sound::kStopped);
                q.pop_front();
            }
        }
    }
    if (full) {
        std::vector<Sound*> dead;
        for (const auto& s : m_loaded)
            if (s->m_uses == 0)
                dead.push_back(s.get());
        for (Sound* s : dead)
            unload(s);
    }
}

Sound* SoundManager::channelSound(int channel) const {
    if (channel < 0 || channel > 15 || m_channels[static_cast<std::size_t>(channel)].empty())
        return nullptr;
    return m_channels[static_cast<std::size_t>(channel)].front();
}

bool SoundManager::channelBusy(int channel) const {
    if (channel < 0 || channel > 15)
        return false;
    const auto& q = m_channels[static_cast<std::size_t>(channel)];
    if (q.empty())
        return false;
    const Sound* h = q.front();
    if (h->isPlaying() || !(h->m_flags & (Sound::kPlaying | Sound::kStopped)))
        return true;
    return q.size() > 1 && ((h->m_flags & Sound::kStopped) || h->isDone());
}

void SoundManager::stopChannel(int channel) {
    if (channel < 0 || channel > 15)
        return;
    for (Sound* s : std::deque<Sound*>(m_channels[static_cast<std::size_t>(channel)]))
        stop(*s);
}

void SoundManager::stopChannelHead(int channel) {
    if (Sound* s = channelSound(static_cast<std::int16_t>(channel)))
        stop(*s);
}

void SoundManager::stopSound(std::string_view name) {
    if (Sound* s = findLoaded(name))
        stop(*s);
}

void SoundManager::stopSound(Sound* sound) {
    if (sound)
        stop(*sound);
}

void SoundManager::stopAll(bool purge) {
    for (int c = 0; c < 16; ++c)
        if (Sound* s = channelSound(c))
            stop(*s);
    if (purge) {
        for (auto& q : m_channels)
            q.clear();
        for (const auto& s : m_loaded)
            if (m_mixer && s->m_voice != audio::VoiceId::None)
                m_mixer->destroyVoice(s->m_voice);
        m_loaded.clear();
    }
}

void SoundManager::pauseAll() {
    for (int c = 0; c < 16; ++c)
        if (Sound* s = channelSound(c))
            pause(*s);
}

void SoundManager::resumeAll() {
    for (int c = 0; c < 16; ++c)
        if (Sound* s = channelSound(c))
            resume(*s);
}

void SoundManager::retain(std::string_view name, bool add) {
    if (Sound* s = findLoaded(name))
        s->m_uses += add ? 1 : -1;
}

void SoundManager::setMasterVolume(int volume) {
    m_master = std::clamp(volume, 0, 127);
    if (m_mixer)
        m_mixer->setMasterVolume(static_cast<float>(m_master) / 127.0f, 0);
}

} // namespace brc::engine
