#pragma once
// Climbing, GTA V style, on the jump button. Facing a wall, Tommy:
//   - vaults a low one (about 0.5-1.3 m, with a drop behind it),
//   - steps up onto a low ledge,
//   - pulls himself up onto a high one (up to about 2.4 m),
//   - climbs out of the water onto a dock, pier, seawall or boat (up to about
//     1.9 m above it); a prompt shows while he swims somewhere he can.
// Lower, higher or with nothing to climb, the jump is the game's own (and in
// the water there's none).
//
// The ledge is found with line checks (CWorld::ProcessLineOfSight,
// ProcessVerticalLine, TestSphereAgainstWorld) when the player's controls
// would jump (CPed::SetJump). He's then moved along the climb with his
// collision off (the checks found the room) and posed for it (body.cpp): his
// hands on the edge, a knee up, the lean.
#include "body.h"

namespace climb {

bool Install();
// Climbing: the controls are his until it's done.
bool Active();
bool Posing();
// From the player hooks (player.cpp): around CPlayerPed::ProcessControl, and
// his pose at render time over the one under it (weight `under`), returning the weight.
void BeforeControl(uint8_t *ped);
void AfterControl(uint8_t *ped);
float Pose(body::Pose &pose, float under);
// The climb-out prompt while he swims, from CHud::Draw.
void Draw();

} // namespace climb
