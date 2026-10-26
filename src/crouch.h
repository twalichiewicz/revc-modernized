#pragma once
// Walking crouched, as GTA V's stealth walk. Vice City crouches him (the
// crouch button: CPed::SetDuck, weapon_crouch, the gun held low) but has no
// crouched walk: pushing the stick started the walk, which blended the crouch
// out, and he stood up. While he's crouched on foot the stick is kept from
// the game (it leaves him crouched) and he's moved here, after
// CPlayerPed::ProcessControl, at a crouch's pace: turning to where he goes,
// or, aiming or in first person, keeping his facing and stepping sideways.
// His legs step (two-bone, his hips raised from the crouch's near-kneel) and
// the rest of him is the game's crouch: the gun held low, aimed as it aims
// (body.cpp's ApplyOver); with the gun raised, its firing pose, turned about
// the root as the game turns it. Sprinting or jumping stands him up, as the game
// does; the crouch doesn't time out while he walks.
#include "body.h"

#include <cstdint>

namespace crouch {

// From CPad::Update after the merge (controls.cpp): the stick, kept from the
// game while he walks crouched.
void TakeStick(uint8_t *pad);
// After CPlayerPed::ProcessControl (player.cpp): his speed and facing.
void AfterControl(uint8_t *ped);
// At render time: his legs' pose (only what it sets) and its weight (0: none).
bool Posing();
float Pose(uint8_t *ped, body::Pose &pose);

} // namespace crouch
