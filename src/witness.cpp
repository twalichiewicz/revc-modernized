#include "witness.h"
#include "log.h"
#include "police.h"
#include "sight.h"
#include "wanted.h"

#include <algorithm>
#include <cmath>
#include <dlfcn.h>
#include <unordered_map>

using namespace game;

namespace witness {
namespace {

// --- The game (macVC 1.0.5217) -------------------------------------------------------------

// CWanted's queue of crimes (CWanted::AddCrimeToQ, UpdateCrimesQ): 16 of
// 0x1c: type (eCrimeType, 0: none), id (the victim), when, where, reported,
// the police don't care.
constexpr size_t kWanted = 0x778, kChaos = 0x0, kLevel = 0x20, kCrimes = 0x28, kCrimeSize = 0x1c;
constexpr int32_t kOneStarChaos = 50; // CWanted::UpdateWantedLevel: one star from this much chaos
constexpr int kNumCrimes = 16;
constexpr size_t kCrimeType = 0x0, kCrimeId = 0x4, kCrimeAt = 0xc, kCrimeReported = 0x18, kCrimeDoesntCare = 0x19;
// CPed::m_pedStats (CPed::SetPedStats); CPedStats' fear, temper, lawfulness.
constexpr size_t kPedStats = 0x500, kFear = 0x24, kTemper = 0x25, kLawfulness = 0x26;
// gPhoneInfo (CPhoneInfo::FindNearestFreePhone): how many phones, then the
// phones (0x58 each: where at +0, state at +0x50, 0 free).
constexpr size_t kPhones = 0x8, kPhoneSize = 0x58, kPhoneState = 0x50;
// CVehicle::m_nAlarmState: -1 armed, more than 0 sounding (ms left).
constexpr size_t kAlarm = 0x214;
constexpr size_t kPedIK = 0x2c0;              // CPed::m_pedIK
constexpr uint32_t kPedMid = 1, kPedHead = 2; // PedNode
constexpr int32_t kCivMale = 4, kCivFemale = 5, kCop = 6; // ePedType
constexpr int32_t kWaitOnFoot = 1, kRunToArea = 25;      // eObjective
constexpr int kMobileModel = 258;                         // MI_MOBILE (CPed::AnswerMobile)
constexpr int kAnimPhoneOut = 0xa5, kAnimPhoneTalk = 0xa6; // standard group
// The rich (default.ide): the RI and BU peds, the golfers, the suit-stat women.
constexpr int kRich[] = {13, 14, 15, 16, 21, 22, 35, 36, 37, 42, 53, 54, 55, 56, 62, 63, 64, 67, 68, 69};
// The game's points for each eCrimeType (CWanted::ReportCrimeNow): 0, nothing to report.
constexpr int kPoints[] = {0, 0, 5, 45, 30, 80, 15, 10, 5, 5, 18, 80, 400, 20, 80, 20, 500, 25, 35, 100};
// What each eCrimeType is, for the log.
constexpr const char *kCrimeName[] = {"none",       "a gun",       "hitting someone", "hitting a cop",
                                      "a shooting", "shooting a cop", "a car stolen", "a red light",
                                      "reckless driving", "speeding", "running someone over",
                                      "running a cop over", "shooting at the helicopter", "someone set on fire",
                                      "a cop set on fire", "a car set on fire", "the Cessna", "an explosion",
                                      "an assault", "an assault on a cop"};

// --- Tuning --------------------------------------------------------------------------------

constexpr float kSeesCrime = 40.0f;      // m: who can see it
constexpr int kMostWitnesses = 3;        // a crime
constexpr float kPhoneShare = 0.35f;     // rich peds with a brick phone
constexpr uint32_t kMobileMs = 4000;     // a brick phone call, till it's through
constexpr uint32_t kPayphoneMs = 6000;   // a payphone call
constexpr float kPhoneReach = 250.0f;    // m: how far they'll run for a payphone
constexpr float kAtPhone = 1.5f;
constexpr float kToldACop = 20.0f;       // m: one with no phone tells a cop this near
constexpr uint32_t kGiveUpMs = 90000;    // running that long, they've given up
constexpr uint32_t kAlarmMs = 8000;      // a car alarm, till someone's called it in
constexpr float kHearsAlarm = 60.0f;
constexpr uint32_t kGotAwayMs = 20000;   // one who ran off (out of sight, or gone) calls it in after this
constexpr uint32_t kRadioMs = 3000;      // cops who heard it, on the radio
constexpr uint32_t kWindowMs[2] = {20000, 35000}; // someone in a building, on the phone
constexpr float kWindowReach = 40.0f;    // m: the buildings that look onto it
constexpr int kWindowRays = 8;
constexpr float kHeardOff = 0.25f, kHeardOffMost = 40.0f; // how far off the spot they place it, by distance
constexpr int kGunStars = 2;             // gun violence (shots, shootings, explosions): two stars at least

// How far a crime is heard (gunfire 150 m, explosions 250 m, screams 50 m), and
// whether it's gun violence, by eCrimeType.
constexpr float kHeard[] = {0, 150, 50, 50, 150, 150, 0, 0, 0, 0, 50, 50, 150, 50, 50, 250, 250, 250, 50, 50};
constexpr bool kGunViolence[] = {false, true, false, false, true, true,  false, false, false, false,
                                 false, false, true,  false, false, true, true,  true,  false, false};
bool Gun(int32_t crime) { return crime > 0 && crime < int32_t(std::size(kGunViolence)) && kGunViolence[crime]; }
float Heard(int32_t crime) { return crime > 0 && crime < int32_t(std::size(kHeard)) ? kHeard[crime] : 0.0f; }

void (*gReportCrimeNow)(uint8_t *wanted, int32_t type, const CVector &at, bool policeDoesntCare);
int32_t (*gPolicePresence)(CVector at, float radius);
void (*gSetObjectiveAt)(uint8_t *ped, int32_t objective, CVector at);
void (*gSetObjective)(uint8_t *ped, int32_t objective);
void (*gClearObjective)(uint8_t *ped);
void (*gSetFlee)(uint8_t *ped, uint8_t *from, int32_t ms);
void (*gAnswerMobile)(uint8_t *ped);
void (*gHangUp)(uint8_t *ped);
int (*gPedRef)(uint8_t *ped);
int (*gVehicleRef)(uint8_t *vehicle);
void (*gComponentPosition)(void *ik, CVector *pos, uint32_t node);
uint8_t **gPedPool;
uint8_t *gPhoneInfo;
bool gReady;

// A call coming: a witness (on a brick phone, running for a payphone,
// running off), someone in a building, or cops who heard it (no ped: those
// can't be stopped).
enum class How : uint8_t { Mobile, Payphone, Flee, Building, Heard };
struct Witness {
	int ped;             // -1: a building's, or the cops'
	How how;
	int32_t crime;
	CVector at;          // the crime (where they'll say)
	bool sawHim, doesntCare;
	uint32_t since;      // when they set off
	uint32_t calling;    // when the call began (0: not yet)
	CVector phone;       // the payphone
	uint32_t due = 0;    // when it goes through (Flee, Building, Heard)
	heat::Looks seen = {}; // what he looked like then, if they saw him
};
std::vector<Witness> gWitnesses;
std::unordered_map<int, bool> gHasPhone; // rolled once a ped
int gAlarmCar = -1;
uint32_t gAlarmAt;
CVector gAlarmWhere;
uint32_t gRandom = 0x7f4a7c15u;

float Random01()
{
	gRandom = gRandom * 1664525u + 1013904223u;
	return float(gRandom >> 8) / float(1u << 24);
}
float Flat(const CVector &a, const CVector &b) { return std::hypot(a.x - b.x, a.y - b.y); }
CVector Part(uint8_t *ped, uint32_t node)
{
	CVector at{0, 0, 0};
	gComponentPosition(ped + kPedIK, &at, node);
	return at;
}
bool Clear(const CVector &a, const CVector &b) { return api.GetIsLineOfSightClear(a, b, true, false, false, true, false, true, false); }

bool Down(const uint8_t *ped)
{
	const int32_t state = PedState(ped);
	return Health(ped) <= 0.0f || state == kPedFall || state == kPedDie || state == kPedDead;
}

// The game's own rule for who calls the police (CCivilianPed::ProcessControl).
bool WouldCall(const uint8_t *ped)
{
	const uint8_t *stats = *reinterpret_cast<uint8_t *const *>(ped + kPedStats);
	if (!stats)
		return false;
	const int8_t fear = int8_t(stats[kFear]), temper = int8_t(stats[kTemper]), law = int8_t(stats[kLawfulness]);
	return fear <= law && temper <= fear;
}

bool HasPhone(uint8_t *ped)
{
	const int model = EntityModel(ped);
	if (std::find(std::begin(kRich), std::end(kRich), model) == std::end(kRich))
		return false;
	if (gHasPhone.size() > 512)
		gHasPhone.clear();
	const int handle = gPedRef(ped);
	auto it = gHasPhone.find(handle);
	if (it == gHasPhone.end())
		it = gHasPhone.emplace(handle, Random01() < kPhoneShare).first;
	return it->second;
}

// The nearest payphone free (and nobody of ours running for it) within reach.
bool NearestPhone(const CVector &from, CVector &out)
{
	const int count = *reinterpret_cast<const int32_t *>(gPhoneInfo);
	float best = kPhoneReach;
	bool found = false;
	for (int i = 0; i < count && i < 64; i++) {
		const uint8_t *phone = gPhoneInfo + kPhones + size_t(i) * kPhoneSize;
		if (*reinterpret_cast<const int32_t *>(phone + kPhoneState) != 0)
			continue;
		const CVector at = *reinterpret_cast<const CVector *>(phone);
		const float d = Flat(at, from);
		const bool taken = std::any_of(gWitnesses.begin(), gWitnesses.end(), [&](const Witness &w) {
			return w.how == How::Payphone && Flat(w.phone, at) < 0.5f;
		});
		if (d < best && !taken) {
			best = d;
			out = at;
			found = true;
		}
	}
	return found;
}

template <typename Each> void ForEachPed(Each &&each)
{
	const uint8_t *pool = gPedPool ? *gPedPool : nullptr;
	if (!pool)
		return;
	uint8_t *entries = *reinterpret_cast<uint8_t *const *>(pool);
	const uint8_t *flags = *reinterpret_cast<const uint8_t *const *>(pool + 0x8);
	const int32_t size = *reinterpret_cast<const int32_t *>(pool + 0x10);
	for (int i = 0; i < size; i++)
		if (!(flags[i] & 0x80))
			each(entries + size_t(i) * 0x8d0);
}

// The witness's ped, if it's one and still about (-1: a building's or the cops' call).
uint8_t *PedOf(const Witness &w) { return w.ped >= 0 ? api.PoolsGetPed(w.ped) : nullptr; }

bool Civilian(const uint8_t *ped)
{
	const int32_t type = PedType(ped);
	return (type == kCivMale || type == kCivFemale) && !PedFromMission(ped) && !PedInVehicle(ped) && !Down(ped);
}

// Those who saw a crime and will call it in: up to three, the nearest first.
void Witnesses(uint8_t *player, int32_t crime, uint32_t victim, const CVector &at, bool doesntCare, uint32_t now)
{
	struct Seen {
		uint8_t *ped;
		float d;
	};
	std::vector<Seen> seen;
	const CVector over{at.x, at.y, at.z + 1.0f};
	ForEachPed([&](uint8_t *ped) {
		if (ped == player || !Civilian(ped) || uint32_t(reinterpret_cast<uintptr_t>(ped)) == victim)
			return;
		if (std::any_of(gWitnesses.begin(), gWitnesses.end(), [&](const Witness &w) { return PedOf(w) == ped; }))
			return; // already on the phone about something
		const float d = Flat(EntityPos(ped), at);
		if (d <= kSeesCrime && WouldCall(ped) && Clear(Part(ped, kPedHead), over))
			seen.push_back({ped, d});
	});
	std::sort(seen.begin(), seen.end(), [](const Seen &a, const Seen &b) { return a.d < b.d; });
	const CVector him = PedInVehicle(player) ? EntityPos(PedVehicle(player)) : Part(player, kPedMid);
	for (size_t k = 0; k < seen.size() && k < size_t(kMostWitnesses); k++) {
		uint8_t *ped = seen[k].ped;
		Witness w{gPedRef(ped), How::Flee, crime, at, Clear(Part(ped, kPedHead), him), doesntCare, now, 0, {0, 0, 0}};
		if (w.sawHim)
			w.seen = wanted::Looks();
		const char *what = crime > 0 && crime < int32_t(std::size(kCrimeName)) ? kCrimeName[crime] : "a crime";
		if (HasPhone(ped)) {
			w.how = How::Mobile;
			w.calling = now ? now : 1;
			api.RequestModel(kMobileModel, 0);
			gSetObjective(ped, kWaitOnFoot);
			gAnswerMobile(ped);
			Log("witness: a rich one saw %s%s and calls it in on a brick phone", what, w.sawHim ? " (and him)" : "");
		} else if (NearestPhone(EntityPos(ped), w.phone)) {
			w.how = How::Payphone;
			gSetObjectiveAt(ped, kRunToArea, w.phone);
			Log("witness: someone saw %s%s and runs for a payphone %.0f m off", what, w.sawHim ? " (and him)" : "",
			    Flat(EntityPos(ped), w.phone));
		} else {
			gSetFlee(ped, player, int32_t(kGiveUpMs));
			w.due = now + kGotAwayMs;
			Log("witness: someone saw %s%s; no phone near, they run off to call it in (or tell a cop they meet)",
			    what, w.sawHim ? " (and him)" : "");
		}
		gWitnesses.push_back(w);
	}
}

// Cops near enough to hear it (one on foot or at the wheel, a unit on
// patrol): on the radio in a moment, a search about where it seemed to be.
bool HeardBy(int32_t crime, const CVector &at, uint32_t now, bool doesntCare)
{
	const float reach = Heard(crime);
	if (reach <= 0.0f)
		return false;
	float nearest = police::UnitWithin(at, reach);
	ForEachPed([&](uint8_t *cop) {
		if (PedType(cop) == kCop && !Down(cop))
			nearest = std::min(nearest, Flat(EntityPos(cop), at));
	});
	if (nearest > reach)
		return false;
	// Where they reckon it was: off a way, more the further off they were.
	const float off = std::min(kHeardOffMost, nearest * kHeardOff) * Random01(), a = Random01() * 6.2831853f;
	Witness w{-1, How::Heard, crime, {at.x + off * std::cos(a), at.y + off * std::sin(a), at.z}, false, doesntCare,
	          now, 0, {0, 0, 0}, now + kRadioMs};
	gWitnesses.push_back(w);
	Log("witness: cops %.0f m off heard %s", nearest, crime < int32_t(std::size(kCrimeName)) ? kCrimeName[crime] : "it");
	return true;
}

// Built up about it (buildings within 40 m on a good few sides): someone at
// a window calls it in, and nothing stops that; one such call at a time.
void Windows(int32_t crime, const CVector &at, uint32_t now, bool doesntCare)
{
	if (!api.ProcessLineOfSight || std::any_of(gWitnesses.begin(), gWitnesses.end(),
	                                           [](const Witness &w) { return w.how == How::Building; }))
		return;
	int faces = 0;
	const CVector from{at.x, at.y, at.z + 2.0f};
	for (int k = 0; k < kWindowRays; k++) {
		const float a = 6.2831853f * k / kWindowRays;
		alignas(16) uint8_t point[64] = {};
		uint8_t *hit = nullptr;
		faces += api.ProcessLineOfSight(from, {from.x + kWindowReach * std::cos(a), from.y + kWindowReach * std::sin(a), from.z},
		                                point, hit, true, false, false, false, false, true, false, false);
	}
	if (faces < 3 || Random01() > float(faces) / kWindowRays)
		return;
	const uint32_t due = now + kWindowMs[0] + uint32_t(Random01() * float(kWindowMs[1] - kWindowMs[0]));
	gWitnesses.push_back({-1, How::Building, crime, at, false, doesntCare, now, 0, {0, 0, 0}, due});
	Log("witness: someone in a building saw it (%d of %d sides built up): called in in %u s, can't be stopped", faces,
	    kWindowRays, (due - now) / 1000);
}

// The game's queue (where he was at each crime): what the game filed itself
// (a cop within 14 m: CEventList::ReportCrimeForEvent; this build queues those
// unreported too) is left be, what a cop or the helicopter can see from
// further off is filed at once, and the rest goes to the witnesses instead.
void Intercept(uint8_t *player, uint8_t *wanted, uint32_t now)
{
	for (int i = 0; i < kNumCrimes; i++) {
		uint8_t *c = wanted + kCrimes + size_t(i) * kCrimeSize;
		const int32_t type = *reinterpret_cast<const int32_t *>(c + kCrimeType);
		if (!type || c[kCrimeReported])
			continue;
		c[kCrimeReported] = 1; // (CWanted::UpdateCrimesQ won't file it)
		const CVector at = *reinterpret_cast<const CVector *>(c + kCrimeAt);
		const bool doesntCare = c[kCrimeDoesntCare] != 0;
		const int least = Gun(type) ? kGunStars : 0;
		if (gPolicePresence(at, 14.0f)) { // (filed by the game; gun violence two stars)
			if (least)
				wanted::CopSees(at, least);
			continue;
		}
		if (sight::CanSee({at.x, at.y, at.z + 0.5f})) {
			gReportCrimeNow(wanted, type, at, doesntCare);
			wanted::CopSees(at, least);
			continue;
		}
		const heat::State state = wanted::Model().state();
		const int points = type > 0 && type < int32_t(std::size(kPoints)) ? kPoints[type] : 0;
		const bool heard = HeardBy(type, at, now, doesntCare);
		// Worth a call: a star in it (the game's points on what's there already),
		// gun violence, or anything while they're after him or searching for
		// him; under the APB, only if he still looks like it (else he's a
		// stranger, and a scooter taken is nothing).
		const int32_t chaos = *reinterpret_cast<const int32_t *>(wanted + kChaos);
		const bool worth = least || chaos + points >= kOneStarChaos || state == heat::State::Wanted ||
		                   state == heat::State::Searching ||
		                   (state == heat::State::Apb && points > 0 && wanted::Model().Matches(wanted::Looks()));
		if (!worth)
			continue;
		Witnesses(player, type, *reinterpret_cast<const uint32_t *>(c + kCrimeId), at, doesntCare, now);
		if (!heard)
			Windows(type, at, now, doesntCare);
	}
}

// Through: the game's points for the crime, and what they told the police.
void Report(uint8_t *wanted, const Witness &w)
{
	const int before = *reinterpret_cast<const int32_t *>(wanted + kLevel);
	gReportCrimeNow(wanted, w.crime, w.at, w.doesntCare);
	// (Gun violence two stars at least; whatever cops heard, a search at least.)
	const int least = Gun(w.crime) ? kGunStars : w.how == How::Heard ? 1 : 0;
	const int stars = std::max(*reinterpret_cast<const int32_t *>(wanted + kLevel), least);
	const bool apb = wanted::Model().state() == heat::State::Apb;
	// Not worth a star (the game's points), unless they saw him and he's on an APB.
	// (Under the APB only someone on its description is him: a stranger's petty crime isn't.)
	if (stars == 0 && !(apb && w.sawHim && wanted::Model().Matches(w.seen))) {
		Log("witness: the call got through, not worth a star");
		return;
	}
	static const char *const kWho[] = {"a brick phone", "a payphone", "one who got away", "a building", "the cops' radio"};
	Log("witness: the call got through (%s; %s; %d stars, were %d)", kWho[int(w.how)],
	    w.sawHim ? "they saw him" : "only where", stars, before);
	wanted::Reported(w.sawHim ? &w.seen : nullptr, w.at, w.since, std::max(stars, 1));
}

void Alarms(uint8_t *player, uint8_t *wanted, uint32_t now)
{
	uint8_t *car = PedInVehicle(player) ? PedVehicle(player) : PedCarInObjective(player);
	if (car) {
		const int16_t alarm = *reinterpret_cast<const int16_t *>(car + kAlarm);
		if (alarm > 0 && gVehicleRef(car) != gAlarmCar) {
			// Someone near to hear it?
			const CVector at = EntityPos(car);
			bool heard = false;
			ForEachPed([&](uint8_t *ped) { heard |= ped != player && Civilian(ped) && Flat(EntityPos(ped), at) < kHearsAlarm; });
			gAlarmCar = gVehicleRef(car); // (once a car)
			if (heard) {
				gAlarmAt = now + kAlarmMs;
				gAlarmWhere = at;
				Log("witness: a car alarm, and someone near to hear it: called in in %u s", kAlarmMs / 1000);
			}
		}
	}
	if (gAlarmAt && now >= gAlarmAt) {
		gAlarmAt = 0;
		const int stars = std::max(1, *reinterpret_cast<const int32_t *>(wanted + kLevel));
		Log("witness: the car alarm's been called in (where only)");
		wanted::Reported(nullptr, gAlarmWhere, now - kAlarmMs, stars);
	}
}

} // namespace

bool Install()
{
	auto sym = [](const char *name) { return dlsym(RTLD_MAIN_ONLY, name); };
	gReportCrimeNow = reinterpret_cast<decltype(gReportCrimeNow)>(sym("_ZN7CWanted14ReportCrimeNowE10eCrimeTypeRK7CVectorb"));
	gPolicePresence = reinterpret_cast<decltype(gPolicePresence)>(sym("_ZN7CWanted21WorkOutPolicePresenceE7CVectorf"));
	gSetObjectiveAt = reinterpret_cast<decltype(gSetObjectiveAt)>(sym("_ZN4CPed12SetObjectiveE10eObjective7CVector"));
	gSetObjective = reinterpret_cast<decltype(gSetObjective)>(sym("_ZN4CPed12SetObjectiveE10eObjective"));
	gClearObjective = reinterpret_cast<decltype(gClearObjective)>(sym("_ZN4CPed14ClearObjectiveEv"));
	gSetFlee = reinterpret_cast<decltype(gSetFlee)>(sym("_ZN4CPed7SetFleeEP7CEntityi"));
	gAnswerMobile = reinterpret_cast<decltype(gAnswerMobile)>(sym("_ZN4CPed15SetAnswerMobileEv"));
	gHangUp = reinterpret_cast<decltype(gHangUp)>(sym("_ZN4CPed17ClearAnswerMobileEv"));
	gPedRef = reinterpret_cast<decltype(gPedRef)>(sym("_ZN6CPools9GetPedRefEP4CPed"));
	gVehicleRef = reinterpret_cast<decltype(gVehicleRef)>(sym("_ZN6CPools13GetVehicleRefEP8CVehicle"));
	gComponentPosition =
	    reinterpret_cast<decltype(gComponentPosition)>(sym("_ZN6CPedIK20GetComponentPositionERN2rw3V3dEj"));
	gPedPool = static_cast<uint8_t **>(sym("_ZN6CPools11ms_pPedPoolE"));
	gPhoneInfo = static_cast<uint8_t *>(sym("gPhoneInfo"));
	gReady = gReportCrimeNow && gPolicePresence && gSetObjectiveAt && gSetObjective && gClearObjective && gSetFlee && gAnswerMobile &&
	         gHangUp && gPedRef && gVehicleRef && gComponentPosition && gPedPool && gPhoneInfo && api.PoolsGetPed &&
	         api.PoolsGetVehicle && api.GetIsLineOfSightClear && api.BlendAnimation && api.RequestModel;
	if (!gReady)
		Log("witness: a function is missing; crimes reach the police as the game has it");
	return gReady;
}

void Update(uint32_t now)
{
	if (!gReady || !wanted::Active())
		return;
	uint8_t *player = api.FindPlayerPed();
	uint8_t *wanted = player ? *reinterpret_cast<uint8_t **>(player + kWanted) : nullptr;
	if (!wanted)
		return;
	Intercept(player, wanted, now);
	Alarms(player, wanted, now);
	for (size_t i = 0; i < gWitnesses.size();) {
		Witness &w = gWitnesses[i];
		uint8_t *ped = PedOf(w);
		bool done = false;
		if (w.how == How::Building || w.how == How::Heard) { // (nothing stops these)
			if (now >= w.due) {
				Report(wanted, w);
				done = true;
			}
		} else if (ped && Down(ped)) {
			Log("witness: stopped (%s) before the call got through", Health(ped) <= 0.0f ? "dead" : "knocked down");
			done = true;
		} else if (!ped) { // out of sight and gone: they got away, and call it in
			if (w.how == How::Mobile && w.calling)
				w.due = w.due ? w.due : w.calling + kMobileMs;
			else if (!w.due)
				w.due = std::max(now, w.since + kGotAwayMs);
			if (now >= w.due) {
				Report(wanted, w);
				done = true;
			}
		} else if (w.how == How::Mobile) {
			if (now - w.calling >= kMobileMs) {
				Report(wanted, w);
				gHangUp(ped);
				gClearObjective(ped);
				done = true;
			}
		} else if (w.how == How::Payphone) {
			if (!w.calling && Flat(EntityPos(ped), w.phone) <= kAtPhone) {
				// At the phone: facing it, the receiver to the ear.
				w.calling = now ? now : 1;
				gSetObjective(ped, kWaitOnFoot);
				const CVector at = EntityPos(ped);
				FaceHeading(ped, HeadingOf(w.phone.x - at.x, w.phone.y - at.y));
				api.BlendAnimation(EntityClump(ped), 0, kAnimPhoneTalk, 4.0f);
			} else if (w.calling && now - w.calling >= kPayphoneMs) {
				Report(wanted, w);
				api.BlendAnimation(EntityClump(ped), 0, kAnimPhoneOut, 8.0f);
				gClearObjective(ped);
				done = true;
			} else if (!w.calling && now - w.since > kGiveUpMs) { // (another phone, somewhere)
				gClearObjective(ped);
				w.how = How::Flee;
				w.due = now + kGotAwayMs / 2;
			} else if (!w.calling && PedObjective(ped) != kRunToArea) {
				gSetObjectiveAt(ped, kRunToArea, w.phone); // (something put it off; on it goes)
			}
		} else { // running off: they tell the first cop they meet, or call when they're away
			bool told = false;
			const CVector at = EntityPos(ped);
			ForEachPed([&](uint8_t *cop) { told |= PedType(cop) == kCop && !Down(cop) && Flat(EntityPos(cop), at) < kToldACop; });
			if (told)
				Log("witness: told a cop");
			if (told || now >= w.due) {
				Report(wanted, w);
				done = true;
			}
		}
		if (done)
			gWitnesses.erase(gWitnesses.begin() + long(i));
		else
			i++;
	}
}

void Reset(bool letGo)
{
	if (letGo) // (off the phone, back to themselves)
		for (const Witness &w : gWitnesses) {
			uint8_t *ped = PedOf(w);
			if (!ped || Down(ped))
				continue;
			if (w.how == How::Mobile)
				gHangUp(ped);
			else if (w.how == How::Payphone && w.calling)
				api.BlendAnimation(EntityClump(ped), 0, kAnimPhoneOut, 8.0f);
			if (w.how != How::Flee)
				gClearObjective(ped);
		}
	if (!gWitnesses.empty() || gAlarmAt)
		Log("witness: %zu calls%s dropped (what the police knew is gone)", gWitnesses.size(),
		    gAlarmAt ? " and a car alarm's" : "");
	gWitnesses.clear();
	gAlarmAt = 0;
	gAlarmCar = -1;
}

float Calling()
{
	const uint32_t now = api.timeMs ? *api.timeMs : 0;
	float most = 0.0f;
	for (const Witness &w : gWitnesses) {
		if (w.how == How::Heard)
			continue; // (the cops' own radio: no phone)
		float p;
		if (w.due) // (running off, a building's: on its way through)
			p = w.due > w.since ? float(now - w.since) / float(w.due - w.since) : 1.0f;
		else // (running for the phone: the icon's up, the ring not started)
			p = w.calling ? float(now - w.calling) / float(w.how == How::Mobile ? kMobileMs : kPayphoneMs) : 0.02f;
		most = std::max(most, std::clamp(p, 0.02f, 1.0f));
	}
	return most;
}

std::vector<CVector> Runners()
{
	std::vector<CVector> out;
	for (const Witness &w : gWitnesses)
		if ((w.how == How::Payphone && !w.calling) || w.how == How::Flee)
			if (uint8_t *ped = PedOf(w))
				out.push_back(EntityPos(ped));
	return out;
}

} // namespace witness
