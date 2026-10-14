#pragma once

#include "engine/Broadcaster.h"
#include "engine/StopClock.h"
#include "engine/Task.h"

#include <cstdint>

namespace brc::engine {

class Engine;

// The Godiva timer (0x42fb10, vtable 0x7f3738): an idle task that broadcasts
// its message to its listeners when `interval` ms have elapsed, then starts
// over. Created stopped; nodes start it by restarting its clock.
// See docs/engine/application.md §6.4.
class Timer final : public Task, public Broadcaster {
public:
    Timer(Engine& engine, std::uint32_t interval, int message);

    void setInterval(std::uint32_t ms) { m_interval = ms; }          // vt+0x08
    std::uint32_t interval() const { return m_interval; }            // vt+0x0c
    void setDefaultInterval(std::uint32_t ms) { m_default = ms; }    // vt+0x10
    std::uint32_t defaultInterval() const { return m_default; }      // vt+0x14
    void resetInterval() { m_interval = m_default; }                 // vt+0x18
    void setMessage(int message) { m_message = message; }            // vt+0x1c
    int message() const { return m_message; }                        // vt+0x20
    void restartIfRunning();                                         // vt+0x24
    void start();  // restart the clock (running from zero)
    void stop();
    bool running() const { return !m_clock.stopped; }
    std::uint32_t elapsed() const;

    void spendTime() override; // 0x42fc70

private:
    Engine& m_engine;
    StopClock m_clock;
    std::uint32_t m_interval;
    std::uint32_t m_default;
    int m_message;
};

} // namespace brc::engine
