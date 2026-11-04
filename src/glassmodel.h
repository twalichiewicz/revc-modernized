#pragma once
#include "types.h"
#include <cstdint>

namespace glassmodel {
enum class Pane : uint8_t { None, Front, Rear, LeftFront, RightFront, LeftRear, RightRear };
// Centre and face normal in the car's frame. Only translucent cabin faces
// on known high-detail body components qualify. The measured 20 cm beltline
// floor and glass texture list exclude lamps/decals; LODs stay native.
Pane Classify(const char *frame, Vec3 centre, Vec3 normal, uint8_t alpha,const char *texture=nullptr);
Pane DoorPane(int door);
constexpr uint8_t Bit(Pane p) { return p==Pane::None?0:uint8_t(1u<<(unsigned(p)-1)); }
}
