#include "weather.h"
#include "game.h"

#include <algorithm>
#include <dlfcn.h>

using namespace game;
namespace weather {
namespace {
const float *gRain, *gWetRoads;
const uint32_t *gFrame;
rainmodel::Model gModel;
rainmodel::Weather gNow;
uint32_t gLastMs, gLastFrame = ~0u;
} // namespace

bool Install()
{
	gRain = static_cast<const float *>(dlsym(RTLD_MAIN_ONLY, "_ZN8CWeather4RainE"));
	gWetRoads = static_cast<const float *>(dlsym(RTLD_MAIN_ONLY, "_ZN8CWeather8WetRoadsE"));
	gFrame = static_cast<const uint32_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN6CTimer14m_FrameCounterE"));
	return gRain && gWetRoads && gFrame && api.timeMs;
}

const rainmodel::Weather &Now()
{
	if (!gRain || !gWetRoads || !gFrame || !api.timeMs || *gFrame == gLastFrame)
		return gNow;
	gLastFrame = *gFrame;
	const uint32_t now = *api.timeMs;
	// Game time: still in the pause menu; a load (time going back) restarts the clock.
	const float dt = gLastMs && now >= gLastMs ? std::min(0.25f, float(now - gLastMs) / 1000.0f) : 0.0f;
	gLastMs = now;
	gNow = gModel.Update(*gRain, *gWetRoads, dt);
	return gNow;
}

} // namespace weather
