#include "gps.h"
#include "config.h"
#include "log.h"

#include <algorithm>
#include <cmath>

using namespace game;

namespace gps {
namespace {

constexpr CRGBA kWaypointColour{164, 76, 242, 255}; // GTA V/VI waypoint purple
constexpr int kMaxNodes = 1500;
constexpr uint32_t kRefreshMs = 1000;    // re-route at least this often
constexpr uint32_t kMinIntervalMs = 200; // ...but no more often than this when merely moving
constexpr float kArrivedDist = 25.0f;
constexpr float kSearchLimit = 999999.88f;
constexpr uint8_t kCarPaths = 0;

struct Target {
	bool valid = false;
	bool waypoint = false;
	CVector pos{};
	CRGBA colour{};
};

std::vector<Vec2> gPath; // road nodes from the player's end to the destination's end
Route gRoute;
bool gHaveRoute;
CVector gLastTarget;
int gLastStartNode = -2;
uint32_t gLastSearch;
CPathNode *gNodes[kMaxNodes];

float Dist2D(const CVector &a, const CVector &b) { return hypotf(a.x - b.x, a.y - b.y); }

// Closest point to p on segment ab, and how far along it is (0..1).
Vec2 OntoSegment(Vec2 p, Vec2 a, Vec2 b, float &t)
{
	const float dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
	t = len2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0f, 1.0f) : 0.0f;
	return {a.x + dx * t, a.y + dy * t};
}

bool BlipPosition(const Blip &b, CVector &out)
{
	const uint8_t *entity = nullptr;
	switch (b.type) {
	case kBlipCar:
		entity = api.PoolsGetVehicle(b.entityHandle);
		break;
	case kBlipChar:
		entity = api.PoolsGetPed(b.entityHandle);
		if (entity && PedInVehicle(entity) && PedVehicle(entity))
			entity = PedVehicle(entity);
		break;
	case kBlipObject:
		entity = api.PoolsGetObject(b.entityHandle);
		break;
	default:
		out = {b.pos2D.x, b.pos2D.y, b.pos.z};
		return true;
	}
	if (!entity)
		return false;
	out = EntityPos(entity);
	return true;
}

// Waypoint first; otherwise the nearest mission blip, preferring places
// (coordinate blips) over characters/vehicles, and never enemies (red).
Target PickTarget(const CVector &player)
{
	Target best;
	if (*api.targetMarkerId != -1) {
		best.valid = best.waypoint = true;
		best.pos = {api.targetMarkerPos->x, api.targetMarkerPos->y, player.z};
		best.colour = kWaypointColour;
		return best;
	}
	int bestRank = 99;
	float bestDist = 0;
	for (int i = 0; i < kNumBlips; i++) {
		const Blip &b = api.blips[i];
		if (!b.inUse || b.shortRange || b.sprite != 0 || (b.display & ~1u) != 2 || b.type == 0 || b.type > 8 ||
		    b.type == 6 /* spotlight */)
			continue;
		CVector pos;
		if (!BlipPosition(b, pos))
			continue;
		const uint32_t c = api.GetRadarTraceColour(b.colour, false);
		const CRGBA colour{uint8_t(c >> 24), uint8_t(c >> 16), uint8_t(c >> 8), 255};
		const bool entity = b.type <= kBlipObject;
		if (entity && colour.r > 2 * colour.g && colour.r > 2 * colour.b)
			continue;
		const int rank = entity ? 1 : 0;
		const float dist = Dist2D(pos, player);
		if (rank < bestRank || (rank == bestRank && dist < bestDist)) {
			bestRank = rank;
			bestDist = dist;
			best = {true, false, pos, colour};
		}
	}
	return best;
}

void Search(const CVector &player, const Target &target, int startNode)
{
	gPath.clear();
	const int targetNode = api.FindNodeClosestToCoors(api.thePaths, target.pos, kCarPaths, kSearchLimit, false,
	                                                  false, false, false);
	if (startNode < 0 || targetNode < 0 || startNode == targetNode)
		return;
	int16_t count = 0;
	float dist = 0;
	api.DoPathSearch(api.thePaths, kCarPaths, player, startNode, target.pos, gNodes, &count, kMaxNodes, nullptr, &dist,
	                 kSearchLimit, targetNode);
	for (int i = 0; i < count; i++) {
		const CVector p = NodePos(gNodes[i]);
		gPath.push_back({p.x, p.y});
	}
	if (gPath.size() < 2)
		gPath.clear(); // not reachable by road
}

// The route as drawn: from the player's spot on the first road segment to the
// destination's spot on the last one.
void BuildRoute(const CVector &player, const Target &target)
{
	gRoute.points.clear();
	if (gPath.size() < 2)
		return;
	size_t first = 0;
	float t;
	Vec2 start = OntoSegment({player.x, player.y}, gPath[0], gPath[1], t);
	// Already past the first segment? Start from the next one that we are beside.
	while (first + 2 < gPath.size() && t >= 1.0f) {
		first++;
		start = OntoSegment({player.x, player.y}, gPath[first], gPath[first + 1], t);
	}
	size_t last = gPath.size() - 1;
	const Vec2 end = OntoSegment({target.pos.x, target.pos.y}, gPath[last - 1], gPath[last], t);
	gRoute.points.push_back(start);
	for (size_t i = first + 1; i < last; i++)
		gRoute.points.push_back(gPath[i]);
	gRoute.points.push_back(end);
	gRoute.colour = target.colour;
}

} // namespace

void Update(const CVector &player)
{
	gHaveRoute = false;
	const Target target = PickTarget(player);
	if (!target.valid || NumCarPathNodes() <= 0) {
		gLastStartNode = -2;
		return;
	}
	const float remaining = Dist2D(player, target.pos);
	if (remaining < kArrivedDist) {
		if (target.waypoint && gConfig.autoClearWaypoint)
			api.ToggleTargetMarker(0.0f, 0.0f); // toggling an existing waypoint removes it
		return;
	}

	const uint32_t now = *api.timeMs;
	const uint32_t elapsed = now - gLastSearch;
	const int startNode =
	    api.FindNodeClosestToCoors(api.thePaths, player, kCarPaths, kSearchLimit, false, false, false, false);
	const bool targetMoved = gLastStartNode == -2 || Dist2D(target.pos, gLastTarget) > 10.0f;
	const bool startMoved = startNode != gLastStartNode && elapsed >= kMinIntervalMs;
	if (targetMoved || startMoved || elapsed >= kRefreshMs || now < gLastSearch) {
		Search(player, target, startNode);
		if (targetMoved)
			Log("gps: %s at %.0f,%.0f -> %zu road nodes", target.waypoint ? "waypoint" : "objective", target.pos.x,
			    target.pos.y, gPath.size());
		gLastTarget = target.pos;
		gLastStartNode = startNode;
		gLastSearch = now;
	}
	BuildRoute(player, target);
	gHaveRoute = gRoute.points.size() >= 2;
}

const Route *Current() { return gHaveRoute ? &gRoute : nullptr; }

} // namespace gps
