#include "traffic.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <dlfcn.h>

using namespace game;

namespace traffic {
namespace {

constexpr size_t kLodDist = 0xf8;         // CCamera::LODDistMultiplier: how far things are drawn
constexpr size_t kGenerationDist = 0xfc;  // CCamera::GenerationDistMultiplier: where cars and people appear
// Faster, further: both grow by up to this much as the player's vehicle speeds up.
constexpr float kSpeedReach = 0.5f, kFullSpeed = 40.0f; // m/s
constexpr float kFadeIn = 1.0f;           // s for a new car or person to fade in (the game: a quarter)
constexpr size_t kPedSize = 0x8d0;        // a ped in CPools::ms_pPedPool
constexpr size_t kRwObject = 0x58;        // CEntity::m_rwObject (the clump)
constexpr int kModelsLoaded = 30;         // car models kept loaded (the game: 12; at most 50)
constexpr size_t kStreamingMemory = 256u << 20; // bytes the streamer may use (the port's ini: 128 MB)
constexpr int kTurnoverFrames = 90;       // between loading car models (the game: 350)
constexpr int kTries = 4;                 // picks of a car model, the least common kept
constexpr size_t kVehicleSize = 0x6f0;    // a vehicle in CPools::ms_pVehiclePool
constexpr size_t kModelIndex = 0x6a;      // CEntity::m_modelIndex (int16)
// CStreaming::StreamVehiclesAndPeds's countdown to the next car model (a
// static) and the instructions that set it to 350 (mov w8, #0x15e; str w8,
// [x20, #0x290]): checked, as another build has it elsewhere.
constexpr uintptr_t kModelTimer = 0x7c6290, kModelTimerSet = 0x1201fc;
constexpr uint32_t kModelTimerCode[2] = {0x52802bc8, 0xb9029288};

int32_t *gDesiredModels;              // desiredNumVehiclesLoaded
size_t *gStreamingMemory;             // CStreaming::ms_memoryAvailable
int32_t *gModelTimer;
uint8_t **gVehiclePool, **gPedPool;
int32_t (*orig_ChooseCarModel)(int32_t vehclass);
int (*gGetAlpha)(void *clump);
void (*gSetAlpha)(void *clump, int alpha);
bool gStarted;
float gReach = 1.0f; // eased speed reach

// One of the game's caps, scaled: whatever it sets is the base (it starts at
// 0 for cars and is set later; missions may change it).
struct Cap {
	int32_t *value = nullptr;
	int32_t base = 0, written = -1;
	void Apply(float scale)
	{
		if (!value)
			return;
		if (*value != written)
			base = *value;
		const int32_t want = int32_t(std::lround(base * scale));
		if (*value < want)
			*value = want;
		written = *value;
	}
};
Cap gCars, gPeds;

// Last frame's alpha of each car and person fading in.
struct Fading {
	const void *entity;
	int alpha;
};
constexpr int kMaxFading = 256;
Fading gFading[kMaxFading];
int gNumFading;

// New cars and people fade in over kFadeIn, not the game's quarter second:
// each frame their alpha goes up no faster than that (the game goes on adding
// its own step; anything going down is fading out, and left alone).
void SlowFadeIns(float dt)
{
	if (!gGetAlpha || !gSetAlpha || dt <= 0.0f)
		return;
	const int step = std::max(1, int(255.0f * dt / kFadeIn));
	Fading now[kMaxFading];
	int count = 0;
	auto visit = [&](uint8_t **poolPtr, size_t stride) {
		const uint8_t *pool = poolPtr ? *poolPtr : nullptr;
		if (!pool)
			return;
		uint8_t *entries = *reinterpret_cast<uint8_t *const *>(pool);
		const uint8_t *flags = *reinterpret_cast<uint8_t *const *>(pool + 0x8);
		const int32_t size = *reinterpret_cast<const int32_t *>(pool + 0x10);
		for (int32_t i = 0; i < size && count < kMaxFading; i++) {
			if (flags[i] & 0x80)
				continue;
			uint8_t *entity = entries + size_t(i) * stride;
			void *clump = *reinterpret_cast<void *const *>(entity + kRwObject);
			if (!clump)
				continue;
			int alpha = gGetAlpha(clump);
			if (alpha <= 0 || alpha >= 255)
				continue;
			for (int j = 0; j < gNumFading; j++)
				if (gFading[j].entity == entity) {
					if (alpha > gFading[j].alpha + step) {
						alpha = gFading[j].alpha + step;
						gSetAlpha(clump, alpha);
					}
					break;
				}
			now[count++] = {entity, alpha};
		}
	};
	visit(gPedPool, kPedSize);
	visit(gVehiclePool, kVehicleSize);
	std::copy(now, now + count, gFading);
	gNumFading = count;
}

// How many cars of a model are about.
int OnTheRoad(int32_t model)
{
	const uint8_t *pool = *gVehiclePool;
	if (!pool)
		return 0;
	const uint8_t *entries = *reinterpret_cast<uint8_t *const *>(pool);
	const uint8_t *flags = *reinterpret_cast<uint8_t *const *>(pool + 0x8);
	const int32_t size = *reinterpret_cast<const int32_t *>(pool + 0x10);
	int count = 0;
	for (int32_t i = 0; i < size; i++)
		if (!(flags[i] & 0x80) &&
		    *reinterpret_cast<const int16_t *>(entries + size_t(i) * kVehicleSize + kModelIndex) == model)
			count++;
	return count;
}

// A few picks of the zone's class (the game's own, weighted by how common
// each model is), keeping the one least on the road.
int32_t Hook_ChooseCarModel(int32_t vehclass)
{
	int32_t best = orig_ChooseCarModel(vehclass);
	if (!gConfig.carVariety || best < 0)
		return best;
	int fewest = OnTheRoad(best);
	for (int i = 1; i < kTries && fewest > 0; i++) {
		const int32_t model = orig_ChooseCarModel(vehclass);
		if (model < 0)
			continue;
		const int n = OnTheRoad(model);
		if (n < fewest) {
			best = model;
			fewest = n;
		}
	}
	return best;
}

} // namespace

void AfterCamera()
{
	const float scale = std::clamp(gConfig.spawnDistance, 1.0f, 4.0f);
	const float dt = *api.timeStep / 50.0f;
	// Once the game is running (it sets these up as it starts).
	if (!gStarted) {
		gStarted = true;
		if (gConfig.carVariety && gStreamingMemory && *gStreamingMemory < kStreamingMemory)
			*gStreamingMemory = kStreamingMemory;
		Log("traffic: spawn distance x%.1f (more at speed), %d car models loaded, fade-in %.1f s", scale,
		    gConfig.carVariety ? kModelsLoaded : (gDesiredModels ? *gDesiredModels : -1), kFadeIn);
	}
	if (scale > 1.0f) {
		// Further still as the player's vehicle speeds up: things come at you sooner.
		float speed = 0.0f;
		if (uint8_t *ped = api.FindPlayerPed(); ped && PedInVehicle(ped) && PedVehicle(ped)) {
			const CVector v = EntitySpeed(PedVehicle(ped));
			speed = std::sqrt(v.x * v.x + v.y * v.y) * 50.0f;
		}
		const float reach = 1.0f + kSpeedReach * std::clamp(speed / kFullSpeed, 0.0f, 1.0f);
		gReach += (reach - gReach) * std::min(1.0f, dt * 2.0f);
		*reinterpret_cast<float *>(api.theCamera + kGenerationDist) *= scale * gReach;
		*reinterpret_cast<float *>(api.theCamera + kLodDist) *= gReach;
		// Room for as many near you over the wider ring.
		gCars.Apply(scale);
		gPeds.Apply(scale);
		SlowFadeIns(dt);
	}
	if (gConfig.carVariety) {
		if (gDesiredModels && *gDesiredModels < kModelsLoaded)
			*gDesiredModels = kModelsLoaded;
		if (gModelTimer && *gModelTimer > kTurnoverFrames)
			*gModelTimer = kTurnoverFrames;
	}
}

bool Install()
{
	gCars.value = static_cast<int32_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN8CCarCtrl21MaxNumberOfCarsInUseE"));
	gPeds.value = static_cast<int32_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN11CPopulation20MaxNumberOfPedsInUseE"));
	gPedPool = static_cast<uint8_t **>(dlsym(RTLD_MAIN_ONLY, "_ZN6CPools11ms_pPedPoolE"));
	gGetAlpha = reinterpret_cast<int (*)(void *)>(dlsym(RTLD_MAIN_ONLY, "_ZN18CVisibilityPlugins13GetClumpAlphaEPN2rw5ClumpE"));
	gSetAlpha = reinterpret_cast<void (*)(void *, int)>(dlsym(RTLD_MAIN_ONLY, "_ZN18CVisibilityPlugins13SetClumpAlphaEPN2rw5ClumpEi"));
	gDesiredModels = static_cast<int32_t *>(dlsym(RTLD_MAIN_ONLY, "desiredNumVehiclesLoaded"));
	gStreamingMemory = static_cast<size_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN10CStreaming18ms_memoryAvailableE"));
	gVehiclePool = static_cast<uint8_t **>(dlsym(RTLD_MAIN_ONLY, "_ZN6CPools15ms_pVehiclePoolE"));
	auto *base = static_cast<uint8_t *>(const_cast<void *>(hooks::ImageBase()));
	const auto *code = reinterpret_cast<const uint32_t *>(base + kModelTimerSet);
	if (code[0] == kModelTimerCode[0] && code[1] == kModelTimerCode[1])
		gModelTimer = reinterpret_cast<int32_t *>(base + kModelTimer);
	else
		Log("traffic: the car model timer isn't where expected; car models turn over as before");
	if (!gVehiclePool || !hooks::Install("_ZN8CCarCtrl14ChooseCarModelEi", Hook_ChooseCarModel, orig_ChooseCarModel))
		return false;
	return true;
}

} // namespace traffic
