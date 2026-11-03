#include "gamedrops.h"
#include "camera.h"
#include "config.h"
#include "hooks.h"

#include <dlfcn.h>

namespace gamedrops {
namespace {
constexpr int kRaindrop2D = 0x50; // tParticleType PARTICLE_RAINDROP_2D (as AddRain adds and removes it)
void (*orig_AddRain)();
void (*orig_ScreenDropletsRender)();
void (*gRemovePSystem)(int type);
} // namespace

void Hook_AddRain()
{
	if (gConfig.rainDrops && gRemovePSystem) {
		gRemovePSystem(kRaindrop2D);
		return;
	}
	orig_AddRain();
}

void Hook_ScreenDropletsRender()
{
	// In third person the camera's a thing in the world, and rained on: the port's droplets show.
	// Through his own eyes (none) or from a seat (the mod's, on the glass or the lens): not.
	if (!gConfig.rainDrops || (!camera::FirstPersonOnFoot() && !camera::InSeat()))
		orig_ScreenDropletsRender();
}

bool Install()
{
	if (!gConfig.rainDrops)
		return true;
	gRemovePSystem = reinterpret_cast<void (*)(int)>(dlsym(RTLD_MAIN_ONLY, "_ZN9CParticle13RemovePSystemE13tParticleType"));
	return gRemovePSystem && hooks::Install("_ZN8CWeather7AddRainEv", Hook_AddRain, orig_AddRain) &&
	       hooks::Install("_ZN14ScreenDroplets6RenderEv", Hook_ScreenDropletsRender, orig_ScreenDropletsRender);
}

} // namespace gamedrops
