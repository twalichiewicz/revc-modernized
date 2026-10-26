#include "sight.h"
#include "alert.h"
#include "heat.h"
#include "log.h"
#include "wanted.h"

#include <algorithm>
#include <cmath>
#include <dlfcn.h>

using namespace game;

namespace sight {
namespace {

// --- The game (macVC 1.0.5217) -------------------------------------------------------------

constexpr size_t kPedSize = 0x8d0; // pool entries
constexpr size_t kPedIK = 0x2c0;   // CPed::m_pedIK
constexpr uint32_t kPedMid = 1, kPedHead = 2; // PedNode
constexpr int32_t kCop = 6;        // ePedType: every CCopPed (police, SWAT, FBI, army)
constexpr int kHelis = 4;          // CHeli::pHelis
constexpr size_t kHeliStatus = 0x390;
constexpr uint8_t kHeliShotDown = 3;
constexpr size_t kWanted = 0x778, kIgnored = 0x1e; // CWanted's ignored flags, bits 0-1
constexpr uint32_t kNoPolice = 0x10;               // CCullZones::CurrentFlags_Player

// How far and how wide they see: a cop on foot within 75 degrees of where he
// faces, one at the wheel 100 degrees of the car's way; a helicopter all round.
constexpr float kPedRange = 90.0f, kHeliRange = 225.0f;
constexpr float kOnFootCone = 75.0f, kDriverCone = 100.0f;
constexpr float kDriverEyes = 1.2f; // above a car's position
constexpr float kLookEvery = 0.05f; // s between looks

uint8_t **gPedPool, **gHelis;
uint32_t *gZoneFlags;
int (*gPedRef)(uint8_t *ped);
void (*gComponentPosition)(void *ik, CVector *pos, uint32_t node);
bool gReady, gSeen;
float gSince; // s since the last look
std::vector<Eye> gEyes;

CVector Part(uint8_t *ped, uint32_t node)
{
	CVector at{0, 0, 0};
	gComponentPosition(ped + kPedIK, &at, node);
	return at;
}
CVector Minus(const CVector &a, const CVector &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float Length(const CVector &v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

bool Alive(const uint8_t *ped)
{
	const int32_t state = PedState(ped);
	return Health(ped) > 0.0f && state != kPedDie && state != kPedDead;
}

// From `eyes`, looking along `forward`, within `cone` degrees (0: all round).
bool Sees(const CVector &eyes, const CVector &forward, float cone, float range, const CVector &him)
{
	const CVector d = Minus(him, eyes);
	const float dist = Length(d);
	if (dist > range)
		return false;
	if (cone > 0.0f) {
		const float flat = std::hypot(forward.x, forward.y), flatD = std::hypot(d.x, d.y);
		if (flat > 1e-3f && flatD > 1.0f &&
		    (forward.x * d.x + forward.y * d.y) / (flat * flatD) < std::cos(cone * 3.14159265f / 180.0f))
			return false;
	}
	// Walls and things stop it; glass and fences don't.
	return api.GetIsLineOfSightClear(eyes, him, true, false, false, true, false, true, false);
}

struct Looker {
	int handle;
	bool helicopter;
	CVector eyes;
	bool sees;
};

// Every cop and police helicopter, and whether each sees `him`.
std::vector<Looker> Look(uint8_t *player, const CVector &him)
{
	std::vector<Looker> found;
	const uint8_t *pool = gPedPool ? *gPedPool : nullptr;
	if (pool) {
		uint8_t *entries = *reinterpret_cast<uint8_t *const *>(pool);
		const uint8_t *flags = *reinterpret_cast<const uint8_t *const *>(pool + 0x8);
		const int32_t size = *reinterpret_cast<const int32_t *>(pool + 0x10);
		for (int i = 0; i < size; i++) {
			if (flags[i] & 0x80)
				continue;
			uint8_t *ped = entries + size_t(i) * kPedSize;
			if (ped == player || PedType(ped) != kCop || !Alive(ped))
				continue;
			Looker l{gPedRef(ped), false, {}, false};
			if (PedInVehicle(ped)) {
				const uint8_t *car = PedVehicle(ped);
				l.eyes = EntityPos(car);
				l.eyes.z += kDriverEyes;
				l.sees = Sees(l.eyes, EntityForward(car), kDriverCone, kPedRange, him);
			} else {
				l.eyes = Part(ped, kPedHead);
				l.sees = Sees(l.eyes, EntityForward(ped), kOnFootCone, kPedRange, him);
			}
			found.push_back(l);
		}
	}
	for (int i = 0; gHelis && i < kHelis; i++) {
		const uint8_t *heli = gHelis[i];
		if (!heli || heli[kHeliStatus] == kHeliShotDown)
			continue;
		Looker l{-1 - i, true, EntityPos(heli), false};
		l.eyes.z -= 1.0f; // its underside, out of its own body
		l.sees = Sees(l.eyes, {0, 0, 0}, 0.0f, kHeliRange, him);
		found.push_back(l);
	}
	return found;
}

} // namespace

void Update(float dt)
{
	if (!gReady || !wanted::Active())
		return;
	gSince += dt;
	if (gSince < kLookEvery)
		return;
	dt = std::min(gSince, 0.5f);
	gSince = 0.0f;
	const heat::Model &model = wanted::Model();
	const heat::State state = model.state();
	uint8_t *player = api.FindPlayerPed();
	uint8_t *wantedOf = player ? *reinterpret_cast<uint8_t **>(player + kWanted) : nullptr;
	if (!player || !wantedOf || state == heat::State::Clear || state == heat::State::Reported) {
		gSeen = false;
		gEyes.clear();
		return;
	}
	const bool unseeable = (wantedOf[kIgnored] & 3) || (gZoneFlags && (*gZoneFlags & kNoPolice));
	const CVector him = PedInVehicle(player) ? EntityPos(PedVehicle(player)) : Part(player, kPedMid);
	std::vector<Looker> lookers = unseeable ? std::vector<Looker>{} : Look(player, him);
	gSeen = std::any_of(lookers.begin(), lookers.end(), [](const Looker &l) { return l.sees; });
	if (state == heat::State::Wanted) {
		// They're on him: anyone seeing him keeps them there (and sees what he looks like now).
		gEyes.clear();
		if (gSeen)
			wanted::CopSees(him);
		return;
	}
	// Searching or under the APB: each one looking at him has to know him first.
	const bool matches = model.Matches(wanted::Looks());
	std::vector<Eye> eyes;
	for (const Looker &l : lookers) {
		auto it = std::find_if(gEyes.begin(), gEyes.end(), [&](const Eye &e) { return e.handle == l.handle; });
		Eye e = it != gEyes.end() ? *it : Eye{l.handle, l.helicopter, l.eyes, 0.0f};
		e.head = l.eyes;
		const float distance = Length(Minus(him, l.eyes));
		e.fill += l.sees ? heat::FillRate(distance, matches, l.helicopter) * dt : -heat::kEyeDecayPerSecond * dt;
		if (e.fill >= 1.0f) {
			Log("sight: recognised by %s at %.0f m (%s)", l.helicopter ? "the helicopter" : "a cop", distance,
			    matches ? "he matched the description" : "a close look");
			alert::Spotted();
			wanted::CopSees(him);
			gEyes.clear();
			return;
		}
		if (e.fill > 0.0f)
			eyes.push_back(e);
	}
	gEyes = std::move(eyes); // (a looker gone from the pool takes its eye with it)
	// The fullest eye, for the sound (only while one's filling: an emptying eye is quiet).
	float fullest = 0.0f;
	for (const Eye &e : gEyes)
		fullest = std::max(fullest, e.fill);
	if (fullest > 0.0f)
		alert::Spotting(fullest);
}

bool Seen() { return gSeen; }

bool CanSee(const CVector &at)
{
	if (!gReady)
		return false;
	uint8_t *player = api.FindPlayerPed();
	uint8_t *wantedOf = player ? *reinterpret_cast<uint8_t **>(player + kWanted) : nullptr;
	if (!wantedOf || (wantedOf[kIgnored] & 3) || (gZoneFlags && (*gZoneFlags & kNoPolice)))
		return false;
	const std::vector<Looker> lookers = Look(player, at);
	return std::any_of(lookers.begin(), lookers.end(), [](const Looker &l) { return l.sees; });
}

const std::vector<Eye> &Eyes() { return gEyes; }

bool Install()
{
	auto sym = [](const char *name) { return dlsym(RTLD_MAIN_ONLY, name); };
	gPedPool = static_cast<uint8_t **>(sym("_ZN6CPools11ms_pPedPoolE"));
	gHelis = static_cast<uint8_t **>(sym("_ZN5CHeli6pHelisE"));
	gZoneFlags = static_cast<uint32_t *>(sym("_ZN10CCullZones19CurrentFlags_PlayerE"));
	gPedRef = reinterpret_cast<decltype(gPedRef)>(sym("_ZN6CPools9GetPedRefEP4CPed"));
	gComponentPosition =
	    reinterpret_cast<decltype(gComponentPosition)>(sym("_ZN6CPedIK20GetComponentPositionERN2rw3V3dEj"));
	gReady = gPedPool && gHelis && gZoneFlags && gPedRef && gComponentPosition && api.GetIsLineOfSightClear;
	if (!gReady)
		Log("sight: a function is missing; nobody recognises him (the stars come back only by crimes)");
	return gReady;
}

} // namespace sight
