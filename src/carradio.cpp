#include "carradio.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <dlfcn.h>

using namespace game;

namespace carradio {
namespace {

using Clock = std::chrono::steady_clock;

constexpr float kHearFrom = 18.0f;   // metres from the car: silent beyond
constexpr float kLoudestIn = 2.5f;   // ... and at its loudest within
constexpr float kOutside = 0.55f;    // of the in-car volume, even right by it
constexpr float kMuffleNear = 0.18f; // OpenAL low-pass gain of the highs, close by
constexpr float kMuffleFar = 0.06f;  // ... and at kHearFrom
constexpr float kOpenGain = 0.9f, kOpenHighs = 0.85f; // a bike, a boat, an open-top or doors-open car
constexpr float kPerOpenDoor = 0.4f;                  // how much each open (or missing) door lets out
constexpr float kKeepBias = 3.0f;    // metres the car already playing wins by, so it doesn't flip between two
constexpr float kStoppedBelow = 0.06f; // per-frame move speed (about 3 m/s): someone else's car counts while slower
constexpr auto kOwnCarPlays = std::chrono::minutes(3);
constexpr int kMusicStream = 0; // cSampleManager stream the radio plays on (1 and 2: mission audio)

// OpenAL EFX, from the game's OpenAL Soft.
using ALuint = unsigned int;
void (*alGenFilters)(int, ALuint *);
void (*alFilteri)(ALuint, int, int);
void (*alFilterf)(ALuint, int, float);
void (*alSourcei)(ALuint, int, int);
constexpr int kAlFilterType = 0x8001, kAlFilterLowpass = 1, kAlLowpassGain = 1, kAlLowpassGainHF = 2;
constexpr int kAlDirectFilter = 0x20005;

// CAutomobile: an open top, and its doors (eDoors 2-5: front left, front right, rear left, rear right).
bool (*IsOpenTopCar)(uint8_t *car);
bool (*IsDoorClosed)(uint8_t *car, int door);
bool (*IsDoorMissing)(uint8_t *car, int door);

uint8_t *(*orig_FindVehicleOfPlayer)(uint8_t *audio);
void (*orig_ServiceGameMode)(uint8_t *music);
void (*orig_SetStreamedVolumeAndPan)(uint8_t *samples, uint8_t volume, uint8_t pan, uint8_t effect, uint8_t stream);

uint8_t *gSampleManager, **gVehiclePool;
const ALuint *gStreamSources; // ALStreamSources[stream][2]
ALuint gFilter;
bool gFiltered;

bool gDeciding;   // inside cMusicManager::ServiceGameMode
uint8_t *gCar;    // the car being heard from outside, or null
uint8_t *gOwnCar; // the last one he drove
Clock::time_point gLeftOwnCar;
float gGain = 1.0f, gHighs = 1.0f;
uint8_t gPan = 63;
uint8_t gVolume, gEffect; // what the music manager last asked for
bool gHaveVolume, gLogged;

uint8_t *Driver(const uint8_t *vehicle) { return *reinterpret_cast<uint8_t *const *>(vehicle + 0x218); }
bool Wrecked(const uint8_t *vehicle) { return (vehicle[0x60] & 0xf8) == 0x28; } // STATUS_WRECKED
float Speed(const uint8_t *vehicle)
{
	const CVector &v = *reinterpret_cast<const CVector *>(vehicle + 0x84); // CPhysical::m_vecMoveSpeed
	return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}
float Distance(const CVector &a, const CVector &b)
{
	return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

// Every live vehicle (CPools::ms_pVehiclePool: entries, flags with 0x80 free).
template <typename Fn>
void ForEachVehicle(Fn fn)
{
	const uint8_t *pool = *gVehiclePool;
	if (!pool)
		return;
	const uint8_t *flags = *reinterpret_cast<uint8_t *const *>(pool + 8);
	const int size = *reinterpret_cast<const int32_t *>(pool + 0x10);
	for (int i = 0; i < size; i++)
		if (!(flags[i] & 0x80))
			if (uint8_t *v = api.PoolsGetVehicle((i << 8) | flags[i]))
				fn(v);
}

// The nearest car he'd hear: his own (recently left), or a slow or stopped one
// with a driver, with its radio on.
uint8_t *Pick(uint8_t *player)
{
	const Clock::time_point now = Clock::now();
	if (PedInVehicle(player)) {
		gOwnCar = PedVehicle(player);
		gLeftOwnCar = now;
		return nullptr;
	}
	const CVector &pos = EntityPos(player);
	uint8_t *best = nullptr;
	float bestDistance = kHearFrom;
	bool ownCarExists = false;
	ForEachVehicle([&](uint8_t *v) {
		const bool own = v == gOwnCar;
		ownCarExists = ownCarExists || own;
		if (own ? now - gLeftOwnCar > kOwnCarPlays : !Driver(v) || Speed(v) > kStoppedBelow)
			return;
		if (Wrecked(v) || VehicleRadio(v) >= kRadioOff || IsAircraft(v) || !RadioUsable(v))
			return;
		const float d = Distance(EntityPos(v), pos) - (v == gCar ? kKeepBias : 0.0f);
		if (d < bestDistance) {
			best = v;
			bestDistance = d;
		}
	});
	if (!ownCarExists)
		gOwnCar = nullptr; // gone (the pool reuses its slot)
	return best;
}

// How little is between the radio and the street: 1 for a bike, a boat or an
// open top, a share for each open door of a closed car.
float Openness(uint8_t *vehicle)
{
	const int32_t type = *reinterpret_cast<const int32_t *>(vehicle + 0x348); // 0 car, 1 boat, 5 bike
	if (type == 1 || type == 5)
		return 1.0f;
	if (type != 0 || !IsOpenTopCar)
		return 0.0f;
	if (IsOpenTopCar(vehicle))
		return 1.0f;
	float open = 0.0f;
	for (int door = 2; door <= 5; door++)
		if (IsDoorMissing(vehicle, door) || !IsDoorClosed(vehicle, door))
			open += kPerOpenDoor;
	return std::min(open, 1.0f);
}

// Volume and highs by distance from Tommy and how open the car is, pan by
// direction from the camera.
void Mix(const uint8_t *player)
{
	const CVector &car = EntityPos(gCar);
	const float t = std::clamp((Distance(car, EntityPos(player)) - kLoudestIn) / (kHearFrom - kLoudestIn), 0.0f, 1.0f);
	const float open = Openness(gCar);
	gGain = (kOutside + (kOpenGain - kOutside) * open) * (1.0f - t) * (1.0f - t);
	gHighs = kMuffleNear + (kMuffleFar - kMuffleNear) * t;
	gHighs += (kOpenHighs - gHighs) * open;
	const CVector &right = *reinterpret_cast<const CVector *>(api.theCamera);     // its matrix's right row
	const CVector &cam = *reinterpret_cast<const CVector *>(api.theCamera + 0x30); // and position
	const float dx = car.x - cam.x, dy = car.y - cam.y, len = std::max(std::hypot(dx, dy), 0.01f);
	gPan = uint8_t(std::clamp(63.0f + 63.0f * (dx * right.x + dy * right.y) / len, 0.0f, 127.0f));
}

// The low-pass on the radio stream's sources (left and right); highs < 0 takes it off.
void Muffle(float highs)
{
	if (!alGenFilters || !gStreamSources)
		return;
	const ALuint *sources = gStreamSources + 2 * kMusicStream;
	if (highs < 0.0f) {
		if (gFiltered)
			for (int c = 0; c < 2; c++)
				alSourcei(sources[c], kAlDirectFilter, 0);
		gFiltered = false;
		return;
	}
	if (!gFilter) {
		alGenFilters(1, &gFilter);
		alFilteri(gFilter, kAlFilterType, kAlFilterLowpass);
	}
	alFilterf(gFilter, kAlLowpassGain, 1.0f);
	alFilterf(gFilter, kAlLowpassGainHF, highs);
	for (int c = 0; c < 2; c++) // attached again so the new values take
		alSourcei(sources[c], kAlDirectFilter, int(gFilter));
	gFiltered = true;
}

uint8_t *Hook_FindVehicleOfPlayer(uint8_t *audio)
{
	uint8_t *vehicle = orig_FindVehicleOfPlayer(audio);
	return vehicle || !gDeciding ? vehicle : gCar;
}

void Hook_SetStreamedVolumeAndPan(uint8_t *samples, uint8_t volume, uint8_t pan, uint8_t effect, uint8_t stream)
{
	if (stream == kMusicStream) {
		gVolume = volume;
		gEffect = effect;
		gHaveVolume = true;
		if (gCar) {
			volume = uint8_t(volume * gGain);
			pan = gPan;
		}
	}
	orig_SetStreamedVolumeAndPan(samples, volume, pan, effect, stream);
}

void Hook_ServiceGameMode(uint8_t *music)
{
	uint8_t *player = api.FindPlayerPed();
	gCar = player && Health(player) > 0.0f ? Pick(player) : nullptr;
	if (gCar) {
		Mix(player);
		if (!gLogged) {
			gLogged = true;
			Log("carradio: playing a car's radio from outside");
		}
	}
	// Not his radio to retune: the station button isn't "just pressed" meanwhile.
	uint8_t *pad = api.GetPad(0);
	const int16_t oldLB = PadOld(pad, kPadLB);
	if (gCar)
		PadOld(pad, kPadLB) = PadNew(pad, kPadLB);
	gDeciding = true;
	orig_ServiceGameMode(music);
	gDeciding = false;
	if (gCar)
		PadOld(pad, kPadLB) = oldLB;
	// Again every frame: he moves while the music manager sets the volume only now and then.
	if (gCar && gHaveVolume)
		orig_SetStreamedVolumeAndPan(gSampleManager, uint8_t(gVolume * gGain), gPan, gEffect, kMusicStream);
	Muffle(gCar ? gHighs : -1.0f);
}

} // namespace

bool Install()
{
	if (!gConfig.carRadios)
		return true;
	gSampleManager = static_cast<uint8_t *>(dlsym(RTLD_MAIN_ONLY, "SampleManager"));
	gVehiclePool = static_cast<uint8_t **>(dlsym(RTLD_MAIN_ONLY, "_ZN6CPools15ms_pVehiclePoolE"));
	gStreamSources = static_cast<const ALuint *>(dlsym(RTLD_MAIN_ONLY, "ALStreamSources"));
	if (!gSampleManager || !gVehiclePool || !api.PoolsGetVehicle)
		return false;
	alGenFilters = reinterpret_cast<decltype(alGenFilters)>(dlsym(RTLD_DEFAULT, "alGenFilters"));
	alFilteri = reinterpret_cast<decltype(alFilteri)>(dlsym(RTLD_DEFAULT, "alFilteri"));
	alFilterf = reinterpret_cast<decltype(alFilterf)>(dlsym(RTLD_DEFAULT, "alFilterf"));
	alSourcei = reinterpret_cast<decltype(alSourcei)>(dlsym(RTLD_DEFAULT, "alSourcei"));
	IsOpenTopCar = reinterpret_cast<decltype(IsOpenTopCar)>(dlsym(RTLD_MAIN_ONLY, "_ZN11CAutomobile12IsOpenTopCarEv"));
	IsDoorClosed = reinterpret_cast<decltype(IsDoorClosed)>(dlsym(RTLD_MAIN_ONLY, "_ZN11CAutomobile12IsDoorClosedE6eDoors"));
	IsDoorMissing =
	    reinterpret_cast<decltype(IsDoorMissing)>(dlsym(RTLD_MAIN_ONLY, "_ZN11CAutomobile13IsDoorMissingE6eDoors"));
	if (!IsDoorClosed || !IsDoorMissing)
		IsOpenTopCar = nullptr;
	if (!alFilteri || !alFilterf || !alSourcei)
		alGenFilters = nullptr;
	if (!hooks::Install("_ZN13cAudioManager19FindVehicleOfPlayerEv", Hook_FindVehicleOfPlayer, orig_FindVehicleOfPlayer) ||
	    !hooks::Install("_ZN14cSampleManager23SetStreamedVolumeAndPanEhhhh", Hook_SetStreamedVolumeAndPan,
	                    orig_SetStreamedVolumeAndPan) ||
	    !hooks::Install("_ZN13cMusicManager15ServiceGameModeEv", Hook_ServiceGameMode, orig_ServiceGameMode)) {
		Log("carradio: hooks missing (re-run install.sh)");
		return true;
	}
	if (!alGenFilters)
		Log("carradio: no OpenAL filters, so car radios aren't muffled");
	return true;
}

} // namespace carradio
