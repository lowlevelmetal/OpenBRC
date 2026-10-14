#include "engine/Broadcaster.h"

#include <algorithm>

namespace brc::engine {

Listener::~Listener() {
    for (Broadcaster* b : std::vector<Broadcaster*>(m_broadcasters))
        b->removeListener(this);
}

Broadcaster::~Broadcaster() {
    for (Listener* l : m_listeners)
        if (l)
            std::erase(l->m_broadcasters, this);
}

void Broadcaster::addListener(Listener* l) {
    if (!l || hasListener(l))
        return;
    m_listeners.push_back(l);
    l->m_broadcasters.push_back(this);
}

void Broadcaster::removeListener(Listener* l) {
    for (auto& entry : m_listeners) {
        if (entry == l) {
            entry = nullptr;
            std::erase(l->m_broadcasters, this);
        }
    }
    if (m_depth == 0)
        std::erase(m_listeners, nullptr);
}

bool Broadcaster::hasListener(const Listener* l) const { return std::ranges::find(m_listeners, l) != m_listeners.end(); }

std::size_t Broadcaster::listenerCount() const {
    return static_cast<std::size_t>(std::ranges::count_if(m_listeners, [](const Listener* l) { return l != nullptr; }));
}

void Broadcaster::broadcast(int message, const MessageData& data) {
    ++m_depth;
    // Index-based: listeners appended during the loop are reached too.
    for (std::size_t i = 0; i < m_listeners.size(); ++i) {
        Listener* l = m_listeners[i];
        if (!l)
            continue;
        l->m_abortBroadcast = false;
        l->listen(message, data);
        if (i < m_listeners.size() && m_listeners[i] == l && l->m_abortBroadcast) {
            l->m_abortBroadcast = false;
            break;
        }
    }
    if (--m_depth == 0)
        std::erase(m_listeners, nullptr);
}

} // namespace brc::engine
