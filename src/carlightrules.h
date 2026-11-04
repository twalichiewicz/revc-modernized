#pragma once
#include "types.h"
#include <algorithm>
#include <cstdint>
namespace carlightrules {
// Native macVC1.0.5217 automobile lamp IDs. Body sunlight reflections begin
// at car+27 (CVehicle::DoSunGlare) and have no electrical dependency.
inline bool ElectricalCorona(uint32_t idOffset)
{
	return idOffset <= 1 || idOffset == 6 || idOffset == 7 || idOffset == 14 || idOffset == 15 ||
	       (idOffset >= 21 && idOffset <= 24);
}
// PreRender's five native point-light returns: taxi/special lamp, siren,
// headlights, tail lights, brake lights. Other nested producers stay native.
inline bool ElectricalPoint(uint32_t returnOffset)
{
	return returnOffset == 0x252fb4 || returnOffset == 0x253668 || returnOffset == 0x256e1c ||
	       returnOffset == 0x256e9c || returnOffset == 0x257178;
}
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
