// Vehicles in the rain (#32) through the actual hooks: what the game's own
// ProcessWheel is handed, the car's speed after, and his aircraft bumped.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../src/rainvehicles.cpp"

namespace game { Api api{}; }
Config gConfig;
void Log(const char *, ...) {}
static rainmodel::Weather gWeather;
namespace weather {
const rainmodel::Weather &Now() { return gWeather; }
bool Install() { return true; }
} // namespace weather
namespace minimap { const mapdata::Map *RainMap() { return nullptr; } }
namespace shaders { float Seconds() { return 12.3f; } }
static float gThrust, gAdhesion;
static int gWheels, gFlights;
static void NativeWheel(uint8_t *, float *, float *, float *, float *, int32_t, float thrust, float, float adhesion, int8_t, float *, void *, uint16_t)
{
	++gWheels;
	gThrust = thrust;
	gAdhesion = adhesion;
}
static void NativeFlying(uint8_t *, int32_t) { ++gFlights; }
namespace hooks {
void *Chain(const char *name, void *)
{
	if (!std::strcmp(name, "_ZN8CVehicle12ProcessWheelER7CVectorS1_S1_S1_ifffaPfP11tWheelStatet")) return reinterpret_cast<void *>(NativeWheel);
	if (!std::strcmp(name, "_ZN8CVehicle13FlyingControlE12eFlightModel")) return reinterpret_cast<void *>(NativeFlying);
	return nullptr;
}
} // namespace hooks

static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)

alignas(16) static uint8_t gCar[0x800], gPlayer[0x900];
static float gStep = 1;
static uint8_t *Player() { return gPlayer; }
static float *Speed(uint8_t *v) { return reinterpret_cast<float *>(v + 0x84); }
static void Wheel(uint8_t surface, int32_t type)
{
	*reinterpret_cast<int32_t *>(gCar + 0x348) = type;
	gCar[0x4e0 + 0x22] = surface; // wheel 0's ground
	float fwd[3] = {0, 1, 0}, right[3] = {1, 0, 0}, contact[3] = {}, point[3] = {}, spin = 0;
	rainvehicles::Hook_ProcessWheel(gCar, fwd, right, contact, point, 4, 1.0f, 0, 1.0f, 0, &spin, nullptr, 0);
}

int main()
{
	game::api.timeStep = &gStep;
	game::api.FindPlayerPed = Player;
	gConfig.rainVehicles = true;
	CHECK(rainvehicles::Install());
	gWeather.wetness = 1;

	// A car's wheel on a soaked field: less drive, less grip, held back along the wheel.
	Speed(gCar)[0] = 0.1f, Speed(gCar)[1] = 0.2f, Speed(gCar)[2] = 0;
	Wheel(2, 0);
	CHECK(std::fabs(gThrust - 0.55f) < 1e-5f && std::fabs(gAdhesion - 0.45f) < 1e-5f);
	CHECK(std::fabs(Speed(gCar)[1] - (0.2f - 3.2f / 2500 / 4)) < 1e-6f && Speed(gCar)[0] == 0.1f);
	// Barely moving: stopped, not pushed back.
	Speed(gCar)[1] = 0.0001f;
	Wheel(2, 0);
	CHECK(Speed(gCar)[1] == 0);
	// On tarmac, a bike's wheel, or the ground dry: as the game has it.
	Speed(gCar)[1] = 0.2f;
	Wheel(1, 0);
	CHECK(gThrust == 1 && gAdhesion == 1 && Speed(gCar)[1] == 0.2f);
	Wheel(2, 5);
	CHECK(gThrust == 1 && gAdhesion == 1 && Speed(gCar)[1] == 0.2f);
	gWeather.wetness = 0;
	Wheel(2, 0);
	CHECK(gThrust == 1 && Speed(gCar)[1] == 0.2f && gWheels == 5);

	// His aircraft, high up in heavy rain: bumped about. On the ground, or not his: left alone.
	gWeather.rain = 0.5f;
	float *m = reinterpret_cast<float *>(gCar + 8);
	m[0] = 1, m[5] = 1, m[10] = 1; // level
	m[12] = 0, m[13] = 0, m[14] = 100;
	gPlayer[0x4c0] = 1;
	*reinterpret_cast<uint8_t **>(gPlayer + 0x4b8) = gCar;
	std::memset(gCar + 0x84, 0, 24);
	rainvehicles::Hook_FlyingControl(gCar, 0);
	const float *spin = reinterpret_cast<float *>(gCar + 0x90);
	CHECK(gFlights == 1 && std::fabs(spin[0]) + std::fabs(spin[1]) + std::fabs(spin[2]) > 0 &&
	      std::fabs(Speed(gCar)[0]) + std::fabs(Speed(gCar)[1]) + std::fabs(Speed(gCar)[2]) > 0); // (its bumps are its own: seeded by where it is in memory)
	std::memset(gCar + 0x84, 0, 24);
	m[14] = 2; // on the ground
	rainvehicles::Hook_FlyingControl(gCar, 0);
	CHECK(gFlights == 2 && spin[0] == 0 && spin[1] == 0 && Speed(gCar)[2] == 0);
	m[14] = 100;
	static uint8_t other[0x800];
	std::memset(gCar + 0x84, 0, 24);
	*reinterpret_cast<uint8_t **>(gPlayer + 0x4b8) = other; // he's in another vehicle: this one isn't his
	rainvehicles::Hook_FlyingControl(gCar, 0);
	CHECK(gFlights == 3 && spin[0] == 0 && Speed(gCar)[2] == 0);
	*reinterpret_cast<uint8_t **>(gPlayer + 0x4b8) = gCar;
	std::memset(gCar + 0x84, 0, 24);
	gPlayer[0x4c0] = 0; // he's not in one at all
	rainvehicles::Hook_FlyingControl(gCar, 0);
	CHECK(gFlights == 4 && spin[0] == 0 && Speed(gCar)[2] == 0);
	if (failures)
		return 1;
	std::puts("rainvehicles: all vehicle rain hook tests passed");
}
