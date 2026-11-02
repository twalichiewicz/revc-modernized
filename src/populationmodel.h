#pragma once
#include "types.h"
#include <cstdint>
#include <vector>

namespace populationmodel {
struct Node {
	Vec3 pos{};
	float demand = 0;
	std::vector<int> links;
};
struct Agent {
	int node = -1, next = -1, handle = -1, station = -1;
	float along = 0;
	bool cop = false, lost = false;
};
class World {
  public:
	void Init(std::vector<Node> nodes, int civilians, int cops, uint32_t seed);
	void Tick(float dt);
	Vec3 Position(const Agent &agent) const;
	std::vector<Agent> &Agents() { return agents_; }
	const std::vector<Agent> &Agents() const { return agents_; }

  private:
	uint32_t Random();
	std::vector<Node> nodes_;
	std::vector<Agent> agents_;
	uint32_t random_ = 1;
};
enum class Kind { Officer, Car };
class Station {
  public:
	bool Queue(uint64_t token, Kind kind, uint32_t now);
	bool Ready(uint64_t token, uint32_t now) const;
	void Collect(uint64_t token);
	size_t Pending() const { return queue_.size(); }

  private:
	struct Order {
		uint64_t token;
		uint32_t at;
	};
	std::vector<Order> queue_;
};
} // namespace populationmodel
