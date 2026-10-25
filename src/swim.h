#pragma once
// Swimming, GTA V style. Vice City drowns Tommy in deep water
// (CPed::ProcessBuoyancy); here he swims: a front crawl along the surface
// where the stick points (camera-relative), faster while sprinting, and
// treading water when he stops. The jump button dives (where there's no ledge
// to climb out on): under the surface he swims a breaststroke where the camera
// looks, straight up while the jump button is held, for as long as his breath
// lasts (a bar under the map) and then he drowns. He wades through shallow
// water as before and gets out by walking up a beach or climbing out
// (climb.cpp). Other peds still drown. His weapon is put away while he swims
// and taken out again after; the camera keeps following him (the game's stops
// to watch a ped in the water).
//
// The game keeps him "standing" in the water, so its own control code runs
// and nothing puts him into a fall; his speed, heading and height are set
// after CPlayerPed::ProcessControl. Vice City has no swimming animation, so
// the strokes are posed bone by bone (body.cpp).
#include "body.h"

namespace swim {

bool Install();
bool Swimming();
bool Underwater();
bool Posing();
// From the jump button at the surface (climb.cpp, when there's nothing to climb out on).
void Dive(uint8_t *ped);
// His breath, 0..1, and whether the bar shows it (under the surface, or until it's back).
float Breath();
bool ShowsBreath();
// The water's tint while the camera is under it, from CHud::Draw before the HUD.
void DrawUnderwater();
// The water's surface where he swims (valid while Swimming).
float Surface();
// From the player hooks (player.cpp): after CPlayerPed::ProcessControl, and
// his pose at render time with its weight (0: none).
void AfterControl(uint8_t *ped);
float Pose(body::Pose &pose);
// CPed::InflictDamage: the drowning damage the water deals him is refused; in
// a sunk vehicle (which drowns those inside) his breath goes instead, so he
// can get out and swim up.
bool RefusesDamage(const uint8_t *ped, int32_t weapon);

} // namespace swim
