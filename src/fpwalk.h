#pragma once
// First person on foot: the stick moves him, not his animations. The game
// moves a ped by its walk and run animations (and turns him to where he goes
// first, then slides him to a halt), which in his eyes feels like steering a
// car. Here, after CPlayerPed::ProcessControl, his velocity comes from the
// stick as Counter-Strike moves a player (fpmove.cpp): friction, then
// acceleration towards the push in the view's frame, at Vice City's own top
// speeds (run_player, run_armed, sprint_panic, crouch.cpp's pace); in the air
// only a little steering. He faces the view throughout, and while this runs
// the game uses its strafing control (CCamera::bFreeCam off, as aimcam.cpp
// does while aiming), so his legs walk, strafe and backpedal to match.
// Swimming, climbing, diving and anything not his to control are left alone.
#include <cstdint>

namespace fpwalk {

bool Install();
// From CPad::Update, before crouch.cpp keeps the stick from the game: the
// push he walks by (the left stick, or the keys on the D-pad's fields).
void TakeStick(const uint8_t *pad);
// Around CPlayerPed::ProcessControl (player.cpp): before it, his velocity as
// the last frame and collision left it, and the strafing control; after it
// (BeforeControl's result to AfterControl), his velocity and facing.
bool BeforeControl();
void AfterControl(uint8_t *ped, bool strafed);
// Around CPlayerPed::ProcessPlayerWeapon, which runs inside ProcessControl:
// the weapon is handled with bFreeCam as the game had it (fists, melee,
// throws and the hip unchanged by the strafing control).
bool BeforeWeapon();
void AfterWeapon(bool restored);
// The stick moved him this frame (the camera's eye stays fixed to his body).
bool Active();

} // namespace fpwalk
