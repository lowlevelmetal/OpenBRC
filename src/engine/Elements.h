#pragma once

#include "engine/Element.h"

#include <memory>
#include <vector>

namespace brc::engine {

// A still picture in a node's world (OMT canvas element, 0x46d860).
class ImageElement final : public Element {
public:
    explicit ImageElement(std::shared_ptr<const Image8> image) : m_image(std::move(image)) {}
    const Image8* image() const override { return m_image.get(); }
    void setImage(std::shared_ptr<const Image8> image) {
        m_image = std::move(image);
        markChanged();
    }

private:
    std::shared_ptr<const Image8> m_image;
};

// An animation built in code (OMT animated element, 0x4717d0, with the base
// sequence player 0x479dd0): one sequence of image frames, time-based, played
// by the same rules as HC2DAnim's base (animation.md §5.3) without the HC
// extras. Used for effects such as the Human Code logo's water ripple.
class FrameAnim final : public Element {
public:
    struct Frame {
        std::shared_ptr<const Image8> image;
        float offsetX = 0, offsetY = 0;
        float durationMs = 40; // OMT default; SetRate(fps) makes it 1000 / fps
    };

    void addFrame(Frame f) { m_frames.push_back(std::move(f)); }
    std::size_t frameCount() const { return m_frames.size(); }
    void setLoop(bool loop) { m_loop = loop; }
    void restart();
    int currentFrame() const { return m_current; }

    const Image8* image() const override;
    float imageOffsetX() const override;
    float imageOffsetY() const override;
    void pause(bool on) override;
    void update(std::uint32_t elapsedMs) override;

private:
    std::vector<Frame> m_frames;
    int m_current = 0;
    bool m_loop = false;
    bool m_started = false;
    int m_pause = 0;
    float m_remaining = 0;
};

} // namespace brc::engine
