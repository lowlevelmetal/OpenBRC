#include "audio/Device.h"

#include "core/Log.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <format>

namespace brc::audio {
namespace {

constexpr std::size_t kChunkFrames = 1024;

} // namespace

std::expected<std::unique_ptr<SdlOutput>, std::string> SdlOutput::open(Mixer& mixer) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        return std::unexpected(std::format("SDL audio: {}", SDL_GetError()));
    std::unique_ptr<SdlOutput> out(new SdlOutput(mixer));
    out->m_buffer.resize(kChunkFrames * 2);
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, static_cast<int>(mixer.outputRate())};
    out->m_stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &SdlOutput::feed, out.get());
    if (!out->m_stream) {
        std::string error = std::format("SDL audio: cannot open the playback device: {}", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return std::unexpected(std::move(error));
    }
    const char* driver = SDL_GetCurrentAudioDriver();
    SDL_AudioSpec device{};
    int frames = 0;
    SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(out->m_stream), &device, &frames);
    out->m_name = std::format("{} ({} Hz, {} channels, {} frames)", driver ? driver : "SDL", device.freq,
                              device.channels, frames);
    if (!SDL_ResumeAudioStreamDevice(out->m_stream))
        log::warn("SDL audio: cannot start the device: {}", SDL_GetError());
    log::info("audio: {}", out->m_name);
    return out;
}

SdlOutput::~SdlOutput() {
    if (m_stream) {
        SDL_DestroyAudioStream(m_stream); // stops the callback before returning
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

// SDL's audio thread asks for `additional` more bytes.
void SdlOutput::feed(void* user, SDL_AudioStream* stream, int additional, int /*total*/) {
    auto* self = static_cast<SdlOutput*>(user);
    auto frames = static_cast<std::size_t>(additional > 0 ? additional : 0) / (sizeof(float) * 2);
    while (frames > 0) {
        const std::size_t n = std::min(frames, kChunkFrames);
        const std::span<float> chunk(self->m_buffer.data(), n * 2);
        self->m_mixer.render(chunk);
        SDL_PutAudioStreamData(stream, chunk.data(), static_cast<int>(chunk.size_bytes()));
        frames -= n;
    }
}

NullOutput::NullOutput(Mixer& mixer, std::uint32_t periodMs)
    : m_mixer(mixer), m_periodMs(std::max<std::uint32_t>(periodMs, 1)),
      m_thread([this](std::stop_token stop) { run(stop); }) {}

NullOutput::~NullOutput() {
    m_thread.request_stop();
    m_wake.notify_all();
}

void NullOutput::run(std::stop_token stop) {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    std::uint64_t rendered = 0;
    std::vector<float> buffer(kChunkFrames * 2);
    std::unique_lock lock(m_mutex);
    while (!stop.stop_requested()) {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
        const std::uint64_t due = static_cast<std::uint64_t>(elapsed) * m_mixer.outputRate() / 1'000'000;
        // After a stall (a debugger, a suspended process) catch up by 250 ms at most.
        rendered = std::max(rendered, due - std::min<std::uint64_t>(due, m_mixer.outputRate() / 4));
        while (rendered < due) {
            const std::size_t n =
                static_cast<std::size_t>(std::min<std::uint64_t>(due - rendered, kChunkFrames));
            m_mixer.render(std::span(buffer.data(), n * 2));
            rendered += n;
        }
        m_wake.wait_for(lock, stop, std::chrono::milliseconds(m_periodMs), [] { return false; });
    }
}

std::unique_ptr<OutputDevice> openOutput(Mixer& mixer, bool allowDevice) {
    if (allowDevice) {
        auto sdl = SdlOutput::open(mixer);
        if (sdl)
            return std::move(*sdl);
        log::warn("{}; continuing without sound", sdl.error());
    }
    return std::make_unique<NullOutput>(mixer);
}

} // namespace brc::audio
