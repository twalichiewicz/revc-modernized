// The police scanner (#34) through its hook on cAudioManager::ServicePoliceRadio:
// heard only in a vehicle whose radio is the scanner; leaving one stops a report
// mid-call, and getting into one doesn't play what was queued meanwhile.
#include "../src/scanner.cpp"
#include <cstdio>

namespace game { Api api{}; }
Config gConfig;
void Log(const char *, ...) {}
static int gServiced, gResets;
static void NativeService(uint8_t *) { ++gServiced; }
static void NativeReset(uint8_t *) { ++gResets; }
static bool NativeUsesPoliceRadio(void *, uint8_t *vehicle) { return game::EntityModel(vehicle) == 156; }
namespace hooks {
void *Install(const char *name, void *)
{
	return !std::strcmp(name, "_ZN13cAudioManager18ServicePoliceRadioEv") ? reinterpret_cast<void *>(NativeService)
	                                                                       : nullptr;
}
} // namespace hooks
static uint8_t gTommy[0x900], gCop[0x700], gTaxi[0x700], gAudio[16];
static uint8_t *Tommy() { return gTommy; }
static void Drive(uint8_t *vehicle)
{
	gTommy[0x4c0] = vehicle != nullptr;
	*reinterpret_cast<uint8_t **>(gTommy + 0x4b8) = vehicle;
}

static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)

int main()
{
	game::api.FindPlayerPed = Tommy;
	*reinterpret_cast<int16_t *>(gCop + 0x6a) = 156;
	*reinterpret_cast<int16_t *>(gTaxi + 0x6a) = 150;
	scanner::gReset = NativeReset;
	scanner::gUsesPoliceRadio = NativeUsesPoliceRadio;
	CHECK(scanner::Install());
	// In a police car: the game's own scanner.
	Drive(gCop);
	scanner::Hook_ServicePoliceRadio(gAudio);
	CHECK(gServiced == 1);
	const int resetsInCar = gResets;
	// On foot: not heard, and a report that was playing stops (once).
	Drive(nullptr);
	scanner::Hook_ServicePoliceRadio(gAudio);
	scanner::Hook_ServicePoliceRadio(gAudio);
	CHECK(gServiced == 1 && gResets == resetsInCar + 1);
	// In a taxi: not heard either.
	Drive(gTaxi);
	scanner::Hook_ServicePoliceRadio(gAudio);
	CHECK(gServiced == 1 && gResets == resetsInCar + 1);
	// Back in a police car: what queued meanwhile goes, then the scanner again.
	Drive(gCop);
	scanner::Hook_ServicePoliceRadio(gAudio);
	CHECK(gServiced == 2 && gResets == resetsInCar + 2);
	scanner::Hook_ServicePoliceRadio(gAudio);
	CHECK(gServiced == 3 && gResets == resetsInCar + 2);
	// Off in the settings: the game's own, everywhere.
	gConfig.scannerInPoliceVehicles = false;
	Drive(nullptr);
	scanner::Hook_ServicePoliceRadio(gAudio);
	CHECK(gServiced == 4);

	if (failures)
		return 1;
	std::puts("scanner: all police scanner tests passed");
}
