#pragma once
#include "carimpact.h"
#include "projectilemodel.h"
#include "firemodel.h"
namespace carmechanics {
bool Install();
bool Enabled();
// Only the certified physical collision owner may deliver this event.
void Collision(uint8_t *vehicle,const carimpact::Collision &);
// Read-only render query: unknown, disabled or protected actors retain supply.
float LightingPower(const uint8_t *vehicle);
// A native damage-boundary lease, not a control scope. Only the certified
// shot owner may begin and deliver one. Stores identity and pose, never mesh
// or ray pointers; delivery revalidates the current actor and model bindings.
struct BulletToken {
 uint64_t revision=0;const void *clump=nullptr,*handling=nullptr,*info=nullptr,*col=nullptr,*source=nullptr;
 std::array<float,16> transform{};
};
BulletToken BeginBullet(uint8_t *vehicle);
void Projectile(uint8_t *vehicle,const BulletToken &,const projectilemodel::Ray &);
// Native fire owner only. Reading never creates history; exposure changes
// integrity without inventing collision alignment impulses.
firemodel::Input FireInput(const uint8_t *vehicle);
void Burn(uint8_t *vehicle,float seconds);
}
