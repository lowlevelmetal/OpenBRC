#pragma once

#include <cstdint>
#include <vector>

namespace brc::engine {

// Payload of a broadcast message. The original passed a pointer to a few
// dwords whose meaning depends on the message; the fields here carry the same
// information (object identities instead of name pointers).
struct MessageData {
    const void* ptr = nullptr;  // first dword (an object, a name, the node)
    std::intptr_t value = 0;    // second dword (a sequence, an argument)
    const void* extra = nullptr;
};

class Broadcaster;

// OMT listener: receives numbered messages from the broadcasters it listens to.
class Listener {
public:
    Listener() = default;
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;
    virtual ~Listener();

    virtual void listen(int message, const MessageData& data) = 0;

    // A listener may set this to stop the current broadcast after it.
    bool m_abortBroadcast = false;

private:
    friend class Broadcaster;
    std::vector<Broadcaster*> m_broadcasters;
};

// OMT broadcaster: delivers messages to its listeners in registration order.
// Like the original's linked list, a listener added during a broadcast is
// reached in the same broadcast, and listeners may be removed at any time.
class Broadcaster {
public:
    Broadcaster() = default;
    Broadcaster(const Broadcaster&) = delete;
    Broadcaster& operator=(const Broadcaster&) = delete;
    ~Broadcaster();

    void addListener(Listener* l);
    void removeListener(Listener* l);
    bool hasListener(const Listener* l) const;
    void broadcast(int message, const MessageData& data = {});
    std::size_t listenerCount() const;

private:
    std::vector<Listener*> m_listeners; // null entries are removed when no broadcast runs
    int m_depth = 0;
};

} // namespace brc::engine
