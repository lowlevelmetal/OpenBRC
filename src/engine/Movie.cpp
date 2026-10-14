#include "engine/Movie.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "engine/Engine.h"
#include "engine/Node.h"

#include <algorithm>
#include <cmath>

namespace brc::engine {
namespace {

std::string resourceFile(std::string_view path) {
    std::string p(path);
    std::ranges::replace(p, '\\', '/');
    if (str::istartsWith(p, "resources/"))
        return str::lower(p);
    return resourcePath(p);
}

std::string baseName(std::string_view path) {
    const auto slash = path.find_last_of("\\/");
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

// One timestamp for all Godiva Smacker players ([0x809f5c]).
std::uint32_t s_smackMovieLast = 0;

} // namespace

// --- MovieBase ---------------------------------------------------------------------------

MovieBase::MovieBase(Engine& engine) : Task(engine.tasks()), m_engine(engine) {}

MovieBase::~MovieBase() {
    if (m_voice != audio::VoiceId::None && m_engine.mixer())
        m_engine.mixer()->destroyVoice(m_voice);
}

void MovieBase::setPaused(bool p) {
    if (p == m_paused)
        return;
    const std::uint32_t now = m_engine.ticks();
    if (p)
        m_pausedAt = now;
    else
        m_nextDue += now - m_pausedAt; // time spent paused does not count
    m_paused = p;
    if (m_voice != audio::VoiceId::None && m_engine.mixer())
        m_engine.mixer()->setPaused(m_voice, p);
}

bool MovieBase::openFile(std::string_view path, bool colorKey, std::string_view what) {
    m_name = path;
    auto file = m_engine.resources().openFile(resourceFile(path));
    if (!file) {
        log::warn("Error loading {} {}: couldn't find!", what, path);
        return false;
    }
    auto smk = formats::Smacker::open(file);
    if (!smk) {
        log::warn("Error getting Smacker info for {} {}! ({})", what, path, smk.error());
        return false;
    }
    m_smk = std::make_unique<formats::Smacker>(std::move(*smk));
    m_world = Image8(static_cast<int>(m_smk->width()), static_cast<int>(m_smk->height()), 0);
    m_world.key = -1;
    if (colorKey) {
        // The RGB(255,0,255) key: the screen palette's magenta entry, if any.
        const auto& pal = m_engine.screenPalette();
        for (int i = 255; i >= 0; --i)
            if ((pal[static_cast<std::size_t>(i)] & 0xFFFFFF) == 0xFF00FF)
                m_world.key = i;
    }
    m_smk->setTarget(m_world.pixels, static_cast<std::size_t>(m_world.width));
    m_frameMs = static_cast<double>(m_smk->frameDurationUs()) / 1000.0;
    m_done = false;
    // All audio tracks are requested; the disc's movies only use track 0.
    if (audio::Mixer* mixer = m_engine.mixer(); mixer && m_smk->audioTrack(0).decodable()) {
        const auto& t = m_smk->audioTrack(0);
        m_voice = mixer->createStream(t.channels(), t.sampleRate);
        m_smk->setAudioTracks(1);
    } else {
        m_smk->setAudioTracks(0);
    }
    return true;
}

bool MovieBase::decodeCurrent() {
    auto r = m_smk->decodeFrame();
    if (!r) {
        log::warn("movie {}: {}", m_name, r.error());
        return false;
    }
    if (m_voice != audio::VoiceId::None) {
        auto pcm = m_smk->audio(0);
        if (!pcm.empty()) {
            m_engine.mixer()->queue(m_voice, pcm);
            if (m_engine.mixer()->state(m_voice) == audio::VoiceState::Stopped)
                m_engine.mixer()->play(m_voice, 1);
        }
    }
    markChanged();
    return true;
}

void MovieBase::applyPalette(bool fullScreenOnly) {
    if (!m_smk->paletteChanged())
        return;
    if (fullScreenOnly && (m_world.width != 640 || m_world.height != 480))
        return; // a world palette: the node's palette already matches (movies.md §5)
    std::array<std::uint32_t, 256> rgb{};
    const auto& p = m_smk->palette();
    for (std::size_t i = 0; i < 256; ++i)
        rgb[i] = std::uint32_t{p[3 * i]} << 16 | std::uint32_t{p[3 * i + 1]} << 8 | p[3 * i + 2];
    m_engine.setScreenPalette(rgb);
}

bool MovieBase::frameDue() const { return static_cast<std::int32_t>(m_engine.ticks() - m_nextDue) >= 0; }

void MovieBase::scheduleNext() {
    m_nextDue += static_cast<std::uint32_t>(std::lround(m_frameMs));
    // Never fall far behind (NOSKIP: frames are shown late rather than dropped).
    const std::uint32_t now = m_engine.ticks();
    if (static_cast<std::int32_t>(now - m_nextDue) > 250)
        m_nextDue = now;
}

void MovieBase::restartTiming() { m_nextDue = m_engine.ticks() + static_cast<std::uint32_t>(std::lround(m_frameMs)); }

// --- HCMovie -------------------------------------------------------------------------------

HcMovie::HcMovie(Engine& engine, Node& owner) : MovieBase(engine), m_owner(owner) {}

bool HcMovie::open(std::string_view name, bool, bool doubleSize, bool directToScreen, bool, int, bool colorKey) {
    // 0x4534e0. Preload/no-skip/extra buffer only tuned the CD streaming.
    if (directToScreen)
        log::debug("DirectToScreen flag ignored for movie {} for the following reason: it doesn't work.", name);
    if (doubleSize)
        log::warn("movie {}: double size is not used by the game and not supported", name);
    if (!openFile(name, colorKey, "movie"))
        return false;
    restartTiming();
    advance(); // the first frame is shown at once
    return true;
}

void HcMovie::advance() {
    // 0x453a20.
    if (!m_smk || (!m_loop && m_done))
        return;
    applyPalette(true);
    decodeCurrent();
    if (m_smk->currentFrame() + 1 >= m_smk->frameCount() && !m_loop) {
        m_done = true;
        if (m_voice != audio::VoiceId::None)
            m_engine.mixer()->endStream(m_voice);
        if (Node* node = m_engine.currentNode())
            node->movieDone(baseName(m_name));
        return;
    }
    m_smk->nextFrame();
}

void HcMovie::spendTime() {
    // 0x453490: paced by SmackWait.
    if (!m_smk || m_paused || m_engine.modal())
        return;
    if (!frameDue())
        return;
    scheduleNext();
    advance();
}

// --- SmackMovie --------------------------------------------------------------------------

SmackMovie::SmackMovie(Engine& engine, int fps, bool usePalette)
    : MovieBase(engine), m_fps(fps), m_maxFps(fps), m_usePalette(usePalette) {
    m_loop = true;
}

bool SmackMovie::open(std::string_view path, bool) {
    // 0x41a700. With fps != 0 SmackFrameRate forces the rate.
    if (!openFile(path, false, "movie"))
        return false;
    if (m_fps > 0)
        m_frameMs = 1000.0 / m_fps;
    restartTiming();
    return true;
}

void SmackMovie::gotoFrame(std::uint32_t frame1Based) {
    if (!m_smk || frame1Based < 1 || frame1Based > m_smk->frameCount())
        return;
    m_smk->gotoFrame(frame1Based - 1);
    m_done = false;
}

void SmackMovie::advance() {
    // 0x41a7e0.
    if (!m_smk)
        return;
    if (m_usePalette)
        applyPalette(false);
    decodeCurrent();
    if (m_smk->currentFrame() + 1 >= m_smk->frameCount()) {
        if (m_loop)
            m_smk->gotoFrame(0);
        else
            m_done = true;
        return;
    }
    m_smk->nextFrame();
}

void SmackMovie::spendTime() {
    // 0x41a640.
    if (!m_smk || m_paused || m_done || m_engine.modal() || m_fps <= 0)
        return;
    const std::uint32_t now = m_engine.ticks();
    if (now - s_smackMovieLast <= static_cast<std::uint32_t>(1000 / m_fps) || !frameDue())
        return;
    s_smackMovieLast = now;
    scheduleNext();
    advance();
    if (m_accelerate)
        m_fps = std::min(m_fps + 1, 24);
    else if (m_decelerate)
        m_fps = m_fps - 1 <= 1 ? 2 : m_fps - 1;
    m_frameMs = 1000.0 / m_fps;
}

void SmackMovie::stepDown() {
    m_fps = std::max(2, m_fps - 1);
    m_frameMs = 1000.0 / m_fps;
}

void SmackMovie::stepUp() {
    m_fps = std::min(m_maxFps, m_fps + 1);
    m_frameMs = 1000.0 / m_fps;
}

} // namespace brc::engine
