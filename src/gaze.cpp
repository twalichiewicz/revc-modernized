#include "gaze.h"
#include "aimcam.h"
#include "climb.h"
#include "config.h"
#include "fpwalk.h"
#include "game.h"
#include "gazemodel.h"
#include "log.h"
#include "reactions.h"
#include "swim.h"

#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <iterator>
#include <unordered_map>

using namespace game;

namespace gaze {
namespace {

void (*gLookAt)(uint8_t *ped, uint8_t *target, bool keepTrying, bool cancelPrevious);
void (*gClearLook)(uint8_t *ped);
bool *gCutscene, gInstalled;
// Peds near Tommy keep their glance state; forgotten once they're out of sight a while.
struct Watcher {
	gazemodel::Memory memory;
	uint32_t seen = 0;
};
std::unordered_map<const uint8_t *, Watcher> gWatchers;
uint8_t *gPlayerLook;    // the person he's looking at, to keep across frames
bool gPlayerDriving;     // his look is ours, so we end it

// CPed fields (source host layout; asserted in tools/host/SourceABI.cpp).
bool Looking(const uint8_t *ped) { return (ped[0x188] & 0x10) != 0; } // bIsLooking
float BodyHeading(const uint8_t *ped) { return *reinterpret_cast<const float *>(ped + 0x46c); } // m_fRotationCur
int32_t &LookFlags(uint8_t *ped) { return *reinterpret_cast<int32_t *>(ped + 0x2e8); }         // m_pedIK.m_flags
constexpr int32_t kHeadOnly = 2;                                                               // CPedIK::LOOKAROUND_HEAD_ONLY
bool Smoke() { static const bool on = std::getenv("VC_REGULAR_SMOKE") != nullptr; return on; }

// States in which a bystander has nothing better to do than look around.
bool Free(int32_t state)
{
	switch (state) {
	case 1:  // idle
	case 4:  // wandering about
	case 5:  // wandering a path
	case 6:  // walking somewhere
	case 11: // following a path
	case 15: // paused
	case 35: // jogging
		return true;
	default:
		return false;
	}
}
bool Alive(const uint8_t *ped) { return PedState(ped) != kPedDie && PedState(ped) != kPedDead && Health(ped) > 0; }
bool Armed(uint8_t *ped) { return WeaponType(PedWeapon(ped, PedCurrentWeaponSlot(ped))) >= 2; } // not fists or knuckles
gazemodel::Point Head(const uint8_t *ped)
{
	const CVector &p = EntityPos(ped);
	return gazemodel::Point{p.x, p.y, p.z + .6f};
}
// Two rolls in [0, 1) for a ped this frame, without a shared RNG the game uses.
void Rolls(const uint8_t *ped, uint32_t now, float &a, float &b)
{
	uint64_t h = reinterpret_cast<uintptr_t>(ped) * 0x9e3779b97f4a7c15ull ^ uint64_t(now) * 0xbf58476d1ce4e5b9ull;
	h ^= h >> 31;
	h *= 0x94d049bb133111ebull;
	h ^= h >> 29;
	a = float(h & 0xffffff) / 16777216.0f;
	b = float((h >> 24) & 0xffffff) / 16777216.0f;
}

void Player(uint8_t *player, const gazemodel::Seen *seen, uint8_t *const *peds, int count)
{
	const bool eligible = !aimcam::Aiming() && !fpwalk::Active() && !swim::Swimming() && !climb::Active() &&
			      !reactions::PlayerDown() && !PedInVehicle(player) && PedState(player) == kPedIdle &&
			      Alive(player);
	int current = -1;
	for (int i = 0; i < count; i++)
		if (peds[i] == gPlayerLook)
			current = seen[i].id;
	const gazemodel::Follow f =
		eligible ? gazemodel::Track(Head(player), BodyHeading(player), CameraHeading(), seen, count, current)
			 : gazemodel::Follow{};
	if (!f.look) {
		if (gPlayerDriving && eligible && gClearLook)
			gClearLook(player); // back to facing forward, eased by the game
		gPlayerDriving = false;
		gPlayerLook = nullptr;
		return;
	}
	// Kept up each frame; a short timer lets the game end it if we stop.
	const bool wasTarget = gPlayerLook != nullptr;
	if (f.target >= 0 && gLookAt) {
		gPlayerLook = peds[f.target];
		gLookAt(player, gPlayerLook, true, true);
	} else {
		gPlayerLook = nullptr;
		api.SetLookFlag(player, f.heading, true, true);
	}
	// Running, only his head turns: his shoulders keep to his stride.
	if (PedMoveState(player) >= 4)
		LookFlags(player) |= kHeadOnly;
	api.SetLookTimer(player, 250);
	if (Smoke() && (!gPlayerDriving || wasTarget != (gPlayerLook != nullptr)))
		std::fprintf(stderr, "[gaze] player look=%s heading=%.2f body=%.2f\n", gPlayerLook ? "person" : "camera", f.heading,
			     BodyHeading(player));
	gPlayerDriving = true;
}

} // namespace

bool Install()
{
	auto sym = [](const char *name) { return dlsym(RTLD_MAIN_ONLY, name); };
	gLookAt = reinterpret_cast<decltype(gLookAt)>(sym("_ZN4CPed11SetLookFlagEP7CEntitybb"));
	gClearLook = reinterpret_cast<decltype(gClearLook)>(sym("_ZN4CPed13ClearLookFlagEv"));
	gCutscene = static_cast<bool *>(sym("_ZN12CCutsceneMgr10ms_runningE"));
	gInstalled = gLookAt && gClearLook && api.SetLookFlag && api.SetLookTimer && api.timeMs && api.timeStep &&
		     api.FindPlayerPed;
	if (!gInstalled)
		Log("gaze: native look functions missing; head looks off");
	return true;
}

void Frame(uint8_t *player)
{
	if (!gInstalled || !gConfig.gaze || !player || player != api.FindPlayerPed() || (gCutscene && *gCutscene))
		return;
	const uint32_t now = *api.timeMs;
	const float dt = *api.timeStep / 50.0f;
	// Who's about: the game keeps Tommy's ten nearest peds.
	uint8_t *peds[10]{};
	gazemodel::Seen seen[11];
	int count = 0;
	for (int i = 0; i < PedNearbyCount(player) && i < 10; i++) {
		uint8_t *ped = PedNearby(player, i);
		if (!ped || !Alive(ped) || PedInVehicle(ped))
			continue;
		peds[count] = ped;
		seen[count] = gazemodel::Seen{count, Head(ped), false, false, PedMoveState(ped) == 5};
		count++;
	}
	Player(player, seen, peds, count);

	// Each bystander with nothing to do may glance at Tommy or at someone else near.
	const gazemodel::Seen tommy{100, Head(player), true, Armed(player), PedMoveState(player) == 5};
	for (int i = 0; i < count; i++) {
		uint8_t *ped = peds[i];
		Watcher &w = gWatchers[ped];
		w.seen = now;
		if (!Free(PedState(ped)) || PedFromMission(ped))
			continue;
		if (Looking(ped) && now >= w.memory.until)
			continue; // someone else's look (the game's, or combat's): leave it
		gazemodel::Seen others[11];
		int n = 0;
		others[n++] = tommy;
		for (int j = 0; j < count; j++)
			if (j != i)
				others[n++] = seen[j];
		float a, b;
		Rolls(ped, now, a, b);
		const gazemodel::Glance g =
			gazemodel::Choose(Head(ped), BodyHeading(ped), others, n, w.memory, now, dt, a, b);
		if (!g)
			continue;
		uint8_t *target = g.target == tommy.id ? player : peds[g.target];
		gLookAt(ped, target, false, false);
		api.SetLookTimer(ped, int(g.ms));
		if (Smoke())
			std::fprintf(stderr, "[gaze] glance at=%s ms=%u armed=%d state=%d looking=%d\n", target == player ? "tommy" : "ped", g.ms,
				     int(tommy.armed), PedState(ped), int(Looking(ped)));
	}
	// Forget peds not near him for ten seconds (a pointer may be reused).
	for (auto it = gWatchers.begin(); it != gWatchers.end();)
		it = now - it->second.seen > 10000 ? gWatchers.erase(it) : std::next(it);
}

} // namespace gaze
