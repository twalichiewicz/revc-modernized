#pragma once
#include "game.h"
#include <cstdint>
#include <vector>

namespace population {
bool Install();
void Frame();
bool Active();
bool Ready(); // city accounting owns intercepted generation, including while indoors
// A lost virtual car and a lost foot officer consume the same finite station queues.
// True means the replacement has departed; its station is returned for road travel.
bool ReinforceCar(uint64_t token,const CVector &origin,uint32_t now,CVector *departure=nullptr);
std::vector<CVector> FootPatrols();
}
