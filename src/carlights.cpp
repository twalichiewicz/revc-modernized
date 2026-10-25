#include "carlights.h"
#include "game.h"
#include "hooks.h"

#include <cstdint>

namespace carlights {
namespace {

// CBrightLights' types (BRIGHTLIGHT_*): traffic lights 1-3, then a vehicle's
// front lights 4-7 and rear lights 8-11 (long, small, big, tall).
constexpr uint8_t kFirstVehicle = 4, kLastVehicle = 11;

void (*orig_RegisterOne)(CVector, CVector, CVector, CVector, uint8_t, uint8_t, uint8_t, uint8_t);

void Hook_RegisterOne(CVector pos, CVector up, CVector side, CVector front, uint8_t type, uint8_t r, uint8_t g,
                      uint8_t b)
{
	if (type >= kFirstVehicle && type <= kLastVehicle)
		return;
	orig_RegisterOne(pos, up, side, front, type, r, g, b);
}

} // namespace

bool Install()
{
	return hooks::Install("_ZN13CBrightLights11RegisterOneE7CVectorS0_S0_S0_hhhh", Hook_RegisterOne, orig_RegisterOne);
}

} // namespace carlights
