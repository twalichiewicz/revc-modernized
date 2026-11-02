#pragma once
// Drops on the view in the rain (#32), without the game. On foot in first
// person, and on bikes and boats, they're on a lens; in a car's (or an
// aircraft's) seat view, on its windscreen. They land with the rain while he's
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
};

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
