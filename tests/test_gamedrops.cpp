// The game's own drops on the screen (#32): with the mod's (RainDrops=1) the
// game adds none of its 2D ones (in third person too) and clears any it had,
// and the port's screen droplets (reVC's ScreenDroplets, drawn on the camera
// on foot and in cars alike) aren't drawn; with RainDrops=0 it's left to them.
#include <cstdio>
#include <cstring>
#include <dlfcn.h>

static void *Symbol(void *, const char *);
#define dlsym Symbol
#include "../src/gamedrops.cpp"
#undef dlsym

Config gConfig;
static int gFailures, gNative, gRemoved, gRemovedType = -1, gDroplets;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++gFailures;                                                                                    \
		}                                                                                                      \
	} while (0)

static void Native() { ++gNative; }
static void NativeDroplets() { ++gDroplets; }
static void Remove(int type)
{
	++gRemoved;
	gRemovedType = type;
}
namespace hooks {
void *Install(const char *name, void *)
{
	if (!std::strcmp(name, "_ZN8CWeather7AddRainEv")) return reinterpret_cast<void *>(Native);
	if (!std::strcmp(name, "_ZN14ScreenDroplets6RenderEv")) return reinterpret_cast<void *>(NativeDroplets);
	return nullptr;
}
}
static void *Symbol(void *, const char *name)
{
	return std::strcmp(name, "_ZN9CParticle13RemovePSystemE13tParticleType") ? nullptr : reinterpret_cast<void *>(Remove);
}

int main()
{
	gConfig.rainDrops = true;
	CHECK(gamedrops::Install());
	gamedrops::Hook_AddRain();
	CHECK(gNative == 0 && gRemoved == 1 && gRemovedType == 0x50); // RAINDROP_2D
	gamedrops::Hook_ScreenDropletsRender();
	CHECK(gDroplets == 0);
	gConfig.rainDrops = false;                                      // (as the INI has it at start)
	gamedrops::Hook_AddRain();
	CHECK(gNative == 1 && gRemoved == 1);
	gamedrops::Hook_ScreenDropletsRender();
	CHECK(gDroplets == 1);
	if (gFailures)
		return 1;
	std::puts("gamedrops: all screen drop tests passed");
}
