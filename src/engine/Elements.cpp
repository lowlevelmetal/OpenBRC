#include "engine/Elements.h"

namespace brc::engine {

void FrameAnim::restart() {
    m_current = 0;
    m_started = false;
    markChanged();
}

const Image8* FrameAnim::image() const {
    return m_frames.empty() ? nullptr : m_frames[static_cast<std::size_t>(m_current)].image.get();
}

float FrameAnim::imageOffsetX() const {
    return m_frames.empty() ? 0.0f : m_frames[static_cast<std::size_t>(m_current)].offsetX;
}

float FrameAnim::imageOffsetY() const {
    return m_frames.empty() ? 0.0f : m_frames[static_cast<std::size_t>(m_current)].offsetY;
}

void FrameAnim::pause(bool on) {
    Element::pause(on);
    if (on) {
        ++m_pause;
    } else if (m_pause > 0) {
        if (--m_pause == 0)
            m_started = false;
    }
}

void FrameAnim::update(std::uint32_t elapsedMs) {
    // Element tick, then the base sequence player's Run (0x47a290).
    Element::update(elapsedMs);
    if (m_pause != 0 || m_frames.empty())
        return;
    const int old = m_current;
    if (!m_started) {
        m_started = true;
        m_remaining = m_frames[static_cast<std::size_t>(m_current)].durationMs;
    } else if (m_frames[static_cast<std::size_t>(m_current)].durationMs != 0.0f) {
        float e = static_cast<float>(static_cast<int>(elapsedMs));
        while (m_remaining <= e) {
            e -= m_remaining;
            // Advance (0x47a3e0): a single frame or the end of a one-shot sequence stops.
            if (m_frames.size() <= 1 || (!m_loop && m_current + 1 >= static_cast<int>(m_frames.size()))) {
                m_remaining = m_frames[static_cast<std::size_t>(m_current)].durationMs;
                e = 0;
                break;
            }
            m_current = (m_current + 1) % static_cast<int>(m_frames.size());
            m_remaining = m_frames[static_cast<std::size_t>(m_current)].durationMs;
            if (m_remaining == 0.0f)
                break;
        }
        m_remaining -= e;
    }
    if (m_current != old)
        markChanged();
}

} // namespace brc::engine
