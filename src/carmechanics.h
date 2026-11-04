#pragma once
#include "carimpact.h"
namespace carmechanics {
bool Install();
// Only the certified physical collision owner may deliver this event.
void Collision(uint8_t *vehicle,const carimpact::Collision &);
// Read-only render query: unknown, disabled or protected actors retain supply.
float LightingPower(const uint8_t *vehicle);
}
