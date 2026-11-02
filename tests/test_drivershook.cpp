// Exercise absence updates through the actual per-frame integration, including
// its early no-aim return, rather than testing an unreachable model input.
#include "../src/drivers.cpp"
#include <cstdio>

namespace game {
Api api{};
}
Config gConfig;
void Log(const char *, ...) {}
static bool gAimed;
namespace aimcam {
bool Aiming()
{
	return gAimed;
}
} // namespace aimcam
namespace driveby {
bool Aiming()
{
	return false;
}
} // namespace driveby
namespace camera {
CVector ViewForward()
{
	return {0, 1, 0};
}
} // namespace camera
static uint8_t gPlayer[0x8d0]{}, gDriver[0x8d0]{}, gCar[0x6f0]{}, gPad[1024]{}, gPool[32]{}, gCamera[4096]{}, gPoolFlag;
static uint8_t *gIgnored;
static int gObjectives, gSightChecks;
static CVector gSightFrom;
static uint8_t *gPoolPtr = gPool;
static uint32_t gNow = 1000;
static uint8_t *Player()
{
	return gPlayer;
}
static uint8_t *Pad(int)
{
	return gPad;
}
static bool Target(uint8_t *)
{
	return false;
}
static uint8_t *Car(int)
{
	return gCar;
}
static int Ref(uint8_t *)
{
	return 17;
}
static bool Sight(const CVector &a, const CVector &, bool, bool, bool, bool, bool, bool, bool)
{
	gSightChecks++;
	gSightFrom = a;
	return true;
}
static void Objective(uint8_t *, int objective, void *)
{
	gObjectives += objective == 16;
}
static void Speech(uint8_t *, uint16_t) {}
int main()
{
	bool cutscene = false;
	int area = 0;
	drivers::gVehicles = &gPoolPtr;
	drivers::gCutscene = &cutscene;
	drivers::gArea = &area;
	drivers::gRef = Ref;
	drivers::gPedRef = Ref;
	drivers::gLast = gNow;
	game::api.timeMs = &gNow;
	game::api.FindPlayerPed = Player;
	game::api.GetPad = Pad;
	game::api.PadGetTarget = Target;
	game::api.PoolsGetVehicle = Car;
	*reinterpret_cast<int32_t *>(gPlayer + 0x44c) = 0x42c80000; // health100
	*reinterpret_cast<int32_t *>(gPlayer + 0x550) = 17;
	intimidation::Threat threat{true, true, true, false, false, false, 12, 0, 20};
	drivers::Driver record;
	record.seen = gNow;
	record.rule.Update(threat, .1f);
	drivers::gDrivers[17] = record;
	for (int i = 0; i < 12; i++) {
		gNow += 250;
		drivers::Frame();
	}
	if (drivers::gDrivers[17].rule.Update(threat, .3f) != intimidation::Response::None) {
		puts("FAIL: separated aim glimpses accumulated through the no-aim early return");
		return 1;
	}
	gConfig.driverReactions = false;
	drivers::Frame();
	if (!drivers::gDrivers.empty()) {
		puts("FAIL: disabled feature retained driver decisions");
		return 1;
	}
	gConfig.driverReactions = true;
	gAimed = true;
	*reinterpret_cast<CVector *>(gCamera + 0x30) = {10, 20, 2};
	*reinterpret_cast<CVector *>(gDriver + 0x38) = {10, 30, 1.5f};
	*reinterpret_cast<float *>(gDriver + 0x44c) = 100;
	*reinterpret_cast<int32_t *>(gDriver + 0x4f8) = 4;
	*reinterpret_cast<int32_t *>(gDriver + 0x2fc) = game::kPedDriving;
	gDriver[0x4c0] = 1;
	*reinterpret_cast<uint8_t **>(gDriver + 0x4b8) = gCar;
	*reinterpret_cast<CVector *>(gCar + 0x18) = {0, -1, 0};
	*reinterpret_cast<uint8_t **>(gCar + 0x218) = gDriver;
	gCar[0x294] = 1;
	*reinterpret_cast<uint8_t **>(gPool) = gCar;
	*reinterpret_cast<uint8_t **>(gPool + 8) = &gPoolFlag;
	*reinterpret_cast<int32_t *>(gPool + 0x10) = 1;
	game::api.theCamera = gCamera;
	game::api.ignoreEntity = &gIgnored;
	game::api.GetIsLineOfSightClear = Sight;
	game::api.SetObjective = Objective;
	game::api.Say = Speech;
	for (int i = 0; i < 8; i++) {
		gNow += 100;
		drivers::Frame();
	}
	if (!gObjectives || !gSightChecks || gSightFrom.x != 10 || gSightFrom.y != 20 || gSightFrom.z != 2 || gIgnored) {
		puts("FAIL: driver perception did not use the camera's actual matrix position");
		return 1;
	}
	puts("drivershook: all passed");
}
