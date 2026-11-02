#pragma once
#include <cstdint>
namespace reacthit {
// All native ped line hits use the same current physical pose as rendering.
// Damage, filtering, surface masks and closest-hit selection remain native.
bool Install();
// Merge current hit-volume extents before gridfix clips sector membership.
// Ordinary actors and invalid/stale records leave the rectangle untouched.
void ExpandBounds(uint8_t *entity, float &left, float &top, float &right, float &bottom);
} // namespace reacthit
