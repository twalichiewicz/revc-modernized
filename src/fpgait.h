#pragma once
// First person's gait decisions, kept apart from the game so they're
// unit-tested (tests/test_fpgait.cpp): the push he walks by (the stick or
// the keys, as the game reads them), when the stick moves him (only when
// he's driving himself, or in the air), how fast, and who counts as an
// enemy for aim slowdown.
#include "types.h"

#include <cstdint>

namespace fpgait {

// The push he walks by (x right, y ahead, at most 1 long): as
// CPad::GetPedWalkLeftRight / GetPedWalkUpDown read it, the stick, or the
// D-pad's fields (where the keyboard's keys land, 255 down) when that's further.
Vec2 Push(int16_t stickX, int16_t stickY, int16_t up, int16_t down, int16_t left, int16_t right);

enum class Mode { kNone, kGround, kAir };
struct Situation {
	int32_t state = 1;         // CPed::m_nPedState
	bool standing = true;      // the ped's standing flag
	bool inTheAir = false;     // ... in the air
	bool inControl = true;     // CPed::IsPedInControl
	bool controlsDisabled = false; // the pad's (a script's, a cutscene's)
	int32_t objective = 0;     // CPed::m_objective (0: none; a car's door, a script's walk...)
	bool scoped = false;       // the mod's sniper scope up (the game's sniper state, his to creep in)
};
// The stick moves him on the ground only while he's driving himself (idle,
// shooting, aiming, on the phone; not fists, which pick punches with it),
// steers him a little in the air (a jump or a fall), and otherwise leaves him
// to the game.
Mode MoveMode(const Situation &s);

struct Gait {
	bool ducking = false, aiming = false, sprinting = false, twoHanded = false;
};
// His top speed (m/s), as his animations carry him: run_player 6.4, run_armed
// 5.0 (a two-handed gun), sprint_panic 9.8; crouched 1.6 (crouch.cpp's pace);
// aiming, CS's walk (0.52 of a run), even with sprint held.
float TopSpeed(const Gait &g);

// Someone fighting him: out to kill him (objective 8 or 9 at him), his gun on
// him, or a cop while he's wanted. Not partners following him or people
// fleeing from him.
bool Enemy(int32_t objective, bool objectiveIsHim, bool gunOnHim, bool cop, bool wanted);

} // namespace fpgait
