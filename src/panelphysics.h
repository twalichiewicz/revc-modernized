#pragma once
#include "carparts.h"
#include <array>
#include "types.h"
#include <type_traits>
namespace panelphysics {
struct Face {uint16_t v[3];uint8_t material=6,piece=0;};
struct Surface {std::array<Vec3,1794> vertices{};std::array<Face,598> faces{};size_t count=0;bool more=false;};
// Current visible articulated sheet in vehicle coordinates; no geometry writes.
bool CollisionSurface(uint8_t *vehicle,int door,Surface &,size_t first=0);
struct Load {uint8_t *vehicle=nullptr;carparts::BulletToken lease{};int node=-1,door=-1;Vec3 point{},inward{};float work=0;};
bool Install();
// Native progression executes inside a point-load scope. If it separates a
// part, its new CObject receives the impulse; otherwise the native hinge/body.
void Apply(const Load &,void (*progress)(void *),void *context);
template<class F> void Apply(const Load &load,F &&progress){
 using Callback=std::remove_reference_t<F>;
 Apply(load,[](void *p){(*static_cast<Callback *>(p))();},&progress);
}
}
