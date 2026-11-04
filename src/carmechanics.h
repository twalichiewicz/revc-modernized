#pragma once
#include "carimpact.h"
namespace carmechanics {
bool Install();
// Only the certified physical collision owner may deliver this event.
void Collision(uint8_t *vehicle,const carimpact::Collision &);
}
