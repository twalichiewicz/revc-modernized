#pragma once
// Where a shot meets the police helicopter, in its own frame (x right, y
// forward, z up; measured from chopper.dff): its searchlight (a lamp hung
// under the chin, turning to its spot on the ground), the mast where the main
// rotor meets the cabin roof, the tail rotor's disc, the pilot behind the
// cockpit's glass, or the body (the cabin and the tail boom, each a capsule).
// Whatever the shot reaches first along its line. No game in it; unit-tested
// (tests/test_helizones.cpp).
#include "types.h"

#include <cstdint>
#include <vector>

namespace helizones {

enum class Part { kNone, kBody, kMast, kTailRotor, kLamp, kPilot };

struct Hit {
	Part part = Part::kNone;
	float t = 0.0f;  // m along the shot
	Vec3 at{}, normal{}; // where, and out of what it hit
};

// What's still there to hit: a broken lamp, the tail rotor shot away or the
// pilot gone (dead, or never aboard) leave only the body behind them.
struct Parts {
	Vec3 lamp{}; // the lamp's middle (as it's aimed: Lamp())
	Vec3 lampForward{0,0,-1}; // the actual housing/lens orientation, shared with LampMesh
	bool haveLamp = true, haveTailRotor = true, havePilot = false;
};

// The shot from `from` along unit `dir`, up to `length` m (all in its frame).
// One starting inside the body (a gunner's own, out of his door) meets none of it.
Hit Trace(Vec3 from, Vec3 dir, float length, const Parts &parts);

// The lamp, turned to look at `target` (its frame) as far as its mount
// lets it: where it is (its middle) and the way its beam goes.
struct Lamp {
	Vec3 middle, forward;
};
Lamp Aim(Vec3 target);
// The optical source on the visible lens: shared by corona, beam and actual spotlight.
Vec3 Lens(const Lamp &lamp);

// The lamp's shape (its frame): its housing, the bezel and its mount, to be
// lit as the helicopter is (`lit`), and its lens: in `glow` while it's on
// (drawn as it is, bright), else glass in `lit` (black, shot out).
struct MeshVertex {
	Vec3 p, n;
	uint8_t r, g, b;
};
struct Mesh {
	std::vector<MeshVertex> v;
	std::vector<uint16_t> i; // triangles
};
void LampMesh(const Lamp &lamp, bool on, bool broken, Mesh &lit, Mesh &glow);

// Its parts' places (its frame), for drawing and effects.
constexpr Vec3 kLampPivot{-0.35f, 2.25f, -0.62f}; // under the chin, a little to the left
constexpr float kLampHang = 0.12f;                // the lamp's middle under its pivot
constexpr float kLampRadius = 0.13f, kLampLength = 0.34f;
constexpr Vec3 kMastLow{0.0f, 0.0f, 1.55f}, kMastHigh{0.0f, 0.0f, 2.4f};
constexpr float kMastRadius = 0.24f;
constexpr Vec3 kTailRotor{-0.15f, -6.76f, 1.1f}; // its hub; the disc is in y and z
constexpr float kTailRotorRadius = 0.9f, kTailRotorThick = 0.3f;
constexpr Vec3 kPilot{0.42f, 1.45f, -0.15f};      // his hips in the right-hand seat
constexpr Vec3 kPilotChest{0.42f, 1.5f, 0.35f};   // ... and his chest, where a shot through the glass finds him
constexpr float kPilotReach = 0.32f;              // m from his chest a shot hits him
constexpr Vec3 kTailFin{0.0f, -7.1f, 2.45f};      // its red beacon, on the fin's top

} // namespace helizones
