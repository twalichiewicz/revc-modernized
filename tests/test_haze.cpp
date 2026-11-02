// Heavy rain closes the view in (#32): after the game's time cycle has set its
// far clip and fog for the frame, they're brought in with the rain; dry, or
// with RainHaze=0, left as the game had them.
#include <cstdio>
#include <cstring>
#include <dlfcn.h>

static void *Symbol(void *, const char *);
#define dlsym Symbol
#include "../src/weather.cpp"
#undef dlsym

namespace game { Api api{}; }
Config gConfig;
static int gFailures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++gFailures;                                                                                    \
		}                                                                                                      \
	} while (0)

static float gRain, gWet, gFar, gFog;
static uint32_t gFrame, gMs = 1000;
static int gUpdates;
static void Update() // the game's: sets the frame's far clip and fog afresh
{
	++gUpdates;
	gFar = 1100;
	gFog = 80;
}
namespace hooks {
void *Install(const char *name, void *) { return std::strcmp(name, "_ZN10CTimeCycle6UpdateEv") ? nullptr : reinterpret_cast<void *>(Update); }
}
static void *Symbol(void *, const char *name)
{
	if (!std::strcmp(name, "_ZN8CWeather4RainE")) return &gRain;
	if (!std::strcmp(name, "_ZN8CWeather8WetRoadsE")) return &gWet;
	if (!std::strcmp(name, "_ZN6CTimer14m_FrameCounterE")) return &gFrame;
	if (!std::strcmp(name, "_ZN10CTimeCycle17m_fCurrentFarClipE")) return &gFar;
	if (!std::strcmp(name, "_ZN10CTimeCycle18m_fCurrentFogStartE")) return &gFog;
	return nullptr;
}

int main()
{
	game::api.timeMs = &gMs;
	gConfig.rainHaze = true;
	CHECK(weather::Install() && weather::InstallHaze());
	weather::Hook_TimeCycleUpdate(); // dry
	CHECK(gUpdates == 1 && gFar == 1100 && gFog == 80);
	gRain = 0.5f; // CATSANDDOGS
	++gFrame;
	weather::Hook_TimeCycleUpdate();
	CHECK(gUpdates == 2 && gFar <= 450 && gFog <= 5);
	gConfig.rainHaze = false;
	++gFrame;
	weather::Hook_TimeCycleUpdate();
	CHECK(gUpdates == 3 && gFar == 1100 && gFog == 80);
	if (gFailures)
		return 1;
	std::puts("haze: all rain haze tests passed");
}
