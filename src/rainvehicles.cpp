#include "rainvehicles.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "minimap.h"
#include "rainmodel.h"
#include "shaders.h"
#include "vehiclerain.h"
#include "weather.h"

#include <algorithm>
#include <cmath>

using namespace game;
namespace rainvehicles {
namespace {

// CPhysical / CVehicle / CAutomobile (macVC 1.0.5217).
constexpr size_t kMatrix = 0x8, kMoveSpeed = 0x84, kTurnSpeed = 0x90, kVehicleType = 0x348;
constexpr size_t kWheelContacts = 0x4e0, kContactSize = 0x28, kContactSurface = 0x22; // CColPoint::surfaceB, the ground's
constexpr int32_t kCar = 0;
constexpr float kAirborne = 3.0f; // m clear of what's under it
constexpr float kWindPush = 0.04f; // m/s² of push per m/s of gust

void (*orig_ProcessWheel)(uint8_t *, float *, float *, float *, float *, int32_t, float, float, float, int8_t, float *, void *, uint16_t);
void (*orig_FlyingControl)(uint8_t *, int32_t);

// m/s² over this frame, in the game's units (m a frame, 50 frames a second).
float Step(float perSecondSquared) { return perSecondSquared / 2500.0f * (api.timeStep ? *api.timeStep : 1.0f); }

bool Airborne(const uint8_t *vehicle)
{
	const float *at = reinterpret_cast<const float *>(vehicle + kMatrix + 0x30);
	const mapdata::Map *map = minimap::RainMap();
	// What's under it (the cover grid's top: the ground, a roof): clear of it by kAirborne; out over the sea, high enough.
	if (map && mapdata::CoveredAt(*map, {at[0], at[1], at[2] - kAirborne}))
		return false;
	if (!map || !mapdata::CoveredAt(*map, {at[0], at[1], -1000}))
		return at[2] > 8.0f;
	return true;
}

} // namespace

void Hook_ProcessWheel(uint8_t *vehicle, float *fwd, float *right, float *contactSpeed, float *contactPoint, int32_t wheelsOnGround,
                       float thrust, float brake, float adhesion, int8_t wheel, float *wheelSpeed, void *state, uint16_t status)
{
	vehiclerain::Ground g{1, 1, 0};
	if (gConfig.rainVehicles && *reinterpret_cast<const int32_t *>(vehicle + kVehicleType) == kCar && wheel >= 0 && wheel < 4) {
		const uint8_t surface = vehicle[kWheelContacts + size_t(wheel) * kContactSize + kContactSurface];
		const rainmodel::Weather &w = weather::Now();
		g = vehiclerain::OnGround(surface, std::max(w.wetness, w.puddles)); // the ground stays soaked while the puddles stand
	}
	orig_ProcessWheel(vehicle, fwd, right, contactSpeed, contactPoint, wheelsOnGround, thrust * g.drive, brake, adhesion * g.grip,
	                  wheel, wheelSpeed, state, status);
	if (g.drag > 0 && wheelsOnGround > 0) {
		// Held back along the wheel (never pushed backwards: only what speed there is goes).
		float *v = reinterpret_cast<float *>(vehicle + kMoveSpeed);
		const float along = v[0] * fwd[0] + v[1] * fwd[1] + v[2] * fwd[2];
		const float cut = std::copysign(std::min(std::fabs(along), Step(g.drag) / float(wheelsOnGround)), along);
		for (int i = 0; i < 3; i++)
			v[i] -= fwd[i] * cut;
	}
}

void Hook_FlyingControl(uint8_t *vehicle, int32_t model)
{
	orig_FlyingControl(vehicle, model);
	if (!gConfig.rainVehicles || !api.FindPlayerPed)
		return;
	const uint8_t *player = api.FindPlayerPed();
	if (!player || !PedInVehicle(player) || PedVehicle(player) != vehicle) // his own aircraft
		return;
	const rainmodel::Weather &w = weather::Now();
	if (w.rain < rainmodel::kRaining || !Airborne(vehicle))
		return;
	const float seconds = shaders::Seconds();
	const rainmodel::Gust gust = rainmodel::GustAt(seconds);
	const vehiclerain::Air air = vehiclerain::InTheAir(w.rain, gust.strength, seconds, uint32_t(reinterpret_cast<uintptr_t>(vehicle) >> 4));
	const float *m = reinterpret_cast<const float *>(vehicle + kMatrix); // right, forward, up (16 bytes each)
	float *v = reinterpret_cast<float *>(vehicle + kMoveSpeed), *spin = reinterpret_cast<float *>(vehicle + kTurnSpeed);
	const float push[3] = {air.shove.x + gust.wind.x * kWindPush, air.shove.y + gust.wind.y * kWindPush, air.shove.z};
	for (int i = 0; i < 3; i++) {
		v[i] += Step(push[i]);
		spin[i] += Step(m[i] * air.twist.x + m[4 + i] * air.twist.y + m[8 + i] * air.twist.z); // its own axes, in the world
	}
}

bool Install()
{
	if (!gConfig.rainVehicles)
		return true;
	const bool wheels = weather::Install() &&
	                    (orig_ProcessWheel = reinterpret_cast<decltype(orig_ProcessWheel)>(hooks::Chain(
	                         "_ZN8CVehicle12ProcessWheelER7CVectorS1_S1_S1_ifffaPfP11tWheelStatet", reinterpret_cast<void *>(Hook_ProcessWheel))));
	const bool flying = (orig_FlyingControl = reinterpret_cast<decltype(orig_FlyingControl)>(
	                         hooks::Chain("_ZN8CVehicle13FlyingControlE12eFlightModel", reinterpret_cast<void *>(Hook_FlyingControl))));
	Log("rain vehicles: %s, %s", wheels ? "soaked ground under the wheels" : "wheel hook missing (re-run install.sh)",
	    flying ? "turbulence in the air" : "flying hook missing");
	return wheels && flying;
}

} // namespace rainvehicles
