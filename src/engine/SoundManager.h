#pragma once

#include "audio/Mixer.h"

#include <array>
#include <cstdint>
#include <deque>
#include <list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace brc::engine {

class Engine;

// One loaded sound (a WAVE chunk), as the HC sound manager keeps it: one
// object per name, reference counted, played through a mixer voice with the
// Miles "quick" semantics. See docs/engine/sound.md §2.
class Sound {
public:
    enum Flags : std::uint32_t { kPlaying = 1, kStopped = 2, kPaused = 4, kNotified = 8 };

    const std::string& name() const { return m_name; }
    bool stoppedFlag() const { return (m_flags & kStopped) != 0; }
    std::uint32_t flags() const { return m_flags; }
    int useCount() const { return m_uses; }
    bool isStream() const { return m_stream; }

    bool isPlaying() const; // vt+0x08
    bool isDone() const;    // vt+0x24: played to the end (after its last loop)
    int volume() const;     // vt+0x0c, 0..127
    void setVolume(int v);  // vt+0x10: affects the playing sound
    // AIL_quick_set_volume on the handle: persists for later plays.
    void setVolumePan(int volume, int pan);

private:
    friend class SoundManager;

    audio::Mixer* m_mixer = nullptr;
    std::string m_name;
    bool m_stream = false;
    int m_uses = 0;
    std::int16_t m_nextVolume = -1, m_nextPan = -1, m_startMs = 0;
    int m_loops = 1;
    std::uint32_t m_flags = 0;
    audio::VoiceId m_voice = audio::VoiceId::None;
};

// The HC sound manager (HCApplication.cpp): 16 FIFO channels of named sounds,
// use counts, completion events through the current node, and a service pass
// every main-loop iteration. See docs/engine/sound.md.
class SoundManager {
public:
    SoundManager(Engine& engine, audio::Mixer* mixer); // no mixer: sound off
    ~SoundManager();

    bool enabled() const { return m_mixer != nullptr; }

    // 0x44c3c0 PlaySound(name, channel, queue, preempt, loops, keep, volume,
    // pan, startMs, stream): 1 on success. channel -1 = first free; loops
    // 0 = forever; volume/pan 0..127 or -1 = leave as is.
    bool playSound(std::string_view name, int channel = -1, bool queue = false, bool preempt = false, int loops = 1,
                   bool keep = false, int volume = -1, int pan = -1, int startMs = 0, bool stream = false);
    Sound* getSound(std::string_view name, bool keep, bool stream); // 0x44c180
    Sound* findLoaded(std::string_view name) const;                 // 0x44c930
    bool isPlaying(std::string_view name) const;                    // 0x44cac0

    void service(bool full); // 0x44c990, once per main-loop pass with full = true

    Sound* channelSound(int channel) const; // 0x44c900: head of the queue
    bool channelBusy(int channel) const;    // 0x44ca50
    void stopChannel(int channel);          // 0x44c740: every queued sound
    void stopChannelHead(int channel);      // script stopChannel
    void stopSound(std::string_view name);  // 0x44c700
    void stopSound(Sound* sound);           // 0x44c720
    void stopAll(bool purge);               // 0x44c780
    void pauseAll();                        // 0x44c850
    void resumeAll();                       // 0x44c880
    void retain(std::string_view name, bool add); // 0x44c8b0

    // Miles digital master volume, 0..127 (127 at start-up).
    int masterVolume() const { return m_master; }
    void setMasterVolume(int volume);

    // 0x44fd10 ConvertSoundSampleRate(…, 22050) on a RIFF image, as the
    // original applies it to memory sounds. Returns false (leaving the data
    // untouched) when the original fails or has nothing to do.
    static bool convertSampleRate(std::vector<std::byte>& riff);

private:
    bool play(Sound& s);   // vt+0x1c
    void stop(Sound& s);   // vt+0x20, sends SoundDone
    void pause(Sound& s);  // vt+0x14
    void resume(Sound& s); // vt+0x18
    void update(Sound& s); // vt+0x04
    bool load(Sound& s);   // vt+0x28
    void unload(Sound* s);

    Engine& m_engine;
    audio::Mixer* m_mixer;
    std::list<std::unique_ptr<Sound>> m_loaded;
    std::array<std::deque<Sound*>, 16> m_channels;
    int m_master = 127;
};

} // namespace brc::engine
