#pragma once
#include <cstdint>
#include "types.h"
namespace carparts {
bool Install();bool Enabled();
bool SeverRotor(uint8_t *,bool main);
struct BulletToken {uint64_t revision=0;};
BulletToken BeginBullet(uint8_t *);
bool CurrentBullet(uint8_t *,const BulletToken &);
void Projectile(uint8_t *,const BulletToken &,int component,float budget,Vec3 point={},Vec3 inward={});
}
