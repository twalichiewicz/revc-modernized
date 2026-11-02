#pragma once
#include "types.h"
#include <algorithm>
#include <cstdint>
namespace carlightrules {
// Return addresses verified in native arm64 macVC1.0.5217 CAutomobile::PreRender.
// These are the unshadowed, omnidirectional red tail/brake point lights.
inline bool SuppressCabinLight(bool seated, bool ownCar, uint32_t returnOffset)
{
	return seated && ownCar && (returnOffset == 0x256e9c || returnOffset == 0x257178);
}
inline bool SuppressCabinCorona(bool seated, bool ownCar, uint32_t idOffset)
{
	return seated && ownCar && idOffset >= 14 && idOffset <= 15;
}
inline uint8_t InteriorAlpha(uint8_t alpha) { return alpha > 0 && alpha < 255 ? std::min<uint8_t>(alpha, 48) : alpha; }
// The fragment shader clips interpolated model coordinates to this lens-sized volume.
inline Vec3 LensCoordinates(Vec3 vertex, Vec3 light)
{
	return {(vertex.x - light.x) / 0.32f, (vertex.y - light.y) / 0.35f, (vertex.z - light.z) / 0.14f};
}
} // namespace carlightrules
