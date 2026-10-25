#pragma once
// The player's hooks several features share (a game function has one hook
// slot): CPlayerPed::ProcessControl (his movement: aiming, swimming,
// climbing), CPlayerPed::ProcessPlayerWeapon (his weapon: raised to aim, not
// used in the water or while climbing), CPed::PreRender (his pose: the
// drive-by arm, swimming, climbing) and CPed::InflictDamage (no drowning,
// rumble on a punch).

#include <cstdint>

namespace player {

// Before the features that use them.
bool Install();
bool ControlHooked(); // ProcessControl and ProcessPlayerWeapon
bool PoseHooked();
bool DamageHooked();
// The game's own damage (CPed::InflictDamage, from nothing, to the torso),
// not refused: drowning once he's out of breath.
void Damage(uint8_t *ped, int32_t weapon, float amount);

} // namespace player
