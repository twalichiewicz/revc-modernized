#pragma once
// His own gun's body, synthesized (no recordings): what Vice City's samples
// lack close up. A low thump with a falling pitch (the gas and the recoil
// through him), a short crack of noise (the muzzle), and a mechanism's click
// (a slide or bolt), sized by the kind of gun; faded to nothing. Played
// centred under the game's own shot (shotsound.cpp). Unit-tested
// (tests/test_shotsynth.cpp).
#include <cstdint>
#include <vector>

namespace shotsynth {

enum class Kind { kNone, kPistol, kSmg, kRifle, kShotgun, kHeavy, kSniper };
// The game's weapon type's kind (none for anything not fired as a gun: the
// rocket and flame launchers have their own sounds).
Kind KindOf(int weapon);
// A shot: mono 16-bit at `rate`, its noise from `seed` (a few to vary them).
std::vector<int16_t> Make(Kind kind, int rate, uint32_t seed);

} // namespace shotsynth
