#include "wanted.h"
#include "config.h"
#include "log.h"
#include "sight.h"
#include "witness.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <string>
#include <sys/stat.h>

using namespace game;

namespace wanted {
namespace {

// --- The game (macVC 1.0.5217) -------------------------------------------------------------

// CWanted, at CPlayerPed::m_pWanted (CWanted::SetWantedLevel, CWanted::Suspend):
// chaos (the points the stars come from), when it was last suspended (only
// CGarage::Update suspends it, for a Pay 'n' Spray), the stars.
constexpr size_t kWanted = 0x778;
constexpr size_t kChaos = 0x0, kSuspendedAt = 0x10, kLevel = 0x20;
// One star from SetWantedLevel is 70 chaos; CWanted::Update takes one a second
// off it with no cop within 18 m and drops the star under 50.
constexpr int32_t kOneStarChaos = 70, kOneStarRunningDown = 55;
// CVehicle colours (the CAutomobile constructor, into ChooseVehicleColour).
constexpr size_t kColour1 = 0x210, kColour2 = 0x211;
constexpr int32_t kPedArrested = 62; // ePedState
constexpr size_t kLawFlags = 0x295;  // CVehicle: a law enforcer's (bit 0)
// CPickup (64 bytes): model (int16) at +0x2a, the handle's counter at +0x2c,
// type at +0x36 (0: none), removed at +0x37.
constexpr size_t kPickupModel = 0x2a, kPickupCounter = 0x2c, kPickupType = 0x36, kPickupRemoved = 0x37;
// The game's save slots, GTAVCsf1.b to GTAVCsf8.b (9 is the mission replay's).
constexpr int kSaveSlots = 8;

constexpr uint32_t kClothesWindowMs = 3000; // a drop and an outfit change this close are a clothes change
constexpr uint32_t kSuspendedWithinMs = 2000;
constexpr uint32_t kSummaryMs = 30000;

void (*gSetWantedLevel)(uint8_t *wanted, int32_t level);
void (*gRemovePickUp)(int32_t handle);
bool (*gOnAMission)();
bool *gCutscene;       // CCutsceneMgr::ms_running
int16_t *gBribeModel;  // MI_PICKUP_BRIBE
bool gReady;

heat::Model gModel;
int gExpected;                 // the game's level, as last seen or set
uint32_t gLastTime, gLastSummary, gLastBribes, gLastSaveCheck;
bool gWasDown, gPending, gLoggedTimeout;
uint32_t gPendingSince;        // a drop waiting on the clothes window
std::string gOutfit;
uint32_t gOutfitChangedAt;
heat::State gWasState = heat::State::Clear;
time_t gApbSince;              // wall clock, against the saves' times
bool gBribesLogged;

Vec3 V(const CVector &v) { return {v.x, v.y, v.z}; }

uint8_t *WantedOf(uint8_t *player) { return *reinterpret_cast<uint8_t **>(player + kWanted); }
int32_t &Field(uint8_t *wanted, size_t off) { return *reinterpret_cast<int32_t *>(wanted + off); }

CVector Position(const uint8_t *player)
{
	return PedInVehicle(player) ? EntityPos(PedVehicle(player)) : EntityPos(player);
}

heat::Looks LooksOf(const uint8_t *player)
{
	heat::Looks l;
	// The player's model is model 0 (CPed::Undress loads the new outfit into it).
	if (const uint8_t *mi = api.modelInfoPtrs ? api.modelInfoPtrs[0] : nullptr) {
		const char *name = reinterpret_cast<const char *>(mi + 8);
		l.outfit.assign(name, strnlen(name, 24));
	}
	if (PedInVehicle(player)) {
		const uint8_t *v = PedVehicle(player);
		l.vehicleModel = EntityModel(v);
		l.colour1 = v[kColour1];
		l.colour2 = v[kColour2];
	}
	return l;
}

bool Down(const uint8_t *player)
{
	const int32_t state = PedState(player);
	return Health(player) <= 0.0f || state == kPedDie || state == kPedDead || state == kPedArrested;
}

// Sets the game's level, and takes what it gave (a mission's cap holds it lower).
void SetLevel(uint8_t *wanted, int level)
{
	gSetWantedLevel(wanted, level);
	gExpected = Field(wanted, kLevel);
	if (gExpected < level && gModel.gameStars() > gExpected)
		gModel.Lowered(gExpected);
}

bool SavedSince(time_t since)
{
	const std::string dir = SupportDir();
	for (int i = 1; i <= kSaveSlots; i++) {
		char path[32];
		snprintf(path, sizeof path, "/GTAVCsf%d.b", i);
		struct stat st;
		if (stat((dir + path).c_str(), &st) == 0 && st.st_mtime > since)
			return true;
	}
	return false;
}

void RemoveBribes(uint32_t now)
{
	if (now - gLastBribes < 2000 || !api.pickups)
		return;
	gLastBribes = now;
	int removed = 0;
	for (int i = 0; i < kNumPickups; i++) {
		const uint8_t *p = Pickup(i);
		if (!p[kPickupType] || p[kPickupRemoved] || *reinterpret_cast<const int16_t *>(p + kPickupModel) != *gBribeModel)
			continue;
		gRemovePickUp(i | int32_t(*reinterpret_cast<const uint16_t *>(p + kPickupCounter)) << 16);
		removed++;
	}
	if (removed && !gBribesLogged) {
		Log("wanted: %d police bribes taken off the map", removed);
		gBribesLogged = true;
	}
}

const char *StateName(heat::State s)
{
	switch (s) {
	case heat::State::Clear: return "clear";
	case heat::State::Reported: return "reported (a call going through)";
	case heat::State::Wanted: return "wanted";
	case heat::State::Searching: return "searching";
	case heat::State::Apb: return "APB";
	}
	return "?";
}

// The description, as "outfit player, car 145 (1/2)".
std::string Describe(const heat::Description &d)
{
	char s[96] = "nothing";
	if (d.haveOutfit && d.haveVehicle)
		snprintf(s, sizeof s, "outfit %s, car %d (%d/%d)", d.outfit.c_str(), d.vehicleModel, d.colour1, d.colour2);
	else if (d.haveOutfit)
		snprintf(s, sizeof s, "outfit %s", d.outfit.c_str());
	else if (d.haveVehicle)
		snprintf(s, sizeof s, "car %d (%d/%d)", d.vehicleModel, d.colour1, d.colour2);
	return s;
}

void LogChange(heat::State was, heat::State now)
{
	switch (now) {
	case heat::State::Wanted:
		Log("wanted: %s -> wanted (%d stars, seen at %.0f,%.0f; they have %s)", StateName(was), gModel.stars(),
		    gModel.lastSeen().x, gModel.lastSeen().y, Describe(gModel.description()).c_str());
		break;
	case heat::State::Searching:
		Log("wanted: %s -> searching (%d stars, centre %.0f,%.0f, radius %.0f m)", StateName(was), gModel.stars(),
		    gModel.searchCentre().x, gModel.searchCentre().y, gModel.searchRadius());
		break;
	case heat::State::Apb:
		Log("wanted: %s -> APB (escaped at %d stars; hollow until a save)", StateName(was), gModel.escapedStars());
		break;
	default:
		Log("wanted: %s -> %s", StateName(was), StateName(now));
		break;
	}
}

void Summary(const uint8_t *player, int level, uint32_t now)
{
	if (gModel.state() == heat::State::Clear || now - gLastSummary < kSummaryMs)
		return;
	gLastSummary = now;
	const heat::Looks l = LooksOf(player);
	Log("wanted: %s at %d stars (the game's %d); they have %s; he matches %s%s%s", StateName(gModel.state()),
	    gModel.stars(), level, Describe(gModel.description()).c_str(), gModel.Matches(l) ? "it" : "nothing",
	    gModel.OutfitStruck(l) ? ", outfit changed" : "", gModel.VehicleStruck(l) ? ", car changed" : "");
}

// Everything the police knew gone, and the calls about it with it (a load
// leaves the peds alone: their handles are the old game's).
void ClearAll(bool loaded)
{
	gModel.Clear();
	witness::Reset(!loaded);
}

// The game's level against what we last saw or set: up is a cop's (or a
// script's) sighting, down is undone or followed (see heat::ClassifyDrop).
void Follow(uint8_t *wanted, const heat::Looks &looks, const CVector &him, uint32_t now)
{
	const int g = Field(wanted, kLevel);
	if (g > gExpected) {
		gModel.CopSees(looks, V(him), now, g);
		gExpected = g;
		gPending = false;
		return;
	}
	if (g == gExpected) {
		gPending = false;
		return;
	}
	if (!gPending) {
		gPending = true;
		gPendingSince = now;
	}
	const uint32_t suspendedAt = uint32_t(Field(wanted, kSuspendedAt));
	const heat::Drop drop = heat::ClassifyDrop({g, Field(wanted, kChaos),
	                                            suspendedAt && now - suspendedAt < kSuspendedWithinMs,
	                                            gOnAMission && gOnAMission(),
	                                            gOutfitChangedAt && now - gOutfitChangedAt <= kClothesWindowMs,
	                                            now - gPendingSince >= kClothesWindowMs});
	switch (drop) {
	case heat::Drop::Wait:
		return;
	case heat::Drop::Respray:
		Log("wanted: respray: stars kept (%d)", gExpected);
		break;
	case heat::Drop::Clothes:
		Log("wanted: clothes: stars kept (%d)", gExpected);
		break;
	case heat::Drop::Timeout:
		if (!gLoggedTimeout)
			Log("wanted: the game's one star running down: kept");
		gLoggedTimeout = true;
		break;
	case heat::Drop::Script:
		gPending = false;
		if (g == 0) {
			if (gModel.state() != heat::State::Clear)
				Log("wanted: cleared by the game (script, cheat, wasted or busted)");
			ClearAll(false);
		} else {
			Log("wanted: lowered by the game to %d", g);
			gModel.Lowered(g);
		}
		gExpected = g;
		return;
	}
	gPending = false;
	SetLevel(wanted, gExpected);
}

} // namespace

bool Install()
{
	auto sym = [](const char *name) { return dlsym(RTLD_MAIN_ONLY, name); };
	gSetWantedLevel = reinterpret_cast<decltype(gSetWantedLevel)>(sym("_ZN7CWanted14SetWantedLevelEi"));
	gRemovePickUp = reinterpret_cast<decltype(gRemovePickUp)>(sym("_ZN8CPickups12RemovePickUpEi"));
	gOnAMission = reinterpret_cast<decltype(gOnAMission)>(sym("_ZN11CTheScripts18IsPlayerOnAMissionEv"));
	gCutscene = static_cast<bool *>(sym("_ZN12CCutsceneMgr10ms_runningE"));
	gBribeModel = static_cast<int16_t *>(sym("MI_PICKUP_BRIBE"));
	gReady = gSetWantedLevel && gRemovePickUp && gOnAMission && gCutscene && gBribeModel && api.FindPlayerPed &&
	         api.modelInfoPtrs && api.pickups;
	sight::Install();
	witness::Install();
	if (!gReady)
		Log("wanted: a function is missing; the game's wanted system as it was");
	else if (gConfig.realisticWanted)
		Log("wanted: the police know him by what they've seen: witnesses, his clothes and car, a search, an APB "
		    "until he saves");
	return gReady;
}

bool Active() { return gReady && gConfig.realisticWanted; }

const heat::Model &Model() { return gModel; }

heat::Looks Looks()
{
	const uint8_t *player = api.FindPlayerPed();
	return player ? LooksOf(player) : heat::Looks{};
}

bool OnHim() { return !Active() || gModel.Knows(sight::Seen(), *api.timeMs); }

bool InPoliceCar()
{
	const uint8_t *player = Active() ? api.FindPlayerPed() : nullptr;
	return player && PedInVehicle(player) && PedVehicle(player) && (PedVehicle(player)[kLawFlags] & 1);
}

CVector Target()
{
	const uint8_t *player = api.FindPlayerPed();
	const CVector real = player ? Position(player) : CVector{0, 0, 0};
	if (OnHim())
		return real;
	const Vec3 v = gModel.state() == heat::State::Wanted ? gModel.lastSeen() : gModel.searchCentre();
	return {v.x, v.y, v.z};
}

void CopSees(const CVector &at, int atLeast)
{
	uint8_t *player = Active() ? api.FindPlayerPed() : nullptr;
	uint8_t *wanted = player ? WantedOf(player) : nullptr;
	if (!wanted)
		return;
	gModel.CopSees(LooksOf(player), V(at), *api.timeMs, std::max(Field(wanted, kLevel), atLeast));
	gExpected = Field(wanted, kLevel); // (seen: the game's level rising with it is this)
}

void Reported(const heat::Looks *seen, const CVector &at, uint32_t when, int stars)
{
	uint8_t *player = Active() ? api.FindPlayerPed() : nullptr;
	uint8_t *wanted = player ? WantedOf(player) : nullptr;
	if (!wanted)
		return;
	gModel.Reported(seen, V(at), when, *api.timeMs, stars);
	gExpected = Field(wanted, kLevel); // (the call's own stars: not a cop's sighting)
}

int ShownStars() { return Active() ? gModel.stars() : 0; }

StarLook Look()
{
	switch (gModel.state()) {
	case heat::State::Searching: return StarLook::Dim;
	case heat::State::Apb: return StarLook::Hollow;
	default: return StarLook::Filled;
	}
}

void Update()
{
	if (!Active())
		return;
	uint8_t *player = api.FindPlayerPed();
	uint8_t *wanted = player ? WantedOf(player) : nullptr;
	if (!wanted)
		return;
	const uint32_t now = *api.timeMs;
	// A game loaded (time back, or on a long way): what the police knew is gone.
	if (gLastTime && (now < gLastTime || now - gLastTime > 10000)) {
		if (gModel.state() != heat::State::Clear)
			Log("wanted: a game loaded; what the police knew is gone");
		ClearAll(true);
		gExpected = Field(wanted, kLevel);
		gPending = false;
		gOutfit.clear();
		gOutfitChangedAt = 0;
	}
	gLastTime = now;
	if (*gCutscene)
		return;
	RemoveBribes(now);
	const heat::Looks looks = LooksOf(player);
	if (looks.outfit != gOutfit) {
		if (!gOutfit.empty())
			gOutfitChangedAt = now ? now : 1;
		gOutfit = looks.outfit;
	}
	// Wasted or busted clears everything; the game's own reset follows at the respawn.
	const bool down = Down(player);
	if (down || gWasDown) {
		if (down && !gWasDown && gModel.state() != heat::State::Clear)
			Log("wanted: wasted or busted: cleared");
		if (down)
			ClearAll(false);
		gWasDown = down;
		gExpected = Field(wanted, kLevel);
		gPending = false;
		if (down)
			return;
	}
	const CVector him = Position(player);
	Follow(wanted, looks, him, now);
	// In one of their cars they always know where he is (once they're after him at all).
	if (InPoliceCar() && gModel.state() != heat::State::Clear && gModel.state() != heat::State::Reported)
		gModel.CopSees(looks, V(him), now, Field(wanted, kLevel));
	sight::Update(*api.timeStep / 50.0f);
	witness::Update(now);
	gModel.Calling(witness::Calling() > 0.0f);
	gModel.Tick(V(him), now);
	if (!gPending) {
		if (gModel.gameStars() != Field(wanted, kLevel))
			SetLevel(wanted, gModel.gameStars());
		// One star doesn't run down on its own: the search decides when it's over.
		if (Field(wanted, kLevel) == 1 && Field(wanted, kChaos) < kOneStarRunningDown)
			Field(wanted, kChaos) = kOneStarChaos;
	}
	// (When it began, before any save is looked at: only a save made since clears it.)
	if (gModel.state() == heat::State::Apb && gWasState != heat::State::Apb)
		gApbSince = time(nullptr);
	if (gModel.state() == heat::State::Apb && now - gLastSaveCheck >= 1000) {
		gLastSaveCheck = now;
		if (SavedSince(gApbSince)) {
			Log("wanted: saved: APB cleared");
			ClearAll(false);
		}
	}
	const heat::State state = gModel.state();
	if (state != gWasState) {
		LogChange(gWasState, state);
		if (state == heat::State::Clear)
			gLoggedTimeout = false;
		gWasState = state;
		gLastSummary = now;
	}
	Summary(player, Field(wanted, kLevel), now);
}

} // namespace wanted
