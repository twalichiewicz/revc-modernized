#pragma once
// When first person draws the new viewmodel, today's (his real arms posed by
// viewmodel.cpp) or neither; and when his arms must be built again. Pure;
// unit-tested (tests/test_fpgun.cpp).
#include <cstdint>

namespace vm {

enum class Use { kNone, kOld, kNew };

struct Moment {
	bool firstPersonOnFoot = false;
	bool enabled = false; // FirstPersonGuns
	bool covered = false; // his gun has a new viewmodel
	bool armsOk = false;  // his model split into articulated arms
	int32_t pedState = 0;
	bool swimming = false, climbing = false;
	bool dodging = false; // the shootdodge's dive and landing: the new gun stays (its firing and crosshair), getting up too
};

Use Choose(const Moment &m);

// What his arms were built from: a change of any (a clothes change) means
// building them again. Every outfit is the same model index, and a new one may
// load where the old one was, so its size counts too.
struct ModelKey {
	const void *geometry = nullptr;
	const void *texture = nullptr;
	int model = -1;
	int vertices = 0, triangles = 0;
};
bool SameModel(const ModelKey &a, const ModelKey &b);

// Whether the drawing pass is taking each frame's viewmodel (Beat: it took
// the last one). Three frames running missed, and it's false for good.
class Heartbeat {
public:
	bool Beat(bool taken);

private:
	int missed_ = 0;
	bool down_ = false;
};

// A shot seen from the clip going down from one frame to the next (`frame`:
// the game's frame counter), never across frames away or a change of gun.
class ShotSeen {
public:
	bool Update(int weapon, int clip, uint32_t frame);

private:
	int weapon_ = -1, clip_ = 0;
	uint32_t frame_ = 0;
};

// A reload's progress, 0..1, from when it started to `end` (the weapon's
// timer, ms), however long the game made it (the fast-reload perk's is a quarter).
class ReloadClock {
public:
	float Progress(bool reloading, uint32_t now, uint32_t end);

private:
	uint32_t start_ = 0, end_ = 0;
	bool running_ = false;
};

} // namespace vm
