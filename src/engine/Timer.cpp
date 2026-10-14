#include "engine/Timer.h"

#include "engine/Engine.h"

namespace brc::engine {

Timer::Timer(Engine& engine, std::uint32_t interval, int message)
    : Task(engine.tasks()), m_engine(engine), m_interval(interval), m_default(interval), m_message(message) {}

void Timer::restartIfRunning() {
    if (running())
        start();
}

void Timer::start() { m_clock.restart(m_engine.ticks()); }

void Timer::stop() { m_clock.stop(m_engine.ticks()); }

std::uint32_t Timer::elapsed() const { return m_clock.elapsed(m_engine.ticks()); }

void Timer::spendTime() {
    if (!running() || elapsed() < m_interval)
        return;
    stop();
    broadcast(m_message, {});
    start();
}

} // namespace brc::engine
