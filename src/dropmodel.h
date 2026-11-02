#pragma once
// Drops on the view in the rain (#32), without the game. On bikes and boats
// (and open cars) they're on a lens; in a car's (or an aircraft's) seat view,
// on its windscreen; on foot, none (his view is his own eyes). They land with the rain while he's
// out in it, merge when they touch, run down once heavy, dry under cover, and
// at speed blow up and aside off the windscreen. Positions are fractions of the
// screen (0, 0 its top left); sizes fractions of its height.
#include "types.h"

#include <cstdint>
#include <vector>

namespace dropmodel {

// eVehicleType
constexpr int kCar = 0, kBoat = 1, kTrain = 2, kHeli = 3, kPlane = 4, kBike = 5;

enum class DropMode { None, Lens, Windscreen };
DropMode ModeFor(bool firstPersonOnFoot, bool seatView, int vehicleType, bool topDown, bool interior);

struct Drop {
	Vec2 pos;
	float radius, trail; // the trail: how far up the screen the wet line it ran down reaches
	float seed;          // 0..1: its own uneven outline (a merged drop keeps the bigger one's)
};

// A windscreen's drops are in a window of directions from his seat, fixed to
// the car (x right, y ahead, z up): a drop's place there (0, 0 its top left)
// as a unit direction, and its size (a fraction of the window's height) as an
// angle. So they stay on the glass as he looks round.
constexpr float kWindscreenWide = 1.745f;  // radians across (50 degrees either side)
constexpr float kWindscreenHigh = 0.611f;  // radians from top to bottom...
constexpr float kWindscreenTop = 0.349f;   // ...from 20 degrees over level
Vec3 WindscreenDirection(Vec2 pos);
float WindscreenAngle(float size);

// A pane of glass (a triangle of the car's). The first one a ray from `from`
// along `dir` (a unit vector) meets: how far, and its normal turned towards
// the ray's start; false if it meets none ahead.
struct Pane {
	Vec3 a, b, c;
};
bool HitGlass(const std::vector<Pane> &glass, Vec3 from, Vec3 dir, float &t, Vec3 &normal);

class Drops {
public:
	void Update(float rain, bool exposed, float speed, DropMode mode, float dt);
	// A drop lands here (taking in any it touches).
	void Land(Vec2 pos, float radius);
	const std::vector<Drop> &All() const { return drops_; }

private:
	float Random();
	std::vector<Drop> drops_;
	DropMode mode_ = DropMode::None;
	float pending_ = 0;
	uint32_t seed_ = 0x2545f491u;
};

} // namespace dropmodel
