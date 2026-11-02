#include "grip.h"

#include <algorithm>
#include <cmath>

namespace grip {
namespace {

// How fast his grip goes (of all of it, a second): a little just hanging on,
// more as the car goes faster than he runs, more turning hard; it comes back
// at a crawl. At a jog (5 m/s) straight he hangs on nearly two seconds; at
// 36 km/h half a second; at a sharp turn less.
constexpr float kHanging = 0.15f, kSpeedPart = 0.6f, kPace = 6.0f, kTurnPart = 1.2f, kTurnPace = 1.2f;
constexpr float kRecover = 0.5f;

} // namespace

AtDoor Reach(float speed)
{
	if (speed < kOpenBelow)
		return AtDoor::kOpen;
	return speed < kGrabBelow ? AtDoor::kGrab : AtDoor::kMiss;
}

Now Hold::Update(float speed, float turn, float dt)
{
	if (speed < kOpenBelow) {
		strain_ = std::max(0.0f, strain_ - kRecover * dt);
		slow_ += dt;
		return slow_ >= kOpensAfter ? Now::kOpens : Now::kHolding;
	}
	slow_ = 0.0f;
	const float s = speed / kPace, t = std::fabs(turn) / kTurnPace;
	strain_ += (kHanging + kSpeedPart * s * s + kTurnPart * t * t) * dt;
	if (strain_ >= 1.0f)
		return Now::kLetsGo;
	return Now::kHolding;
}

} // namespace grip
