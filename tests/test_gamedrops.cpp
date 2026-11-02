// The game's own drops on the screen (#32): with the mod's (RainDrops=1) the
// game adds none of its 2D ones (in third person too) and clears any it had;
// with RainDrops=0 it's left to it.
#include <cstdio>
#include <cstring>
#include <dlfcn.h>

static void *Symbol(void *, const char *);
#define dlsym Symbol
#include "../src/gamedrops.cpp"
#undef dlsym

Config gConfig;
static int gFailures, gNative, gRemoved, gRemovedType = -1;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++gFailures;                                                                                    \
		}                                                                                                      \
	} while (0)

static void Native() { ++gNative; }
static void Remove(int type)
{
	++gRemoved;
	gRemovedType = type;
}
namespace hooks {
void *Install(const char *name, void *) { return std::strcmp(name, "_ZN8CWeather7AddRainEv") ? nullptr : reinterpret_cast<void *>(Native); }
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
	gConfig.rainDrops = false;                                      // (as the INI has it at start)
	gamedrops::Hook_AddRain();
	CHECK(gNative == 1 && gRemoved == 1);
	if (gFailures)
		return 1;
	std::puts("gamedrops: all screen drop tests passed");
}
