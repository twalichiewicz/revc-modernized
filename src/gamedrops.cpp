#include "gamedrops.h"
#include "config.h"
#include "hooks.h"

#include <dlfcn.h>

namespace gamedrops {
namespace {
constexpr int kRaindrop2D = 0x50; // tParticleType PARTICLE_RAINDROP_2D (as AddRain adds and removes it)
void (*orig_AddRain)();
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

bool Install()
{
	if (!gConfig.rainDrops)
		return true;
	gRemovePSystem = reinterpret_cast<void (*)(int)>(dlsym(RTLD_MAIN_ONLY, "_ZN9CParticle13RemovePSystemE13tParticleType"));
	return gRemovePSystem && hooks::Install("_ZN8CWeather7AddRainEv", Hook_AddRain, orig_AddRain);
}

} // namespace gamedrops
