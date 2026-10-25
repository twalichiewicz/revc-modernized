#pragma once
// Free-aim drive-bys, GTA V style. Vice City's drive-by only shoots straight
// out of the side window you look out of (L2 / R2), and snaps to nearby
// targets. Here, holding LB in a vehicle with an SMG or a pistol aims: the
// camera rises over the vehicle and turns with the right stick, the crosshair
// sits in the middle of the screen, Tommy leans out of the side it faces and
// points his arm at it, and RB shoots there. The game's own drive-by does the
// rest (animation, ammo, reloading).

#include <cstdint>

namespace driveby {

bool Install();
// Turned on and hooked: LB aims in vehicles.
bool Enabled();
// From the pad hook, every frame: LB held in a vehicle, RB held, and the right
// stick's turn this frame (radians; yaw to the right, pitch up).
void SetInput(bool aim, bool fire, float yaw, float pitch);
bool Aiming();
// The crosshair, from CHud::Draw.
void Draw();
// From CPed::PreRender (player.cpp), before the skeleton is rebuilt: his arm points at the crosshair.
void PreRender(uint8_t *ped);

} // namespace driveby
