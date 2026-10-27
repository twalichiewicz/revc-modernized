#pragma once
// Over-the-shoulder aiming, GTA V style. While the player aims a gun that
// uses the third-person crosshair (holding the target button, LT here), the
// PC follow camera (CCam::Process_FollowPedWithMouse) eases in behind the
// right shoulder and zooms, and the crosshair moves to the screen centre.
// The M4, Ruger and M60, which the game aims in first person, use it too (a
// skip patch in CPlayerPed::ProcessPlayerWeapon); scoped weapons keep scopes.
// Bullets still go where the crosshair points: the game aims along the same
// camera (CCamera::Find3rdPersonCamTargetVector). Tommy raises the gun while
// aiming, not only once he fires, so the first shot goes at once, and keeps a
// two-handed gun up at the shoulder while he walks.

#include <cstdint>

namespace aimcam {

// How far the pad hook lets the left stick go while aiming (a walk, not a run).
constexpr float kWalkStick = 0.45f;
// The follow camera's field of view, and aiming's (degrees).
constexpr float kHipFov = 70.0f, kAimFov = 52.0f;

bool Install();
// The player is aiming a gun with the third-person crosshair.
bool Aiming();
// From the player hooks (player.cpp): after CPlayerPed::ProcessPlayerWeapon,
// and around CPlayerPed::ProcessControl (BeforeControl's result goes to AfterControl).
void AfterPlayerWeapon(uint8_t *ped, uint8_t *pad);
// The game's weapon handling was skipped (swimming, climbing): the gun goes down.
void Lower(uint8_t *ped);
// From CPed::PreRender (player.cpp), with no other pose on him: a two-handed
// gun held up at the shoulder while he walks aiming (the standing aim's arms).
void PoseArms(uint8_t *ped);
bool BeforeControl();
void AfterControl(uint8_t *ped, bool strafed);

} // namespace aimcam
