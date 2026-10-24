#pragma once
// Over-the-shoulder aiming, GTA V style. While the player aims a gun that
// uses the third-person crosshair (holding the target button, LT here), the
// PC follow camera (CCam::Process_FollowPedWithMouse) eases in behind the
// right shoulder and zooms, and the crosshair moves to the screen centre.
// The M4, Ruger and M60, which the game aims in first person, use it too (a
// skip patch in CPlayerPed::ProcessPlayerWeapon); scoped weapons keep scopes.
// Bullets still go where the crosshair points: the game aims along the same
// camera (CCamera::Find3rdPersonCamTargetVector).

namespace aimcam {

bool Install();
// The player is aiming a gun with the third-person crosshair.
bool Aiming();

} // namespace aimcam
