#include "platform/Clock.h"

#include <SDL3/SDL_timer.h>

namespace brc::platform {

void sleep(std::chrono::nanoseconds duration) {
    if (duration.count() > 0)
        SDL_DelayNS(static_cast<Uint64>(duration.count()));
}

void sleepPrecise(std::chrono::nanoseconds duration) {
    if (duration.count() > 0)
        SDL_DelayPrecise(static_cast<Uint64>(duration.count()));
}

} // namespace brc::platform
