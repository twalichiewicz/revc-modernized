#include "streetmodel.h"
#include <algorithm>
#include <cmath>

namespace streetmodel {
namespace {

constexpr float kCell = 64.0f;
std::pair<int, int> CellOf(Vec2 p) { return {int(std::floor(p.x / kCell)), int(std::floor(p.y / kCell))}; }

} // namespace

Index::Index(const mapdata::Map &map)
{
	for (size_t r = 0; r < map.roads.size(); r++) {
		const mapdata::Road &road = map.roads[r];
		const float reach = road.width / 2 + kPavement;
		const auto lo = CellOf({std::min(road.a.x, road.b.x) - reach, std::min(road.a.y, road.b.y) - reach});
		const auto hi = CellOf({std::max(road.a.x, road.b.x) + reach, std::max(road.a.y, road.b.y) + reach});
		for (int cx = lo.first; cx <= hi.first; cx++)
			for (int cy = lo.second; cy <= hi.second; cy++)
				cells_[{cx, cy}].push_back(r);
	}
}

const std::vector<size_t> *Index::Near(Vec2 p) const
{
	const auto found = cells_.find(CellOf(p));
	return found == cells_.end() ? nullptr : &found->second;
}

int StreetAt(const Index &index, const mapdata::Map &map, Vec3 pos, Vec2 heading, bool driving)
{
	const std::vector<size_t> *near = index.Near({pos.x, pos.y});
	if (!near)
		return -1;
	const float minCos = std::cos(kHeading * 3.14159265f / 180.0f);
	const float headingLength = std::hypot(heading.x, heading.y);
	int best = -1;
	float bestDistance = INFINITY;
	for (size_t r : *near) {
		if (r >= map.roadStreet.size() || map.roadStreet[r] == mapdata::Map::kNoStreet ||
		    map.roadStreet[r] >= map.streets.size())
			continue;
		const mapdata::Road &road = map.roads[r];
		const float dx = road.b.x - road.a.x, dy = road.b.y - road.a.y, len2 = dx * dx + dy * dy;
		if (len2 <= 0)
			continue;
		if (driving && headingLength > 0 &&
		    std::fabs(dx * heading.x + dy * heading.y) / (std::sqrt(len2) * headingLength) < minCos)
			continue; // across his way
		const float t = std::clamp(((pos.x - road.a.x) * dx + (pos.y - road.a.y) * dy) / len2, 0.0f, 1.0f);
		const float distance = std::hypot(pos.x - (road.a.x + dx * t), pos.y - (road.a.y + dy * t));
		const float z = r * 2 + 1 < map.roadZ.size() ? map.roadZ[r * 2] + (map.roadZ[r * 2 + 1] - map.roadZ[r * 2]) * t
		                                              : pos.z;
		if (distance > road.width / 2 + kPavement || std::fabs(pos.z - z) > kHeight || distance >= bestDistance)
			continue;
		bestDistance = distance;
		best = map.roadStreet[r];
	}
	return best;
}

int Tracker::Update(int seen, Vec3 pos, float dt)
{
	if (!started_) { // the first street he's seen on is his at once
		started_ = true;
		current_ = seen;
		return current_;
	}
	if (seen == current_) {
		candidate_ = current_;
		candidateTime_ = 0;
		return current_;
	}
	if (seen != candidate_) {
		candidate_ = seen;
		candidateTime_ = 0;
		candidateFrom_ = pos;
		return current_;
	}
	candidateTime_ += dt;
	const float moved = std::hypot(pos.x - candidateFrom_.x, pos.y - candidateFrom_.y);
	if (seen < 0 ? candidateTime_ >= kNoneAfter : (candidateTime_ >= kSwitchAfter || moved >= kSwitchFar))
		current_ = seen;
	return current_;
}

} // namespace streetmodel
