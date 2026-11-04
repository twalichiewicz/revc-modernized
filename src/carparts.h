#pragma once
#include <cstdint>
namespace carparts {
bool Install();bool Enabled();
struct BulletToken {uint64_t revision=0;};
BulletToken BeginBullet(uint8_t *);
void Projectile(uint8_t *,const BulletToken &,int component,float budget);
}
