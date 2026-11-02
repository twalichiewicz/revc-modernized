#pragma once
#include "types.h"
#include <array>
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
// A solid arrow (#28), floating in the checkpoint: its top outline (0-6, the
// tip first) and its underside (7-13) a thickness below, walled between.
constexpr int kArrowVertices = 14;
constexpr int kArrowIndexCount = (5 + 5 + 14) * 3 * 2; // top, underside, sides; both windings
extern const std::array<uint16_t, kArrowIndexCount> kArrowIndices;
struct Arrow {
	bool valid = false;
	Vec2 direction{};
	Vec3 vertices[kArrowVertices]{};
};
// Live handle generations and coordinates must both match the racing script.
// A finish, a missing next checkpoint, and an ended mission all return no arrow.
Arrow Resolve(const Input &input, const Blip *blips, size_t count);
} // namespace checkpointmodel
