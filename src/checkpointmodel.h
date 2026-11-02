#pragma once
#include "types.h"
#include <cstddef>
#include <cstdint>
namespace checkpointmodel {
struct Blip {
	bool inUse = false;
	uint16_t generation = 0;
	Vec3 pos{};
};
struct Input {
	bool racing = false;
	int32_t currentHandle = -1, nextHandle = -1;
	Vec3 current{}, next{};
	float radius = 4;
};
struct Arrow {
	bool valid = false;
	Vec2 direction{};
	Vec3 vertices[7]{};
};
// Live handle generations and coordinates must both match the racing script.
// A finish, a missing next checkpoint, and an ended mission all return no arrow.
Arrow Resolve(const Input &input, const Blip *blips, size_t count);
} // namespace checkpointmodel
