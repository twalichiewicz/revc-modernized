#include "police.h"
#include "aimcam.h"
#include "config.h"
#include "game.h"
#include "heat.h"
#include "log.h"
#include "wanted.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <functional>
#include <queue>
#include <vector>

using namespace game;

namespace police {
namespace {

// --- The game (macVC 1.0.5217) -------------------------------------------------------------

// CWanted, at CPlayerPed::m_pWanted: the cops it lets after him at once (it
// keeps pointers to 10), the police cars it makes, its roadblocks' odds, its
// ignored flags (bits 0-1), the stars.
constexpr size_t kWanted = 0x778;
constexpr size_t kMaxCops = 0x19, kMaxCars = 0x1a, kRoadblockOdds = 0x1c, kIgnored = 0x1e, kStars = 0x20;
constexpr int kMostCops = 10;
// CPathFind (ThePaths): the car nodes first, 0x14 each (position in 1/8 m at
// +4, its first link at +0xc, a divider's width at +0xe, flags at +0x10:
// links in the low nibble, switched off (bit 5), water (bit 8)); the roads'
// lanes (0xc each, left and right lanes in bits 0-2 and 3-5 at +8); where
// each link goes (low 14 bits), and which road it is.
constexpr size_t kNodeSize = 0x14, kCarLinks = 0x2f1e8, kCarLinkSize = 0xc, kConnections = 0x3bd08,
                 kConnectionLinks = 0x4ac18;
constexpr uint32_t kNodeOff = 1u << 5, kNodeWater = 1u << 8;
constexpr float kLaneWidth = 5.0f;
// CVehicle: its status (bits 3-7 of +0x60), the autopilot's driving style,
// mission, temporary action and until when, speeds; who made it, the
// law-enforcer flag (bit 0), the siren, its driver, passengers and seats;
// what kind of vehicle (0 car, 5 bike).
constexpr size_t kStatusByte = 0x60, kDrivingStyle = 0x19d, kMission = 0x19e, kTempAction = 0x19f,
                 kTempActionUntil = 0x1a0, kMaxTrafficSpeed = 0x1a4, kCruiseSpeed = 0x1a8, kCreatedBy = 0x294,
                 kLawFlags = 0x295, kSiren = 0x2f1, kDriver = 0x218, kPassengers = 0x220, kSeats = 0x264,
                 kVehicleType = 0x348;
constexpr uint8_t kSimple = 2, kPhysics = 3, kAbandoned = 4, kWrecked = 5; // eEntityStatus
constexpr uint8_t kRandomVehicle = 1;                                      // eVehicleCreatedBy
// eCarMission: 2-6 after the player; an ambulance's stop, which brakes and
// holds (CCarAI::UpdateCarAI puts a police car's other missions back to the
// chase while it's 10 m or more from him, and with MISSION_NONE a cop at the
// wheel drives off cruising).
constexpr uint8_t kRamFar = 2, kBlockHandbrakeStop = 6, kEmergencyStop = 10;
constexpr uint8_t kHandbrakeLeft = 3, kAvoidCars = 2; // eCarTempAction, eCarDrivingStyle
// Driving to a point: MISSION_GOTOCOORDS (CCarAI::UpdateCarAI leaves a police
// car on it, then MISSION_GOTOCOORDS_STRAIGHT the last stretch; arrived,
// MISSION_NONE, and the chase again), and MISSION_CRUISE; where it's going
// (CAutoPilot::m_vecDestinationCoors, which JoinCarWithRoadSystemGotoCoors sets).
constexpr uint8_t kNoMission = 0, kCruise = 1, kGotoCoords = 8, kGotoCoordsStraight = 9;
constexpr size_t kDestination = 0x1b4;
constexpr int32_t kCarType = 0, kBoatType = 1, kBikeType = 5;
constexpr uint8_t kAttackPlayer = 21; // eCarMission: a police boat's chase (CCarAI::FindPoliceBoatMissionForWantedLevel)
// CCopPed: its roadblock's car, after him, kept in place (a roadblock's), with
// a spike strip to throw, the strip (CStinger: deployed first).
constexpr size_t kRoadblockCar = 0x778, kInPursuit = 0x784, kDisabledCop = 0x785, kThrowsSpikes = 0x794,
                 kStinger = 0x7b0;
constexpr int32_t kCop = 6;                       // ePedType
constexpr int32_t kKillOnFoot = 8, kKillAnyMeans = 9; // eObjective OBJECTIVE_KILL_CHAR_ON_FOOT, ..._ANY_MEANS
constexpr int32_t kRunToArea = 25;                     // eObjective OBJECTIVE_RUN_TO_AREA
constexpr size_t kAttachedTo = 0x720;                  // CPed::m_attachedTo (a helicopter's door gunner)
constexpr int32_t kColt = 17, kFirstThrown = 12;  // eWeaponType: from 12 on, thrown and fired weapons
// Models (data/default.ide): the police car, SWAT's van, SWAT.
constexpr int kPoliceCar = 156, kEnforcer = 157, kSwat = 2;
// CStreaming::ms_aInfoForModel: 0x20 a model, loaded (1) at +0x10.
constexpr size_t kStreamInfoSize = 0x20, kLoadState = 0x10;
constexpr int kKeepLoaded = 1;                           // STREAMFLAGS_DONT_REMOVE
constexpr size_t kVehicleSize = 0x6f0, kPedSize = 0x8d0; // pool entries
constexpr uint32_t kNoPolice = 0x10;                     // CCullZones::CurrentFlags_Player
// The game's own caps by stars (CWanted::UpdateWantedLevel): its police cars
// about him at once, and cops after him; for where it has them again.
constexpr uint8_t kGameCars[7] = {0, 1, 2, 2, 2, 3, 3};
constexpr uint8_t kGameCops[7] = {0, 1, 3, 4, 6, 8, 10};

uint8_t **gVehiclePool, **gPedPool;
uint8_t *gStreamInfo;
CVector *gStations;   // CRestart::PoliceRestartPoints
int16_t *gNumStations;
uint32_t *gZoneFlags; // CCullZones::CurrentFlags_Player
int32_t *gArea;       // CGame::currArea: 0 outdoors
bool *gCutscene;      // CCutsceneMgr::ms_running
int16_t *gStingerModel, *gBarrierModel;
void (*gZoneInfo)(const CVector *at, uint8_t *info);
void *(*gNewVehicle)(size_t);
void (*gAutomobile)(void *self, int model, uint8_t createdBy);
float (*gBaseOfModel)(uint8_t *entity);
void (*gWorldAdd)(uint8_t *entity);
void (*gJoinRoads)(uint8_t *vehicle);
bool (*gJoinRoadsTo)(uint8_t *vehicle, CVector to, bool now);
void (*gLawEnforcer)(uint8_t *vehicle, uint8_t on);
void (*gAddCrew)(uint8_t *vehicle);
uint8_t (*gChaseMission)();
int32_t (*gChaseSpeed)(uint8_t *vehicle);
void (*gLeaveCar)(uint8_t *vehicle);
int32_t (*gPoliceModel)();
void (*gSetAlpha)(void *clump, int alpha);
void (*gColliding)(const CVector &at, float radius, bool only2D, int16_t *count, int16_t most, uint8_t **found,
                   bool buildings, bool vehicles, bool peds, bool objects, bool dummies);
bool (*gSphereVisible)(uint8_t *camera, const CVector &at, float radius);
void (*gRoadblock)(CVector a, CVector b);
void (*gSetWeapon)(uint8_t *ped, int32_t type);
int32_t (*gGiveWeapon)(uint8_t *ped, int32_t type, uint32_t ammo, bool unlimited);
void (*gPedZ)(CVector *at);
void (*gClearObjective)(uint8_t *ped);
void (*gSetObjectiveAt)(uint8_t *ped, int32_t objective, CVector at);
int (*gVehicleRef)(uint8_t *vehicle);
int (*gPedRef)(uint8_t *ped);
bool gReady;

uint32_t gRandom = 0x9e3779b9u;
uint32_t NextRandom()
{
	gRandom = gRandom * 1664525u + 1013904223u;
	return gRandom >> 8;
}
float Random(float lo, float hi) { return lo + (hi - lo) * float(NextRandom()) / float(1u << 24); }
int RandomInt(int n) { return n > 0 ? int(NextRandom() % uint32_t(n)) : 0; }

float Flat(const CVector &a, const CVector &b) { return std::hypot(a.x - b.x, a.y - b.y); }
CVector Plus(const CVector &a, const CVector &d, float k) { return {a.x + d.x * k, a.y + d.y * k, a.z + d.z * k}; }
CVector Mix(const CVector &a, const CVector &b, float t) { return Plus(a, {b.x - a.x, b.y - a.y, b.z - a.z}, t); }
CVector Towards(const CVector &a, const CVector &b)
{
	const float d = std::fmax(Flat(a, b), 1e-3f);
	return {(b.x - a.x) / d, (b.y - a.y) / d, 0.0f};
}

// A pool's entries, those in use.
struct Slots {
	uint8_t *entries = nullptr;
	const uint8_t *flags = nullptr;
	int32_t size = 0;
	size_t stride = 0;
	uint8_t *At(int i) const { return flags[i] & 0x80 ? nullptr : entries + size_t(i) * stride; }
	int Free() const
	{
		int free = 0;
		for (int i = 0; i < size; i++)
			free += (flags[i] & 0x80) != 0;
		return free;
	}
};
Slots Pool(uint8_t **pool, size_t stride)
{
	Slots s;
	const uint8_t *p = pool ? *pool : nullptr;
	if (!p)
		return s;
	s.entries = *reinterpret_cast<uint8_t *const *>(p);
	s.flags = *reinterpret_cast<uint8_t *const *>(p + 0x8);
	s.size = *reinterpret_cast<const int32_t *>(p + 0x10);
	s.stride = stride;
	return s;
}

bool Loaded(int model) { return gStreamInfo[size_t(model) * kStreamInfoSize + kLoadState] == 1; }
uint8_t Status(const uint8_t *entity) { return entity[kStatusByte] >> 3; }
bool LawCar(const uint8_t *vehicle) { return vehicle[kLawFlags] & 1; }
float Speed(uint8_t *entity)
{
	const CVector v = EntitySpeed(entity);
	return std::hypot(v.x, v.y) * 50.0f; // m/s
}
int32_t WeaponOf(uint8_t *ped) { return WeaponType(PedWeapon(ped, PedCurrentWeaponSlot(ped))); }

// --- The roads -----------------------------------------------------------------------------

const uint8_t *NodeData(int i) { return api.thePaths + size_t(i) * kNodeSize; }
uint32_t NodeFlags(int i) { return *reinterpret_cast<const uint32_t *>(NodeData(i) + 0x10); }
int NumLinks(int i) { return NodeFlags(i) & 0xf; }
int FirstLink(int i) { return *reinterpret_cast<const int16_t *>(NodeData(i) + 0xc); }
int Linked(int link) { return *reinterpret_cast<const uint16_t *>(api.thePaths + kConnections + 2 * size_t(link)) & 0x3fff; }
bool Road(int i) { return !(NodeFlags(i) & kNodeWater); }          // not for boats
bool Open(int i) { return !(NodeFlags(i) & (kNodeWater | kNodeOff)); } // ...and not switched off
CVector At(int i) { return NodePos(reinterpret_cast<const CPathNode *>(NodeData(i))); }
float Divider(int i) { return NodeData(i)[0xe] / 16.0f; } // half its width
int Lanes(int link)
{
	const int16_t road = *reinterpret_cast<const int16_t *>(api.thePaths + kConnectionLinks + 2 * size_t(link));
	if (road < 0)
		return 2;
	const uint16_t bits = *reinterpret_cast<const uint16_t *>(api.thePaths + kCarLinks + size_t(road) * kCarLinkSize + 8);
	return std::max(1, (bits & 7) + ((bits >> 3) & 7));
}
int Nearest(const CVector &at)
{
	return api.FindNodeClosestToCoors(api.thePaths, at, 0, 999999.0f, false, false, false, false);
}

constexpr float kFar = 1e9f;

// The ways to a node by road: how far each node is from it, and the next node
// on the way there (Dijkstra's, over the open roads).
struct Tree {
	std::vector<float> dist;
	std::vector<int16_t> toward;
	int root = -1;
	bool Reaches(int i) const { return i >= 0 && i < int(dist.size()) && dist[i] < kFar; }
	void Grow(int from, float limit)
	{
		const int n = NumCarPathNodes();
		dist.assign(size_t(std::max(n, 0)), kFar);
		toward.assign(size_t(std::max(n, 0)), -1);
		root = from;
		if (from < 0 || from >= n)
			return;
		using Item = std::pair<float, int>;
		std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
		dist[size_t(from)] = 0.0f;
		open.push({0.0f, from});
		while (!open.empty()) {
			const auto [d, i] = open.top();
			open.pop();
			if (d > dist[size_t(i)] || d > limit)
				continue;
			const CVector p = At(i);
			const int first = FirstLink(i), count = NumLinks(i);
			for (int k = 0; k < count; k++) {
				const int j = Linked(first + k);
				if (j >= n || !Open(j))
					continue;
				const float through = d + Flat(p, At(j));
				if (through < dist[size_t(j)]) {
					dist[size_t(j)] = through;
					toward[size_t(j)] = int16_t(i);
					open.push({through, j});
				}
			}
		}
	}
};
Tree gToHim;            // the ways to him
uint32_t gGrown;        // when
int gGrownFrom = -2;

// --- Stars ---------------------------------------------------------------------------------

struct Level {
	int first;          // units sent at once
	float every;        // s between more sent
	int most;           // units on it at most
	int here;           // police cars about him at once
	int pursuit;        // cops after him at once (the game's cap, to 10)
	float ring;         // the perimeter's radius (0: none)
	float blocksAfter;  // s at these stars before the first roadblock
	float cover, more;  // the share of its roads blocked at first, and more a second
	int heavy;          // how heavy its roadblocks are at first (0 part of the road, 1 all of
	float heavier;      // it, 2 and spikes, 3 and a second line), and s to the next heavier
	float recentre;     // s out of it before it's set up again about him
	float speed;        // m/s a unit drives to him at (off the map: no traffic in its way)
	int standing;       // roadblocks standing about him at once
};
constexpr Level kLevels[7] = {
    {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0},
    {1, 60, 2, 1, 3, 0, 0, 0, 0, 0, 1, 1, 22, 0},
    {3, 30, 6, 3, 6, 300, 45, 0.20f, 0.0015f, 0, 120, 15, 26, 2},
    {4, 20, 9, 4, 10, 280, 15, 0.35f, 0.004f, 1, 90, 12, 30, 3},
    {6, 15, 12, 5, 10, 260, 0, 0.50f, 0.007f, 2, 75, 10, 33, 4},
    {8, 12, 14, 6, 10, 240, 0, 0.70f, 0.010f, 2, 45, 8, 35, 5},
    {10, 10, 16, 7, 10, 220, 0, 0.90f, 0.020f, 3, 45, 6, 37, 6},
};

int gStars;
uint32_t gStarsSince, gSince;   // when these stars came, and the first
uint32_t gShot;                 // when he last fired (0: not lately)
uint32_t gLastTick, gLastSpawn, gLastLog, gLastTime, gFirstSeen, gLastRetask;

struct Tally {
	int sent, came, lost, blocks, spikes, pulledUp, most, retasked, elsewhere, joined, abandoned;
	float firstMetres;
} gTally;
// Why units near him didn't come as cars where they were (counted a tick
// each), and how often none could anywhere, since the last report.
struct Why {
	int onTop, seen, room, crowded, ground, model, waiting, nowhere;
} gWhy;

// --- Units -----------------------------------------------------------------------------

enum class Duty : uint8_t { Patrol, Station, Sent, Here, Down };

struct Unit {
	Duty duty = Duty::Patrol;
	int node = -1, next = -1; // on the road from node to next
	float along = 0.0f;       // of the way
	int station = -1;         // its station's node
	uint32_t since = 0;       // when its duty began (Down: when it went)
	uint32_t ready = 0;       // Sent: since when it's been near enough to come (0: not)
	int car = -1;             // Here: its car's handle, and its cops'
	int crew[4] = {-1, -1, -1, -1};
	bool fell[4] = {};
	CVector last{};           // where it was last seen
};
constexpr int kPatrols = 40, kPerStation = 4, kMostStations = 8;
std::vector<Unit> gUnits;
std::vector<int> gStationNodes;
constexpr float kPatrolSpeed = 9.0f;
constexpr uint32_t kBackAfter = 90000; // a unit lost is made up at a station after this

// Its next road at a node, not back the way it came (unless it must).
int Wander(int at, int from)
{
	int choices[16], count = 0;
	const int n = NumCarPathNodes(), first = FirstLink(at), links = std::min(NumLinks(at), 16);
	for (int k = 0; k < links; k++) {
		const int j = Linked(first + k);
		if (j < n && Road(j) && j != from)
			choices[count++] = j;
	}
	return count ? choices[RandomInt(count)] : from;
}

CVector Where(const Unit &u)
{
	if (u.node < 0)
		return {0, 0, 0};
	return u.next >= 0 ? Mix(At(u.node), At(u.next), u.along) : At(u.node);
}

// It drives on `metres`: wandering, or to him along the way.
void Drive(Unit &u, float metres, bool toHim)
{
	for (int steps = 0; metres > 0.0f && steps < 32 && u.node >= 0; steps++) {
		if (toHim) {
			const int to = gToHim.Reaches(u.node) ? gToHim.toward[size_t(u.node)] : -1;
			if (to < 0) {
				u.next = -1;
				u.along = 0.0f;
				return; // there, or no way there
			}
			if (u.next != to) {
				u.next = to;
				u.along = 0.0f;
			}
		} else if (u.next < 0) {
			u.next = Wander(u.node, -1);
			if (u.next < 0)
				return;
		}
		const float length = std::fmax(1.0f, Flat(At(u.node), At(u.next)));
		const float left = (1.0f - u.along) * length;
		if (metres < left) {
			u.along += metres / length;
			return;
		}
		metres -= left;
		const int from = u.node;
		u.node = u.next;
		u.along = 0.0f;
		u.next = toHim ? -1 : Wander(u.node, from);
	}
}

float RoadTo(const Unit &u) { return gToHim.Reaches(u.node) ? gToHim.dist[size_t(u.node)] : kFar; }

// The units: patrols over the roads, more where the zones have more traffic;
// and some at each station.
bool Muster()
{
	const int n = NumCarPathNodes();
	if (n <= 0 || !gZoneInfo)
		return false;
	std::vector<int> nodes;
	std::vector<float> upTo;
	float total = 0.0f;
	alignas(8) uint8_t info[256];
	for (int i = 0; i < n; i += 3) {
		if (!Road(i) || NumLinks(i) == 0)
			continue;
		std::memset(info, 0, sizeof info);
		const CVector p = At(i);
		gZoneInfo(&p, info);
		total += 10.0f + std::fmax(0.0f, float(*reinterpret_cast<const int16_t *>(info))); // CZoneInfo::carDensity
		nodes.push_back(i);
		upTo.push_back(total);
	}
	if (nodes.empty())
		return false;
	gUnits.clear();
	for (int k = 0; k < kPatrols; k++) {
		const float pick = Random(0.0f, total);
		const size_t at = size_t(std::lower_bound(upTo.begin(), upTo.end(), pick) - upTo.begin());
		Unit u;
		u.node = nodes[std::min(at, nodes.size() - 1)];
		u.next = Wander(u.node, -1);
		u.along = Random(0.0f, 1.0f);
		gUnits.push_back(u);
	}
	gStationNodes.clear();
	const int stations = gStations && gNumStations ? std::clamp<int>(*gNumStations, 0, kMostStations) : 0;
	for (int s = 0; s < stations; s++) {
		const int node = Nearest(gStations[s]);
		if (node < 0 || node >= n)
			continue;
		gStationNodes.push_back(node);
		for (int k = 0; k < kPerStation; k++) {
			Unit u;
			u.duty = Duty::Station;
			u.node = u.station = node;
			gUnits.push_back(u);
		}
	}
	Log("police: %d patrol cars about the city (more where the traffic is), %d at each of %zu stations", kPatrols,
	    kPerStation, gStationNodes.size());
	return true;
}

void BackOnPatrol(Unit &u)
{
	if (u.duty == Duty::Here && u.car >= 0) {
		const int node = Nearest(u.last);
		if (node >= 0)
			u.node = node;
	}
	u.duty = u.station >= 0 && u.duty != Duty::Here ? Duty::Station : Duty::Patrol;
	if (u.duty == Duty::Station)
		u.node = u.station;
	u.next = -1;
	u.along = 0.0f;
	u.car = -1;
	u.ready = 0;
	std::fill(std::begin(u.crew), std::end(u.crew), -1);
}

void Lose(Unit &u, uint32_t now)
{
	u.duty = Duty::Down;
	u.since = now;
	u.car = -1;
	u.ready = 0;
	std::fill(std::begin(u.crew), std::end(u.crew), -1);
	gTally.lost++;
}

// --- Seeing ------------------------------------------------------------------------------

const CVector &CameraAt() { return *reinterpret_cast<const CVector *>(api.theCamera + 0x30); }

// How far off a police car is kept (CCarCtrl::PossiblyRemoveVehicle: 120 m
// times CCamera::GenerationDistMultiplier, which traffic.cpp has widened):
// units come within, and as cars, well inside it.
float KeptWithin() { return 120.0f * std::fmax(1.0f, *reinterpret_cast<const float *>(api.theCamera + 0xfc)); }
float Near() { return std::fmin(150.0f, KeptWithin() * 0.8f); }         // near enough to come
float OnlyACar() { return std::fmin(120.0f, KeptWithin() * 0.7f); }     // in his view, only a car coming

// Not in his view: off the screen, or a building in the way.
bool Unseen(const CVector &p)
{
	const CVector up{p.x, p.y, p.z + 1.2f};
	if (!gSphereVisible(api.theCamera, up, 3.0f))
		return true;
	return !api.GetIsLineOfSightClear(CameraAt(), up, true, false, false, false, false, true, false);
}

// Where a unit near him comes in when it can't where it is (on top of him, in
// his view, something in its way): another road near it, a way off him, out
// of his view (or far enough off that it's only a car coming), not much
// further by road than it looks; the nearest it first.
constexpr float kComeInFrom = 40.0f;
int Elsewhere(const CVector &from, const CVector &him, int *out, int most)
{
	struct Pick {
		float r;
		int node;
	} best[8];
	int kept = 0;
	const int n = NumCarPathNodes();
	const float furthest = Near() - 10.0f;
	for (int i = 0; i < n; i++) {
		if (!gToHim.Reaches(i) || gToHim.toward[size_t(i)] < 0 || !Open(i))
			continue;
		const CVector p = At(i);
		const float d = Flat(p, him);
		if (d < kComeInFrom || d > furthest || gToHim.dist[size_t(i)] > d * 2.0f + 50.0f)
			continue;
		const float r = Flat(p, from);
		int k = std::min(kept, 7);
		if (kept == 8 && r >= best[k].r)
			continue;
		while (k > 0 && best[k - 1].r > r) {
			best[k] = best[k - 1];
			k--;
		}
		best[k] = {r, i};
		kept = std::min(kept + 1, 8);
	}
	int count = 0;
	for (int k = 0; k < kept && count < most; k++) {
		const CVector p = At(best[k].node);
		if (Flat(p, him) >= OnlyACar() || Unseen(p))
			out[count++] = best[k].node;
	}
	return count;
}

// --- Cars coming -----------------------------------------------------------------------

// Police about him still at it: our units there (their cops alive), and the
// game's own cars with a cop at the wheel (not an empty one left standing).
int CarsAbout(const CVector &him)
{
	int count = 0, ours[64], numOurs = 0;
	for (const Unit &u : gUnits)
		if (u.duty == Duty::Here) {
			count++;
			if (u.car >= 0 && numOurs < 64)
				ours[numOurs++] = u.car;
		}
	const Slots cars = Pool(gVehiclePool, kVehicleSize);
	for (int i = 0; i < cars.size; i++) {
		uint8_t *car = cars.At(i);
		if (!car || !LawCar(car) || Status(car) == kWrecked || Flat(EntityPos(car), him) > 250.0f)
			continue;
		const uint8_t *driver = *reinterpret_cast<uint8_t *const *>(car + kDriver);
		if (driver && Health(driver) > 0.0f && std::find(ours, ours + numOurs, gVehicleRef(car)) == ours + numOurs)
			count++;
	}
	return count;
}

int Model()
{
	if (!Loaded(kPoliceCar)) {
		api.RequestModel(kPoliceCar, kKeepLoaded);
		return -1;
	}
	int model = gPoliceModel ? gPoliceModel() : kPoliceCar;
	// SWAT come more than the game's one in sixteen.
	if (gStars == 4 && Loaded(kEnforcer) && Loaded(kSwat) && Random(0.0f, 1.0f) < 0.35f)
		model = kEnforcer;
	return model >= 0 && Loaded(model) ? model : kPoliceCar;
}

// A unit as a police car where it is on its road, facing along it, driving.
enum class Came { Yes, Room, Ground, Crowded, Model };
Came Spawn(Unit &u, const CVector &him, uint32_t now)
{
	const Slots cars = Pool(gVehiclePool, kVehicleSize), peds = Pool(gPedPool, kPedSize);
	if (cars.Free() < 10 || peds.Free() < 12)
		return Came::Room;
	const CVector at = Where(u);
	const int ahead = u.next >= 0 ? u.next : (gToHim.Reaches(u.node) ? gToHim.toward[size_t(u.node)] : -1);
	const CVector dir = ahead >= 0 && Flat(At(ahead), at) > 0.5f ? Towards(at, At(ahead)) : Towards(at, him);
	alignas(16) uint8_t point[64] = {};
	uint8_t *hit = nullptr;
	if (!api.ProcessVerticalLine({at.x, at.y, at.z + 4.0f}, at.z - 6.0f, point, hit, true, false, false, false, true,
	                             false, nullptr))
		return Came::Ground;
	const float ground = reinterpret_cast<const CVector *>(point)->z;
	int16_t count = 0;
	gColliding({at.x, at.y, ground + 1.0f}, 6.0f, true, &count, 2, nullptr, false, true, true, false, false);
	if (count)
		return Came::Crowded;
	const int model = Model();
	if (model < 0)
		return Came::Model;
	auto *car = static_cast<uint8_t *>(gNewVehicle(kVehicleSize));
	if (!car)
		return Came::Room;
	gAutomobile(car, model, kRandomVehicle);
	*reinterpret_cast<CVector *>(car + 0x08) = {dir.y, -dir.x, 0.0f}; // right
	*reinterpret_cast<CVector *>(car + 0x18) = {dir.x, dir.y, 0.0f};  // forward
	*reinterpret_cast<CVector *>(car + 0x28) = {0.0f, 0.0f, 1.0f};    // up
	*reinterpret_cast<CVector *>(car + 0x38) = {at.x, at.y, ground};
	reinterpret_cast<CVector *>(car + 0x38)->z = ground + gBaseOfModel(car);
	EntitySpeed(car) = {dir.x * 0.3f, dir.y * 0.3f, 0.0f}; // 15 m/s
	car[kStatusByte] = uint8_t((car[kStatusByte] & 7) | (kPhysics << 3));
	car[kMission] = gChaseMission();
	car[kCruiseSpeed] = uint8_t(std::clamp(gChaseSpeed(car), 0, 255));
	*reinterpret_cast<float *>(car + kMaxTrafficSpeed) = car[kCruiseSpeed] / 2.0f;
	car[kDrivingStyle] = kAvoidCars;
	car[kTempAction] = 0;
	gJoinRoads(car);
	gLawEnforcer(car, 1);
	if (api.VehicleUsesSiren(car))
		car[kSiren] = 1;
	if (void *clump = EntityClump(car))
		gSetAlpha(clump, 0); // and it fades in
	gWorldAdd(car);
	gAddCrew(car);
	u.duty = Duty::Here;
	u.since = now;
	u.car = gVehicleRef(car);
	u.last = EntityPos(car);
	std::fill(std::begin(u.crew), std::end(u.crew), -1);
	std::fill(std::begin(u.fell), std::end(u.fell), false);
	if (uint8_t *driver = *reinterpret_cast<uint8_t **>(car + kDriver))
		u.crew[0] = gPedRef(driver);
	const int seats = std::min<int>(car[kSeats], 3);
	for (int i = 0; i < seats; i++)
		if (uint8_t *p = *reinterpret_cast<uint8_t **>(car + kPassengers + 8 * size_t(i)))
			u.crew[1 + i] = gPedRef(p);
	gTally.came++;
	return Came::Yes;
}

// Its car and cops: still at it, gone off (it comes on again from there), or
// lost (its cops dead, or its car wrecked with no one left).
void Check(Unit &u, uint32_t now)
{
	uint8_t *car = u.car >= 0 ? api.PoolsGetVehicle(u.car) : nullptr;
	if (car)
		u.last = EntityPos(car);
	bool alive = false, anyone = false, fell = false;
	for (int i = 0; i < 4; i++) {
		fell |= u.fell[i];
		if (u.crew[i] < 0)
			continue;
		uint8_t *ped = api.PoolsGetPed(u.crew[i]);
		if (!ped) {
			u.crew[i] = -1;
			continue;
		}
		anyone = true;
		if (Health(ped) > 0.0f) {
			alive = true;
			if (!PedInVehicle(ped))
				u.last = EntityPos(ped);
		} else {
			u.fell[i] = fell = true;
		}
	}
	if (alive)
		return;
	if (fell || (car && Status(car) == kWrecked) || (car && !anyone)) {
		Lose(u, now);
		return;
	}
	if (!car && !anyone) { // left behind: it drives on after him from there
		const int node = Nearest(u.last);
		u.duty = Duty::Sent;
		u.node = node >= 0 ? node : u.node;
		u.next = -1;
		u.along = 0.0f;
		u.car = -1;
		u.ready = 0;
	}
}

// --- Pulling up short of him on foot -------------------------------------------------------

struct Stopping {
	int car = -1;
	uint32_t since = 0;
};
Stopping gStopping[12];

void PullUp(uint8_t *player, const CVector &him, bool onHim, uint32_t now)
{
	if (onHim && !PedInVehicle(player)) {
		const Slots cars = Pool(gVehiclePool, kVehicleSize);
		const CVector head{him.x, him.y, him.z + 0.6f};
		for (int i = 0; i < cars.size; i++) {
			uint8_t *car = cars.At(i);
			if (!car || !LawCar(car) || car[kCreatedBy] != kRandomVehicle ||
			    (Status(car) != kSimple && Status(car) != kPhysics) || car[kMission] < kRamFar ||
			    car[kMission] > kBlockHandbrakeStop)
				continue;
			uint8_t *driver = *reinterpret_cast<uint8_t **>(car + kDriver);
			if (!driver || PedType(driver) != kCop)
				continue;
			const CVector at = EntityPos(car);
			const float d = Flat(at, him);
			if (d > 45.0f)
				continue;
			// Hidden from him (round a corner, behind something), or near enough side-on.
			if (d > 28.0f && api.GetIsLineOfSightClear(head, {at.x, at.y, at.z + 1.0f}, true, false, false, true,
			                                           false, true, false))
				continue;
			const int handle = gVehicleRef(car);
			Stopping *slot = nullptr;
			bool already = false;
			for (Stopping &s : gStopping) {
				already |= s.car == handle;
				if (!slot && s.car < 0)
					slot = &s;
			}
			if (already || !slot)
				continue;
			car[kMission] = kEmergencyStop;
			car[kCruiseSpeed] = 0;
			if (Speed(car) > 7.0f) { // swung side-on to him, the driver's door away
				car[kTempAction] = kHandbrakeLeft;
				*reinterpret_cast<uint32_t *>(car + kTempActionUntil) = now + 450;
			}
			*slot = {handle, now};
			gTally.pulledUp++;
		}
	}
	// Once it's all but stopped (CVehicle::CanPedExitCar), the cops get out.
	// He's back in a car before they're out: after him again.
	for (Stopping &s : gStopping) {
		if (s.car < 0)
			continue;
		uint8_t *car = api.PoolsGetVehicle(s.car);
		if (!car || car[kMission] != kEmergencyStop) {
			s.car = -1;
			continue;
		}
		if (PedInVehicle(player) || now - s.since > 8000) {
			car[kMission] = gChaseMission();
			car[kCruiseSpeed] = uint8_t(std::clamp(gChaseSpeed(car), 0, 255));
			s.car = -1;
		} else if (Speed(car) < 3.0f) {
			gLeaveCar(car); // it stays stopped till they're out, then it's left standing
			s.car = -1;
		}
	}
}

// --- Guns --------------------------------------------------------------------------------

constexpr uint32_t kArmedFor = 60000; // a gun fired is "in play" this long

bool Armed(uint8_t *player, uint32_t now)
{
	if (gShot && now - gShot < kArmedFor)
		return true;
	return aimcam::Aiming() && WeaponOf(player) >= kFirstThrown;
}

// Against a gun, a cop after him has his: the pistol, not the nightstick.
void DrawGuns()
{
	const Slots peds = Pool(gPedPool, kPedSize);
	for (int i = 0; i < peds.size; i++) {
		uint8_t *ped = peds.At(i);
		if (!ped || PedType(ped) != kCop || Health(ped) <= 0.0f || PedInVehicle(ped) || !ped[kInPursuit] ||
		    WeaponOf(ped) >= kColt)
			continue;
		gSetWeapon(ped, kColt);
		if (WeaponOf(ped) < kColt) {
			gGiveWeapon(ped, kColt, 1000, true);
			gSetWeapon(ped, kColt);
		}
	}
}

// CWanted keeps ten cops after him at once; the rest it leaves standing by
// their cars, or sends off (CCopPed::ClearPursuit). Now those near him on
// foot go for him too, and are let go once the stars are down to one (the
// game would leave one with no car of its own at it).
constexpr int kMostJoined = 24;
int gJoined[kMostJoined], gNumJoined;

bool CanEnlist() { return gClearObjective && api.SetObjective && api.IsPedInControl; }

// It goes for him, with a pistol if it has none; kept on the list to let go
// (false if there's no room on it).
bool Enlist(uint8_t *cop, uint8_t *player)
{
	const int handle = gPedRef(cop);
	if (std::find(gJoined, gJoined + gNumJoined, handle) == gJoined + gNumJoined) {
		if (gNumJoined == kMostJoined) { // room for it: those gone or dead off the list
			int k = 0;
			for (int j = 0; j < gNumJoined; j++)
				if (uint8_t *p = api.PoolsGetPed(gJoined[j]); p && Health(p) > 0.0f)
					gJoined[k++] = gJoined[j];
			gNumJoined = k;
			if (gNumJoined == kMostJoined)
				return false;
		}
		gJoined[gNumJoined++] = handle;
	}
	if (WeaponOf(cop) < kColt) {
		gGiveWeapon(cop, kColt, 1000, true);
		gSetWeapon(cop, kColt);
	}
	api.SetObjective(cop, kKillOnFoot, player);
	return true;
}

void JoinIn(uint8_t *player, const CVector &him)
{
	if (!CanEnlist() || PedInVehicle(player) || Health(player) <= 0.0f)
		return;
	const Slots peds = Pool(gPedPool, kPedSize);
	for (int i = 0; i < peds.size; i++) {
		uint8_t *cop = peds.At(i);
		if (!cop || PedType(cop) != kCop || Health(cop) <= 0.0f || PedInVehicle(cop) || cop[kInPursuit] ||
		    cop[kDisabledCop] || cop[kThrowsSpikes] || PedObjective(cop) == kKillOnFoot ||
		    Flat(EntityPos(cop), him) > 60.0f || !api.IsPedInControl(cop))
			continue;
		gTally.joined += Enlist(cop, player);
	}
}

// A roadblock's cops are kept beside its cars (CCopPed::CopAI) till he comes
// close; those beside a car on fire are let go, and (him on foot) go for him:
// combat.cpp has them clear of the blast first.
constexpr size_t kPedFlags = 0x188;
constexpr uint64_t kStayInPlace = 1ull << 32; // bKindaStayInSamePlace

void Abandon(uint8_t *player, bool onHim)
{
	CVector fires[8];
	int numFires = 0;
	const Slots cars = Pool(gVehiclePool, kVehicleSize);
	for (int i = 0; i < cars.size && numFires < 8; i++)
		if (uint8_t *car = cars.At(i); car && OnFire(car) && !IsAircraft(car))
			fires[numFires++] = EntityPos(car);
	if (!numFires)
		return;
	const Slots peds = Pool(gPedPool, kPedSize);
	for (int i = 0; i < peds.size; i++) {
		uint8_t *cop = peds.At(i);
		if (!cop || PedType(cop) != kCop || !cop[kDisabledCop] || cop[kThrowsSpikes] || Health(cop) <= 0.0f ||
		    PedInVehicle(cop) || PedFromMission(cop))
			continue;
		const CVector at = EntityPos(cop);
		bool near = false;
		for (int k = 0; k < numFires && !near; k++)
			near = Flat(fires[k], at) < 12.0f && std::fabs(fires[k].z - at.z) < 4.0f;
		if (!near)
			continue;
		cop[kDisabledCop] = 0;
		*reinterpret_cast<uint64_t *>(cop + kPedFlags) &= ~kStayInPlace;
		gTally.abandoned++;
		if (onHim && CanEnlist() && !PedInVehicle(player) && Health(player) > 0.0f)
			Enlist(cop, player);
	}
}

void LetGo()
{
	for (int k = 0; k < gNumJoined; k++)
		if (uint8_t *cop = api.PoolsGetPed(gJoined[k]))
			if (PedType(cop) == kCop && Health(cop) > 0.0f && !cop[kInPursuit] && PedObjective(cop) == kKillOnFoot)
				gClearObjective(cop);
	gNumJoined = 0;
}

// --- The perimeter -------------------------------------------------------------------------

struct Site {
	CVector at;       // where its road crosses the ring
	CVector out;      // along the road, out
	float width;      // kerb to kerb
	float divider;    // half the divider's width (0: none)
	float rank;       // which are manned first
	bool placed = false;
	bool spikes = false; // it wants a spike strip
	int spiker = -1;     // the cop with it
	int cars[12];
	int numCars = 0;
};
struct Ring {
	bool set = false;
	CVector centre{};
	float radius = 0.0f;
	uint32_t outside = 0; // since when he's been out of it (0: in)
	std::vector<Site> sites;
	Tree tree;
} gRing;

// Roadblocks set up (of this perimeter or one before), while their cars stand.
struct Block {
	CVector at;
	int cars[12];
	int numCars;
};
std::vector<Block> gBlocks;

int Standing(const CVector &him)
{
	int standing = 0;
	for (size_t i = 0; i < gBlocks.size();) {
		bool any = false;
		for (int k = 0; k < gBlocks[i].numCars && !any; k++)
			any = api.PoolsGetVehicle(gBlocks[i].cars[k]) != nullptr;
		if (!any) {
			gBlocks[i] = gBlocks.back();
			gBlocks.pop_back();
			continue;
		}
		standing += Flat(gBlocks[i].at, him) < 350.0f;
		i++;
	}
	return standing;
}

bool BlockNear(const CVector &at)
{
	for (const Block &b : gBlocks)
		if (Flat(b.at, at) < 50.0f)
			return true;
	return false;
}

// The roads out of a ring about `centre`: where each crosses it (one end
// nearer by road, the other further), the bigger roads and those ahead of him
// first.
void SetUp(const CVector &centre, const CVector &heading, int stars)
{
	Ring &r = gRing;
	r.set = true;
	r.centre = centre;
	r.radius = kLevels[stars].ring;
	r.outside = 0;
	r.sites.clear();
	const int root = Nearest(centre);
	if (root < 0)
		return;
	r.tree.Grow(root, r.radius * 1.4f);
	const int n = NumCarPathNodes();
	for (int i = 0; i < n; i++) {
		if (!r.tree.Reaches(i) || r.tree.dist[size_t(i)] >= r.radius)
			continue;
		const int first = FirstLink(i), count = NumLinks(i);
		for (int k = 0; k < count; k++) {
			const int j = Linked(first + k);
			if (j >= n || !Open(j) || (r.tree.Reaches(j) && r.tree.dist[size_t(j)] < r.radius))
				continue;
			const CVector a = At(i), b = At(j);
			const float length = Flat(a, b);
			if (length < 1.0f)
				continue;
			Site s;
			s.at = Mix(a, b, std::clamp((r.radius - r.tree.dist[size_t(i)]) / length, 0.0f, 1.0f));
			s.out = Towards(a, b);
			const int lanes = Lanes(first + k);
			s.width = kLaneWidth * float(lanes + 1);
			s.divider = std::fmin(Divider(i), Divider(j));
			s.rank = float(lanes) * 2.0f + (s.out.x * heading.x + s.out.y * heading.y) * 3.0f + Random(0.0f, 1.0f);
			bool merged = false;
			for (Site &o : r.sites)
				if (Flat(o.at, s.at) < 30.0f) { // the same road (its other side), or a junction
					if (s.width > o.width)
						o = s;
					merged = true;
					break;
				}
			if (!merged)
				r.sites.push_back(s);
		}
	}
	std::sort(r.sites.begin(), r.sites.end(), [](const Site &a, const Site &b) { return a.rank > b.rank; });
	if (gBarrierModel)
		api.RequestModel(*gBarrierModel, kKeepLoaded);
	for (Site &s : r.sites) // one standing there already from before
		s.placed = BlockNear(s.at);
	Log("police: a perimeter of %.0f m about him at %.0f,%.0f, %zu roads out of it", r.radius, centre.x, centre.y,
	    r.sites.size());
}

// Cars left standing about a spot (roadblocks' are; to tell a new one's own).
int ParkedNear(const CVector &at, float radius, int *out, int most)
{
	const Slots cars = Pool(gVehiclePool, kVehicleSize);
	int count = 0;
	for (int i = 0; i < cars.size && count < most; i++) {
		uint8_t *car = cars.At(i);
		if (car && Flat(EntityPos(car), at) < radius && Status(car) == kAbandoned)
			out[count++] = gVehicleRef(car);
	}
	return count;
}

// The game's roadblock (cars across the road, its cops made as he comes near,
// barriers in front) across `s`: heavier, all of it, a second line behind.
void Place(Site &s, int heavy)
{
	int before[12];
	const int had = ParkedNear(s.at, 30.0f, before, 12);
	const CVector across{-s.out.y, s.out.x, 0.0f};
	const float half = s.width / 2.0f + 2.0f; // to past the kerbs: no gap at the ends
	auto line = [&](const CVector &mid) {
		if (heavy == 0) { // from one kerb, most of the way across
			const float side = Random(0.0f, 1.0f) < 0.5f ? 1.0f : -1.0f;
			gRoadblock(Plus(mid, across, side * half), Plus(mid, across, side * (half - s.width * 0.6f)));
		} else if (s.divider > 0.0f) {
			gRoadblock(Plus(mid, across, half + s.divider), Plus(mid, across, s.divider));
			gRoadblock(Plus(mid, across, -s.divider), Plus(mid, across, -half - s.divider));
		} else {
			gRoadblock(Plus(mid, across, half), Plus(mid, across, -half));
		}
	};
	line(s.at);
	// (The game makes the cars, and later two cops a car, without looking for
	// room in their pools: the second line only where the road's narrow.)
	if (heavy >= 3 && s.width <= 16.0f)
		line(Plus(s.at, s.out, 10.0f));
	s.placed = true;
	s.spikes = heavy >= 2;
	int now[12];
	const int has = ParkedNear(s.at, 30.0f, now, 12);
	s.numCars = 0;
	for (int i = 0; i < has; i++)
		if (std::find(before, before + had, now[i]) == before + had)
			s.cars[s.numCars++] = now[i];
	Block b{s.at, {}, s.numCars};
	std::copy(s.cars, s.cars + s.numCars, b.cars);
	if (s.numCars)
		gBlocks.push_back(b);
	gTally.blocks++;
	if (gTally.blocks <= 6)
		Log("police: a roadblock %s (%d cars, %s)", heavy == 0 ? "across part of the road" : "across the road",
		    s.numCars, heavy >= 3 ? "two lines and a spike strip" : heavy == 2 ? "and a spike strip" : "no spikes");
}

bool InCarOrBike(uint8_t *player)
{
	if (!PedInVehicle(player) || !PedVehicle(player))
		return false;
	const int32_t type = *reinterpret_cast<const int32_t *>(PedVehicle(player) + kVehicleType);
	return type == kCarType || type == kBikeType;
}

// One of its cops to the road in front of it, a spike strip to throw at him
// as he comes (CCopPed::ProcessStingerCop; while he isn't driving, he's left
// a cop like the rest).
void Spikes(Site &s, uint8_t *player)
{
	if (!s.spikes)
		return;
	if (s.spiker >= 0) {
		uint8_t *cop = api.PoolsGetPed(s.spiker);
		const uint8_t *strip = cop ? *reinterpret_cast<uint8_t **>(cop + kStinger) : nullptr;
		if (!cop || Health(cop) <= 0.0f || !strip || strip[0]) { // gone, or it's down
			s.spikes = false;
			return;
		}
		cop[kThrowsSpikes] = InCarOrBike(player);
		return;
	}
	uint8_t *mine[12];
	int count = 0;
	for (int i = 0; i < s.numCars; i++)
		if (uint8_t *car = api.PoolsGetVehicle(s.cars[i]))
			mine[count++] = car;
	if (!count)
		return;
	const Slots peds = Pool(gPedPool, kPedSize);
	for (int i = 0; i < peds.size; i++) {
		uint8_t *cop = peds.At(i);
		if (!cop || PedType(cop) != kCop || Health(cop) <= 0.0f ||
		    std::find(mine, mine + count, *reinterpret_cast<uint8_t **>(cop + kRoadblockCar)) == mine + count)
			continue;
		// To the kerb, a way in front (towards where he comes from).
		const CVector across{-s.out.y, s.out.x, 0.0f};
		const float side = (EntityPos(cop).x - s.at.x) * across.x + (EntityPos(cop).y - s.at.y) * across.y >= 0 ? 1.0f : -1.0f;
		CVector spot = Plus(Plus(s.at, s.out, -14.0f), across, side * std::fmax(1.0f, s.width / 2.0f - 1.5f));
		spot.z = EntityPos(cop).z;
		gPedZ(&spot);
		*reinterpret_cast<CVector *>(cop + 0x38) = spot;
		EntitySpeed(cop) = {0.0f, 0.0f, 0.0f};
		cop[kThrowsSpikes] = InCarOrBike(player);
		s.spiker = gPedRef(cop);
		gTally.spikes++;
		if (gStingerModel)
			api.RequestModel(*gStingerModel, kKeepLoaded);
		return;
	}
}

void Perimeter(uint8_t *player, const CVector &him, const CVector &heading, uint32_t now)
{
	const Level &l = kLevels[gStars];
	if (l.ring <= 0.0f || !gConfig.roadblocks) {
		gRing.set = false;
		return;
	}
	if (!gRing.set || gRing.radius != l.ring)
		SetUp(him, heading, gStars);
	// Out of it a while: set up again about him.
	if (Flat(him, gRing.centre) > gRing.radius * 1.15f) {
		if (!gRing.outside)
			gRing.outside = now;
		else if (now - gRing.outside > uint32_t(l.recentre * 1000.0f))
			SetUp(him, heading, gStars);
	} else {
		gRing.outside = 0;
	}
	for (Site &s : gRing.sites)
		if (s.placed)
			Spikes(s, player);
	const float t = float(now - gStarsSince) / 1000.0f - l.blocksAfter;
	if (t < 0.0f || *gArea != 0)
		return;
	const float share = std::fmin(1.0f, l.cover + l.more * t);
	const int heavy = std::min(3, l.heavy + int(t / l.heavier));
	const size_t manned = std::min(gRing.sites.size(), size_t(std::ceil(share * float(gRing.sites.size()))));
	if (heavy >= 2 && gStingerModel)
		api.RequestModel(*gStingerModel, kKeepLoaded);
	if (Standing(him) >= l.standing)
		return;
	for (size_t k = 0; k < manned; k++) {
		Site &s = gRing.sites[k];
		const float d = Flat(s.at, him);
		if (s.placed || d > 170.0f || d < 45.0f || (d < 110.0f && !Unseen(s.at)))
			continue;
		const Slots cars = Pool(gVehiclePool, kVehicleSize), peds = Pool(gPedPool, kPedSize);
		if (cars.Free() < 20 || peds.Free() < 30)
			return;
		Place(s, heavy);
		return; // one a tick
	}
}

// --- The incident --------------------------------------------------------------------------

int Busy()
{
	int busy = 0;
	for (const Unit &u : gUnits)
		busy += u.duty == Duty::Sent || u.duty == Duty::Here;
	return busy;
}

void Begin(uint32_t now)
{
	gSince = now;
	gTally = {};
	gTally.firstMetres = -1.0f;
	gWhy = {};
	gRing.set = false;
	gLastLog = now;
}

void End(uint32_t now)
{
	for (Unit &u : gUnits)
		if (u.duty == Duty::Sent || u.duty == Duty::Here)
			BackOnPatrol(u);
	for (Stopping &s : gStopping)
		s.car = -1;
	LetGo();
	gRing.set = false;
	const uint32_t s = (now - gSince) / 1000;
	Log("police: over after %u:%02u at most %d stars: %d units sent (the first %.0f m off by road, %d sent instead of "
	    "others left behind), %d cars came (%d on another road than their own), %d lost, %d pulled up short, %d cops "
	    "joined in past the game's ten, %d roadblocks, %d spike strips, %d roadblock cops left a car on fire",
	    s / 60, s % 60, gTally.most, gTally.sent, gTally.firstMetres, gTally.retasked, gTally.came, gTally.elsewhere,
	    gTally.lost, gTally.pulledUp, gTally.joined, gTally.blocks, gTally.spikes, gTally.abandoned);
}

// More units on it, the nearest by road first.
void Dispatch(uint32_t now)
{
	const Level &l = kLevels[gStars];
	const float t = float(now - gStarsSince) / 1000.0f;
	const int want = std::min(l.most, l.first + int(t / l.every));
	for (int busy = Busy(); busy < want; busy++) {
		Unit *best = nullptr;
		float nearest = kFar;
		for (Unit &u : gUnits)
			if ((u.duty == Duty::Patrol || u.duty == Duty::Station) && RoadTo(u) < nearest) {
				nearest = RoadTo(u);
				best = &u;
			}
		if (!best)
			return; // none can get to him by road
		best->duty = Duty::Sent;
		best->since = now;
		best->ready = 0;
		gTally.sent++;
		if (gTally.firstMetres < 0.0f) {
			gTally.firstMetres = nearest;
			const CVector at = Where(*best);
			Log("police: %d stars: the nearest unit is %.0f m away by road (about %.0f s), at %.0f,%.0f", gStars,
			    nearest, nearest / l.speed, at.x, at.y);
		}
	}
	// One sent that's fallen far behind him (he's away from it fast): one
	// nearer by road goes instead, a second at a time (they cut him off).
	if (now - gLastRetask < 1000)
		return;
	gLastRetask = now;
	Unit *behind = nullptr, *nearer = nullptr;
	for (Unit &u : gUnits) {
		if (u.duty == Duty::Sent && !u.ready && RoadTo(u) > 400.0f && (!behind || RoadTo(u) > RoadTo(*behind)))
			behind = &u;
		if ((u.duty == Duty::Patrol || u.duty == Duty::Station) && (!nearer || RoadTo(u) < RoadTo(*nearer)))
			nearer = &u;
	}
	if (behind && nearer && RoadTo(*nearer) < RoadTo(*behind) * 0.5f) {
		BackOnPatrol(*behind);
		nearer->duty = Duty::Sent;
		nearer->since = now;
		nearer->ready = 0;
		gTally.retasked++;
	}
}

// Units near enough come in as cars, one at a time; against a gunman, the
// first wait out of sight for a second before going in (or 10 s).
void Arrive(const CVector &him, const CVector &real, uint8_t *player, uint32_t now)
{
	if (now - gLastSpawn < 600 || *gArea != 0)
		return;
	int ready = 0;
	uint32_t waited = 0;
	for (Unit &u : gUnits) {
		if (u.duty != Duty::Sent)
			continue;
		// Near where they're going, and where the game keeps police cars (about him).
		if (Flat(Where(u), him) > Near() || Flat(Where(u), real) > Near()) {
			u.ready = 0;
			continue;
		}
		if (!u.ready)
			u.ready = now;
		ready++;
		waited = std::max(waited, now - u.ready);
	}
	const int about = ready ? CarsAbout(him) : 0;
	if (!ready || about >= kLevels[gStars].here)
		return;
	if (about == 0 && Armed(player, now) && gStars >= 2 && ready < 2 && waited < 10000) {
		gWhy.waiting++;
		return;
	}
	// The first of them where it is now could come unseen: out of his view,
	// or far enough off that it's only a car coming.
	Unit *stuck = nullptr;
	for (Unit &u : gUnits) {
		if (u.duty != Duty::Sent || !u.ready)
			continue;
		const CVector at = Where(u);
		const float d = Flat(at, real);
		if (d < 18.0f) {
			gWhy.onTop++;
			stuck = stuck ? stuck : &u;
			continue;
		}
		if (!Unseen(at) && d < OnlyACar()) {
			gWhy.seen++;
			stuck = stuck ? stuck : &u;
			continue;
		}
		switch (Spawn(u, him, now)) {
		case Came::Yes:
			gLastSpawn = now;
			return;
		case Came::Room: gWhy.room++; return; // the same for the rest
		case Came::Model: gWhy.model++; return;
		case Came::Ground: gWhy.ground++; break;
		case Came::Crowded: gWhy.crowded++; break;
		}
		stuck = stuck ? stuck : &u;
	}
	// None could: the first of them in on another road near it (near him, so
	// only while they know where he is).
	if (!stuck || Flat(him, real) > 1.0f)
		return;
	int spots[4];
	const int count = Elsewhere(Where(*stuck), him, spots, 4);
	const int node = stuck->node, next = stuck->next;
	const float along = stuck->along;
	for (int k = 0; k < count; k++) {
		stuck->node = spots[k];
		stuck->next = gToHim.toward[size_t(spots[k])];
		stuck->along = 0.0f;
		const Came came = Spawn(*stuck, him, now);
		if (came == Came::Yes) {
			gLastSpawn = now;
			gTally.elsewhere++;
			return;
		}
		stuck->node = node;
		stuck->next = next;
		stuck->along = along;
		if (came == Came::Room || came == Came::Model)
			return;
	}
	gWhy.nowhere++;
}

// Every unit a tick on: patrols wander, those sent drive to him (and wait
// where he'd see them come), those there are checked on, the lost made up.
void Units(const CVector &him, uint8_t *player, float dt, uint32_t now)
{
	const Level &l = kLevels[gStars];
	const bool armedWait = Armed(player, now) && gStars >= 2 && CarsAbout(him) == 0;
	for (Unit &u : gUnits) {
		switch (u.duty) {
		case Duty::Patrol:
			Drive(u, kPatrolSpeed * dt, false);
			break;
		case Duty::Station:
			break;
		case Duty::Sent: {
			const CVector at = Where(u);
			const float d = Flat(at, him);
			// Waiting for another, or about to be in his sight close by, or near
			// enough (it comes in from where it is when there's room): it holds.
			if ((armedWait && d < Near()) || d < 30.0f || (d < 35.0f && !Unseen(at)))
				break;
			if (!gToHim.Reaches(u.node)) { // no way to him now
				BackOnPatrol(u);
				break;
			}
			Drive(u, l.speed * dt, true);
			break;
		}
		case Duty::Here:
			Check(u, now);
			break;
		case Duty::Down:
			if (now - u.since > kBackAfter) {
				u.duty = gStationNodes.empty() ? Duty::Patrol : Duty::Station;
				u.station = gStationNodes.empty() ? -1 : gStationNodes[size_t(RandomInt(int(gStationNodes.size())))];
				if (u.station >= 0)
					u.node = u.station;
				u.next = -1;
				u.along = 0.0f;
			}
			break;
		}
	}
}

void Report(uint32_t now)
{
	if (now - gLastLog < 30000)
		return;
	gLastLog = now;
	int sent = 0, here = 0;
	for (const Unit &u : gUnits) {
		sent += u.duty == Duty::Sent;
		here += u.duty == Duty::Here;
	}
	int placed = 0;
	for (const Site &s : gRing.sites)
		placed += s.placed;
	const uint32_t s = (now - gStarsSince) / 1000;
	const Slots cars = Pool(gVehiclePool, kVehicleSize), peds = Pool(gPedPool, kPedSize);
	Log("police: %d stars for %u:%02u: %d units on the way (%d sent elsewhere as he left them behind), %d here (%d "
	    "of them came in on another road), %d lost so far, %d cops joined in past the game's ten; %d roadblocks of "
	    "%zu roads out, %d standing; near him but not come where they were (ticks): %d on top of him, %d in his view, "
	    "%d waiting for another, %d no room (cars %d/%d free, people %d/%d), %d cars or people in the way, %d no "
	    "ground, %d no model; %d with nowhere else to come in",
	    gStars, s / 60, s % 60, sent, gTally.retasked, here, gTally.elsewhere, gTally.lost, gTally.joined, placed,
	    gRing.sites.size(), Standing(EntityPos(api.FindPlayerPed())), gWhy.onTop, gWhy.seen, gWhy.waiting, gWhy.room,
	    cars.Free(), cars.size, peds.Free(), peds.size, gWhy.crowded, gWhy.ground, gWhy.model, gWhy.nowhere);
	gWhy = {};
}


// --- The search (the wanted overhaul) ------------------------------------------------------

// While they search and nobody sees him, police cars after him drive to
// points about the search area instead (the road nodes in it, shuffled once a
// search), a new one each as it gets near; seen again, they're back on the
// chase, and with the search over and no stars, back to cruising.
constexpr float kArrived = 15.0f, kCell = 40.0f;
constexpr int kMostInACell = 3;
std::vector<int> gSearchNodes;
CVector gSearchCentre{0, 0, 0};
float gSearchRadius;
struct Searcher {
	int car, slot;
};
std::vector<Searcher> gSearchers;
int gNextSlot;
bool gSearchLogged;

// (A cop seeing him isn't enough: only one who's recognised him ends the search.)
bool Searching() { return wanted::Active() && wanted::Model().state() == heat::State::Searching; }

const std::vector<int> &SearchNodes()
{
	const Vec3 c = wanted::Model().searchCentre();
	const float r = wanted::Model().searchRadius();
	if (!gSearchNodes.empty() && c.x == gSearchCentre.x && c.y == gSearchCentre.y && r == gSearchRadius)
		return gSearchNodes;
	gSearchCentre = {c.x, c.y, c.z};
	gSearchRadius = r;
	gSearchNodes.clear();
	const int n = NumCarPathNodes();
	for (int i = 0; i < n; i++)
		if (Open(i) && Flat(At(i), gSearchCentre) <= r)
			gSearchNodes.push_back(i);
	if (gSearchNodes.empty()) // (no road in it: the nearest)
		if (const int nearest = Nearest(gSearchCentre); nearest >= 0)
			gSearchNodes.push_back(nearest);
	for (size_t i = gSearchNodes.size(); i > 1; i--) // shuffled
		std::swap(gSearchNodes[i - 1], gSearchNodes[size_t(RandomInt(int(i)))]);
	return gSearchNodes;
}

// Its cell on a 40 m grid, to keep the cars spread out.
int64_t Cell(const CVector &p) { return int64_t(std::floor(p.x / kCell)) * 100000 + int64_t(std::floor(p.y / kCell)); }

// A point for a car, from its next slot on, where fewer than three are going.
CVector PickFor(uint8_t *car, int &slot)
{
	for (int tries = 0; tries < 8; tries++, slot = gNextSlot++) {
		const CVector p = SearchPoint(slot);
		int sharing = 0;
		for (const Searcher &s : gSearchers)
			if (uint8_t *other = api.PoolsGetVehicle(s.car); other && other != car)
				sharing += Cell(*reinterpret_cast<const CVector *>(other + kDestination)) == Cell(p);
		if (sharing < kMostInACell)
			return p;
	}
	return SearchPoint(slot);
}

bool Boat(const uint8_t *vehicle) { return *reinterpret_cast<const int32_t *>(vehicle + kVehicleType) == kBoatType; }
bool Chasing(const uint8_t *car)
{
	return (car[kMission] >= kRamFar && car[kMission] <= kBlockHandbrakeStop) ||
	       (Boat(car) && car[kMission] == kAttackPlayer);
}
// Set searching by us: a car going to a point, or a boat holding where it is.
bool Ours(const uint8_t *car)
{
	return Boat(car) ? car[kMission] == kEmergencyStop
	                 : car[kMission] == kGotoCoords || car[kMission] == kGotoCoordsStraight;
}

void Release(bool wanted)
{
	for (const Searcher &s : gSearchers) {
		uint8_t *car = api.PoolsGetVehicle(s.car);
		if (!car || !Ours(car))
			continue;
		if (wanted) { // (CCarAI::UpdateCarAI puts a police car or boat with none back on its chase)
			car[kMission] = kNoMission;
		} else if (Boat(car)) {
			car[kMission] = kNoMission;
			car[kCruiseSpeed] = 0;
		} else {
			gJoinRoads(car);
			car[kMission] = kCruise;
			car[kCruiseSpeed] = 17;
			car[kSiren] = 0;
		}
		car[kTempAction] = 0;
	}
	gSearchers.clear();
	gSearchNodes.clear();
	gSearchLogged = false;
}

void Search(bool wantedNow)
{
	if (!Searching()) {
		if (!gSearchers.empty())
			Release(wantedNow);
		return;
	}
	if (SearchNodes().empty())
		return;
	const Slots cars = Pool(gVehiclePool, kVehicleSize);
	int sent = 0;
	for (int i = 0; i < cars.size; i++) {
		uint8_t *car = cars.At(i);
		const int32_t type = car ? *reinterpret_cast<const int32_t *>(car + kVehicleType) : -1;
		if (!car || !LawCar(car) || car[kCreatedBy] != kRandomVehicle ||
		    (Status(car) != kSimple && Status(car) != kPhysics) ||
		    (type != kCarType && type != kBikeType && type != kBoatType))
			continue;
		const uint8_t *driver = *reinterpret_cast<uint8_t *const *>(car + kDriver);
		if (!driver || PedType(driver) != kCop || Health(driver) <= 0.0f)
			continue;
		const int handle = gVehicleRef(car);
		auto it = std::find_if(gSearchers.begin(), gSearchers.end(), [&](const Searcher &s) { return s.car == handle; });
		const bool listed = it != gSearchers.end();
		if (type == kBoatType) { // (no roads for it to search: it holds where it is)
			if (Chasing(car)) {
				car[kMission] = kEmergencyStop;
				car[kCruiseSpeed] = 0;
				if (!listed)
					gSearchers.push_back({handle, -1});
			}
			sent += listed || Ours(car);
			continue;
		}
		const bool going = car[kMission] == kGotoCoords || car[kMission] == kGotoCoordsStraight;
		// After him, or one of ours that's got where it was going (the game stops it there).
		if (!Chasing(car) && !(listed && (going || car[kMission] == kNoMission)))
			continue; // (pulling up, its cops getting out, a mission's)
		if (listed && going &&
		    Flat(EntityPos(car), *reinterpret_cast<const CVector *>(car + kDestination)) > kArrived) {
			sent++;
			continue; // on its way
		}
		int slot = gNextSlot++;
		const CVector to = PickFor(car, slot);
		if (!listed)
			gSearchers.push_back({handle, slot});
		else
			it->slot = slot;
		gJoinRoadsTo(car, to, false);
		car[kMission] = kGotoCoords;
		car[kTempAction] = 0;
		car[kCruiseSpeed] = uint8_t(std::clamp(gChaseSpeed(car), 0, 255));
		*reinterpret_cast<float *>(car + kMaxTrafficSpeed) = car[kCruiseSpeed] / 2.0f;
		sent++;
	}
	// (those gone from the pool off the list)
	gSearchers.erase(std::remove_if(gSearchers.begin(), gSearchers.end(),
	                                [](const Searcher &s) { return !api.PoolsGetVehicle(s.car); }),
	                 gSearchers.end());
	if (sent && !gSearchLogged) {
		Log("police: searching: %d cars round %.0f,%.0f (%.0f m, %zu roads)", sent, gSearchCentre.x, gSearchCentre.y,
		    gSearchRadius, gSearchNodes.size());
		gSearchLogged = true;
	}
}

// Cops on foot go about the search area too (OBJECTIVE_RUN_TO_AREA), a new
// point as each gets there: those after him, in a gunfight with him or not
// (the game's pursuit is let go meanwhile, see NoHoming), and those near it.
// Seen again, those near him go for him; with the search over, they're let be.
struct FootSearcher {
	int ped;
	CVector to;
};
std::vector<FootSearcher> gFootSearchers;
constexpr float kOnFootNear = 150.0f, kOnFootArrived = 3.0f, kGoForHim = 60.0f;

void SearchOnFoot(uint8_t *player, const CVector &real, int stars)
{
	if (!Searching()) {
		const bool goForHim = stars > 0 && wanted::OnHim();
		for (const FootSearcher &s : gFootSearchers) {
			uint8_t *cop = api.PoolsGetPed(s.ped);
			if (!cop || Health(cop) <= 0.0f || PedObjective(cop) != kRunToArea)
				continue;
			if (goForHim && Flat(EntityPos(cop), real) < kGoForHim)
				api.SetObjective(cop, kKillOnFoot, player);
			else
				gClearObjective(cop);
		}
		gFootSearchers.clear();
		return;
	}
	if (SearchNodes().empty() || !CanEnlist())
		return;
	const Slots peds = Pool(gPedPool, kPedSize);
	for (int i = 0; i < peds.size; i++) {
		uint8_t *cop = peds.At(i);
		if (!cop || PedType(cop) != kCop || Health(cop) <= 0.0f || PedInVehicle(cop) || cop[kDisabledCop] ||
		    cop[kThrowsSpikes] || PedFromMission(cop) || *reinterpret_cast<void *const *>(cop + kAttachedTo) ||
		    !api.IsPedInControl(cop))
			continue; // (one in a gunfight with him too: it has to know him again first)
		const int handle = gPedRef(cop);
		auto it = std::find_if(gFootSearchers.begin(), gFootSearchers.end(),
		                       [&](const FootSearcher &s) { return s.ped == handle; });
		const CVector at = EntityPos(cop);
		const int32_t objective = PedObjective(cop);
		if (it == gFootSearchers.end()) {
			const bool after = objective == kKillOnFoot || objective == kKillAnyMeans;
			const bool near = Flat(at, gSearchCentre) < kOnFootNear || Flat(at, real) < kOnFootNear;
			if (!(after || objective == 0) || !near)
				continue;
		} else if (objective == kRunToArea && Flat(at, it->to) > kOnFootArrived) {
			continue; // on its way
		}
		const CVector to = SearchPoint(gNextSlot++);
		gSetObjectiveAt(cop, kRunToArea, to);
		if (it == gFootSearchers.end())
			gFootSearchers.push_back({handle, to});
		else
			it->to = to;
	}
	gFootSearchers.erase(std::remove_if(gFootSearchers.begin(), gFootSearchers.end(),
	                                    [](const FootSearcher &s) { return !api.PoolsGetPed(s.ped); }),
	                     gFootSearchers.end());
}

// What the wanted overhaul needs of the police, SmarterPolice or not: while
// nobody sees him the game's pursuit is let go (CCopPed::CopAI drops the
// furthest while there are more than the cap), and cars and cops on foot
// search the area instead of homing; a tick a quarter second.
uint32_t gLastSearch;
void NoHoming(uint8_t *player, uint8_t *wanted, int stars, bool onHim, const CVector &real, uint32_t now)
{
	if (!onHim && stars > 0)
		wanted[kMaxCops] = 0;
	if (now - gLastSearch < 250)
		return;
	gLastSearch = now;
	Search(stars > 0);
	SearchOnFoot(player, real, stars);
}

} // namespace

float UnitWithin(const CVector &at, float reach)
{
	float nearest = reach + 1.0f;
	for (const Unit &u : gUnits)
		if (u.duty != Duty::Down && u.duty != Duty::Here) // (those here are cars, and their cops)
			nearest = std::min(nearest, Flat(Where(u), at));
	return nearest;
}

std::vector<CVector> UnitsAbout()
{
	std::vector<CVector> out;
	for (const Unit &u : gUnits)
		if (u.duty != Duty::Down && u.duty != Duty::Here)
			out.push_back(Where(u));
	return out;
}

CVector SearchPoint(int slot)
{
	const std::vector<int> &nodes = SearchNodes();
	if (nodes.empty())
		return gSearchCentre;
	return At(nodes[size_t(slot) % nodes.size()]);
}

void PlayerFired(int32_t weapon)
{
	if (weapon >= kFirstThrown)
		gShot = *api.timeMs ? *api.timeMs : 1;
}

void Update()
{
	if (!gReady)
		return;
	uint8_t *player = api.FindPlayerPed();
	if (!player || NumCarPathNodes() <= 0)
		return;
	const uint32_t now = *api.timeMs;
	// A game loaded (time back, or on a long way): what we knew is gone.
	if (gLastTime && (now < gLastTime || now - gLastTime > 10000)) {
		if (gStars)
			End(gLastTime);
		gStars = 0;
		for (Unit &u : gUnits)
			if (u.duty == Duty::Here || u.duty == Duty::Sent)
				BackOnPatrol(u);
		gShot = 0;
		gNumJoined = 0; // (its cops gone with it)
		gSearchers.clear(); // (the old game's handles)
		gFootSearchers.clear();
		gSearchNodes.clear();
	}
	gLastTime = now;
	if (*gCutscene)
		return;
	if (gConfig.smarterPolice && gUnits.empty()) {
		// The stations are the script's to say (CRestart::AddPoliceRestartPoint, as the game starts).
		if (!gFirstSeen)
			gFirstSeen = now ? now : 1;
		if ((!gNumStations || *gNumStations == 0) && now - gFirstSeen < 10000)
			return;
		if (!Muster())
			return;
	}
	uint8_t *wanted = *reinterpret_cast<uint8_t **>(player + kWanted);
	if (!wanted)
		return;
	int stars = std::clamp(*reinterpret_cast<const int32_t *>(wanted + kStars), 0, 6);
	if ((wanted[kIgnored] & 3) || (*gZoneFlags & kNoPolice))
		stars = 0;
	uint8_t *vehicle = PedInVehicle(player) ? PedVehicle(player) : nullptr;
	// Where they think he is (the wanted overhaul: where he was last seen, while
	// nobody sees him), and where he is: what's not theirs to know (whether
	// he'd see a car come, what's kept about him) goes by that.
	const CVector real = vehicle ? EntityPos(vehicle) : EntityPos(player);
	const bool onHim = !wanted::Active() || wanted::OnHim();
	const CVector him = onHim ? real : wanted::Target();
	NoHoming(player, wanted, stars, onHim, real, now);
	if (!gConfig.smarterPolice)
		return;

	if (stars != gStars) {
		if (!gStars)
			Begin(now);
		if (!stars)
			End(now);
		gStars = stars;
		gStarsSince = now;
		gTally.most = std::max(gTally.most, stars);
	}
	if (!gStars) {
		if (now - gLastTick > 250) { // the patrols drive on
			const float dt = std::fmin(1.0f, float(now - gLastTick) / 1000.0f);
			gLastTick = now;
			for (Unit &u : gUnits)
				if (u.duty == Duty::Patrol)
					Drive(u, kPatrolSpeed * dt, false);
				else if (u.duty == Duty::Down && now - u.since > kBackAfter)
					BackOnPatrol(u);
		}
		return;
	}
	const Level &l = kLevels[gStars];
	// The game's caps: more cops after him at once; its own police cars only
	// on the water and in the air (ours come by road), its own roadblocks none.
	const bool byRoad = !vehicle || InCarOrBike(player);
	// (Indoors, where cops come on foot, 1.6 times its cap: as the game had it.)
	wanted[kMaxCops] = *gArea == 0 ? uint8_t(std::min(kMostCops, std::max<int>(wanted[kMaxCops], l.pursuit)))
	                               : kGameCops[gStars];
	wanted[kMaxCars] = byRoad ? 0 : kGameCars[gStars];
	// Nobody sees him: the game lets its pursuit go (CCopPed::CopAI drops the
	// furthest while there are more than this), so they search instead of homing.
	if (!onHim)
		wanted[kMaxCops] = 0;
	if (gConfig.roadblocks)
		*reinterpret_cast<int16_t *>(wanted + kRoadblockOdds) = 0;
	PullUp(player, him, onHim, now);
	if (now - gLastTick < 250)
		return;
	const float dt = std::fmin(1.0f, float(now - gLastTick) / 1000.0f);
	gLastTick = now;
	if (Armed(player, now))
		DrawGuns();
	if (gStars >= 2 && onHim)
		JoinIn(player, him);
	else if (gStars < 2 && gNumJoined)
		LetGo();
	Abandon(player, onHim);
	const int from = Nearest(him);
	if (from != gGrownFrom || now - gGrown > 1000) {
		gToHim.Grow(from, kFar);
		gGrownFrom = from;
		gGrown = now;
	}
	Dispatch(now);
	Units(him, player, dt, now);
	if (byRoad)
		Arrive(him, real, player, now);
	// (Which way he's heading only while they have him.)
	CVector heading = !onHim ? CVector{0, 0, 0} : vehicle ? EntitySpeed(vehicle) : EntityForward(player);
	const float h = std::fmax(1e-3f, std::hypot(heading.x, heading.y));
	heading = {heading.x / h, heading.y / h, 0.0f};
	Perimeter(player, him, heading, now);
	Report(now);
}

bool Install()
{
	auto sym = [](const char *name) { return dlsym(RTLD_MAIN_ONLY, name); };
	gVehiclePool = static_cast<uint8_t **>(sym("_ZN6CPools15ms_pVehiclePoolE"));
	gPedPool = static_cast<uint8_t **>(sym("_ZN6CPools11ms_pPedPoolE"));
	gStreamInfo = static_cast<uint8_t *>(sym("_ZN10CStreaming16ms_aInfoForModelE"));
	gStations = static_cast<CVector *>(sym("_ZN8CRestart19PoliceRestartPointsE"));
	gNumStations = static_cast<int16_t *>(sym("_ZN8CRestart22NumberOfPoliceRestartsE"));
	gZoneFlags = static_cast<uint32_t *>(sym("_ZN10CCullZones19CurrentFlags_PlayerE"));
	gArea = static_cast<int32_t *>(sym("_ZN5CGame8currAreaE"));
	gCutscene = static_cast<bool *>(sym("_ZN12CCutsceneMgr10ms_runningE"));
	gStingerModel = static_cast<int16_t *>(sym("MI_PLC_STINGER"));
	gBarrierModel = static_cast<int16_t *>(sym("MI_ROADWORKBARRIER1"));
	gZoneInfo = reinterpret_cast<decltype(gZoneInfo)>(sym("_ZN9CTheZones23GetZoneInfoForTimeOfDayEPK7CVectorP9CZoneInfo"));
	gNewVehicle = reinterpret_cast<decltype(gNewVehicle)>(sym("_ZN8CVehiclenwEm"));
	gAutomobile = reinterpret_cast<decltype(gAutomobile)>(sym("_ZN11CAutomobileC1Eih"));
	gBaseOfModel = reinterpret_cast<decltype(gBaseOfModel)>(sym("_ZN7CEntity40GetDistanceFromCentreOfMassToBaseOfModelEv"));
	gWorldAdd = reinterpret_cast<decltype(gWorldAdd)>(sym("_ZN6CWorld3AddEP7CEntity"));
	gJoinRoads = reinterpret_cast<decltype(gJoinRoads)>(sym("_ZN8CCarCtrl21JoinCarWithRoadSystemEP8CVehicle"));
	gJoinRoadsTo = reinterpret_cast<decltype(gJoinRoadsTo)>(sym("_ZN8CCarCtrl30JoinCarWithRoadSystemGotoCoorsEP8CVehicle7CVectorb"));
	gLawEnforcer = reinterpret_cast<decltype(gLawEnforcer)>(sym("_ZN8CVehicle22ChangeLawEnforcerStateEh"));
	gAddCrew = reinterpret_cast<decltype(gAddCrew)>(sym("_ZN6CCarAI21AddPoliceCarOccupantsEP8CVehicle"));
	gChaseMission = reinterpret_cast<decltype(gChaseMission)>(sym("_ZN6CCarAI34FindPoliceCarMissionForWantedLevelEv"));
	gChaseSpeed = reinterpret_cast<decltype(gChaseSpeed)>(sym("_ZN6CCarAI32FindPoliceCarSpeedForWantedLevelEP8CVehicle"));
	gLeaveCar = reinterpret_cast<decltype(gLeaveCar)>(sym("_ZN6CCarAI23TellOccupantsToLeaveCarEP8CVehicle"));
	gPoliceModel = reinterpret_cast<decltype(gPoliceModel)>(sym("_ZN8CCarCtrl20ChoosePoliceCarModelEv"));
	gSetAlpha = reinterpret_cast<decltype(gSetAlpha)>(sym("_ZN18CVisibilityPlugins13SetClumpAlphaEPN2rw5ClumpEi"));
	gColliding = reinterpret_cast<decltype(gColliding)>(sym("_ZN6CWorld25FindObjectsKindaCollidingERK7CVectorfbPssPP7CEntitybbbbb"));
	gSphereVisible = reinterpret_cast<decltype(gSphereVisible)>(sym("_ZN7CCamera15IsSphereVisibleERK7CVectorf"));
	gRoadblock = reinterpret_cast<decltype(gRoadblock)>(sym("_ZN11CRoadBlocks29CreateRoadBlockBetween2PointsE7CVectorS0_"));
	gSetWeapon = reinterpret_cast<decltype(gSetWeapon)>(sym("_ZN4CPed16SetCurrentWeaponE11eWeaponType"));
	gGiveWeapon = reinterpret_cast<decltype(gGiveWeapon)>(sym("_ZN4CPed10GiveWeaponE11eWeaponTypejb"));
	gClearObjective = reinterpret_cast<decltype(gClearObjective)>(sym("_ZN4CPed14ClearObjectiveEv"));
	gSetObjectiveAt = reinterpret_cast<decltype(gSetObjectiveAt)>(sym("_ZN4CPed12SetObjectiveE10eObjective7CVector"));
	gPedZ = reinterpret_cast<decltype(gPedZ)>(sym("_ZN13CPedPlacement15FindZCoorForPedEP7CVector"));
	gVehicleRef = reinterpret_cast<decltype(gVehicleRef)>(sym("_ZN6CPools13GetVehicleRefEP8CVehicle"));
	gPedRef = reinterpret_cast<decltype(gPedRef)>(sym("_ZN6CPools9GetPedRefEP4CPed"));
	gReady = gVehiclePool && gPedPool && gStreamInfo && gZoneFlags && gArea && gCutscene && gZoneInfo && gNewVehicle &&
	         gAutomobile && gBaseOfModel && gWorldAdd && gJoinRoads && gLawEnforcer && gAddCrew && gChaseMission &&
	         gChaseSpeed && gLeaveCar && gSetAlpha && gColliding && gSphereVisible && gRoadblock && gSetWeapon &&
	         gGiveWeapon && gPedZ && gVehicleRef && gPedRef && gJoinRoadsTo && gSetObjectiveAt && api.thePaths && api.FindNodeClosestToCoors &&
	         api.PoolsGetVehicle && api.PoolsGetPed && api.ProcessVerticalLine && api.GetIsLineOfSightClear &&
	         api.RequestModel && api.VehicleUsesSiren;
	if (!gReady)
		Log("police: a function is missing; the game's police as they were");
	return gReady;
}

} // namespace police
