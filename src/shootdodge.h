#pragma once
// Bullet time and the shootdodge, as in Max Payne.
//
// Bullet time: aiming (LT), a click of the right stick slows the world
// (CTimer::ms_fTimeScale) for as long as his stamina lasts: it drains in real
// time, and comes back as the game has it after sprinting. Another click,
// letting go of the aim, or the stamina running out ends it. The camera
// turns at its usual speed meanwhile (the stick drives it as mouse movement,
// which the clock doesn't slow), and the picture loses some colour
// (grade.cpp).
//
// The shootdodge: aiming, jump with the stick pushed dives him that way, flat
// out through the air, still facing and shooting where he aims. His body is
// laid along the dive and turned to the aim (on his back diving away, on his
// side diving sideways, chest down diving at it), his arms out to it. He
// lands and lies a moment, still shooting. The dive is in bullet time if he
// has the stamina for it. Till then the game thinks he's standing aiming:
// he's moved here after CPlayerPed::ProcessControl, and posed (body.cpp)
// before the skeleton is built, so his shots leave the gun where it's drawn.
// Then he gets up as the game gets peds up off the floor (CPed::SetGetUp: its
// getup animation from his back, getup_front from his front), turned so it
// starts lying as he lies, and turns to the aim as he comes up.
#include "body.h"

#include <cstdint>

namespace shootdodge {

bool Install();
// From CPad::Update after the merge (controls.cpp): the right stick's click
// and jump, while aiming; the stick kept from the game while he dives.
void FromPad(uint8_t *pad);
// After CPlayerPed::ProcessControl (player.cpp): his speed while diving, and
// bullet time's stamina.
void AfterControl(uint8_t *ped);
// At render time: the dive's pose and its weight (0: none).
bool Posing();
float Pose(uint8_t *ped, body::Pose &pose);

} // namespace shootdodge
