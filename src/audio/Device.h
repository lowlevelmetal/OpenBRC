#pragma once

#include "audio/Mixer.h"

#include <condition_variable>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

struct SDL_AudioStream;

namespace brc::audio {

// Something that pulls audio from a Mixer. Destroy it before the mixer.
class OutputDevice {
public:
    virtual ~OutputDevice() = default;
    virtual std::string_view name() const = 0;
};

// Plays the mixer on SDL's default playback device. SDL converts from the
// mixer's rate to the device's if they differ.
class SdlOutput final : public OutputDevice {
public:
    // Initialises SDL's audio subsystem (reference counted by SDL).
    static std::expected<std::unique_ptr<SdlOutput>, std::string> open(Mixer& mixer);
    ~SdlOutput() override;

    std::string_view name() const override { return m_name; }

private:
    explicit SdlOutput(Mixer& mixer) : m_mixer(mixer) {}
    static void feed(void* user, SDL_AudioStream* stream, int additional, int total);

    Mixer& m_mixer;
    SDL_AudioStream* m_stream = nullptr;
    std::vector<float> m_buffer;
    std::string m_name;
};

// No sound: renders the mixer in real time on its own thread and discards
// the result, so voices still advance, report positions and finish (the
// game waits for sounds to end). Used without an audio device and in tests.
class NullOutput final : public OutputDevice {
public:
    explicit NullOutput(Mixer& mixer, std::uint32_t periodMs = 10);
    ~NullOutput() override;

    std::string_view name() const override { return "null"; }

private:
    void run(std::stop_token stop);

    Mixer& m_mixer;
    std::uint32_t m_periodMs;
    std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::jthread m_thread;
};

// SdlOutput, or NullOutput (with a warning in the log) when no device opens.
std::unique_ptr<OutputDevice> openOutput(Mixer& mixer, bool allowDevice = true);

} // namespace brc::audio
