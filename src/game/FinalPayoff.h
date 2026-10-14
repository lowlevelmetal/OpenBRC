#pragma once

// A session flag the final payoff sets for the tack room, which it returns to.
namespace brc::game::flags {

// [0x809b00] The final payoff movie was shown (FinalPayoff ctor 0x411810).
// The tack room greets with DB9025 instead of its usual lines and clears it.
inline bool finalPayoffShown = false;

} // namespace brc::game::flags
