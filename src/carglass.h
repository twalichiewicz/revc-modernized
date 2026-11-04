#pragma once
#include "types.h"
#include "dentmodel.h"
#include <cstdint>
namespace carglass {
bool Install();
// A certified, real bullet impact. Atomic identity and triangle index belong
// to the current car clump; read-only line queries must never call this.
void Bullet(uint8_t *vehicle,const void *atomic,int triangle,Vec3 point,Vec3 normal);
// The shared private body-mesh owner combines dents with broken glass.
// Impact is already in the car frame, certified by the collision owner.
bool Dent(uint8_t *vehicle,const dentmodel::Impact &);
}
