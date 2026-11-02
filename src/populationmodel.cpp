#include "populationmodel.h"
#include <algorithm>
#include <cmath>
#include <utility>
namespace populationmodel {
uint32_t World::Random()
{
	random_ = random_ * 1664525u + 1013904223u;
	return random_ >> 8;
}
void World::Init(std::vector<Node> nodes, int civilians, int cops, uint32_t seed)
{
	nodes_ = std::move(nodes);
	agents_.clear();
	random_ = seed;
	std::vector<float> cumulative;
	float total = 0;
	for (const Node &n : nodes_) {
		total += n.links.empty() || !std::isfinite(n.demand) ? 0.0f : std::max(0.0f, n.demand);
		cumulative.push_back(total);
	}
	if (total <= 0)
		return;
	const int count = std::clamp(civilians, 0, 4096) + std::clamp(cops, 0, 256);
	for (int i = 0; i < count; i++) {
		const float pick = float(Random()) / float(1u << 24) * total;
		int at = int(std::upper_bound(cumulative.begin(), cumulative.end(), pick) - cumulative.begin());
		at = std::min(at, int(nodes_.size()) - 1);
		const auto &links = nodes_[size_t(at)].links;
		Agent a;
		a.node = at;
		a.next = links[size_t(Random()) % links.size()];
		a.along = float(Random()) / float(1u << 24);
		a.cop = i >= civilians;
		agents_.push_back(a);
	}
}
Vec3 World::Position(const Agent &a) const
{
	if (a.node < 0 || size_t(a.node) >= nodes_.size())
		return {};
	const Vec3 p = nodes_[size_t(a.node)].pos;
	if (a.next < 0 || size_t(a.next) >= nodes_.size())
		return p;
	const Vec3 q = nodes_[size_t(a.next)].pos;
	return {p.x + (q.x - p.x) * a.along, p.y + (q.y - p.y) * a.along, p.z + (q.z - p.z) * a.along};
}
void World::Tick(float dt)
{
	dt = std::clamp(dt, 0.0f, 2.0f);
	for (Agent &a : agents_) {
		if (a.handle >= 0 || a.lost || a.node < 0 || a.next < 0)
			continue;
		float distance = dt * (a.cop ? 1.35f : 1.1f);
		for (int steps = 0; distance > 0 && steps < 8; steps++) {
			const Vec3 p = nodes_[size_t(a.node)].pos, q = nodes_[size_t(a.next)].pos;
			const float length = std::max(0.1f, std::hypot(q.x - p.x, q.y - p.y));
			const float remain = length * (1 - a.along);
			if (distance < remain) {
				a.along += distance / length;
				break;
			}
			distance -= remain;
			const int previous = a.node;
			a.node = a.next;
			a.along = 0;
			const auto &links = nodes_[size_t(a.node)].links;
			if (links.empty()) {
				a.next = -1;
				break;
			}
			a.next = links[size_t(Random()) % links.size()];
			if (a.next == previous && links.size() > 1)
				for (int next : links)
					if (next != previous) {
						a.next = next;
						break;
					}
		}
	}
}
bool Station::Queue(uint64_t token, Kind kind, uint32_t now)
{
	for (const Order &o : queue_)
		if (o.token == token)
			return true;
	if (queue_.size() >= 8)
		return false;
	const uint32_t base = !queue_.empty() && int32_t(queue_.back().at - now) > 0 ? queue_.back().at : now;
	queue_.push_back({token, base + (kind == Kind::Officer ? 20000u : 45000u)});
	return true;
}
bool Station::Ready(uint64_t token, uint32_t now) const
{
	for (const Order &o : queue_)
		if (o.token == token)
			return int32_t(now - o.at) >= 0;
	return false;
}
void Station::Collect(uint64_t token)
{
	queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [&](const Order &o) { return o.token == token; }),
				 queue_.end());
}
} // namespace populationmodel
