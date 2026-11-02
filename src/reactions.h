#pragma once
#include "types.h"
#include "reactrig.h"
#include <cstdint>
namespace reactions {
// Native entity contacts feed Tommy even when optional NPC reactions are disabled.
bool Install();
// Accepted damage only. Scoped native bullet points/rays localize the impulse;
// other events retain a coarse fallback. Health/armour and mission rules stay native.
void Hit(uint8_t *ped, uint8_t *by, int weapon, int piece);
// Around CPed::PreRender: restore the animation's last matrices, then apply the physical pose.
void BeforePreRender(uint8_t *ped);
void AfterPreRender(uint8_t *ped);
// A read-only snapshot of the same blended pose used for rendering. Native
// hit queries must not depend on whether PreRender has run this frame.
bool CopyHitPose(uint8_t *ped, reactrig::Matrices &pose);
// Releases stale/distant physical bodies and resets state on a game load.
void Frame();
// Damage-free wind delta in m/s. True retains it through native standing
// control and feeds an available NPC body; false leaves native force to caller.
bool Wind(uint8_t *ped, Vec3 deltaVelocity);
// Tommy is down: a native knockdown articulated him (#25), and his ragdoll owns
// his root until he's back up. What poses, turns or aims him stands down.
bool PlayerDown();
// Down in first person (#25, fully attached): the eye in his ragdoll's head and
// the view his head has now, its orientation at the knockdown kept relative to
// it (so it starts where he faced and rolls with the tumble). False otherwise.
bool PlayerHeadView(Vec3 &eye, Vec3 &right, Vec3 &forward, Vec3 &up);
} // namespace reactions
