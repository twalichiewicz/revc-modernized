#include "doorgrab.h"
#include "config.h"
#include "game.h"
#include "grip.h"
#include "hooks.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <dlfcn.h>
#include <vector>

using namespace game;

namespace doorgrab {
namespace {

// CPed, CVehicle (macVC 1.0.5217).
constexpr size_t kDriver = 0x218, kVehicleType = 0x348, kTurnSpeed = 0x90, kAttachedTo = 0x720;
constexpr int32_t kCar = 0;                    // eVehicleType
constexpr int32_t kEnterAsDriver = 18;         // eObjective: jacking the driver (a cop: arresting him)
constexpr int32_t kDriving = 0x32, kOpenDoor = 0x35, kCarJack = 0x38, kDie = 0x36, kDead = 0x37; // ePedState
constexpr uint32_t kDoorLockedLeft = 77, kDoorLockedRight = 93; // CAR_doorlocked_LHS / RHS: tugging at the handle
constexpr int32_t kSkidBack = 27;               // KO_skid_back: down on his back, sliding
constexpr uint16_t kFacingRight = 3, kFacingLeft = 1; // CPed::PositionAttachedPed: a quarter right or left of the car's way
constexpr size_t kPedSize = 0x8d0;
// One already opening it gives up and hangs on once it's going this fast (m/s).
constexpr float kPullsAway = 3.0f;
// Letting go he keeps this much of the car's speed, and goes off its side this fast (m a frame), down this long (ms).
constexpr float kKeeps = 0.6f, kOffTheSide = 0.04f;
constexpr int32_t kDownMs = 1500;
constexpr float kLongest = 6.0f; // s: no one hangs on longer
constexpr int kMostHolding = 2;   // (one each side)

bool (*orig_CanPedEnterCar)(uint8_t *vehicle);
void (*orig_SeekCar)(uint8_t *ped);
void (*gAttach)(uint8_t *ped, uint8_t *to, CVector offset, uint16_t type, float turn, int32_t weapon);
void (*gDetach)(uint8_t *ped);
void (*gSetFall)(uint8_t *ped, int32_t ms, int32_t anim, uint8_t evenIfNotInControl);
void (*gQuitEnteringCar)(uint8_t *ped);
void *(*gBlendAnimation)(void *clump, int32_t group, uint32_t anim, float delta);
void *(*gGetAssociation)(void *clump, uint32_t anim);
int (*gPedRef)(uint8_t *ped);
int (*gVehicleRef)(uint8_t *vehicle);
uint8_t **gPedPool;
bool gReady;

uint8_t *gSeeker; // the ped in CPed::SeekCar now

struct Holding {
	int ped = -1, car = -1;
	bool left = true; // the car's left side (the driver's)
	grip::Hold hold;
	float time = 0.0f;
};
std::vector<Holding> gHolding;

float Speed(uint8_t *car)
{
	const CVector &v = EntitySpeed(car);
	return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z) * 50.0f;
}
float Turn(const uint8_t *car) { return reinterpret_cast<const CVector *>(car + kTurnSpeed)->z * 50.0f; }
CVector InCar(const uint8_t *car, const CVector &world)
{
	auto row = [&](int i) { return *reinterpret_cast<const CVector *>(car + 8 + 16 * i); };
	const CVector o = row(3), d{world.x - o.x, world.y - o.y, world.z - o.z};
	auto dot = [](const CVector &a, const CVector &b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
	return {dot(d, row(0)), dot(d, row(1)), dot(d, row(2))};
}

// The player's own car, him at the wheel.
bool HisCar(const uint8_t *car)
{
	const uint8_t *player = api.FindPlayerPed();
	return car && player && *reinterpret_cast<uint8_t *const *>(car + kDriver) == player && PedInVehicle(player) &&
	       PedVehicle(player) == car;
}

bool Alive(const uint8_t *ped) { return Health(ped) > 0.0f && PedState(ped) != kDie && PedState(ped) != kDead; }

Holding *Find(uint8_t *ped)
{
	const int handle = gPedRef(ped);
	for (Holding &h : gHolding)
		if (h.ped == handle)
			return &h;
	return nullptr;
}

void Tug(uint8_t *ped, bool left)
{
	void *clump = EntityClump(ped);
	const uint32_t anim = left ? kDoorLockedLeft : kDoorLockedRight;
	if (clump && !gGetAssociation(clump, anim))
		gBlendAnimation(clump, 0, anim, 8.0f);
}

// He's caught hold of the handle: held to the car where he stands at its door.
void Grab(uint8_t *ped, uint8_t *car)
{
	const CVector at = InCar(car, EntityPos(ped));
	const int handle = gVehicleRef(car);
	const bool taken = std::any_of(gHolding.begin(), gHolding.end(),
	                               [&](const Holding &h) { return h.car == handle && h.left == (at.x < 0.0f); });
	if (taken || gHolding.size() >= kMostHolding || Find(ped) || *reinterpret_cast<void *const *>(ped + kAttachedTo))
		return;
	Holding h;
	h.ped = gPedRef(ped);
	h.car = handle;
	h.left = at.x < 0.0f;
	gAttach(ped, car, at, h.left ? kFacingRight : kFacingLeft, 0.0f, WeaponType(PedWeapon(ped, PedCurrentWeaponSlot(ped))));
	Tug(ped, h.left);
	gHolding.push_back(h);
	Log("doorgrab: %s catches hold of his %s door at %.0f km/h", PedType(ped) == 6 ? "a cop" : "someone",
	    h.left ? "driver's" : "passenger's", Speed(car) * 3.6f);
}

// Let go: off the car, down on his back, sliding on with some of its speed.
void LetGo(uint8_t *ped, uint8_t *car, bool left)
{
	if (*reinterpret_cast<void *const *>(ped + kAttachedTo))
		gDetach(ped);
	if (!car || !Alive(ped))
		return;
	gSetFall(ped, kDownMs, kSkidBack, 1);
	const CVector &v = EntitySpeed(car), &right = *reinterpret_cast<const CVector *>(car + 8);
	const float out = left ? -kOffTheSide : kOffTheSide;
	EntitySpeed(ped) = {v.x * kKeeps + right.x * out, v.y * kKeeps + right.y * out, v.z * kKeeps};
	Log("doorgrab: he lets go at %.0f km/h", Speed(car) * 3.6f);
}

// Who's after his car's door: when one reaches it (CPed::SeekCar asks), as grip.h has it.
bool Hook_CanPedEnterCar(uint8_t *vehicle)
{
	uint8_t *ped = gSeeker;
	if (!ped || ped == api.FindPlayerPed() || PedObjective(ped) != kEnterAsDriver || !HisCar(vehicle))
		return orig_CanPedEnterCar(vehicle);
	if (Find(ped))
		return false; // hanging on: the door stays shut till it slows (Update lets him go to it)
	const bool car = *reinterpret_cast<const int32_t *>(vehicle + kVehicleType) == kCar;
	switch (grip::Reach(Speed(vehicle))) {
	case grip::AtDoor::kOpen:
		return orig_CanPedEnterCar(vehicle);
	case grip::AtDoor::kGrab:
		if (car && Alive(ped))
			Grab(ped, vehicle);
		return false;
	case grip::AtDoor::kMiss:
		return false;
	}
	return false;
}

void Hook_SeekCar(uint8_t *ped)
{
	uint8_t *was = gSeeker;
	gSeeker = ped;
	orig_SeekCar(ped);
	gSeeker = was;
}

// One opening his door as he pulls away: he stops, and hangs on instead.
void PullingAway(uint8_t *player, uint8_t *car)
{
	const uint8_t *pool = gPedPool ? *gPedPool : nullptr;
	if (!pool || PedState(player) != kDriving || Speed(car) < kPullsAway ||
	    *reinterpret_cast<const int32_t *>(car + kVehicleType) != kCar)
		return;
	uint8_t *entries = *reinterpret_cast<uint8_t *const *>(pool);
	const uint8_t *flags = *reinterpret_cast<const uint8_t *const *>(pool + 0x8);
	const int32_t size = *reinterpret_cast<const int32_t *>(pool + 0x10);
	for (int i = 0; i < size; i++) {
		if (flags[i] & 0x80)
			continue;
		uint8_t *ped = entries + size_t(i) * kPedSize;
		const int32_t state = PedState(ped);
		if (ped == player || (state != kCarJack && state != kOpenDoor) || PedVehicle(ped) != car ||
		    PedObjective(ped) != kEnterAsDriver || !Alive(ped))
			continue; // (a passenger getting in, Lance say, is left to it)
		gQuitEnteringCar(ped);
		Log("doorgrab: he pulls away as the door's opened");
		if (grip::Reach(Speed(car)) == grip::AtDoor::kGrab)
			Grab(ped, car);
	}
}

template <typename T>
bool Bind(T &out, const char *name)
{
	out = reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY, name));
	if (!out)
		Log("doorgrab: missing %s", name);
	return out != nullptr;
}

} // namespace

void Update()
{
	if (!gReady)
		return;
	const float dt = *api.timeStep / 50.0f;
	uint8_t *player = api.FindPlayerPed();
	if (player && PedInVehicle(player) && HisCar(PedVehicle(player)))
		PullingAway(player, PedVehicle(player));
	for (size_t i = 0; i < gHolding.size();) {
		Holding &h = gHolding[i];
		uint8_t *ped = api.PoolsGetPed(h.ped), *car = api.PoolsGetVehicle(h.car);
		bool done = !ped || !Alive(ped) || !car || Wrecked(car) ||
		            *reinterpret_cast<uint8_t *const *>(ped + kAttachedTo) != car;
		if (!done && !HisCar(car)) {
			// He's out of it (or someone else drives it): off it, falling only if it's moving.
			if (Speed(car) < grip::kOpenBelow)
				gDetach(ped);
			else
				LetGo(ped, car, h.left);
			done = true;
		} else if (!done) {
			h.time += dt;
			const grip::Now now = h.hold.Update(Speed(car), Turn(car), dt);
			if (now == grip::Now::kOpens) {
				gDetach(ped); // at the door: his next try opens it
				Log("doorgrab: it slows; he goes for the door");
				done = true;
			} else if (now == grip::Now::kLetsGo || h.time > kLongest) {
				LetGo(ped, car, h.left);
				done = true;
			} else {
				Tug(ped, h.left); // (his objective stays: he tries again once he's up)
			}
		} else if (ped && *reinterpret_cast<void *const *>(ped + kAttachedTo)) {
			gDetach(ped);
		}
		if (done)
			gHolding.erase(gHolding.begin() + long(i));
		else
			i++;
	}
}

bool Install()
{
	if (!gConfig.copsGrabDoors)
		return true;
	gReady = Bind(gAttach, "_ZN4CPed17AttachPedToEntityEP7CEntity7CVectortf11eWeaponType") &&
	         Bind(gDetach, "_ZN4CPed20DettachPedFromEntityEv") && Bind(gSetFall, "_ZN4CPed7SetFallEi11AnimationIdh") &&
	         Bind(gQuitEnteringCar, "_ZN4CPed15QuitEnteringCarEv") &&
	         Bind(gBlendAnimation, "_ZN12CAnimManager14BlendAnimationEPN2rw5ClumpE12AssocGroupId11AnimationIdf") &&
	         Bind(gGetAssociation, "_Z30RpAnimBlendClumpGetAssociationPN2rw5ClumpEj") &&
	         Bind(gPedRef, "_ZN6CPools9GetPedRefEP4CPed") && Bind(gVehicleRef, "_ZN6CPools13GetVehicleRefEP8CVehicle") &&
	         Bind(gPedPool, "_ZN6CPools11ms_pPedPoolE") && api.PoolsGetPed && api.PoolsGetVehicle && api.FindPlayerPed;
	if (gReady && (!hooks::Install("_ZN4CPed7SeekCarEv", Hook_SeekCar, orig_SeekCar) ||
	               !hooks::Install("_ZN8CVehicle14CanPedEnterCarEv", Hook_CanPedEnterCar, orig_CanPedEnterCar)))
		gReady = false;
	if (!gReady)
		Log("doorgrab: hooks or functions missing (re-run install.sh); cops open his door as the game had it");
	return gReady;
}

} // namespace doorgrab
