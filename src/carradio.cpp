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
void (*alSource3f)(ALuint, int, float, float, float);
constexpr int kAlPosition = 0x1004;
constexpr int kAlFilterType = 0x8001, kAlFilterLowpass = 1, kAlLowpassGain = 1, kAlLowpassGainHF = 2;
constexpr int kAlDirectFilter = 0x20005;

// CAutomobile: an open top, and its doors (eDoors 2-5: front left, front right, rear left, rear right).
bool (*IsOpenTopCar)(uint8_t *car);
bool (*IsDoorClosed)(uint8_t *car, int door);
bool (*IsDoorMissing)(uint8_t *car, int door);

// The radio stream stuck repeating a moment of its song (the port's streaming,
// sometimes after a pause): playing, but its place in the song doesn't move.
// It's stopped and the music manager forgets what it was playing, so it starts
// the station again where it would be by now.
constexpr auto kStuckWindow = std::chrono::seconds(4);
constexpr int32_t kStuckBelow = 2000;       // ms the song moves on in that time, at least
constexpr auto kRestartsApart = std::chrono::seconds(20);
constexpr size_t kPlayingTrack = 0x3988;    // cMusicManager::m_nPlayingTrack
// cMusicManager::m_bGameplayAllowsRadio: last frame's, which ServiceGameMode
// takes as "the radio's already on" (m_bRadioStreamReady) and so keeps the
// station it was playing. Off, it starts the car's own (GetCarTuning).
constexpr size_t kGameplayAllowsRadio = 0x3996;
constexpr int32_t kNoTrack = 0x4c9;         // NO_TRACK
bool (*IsStreamPlaying)(uint8_t *samples, uint8_t stream);
int32_t (*GetStreamedFilePosition)(uint8_t *samples, uint8_t stream);
void (*StopStreamedFile)(uint8_t *samples, uint8_t stream);

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
uint8_t *gHeard;  // the car the music manager played last frame (his, or one outside)
Clock::time_point gLeftOwnCar;
float gGain = 1.0f, gHighs = 1.0f;
uint8_t gPan = 63;
uint8_t gVolume, gEffect; // what the music manager last asked for
bool gHaveVolume, gLogged;

uint8_t *Driver(const uint8_t *vehicle) { return *reinterpret_cast<uint8_t *const *>(vehicle + 0x218); }
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

bool HasRadioOn(uint8_t *v) { return !Wrecked(v) && VehicleRadio(v) < kRadioOff && !IsAircraft(v) && RadioUsable(v); }

// Getting into a car (opening it, pulling someone out): ePedState.
constexpr int32_t kPedCarjack = 56, kPedEnterCar = 58, kPedStealCar = 59;

// The nearest car he'd hear: his own (recently left), or a slow or stopped one
// with a driver, with its radio on. Getting into a car, only that one (none,
// for a police car: its radio is the scanner, inside).
uint8_t *Pick(uint8_t *player)
{
	const Clock::time_point now = Clock::now();
	if (PedInVehicle(player)) {
		gOwnCar = PedVehicle(player);
		gLeftOwnCar = now;
		return nullptr;
	}
	const int32_t state = PedState(player);
	if (state == kPedCarjack || state == kPedEnterCar || state == kPedStealCar) {
		uint8_t *entering = PedVehicle(player);
		return entering && HasRadioOn(entering) ? entering : nullptr;
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
		if (!HasRadioOn(v))
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

// From outside, the car is where the music comes from: both of the stream's
// sources (its left and right, which CStream::SetPan only spreads apart, so
// the music stayed wide on his ears like headphones) put the way to the car,
// in the listener's frame (they're relative to it: x right, y up, -z ahead).
// The game's own SetPan (on his own radio) puts them back.
bool gPlaced;
void Place()
{
	if (!alSource3f || !gStreamSources || !gCar)
		return;
	const CVector &right = *reinterpret_cast<const CVector *>(api.theCamera);         // its matrix (no vtable)
	const CVector &ahead = *reinterpret_cast<const CVector *>(api.theCamera + 0x10);
	const CVector &up = *reinterpret_cast<const CVector *>(api.theCamera + 0x20);
	const CVector &cam = *reinterpret_cast<const CVector *>(api.theCamera + 0x30);
	const CVector &car = EntityPos(gCar);
	CVector d{car.x - cam.x, car.y - cam.y, car.z + 0.5f - cam.z};
	const float len = std::max(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z), 0.01f);
	d = {d.x / len, d.y / len, d.z / len};
	const float x = d.x * right.x + d.y * right.y + d.z * right.z, y = d.x * up.x + d.y * up.y + d.z * up.z,
	            z = -(d.x * ahead.x + d.y * ahead.y + d.z * ahead.z);
	const ALuint *sources = gStreamSources + 2 * kMusicStream;
	for (int c = 0; c < 2; c++)
		alSource3f(sources[c], kAlPosition, x, y, z);
	gPlaced = true;
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
	if (stream == kMusicStream && gCar)
		Place(); // (after SetPan, which spread them out again)
}

// Every frame of the game's music (not while paused): is his radio moving on?
void Watch(uint8_t *music, uint8_t *player)
{
	static Clock::time_point windowAt, lastAt, restartedAt;
	static int32_t from = -1, furthest = -1; // where the window started, the furthest it's got
	const Clock::time_point now = Clock::now();
	const bool playing = IsStreamPlaying && player && (PedInVehicle(player) || gCar) &&
	                     IsStreamPlaying(gSampleManager, kMusicStream);
	if (!playing || now - lastAt > std::chrono::milliseconds(500)) // stopped, or the game was paused
		from = -1;
	lastAt = now;
	if (!playing)
		return;
	// Stuck, it goes back and forth over the same moment: the furthest it's got
	// is what counts. Far back is a fresh start (another track, or the station
	// going round to its start).
	const int32_t at = GetStreamedFilePosition(gSampleManager, kMusicStream);
	if (from < 0 || at < furthest - 10000) {
		from = furthest = at;
		windowAt = now;
	}
	furthest = std::max(furthest, at);
	if (now - windowAt < kStuckWindow)
		return;
	if (furthest - from < kStuckBelow && now - restartedAt > kRestartsApart) {
		Log("carradio: the radio was stuck repeating (%d ms on in %d s); started it again", furthest - from,
		    int(std::chrono::duration_cast<std::chrono::seconds>(kStuckWindow).count()));
		StopStreamedFile(gSampleManager, kMusicStream);
		*reinterpret_cast<int32_t *>(music + kPlayingTrack) = kNoTrack;
		restartedAt = now;
		from = -1;
		return;
	}
	from = furthest = at;
	windowAt = now;
}

void Hook_ServiceGameMode(uint8_t *music)
{
	uint8_t *player = api.FindPlayerPed();
	gCar = gConfig.carRadios && player && Health(player) > 0.0f ? Pick(player) : nullptr;
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
	// Another car than the one it was playing (his own once he's in, after the
	// one he heard outside): that car's radio, from its start, not the station on.
	uint8_t *heard = player && PedInVehicle(player) ? PedVehicle(player) : gCar;
	if (heard && gHeard && heard != gHeard)
		music[kGameplayAllowsRadio] = 0;
	gHeard = heard;
	gDeciding = true;
	orig_ServiceGameMode(music);
	gDeciding = false;
	if (gCar)
		PadOld(pad, kPadLB) = oldLB;
	// Again every frame: he moves while the music manager sets the volume only now and then.
	if (gCar && gHaveVolume) {
		orig_SetStreamedVolumeAndPan(gSampleManager, uint8_t(gVolume * gGain), gPan, gEffect, kMusicStream);
		Place();
	} else if (gPlaced && gHaveVolume) { // (his own radio again: SetPan puts its two sides back)
		orig_SetStreamedVolumeAndPan(gSampleManager, gVolume, 63, gEffect, kMusicStream);
		gPlaced = false;
	}
	Muffle(gCar ? gHighs : -1.0f);
	Watch(music, player);
}

} // namespace

bool Install()
{
	gSampleManager = static_cast<uint8_t *>(dlsym(RTLD_MAIN_ONLY, "SampleManager"));
	gVehiclePool = static_cast<uint8_t **>(dlsym(RTLD_MAIN_ONLY, "_ZN6CPools15ms_pVehiclePoolE"));
	gStreamSources = static_cast<const ALuint *>(dlsym(RTLD_MAIN_ONLY, "ALStreamSources"));
	if (!gSampleManager || !gVehiclePool || !api.PoolsGetVehicle)
		return false;
	alGenFilters = reinterpret_cast<decltype(alGenFilters)>(dlsym(RTLD_DEFAULT, "alGenFilters"));
	alFilteri = reinterpret_cast<decltype(alFilteri)>(dlsym(RTLD_DEFAULT, "alFilteri"));
	alFilterf = reinterpret_cast<decltype(alFilterf)>(dlsym(RTLD_DEFAULT, "alFilterf"));
	alSourcei = reinterpret_cast<decltype(alSourcei)>(dlsym(RTLD_DEFAULT, "alSourcei"));
	alSource3f = reinterpret_cast<decltype(alSource3f)>(dlsym(RTLD_DEFAULT, "alSource3f"));
	IsOpenTopCar = reinterpret_cast<decltype(IsOpenTopCar)>(dlsym(RTLD_MAIN_ONLY, "_ZN11CAutomobile12IsOpenTopCarEv"));
	IsDoorClosed = reinterpret_cast<decltype(IsDoorClosed)>(dlsym(RTLD_MAIN_ONLY, "_ZN11CAutomobile12IsDoorClosedE6eDoors"));
	IsDoorMissing =
	    reinterpret_cast<decltype(IsDoorMissing)>(dlsym(RTLD_MAIN_ONLY, "_ZN11CAutomobile13IsDoorMissingE6eDoors"));
	if (!IsDoorClosed || !IsDoorMissing)
		IsOpenTopCar = nullptr;
	IsStreamPlaying = reinterpret_cast<decltype(IsStreamPlaying)>(dlsym(RTLD_MAIN_ONLY, "_ZN14cSampleManager15IsStreamPlayingEh"));
	GetStreamedFilePosition = reinterpret_cast<decltype(GetStreamedFilePosition)>(
	    dlsym(RTLD_MAIN_ONLY, "_ZN14cSampleManager23GetStreamedFilePositionEh"));
	StopStreamedFile = reinterpret_cast<decltype(StopStreamedFile)>(dlsym(RTLD_MAIN_ONLY, "_ZN14cSampleManager16StopStreamedFileEh"));
	if (!GetStreamedFilePosition || !StopStreamedFile)
		IsStreamPlaying = nullptr;
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
