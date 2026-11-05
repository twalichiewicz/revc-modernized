#pragma once
#include "airframemodel.h"
#include <cstdint>
namespace airframedamage {
struct Token {uint64_t revision=0;void *clump=nullptr;std::array<float,16> pose{};};
bool ComponentHit(uint8_t *,Vec3 from,Vec3 to,float entry,float &closest,Vec3 &normal);
bool Install();bool Enabled(const uint8_t *);
Token BeginBullet(uint8_t *);
bool Projectile(uint8_t *,const Token &,const projectilemodel::Ray &,std::array<float,projectilemodel::kLayers> *passed=nullptr,int exposedRotor=-1);
void Perforated(uint8_t *,const Token &,float area);
}
