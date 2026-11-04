#pragma once
#include "types.h"
#include <cstdint>
namespace carimpact {
struct Collision {Vec3 point,inward;float severity;uint8_t piece;};
bool Install();
}
