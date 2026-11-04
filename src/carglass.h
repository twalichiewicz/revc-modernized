#pragma once
#include "types.h"
#include <cstdint>
namespace carglass {
bool Install();
// A certified, real bullet impact. Atomic identity and triangle index belong
// to the current car clump; read-only line queries must never call this.
void Bullet(uint8_t *vehicle,const void *atomic,int triangle,Vec3 point,Vec3 normal);
}
