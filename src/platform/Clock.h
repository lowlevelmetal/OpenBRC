#pragma once

#include <chrono>
#include <cstdint>

namespace brc::platform {

// Monotonic time since construction (or the last restart()). Never goes backwards and is
// unaffected by changes to the wall clock.
class Clock {
public:
    using Source = std::chrono::steady_clock;

    Clock() : m_start(Source::now()) {}

    void restart() { m_start = Source::now(); }

    std::chrono::nanoseconds elapsed() const { return Source::now() - m_start; }
    std::uint64_t nanoseconds() const { return static_cast<std::uint64_t>(elapsed().count()); }
    std::uint64_t microseconds() const {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(elapsed()).count());
    }
    std::uint64_t milliseconds() const {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed()).count());
    }

private:
    Source::time_point m_start;
};

// Sleeps for at least `duration`. sleepPrecise() spins for the last stretch to wake up
// on time (frame pacing); sleep() yields the CPU for the whole duration.
void sleep(std::chrono::nanoseconds duration);
void sleepPrecise(std::chrono::nanoseconds duration);

} // namespace brc::platform
