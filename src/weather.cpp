#include "weather.h"
#include "config.h"
#include "game.h"
#include "hooks.h"

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
void (*orig_TimeCycleUpdate)();
float *gFarClip, *gFogStart;
} // namespace

void Hook_TimeCycleUpdate()
{
	orig_TimeCycleUpdate();
	if (!gConfig.rainHaze || !gFarClip || !gFogStart)
		return;
	float far, fog;
	rainmodel::Haze(Now().rain, *gFarClip, *gFogStart, far, fog);
	*gFarClip = far;
	*gFogStart = fog;
}

bool InstallHaze()
{
	if (!gConfig.rainHaze)
		return true;
	gFarClip = static_cast<float *>(dlsym(RTLD_MAIN_ONLY, "_ZN10CTimeCycle17m_fCurrentFarClipE"));
	gFogStart = static_cast<float *>(dlsym(RTLD_MAIN_ONLY, "_ZN10CTimeCycle18m_fCurrentFogStartE"));
	return gFarClip && gFogStart && hooks::Install("_ZN10CTimeCycle6UpdateEv", Hook_TimeCycleUpdate, orig_TimeCycleUpdate);
}

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
