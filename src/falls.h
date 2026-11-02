#pragma once
// Landing hurts as it would (falldamage.h) instead of the game's little, for
// every ped, Tommy too; and armour doesn't soften it (CPed::InflictDamage
// takes a fall's damage from armour first: SWAT, in 50, walked away from a
// fall out of a helicopter). From CPed::InflictDamage's hook (player.cpp).
#include <cstdint>

namespace falls {

bool Install();
// Before the game's CPed::InflictDamage: a landing's (WEAPONTYPE_FALL)
// `damage` raised to what the fall deals, his armour put aside; returns the
// armour, for Landed. (Anything else: left be, 0.)
float Land(uint8_t *ped, int32_t weapon, float &damage);
// After it: his armour back.
void Landed(uint8_t *ped, float armour);

} // namespace falls
