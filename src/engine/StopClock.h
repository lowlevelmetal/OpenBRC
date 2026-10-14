#pragma once

#include <cstdint>

namespace brc::engine {

// The clock pattern of OMT worlds and Godiva timers (application.md §6.3):
// elapsed = (stopped or frozen) ? frozenValue : now - start + accumulated,
// 32-bit unsigned milliseconds.
struct StopClock {
    std::uint32_t start = 0;
    std::uint32_t frozenValue = 0;
    std::uint32_t accumulated = 0;
    bool stopped = true;
    bool frozen = false;

    std::uint32_t elapsed(std::uint32_t now) const { return (stopped || frozen) ? frozenValue : now - start + accumulated; }
    void stop(std::uint32_t now) {
        frozenValue = elapsed(now);
        stopped = true;
    }
    // Restart from zero.
    void restart(std::uint32_t now) {
        stop(now);
        accumulated = 0;
        start = now;
        stopped = false;
    }
    // Continue counting from the stopped value.
    void resume(std::uint32_t now) {
        if (!stopped)
            return;
        accumulated = frozenValue;
        start = now;
        stopped = false;
    }
};

} // namespace brc::engine
