#include "headbalance.h"

#include <algorithm>
#include <cmath>

namespace headbalance {
namespace {

constexpr float kDeg = 3.14159265f / 180.0f;
// How much of the view's roll he levels, and at most; how much of its quick
// pitching (faster than kSlowPitch), and at most; how long his neck takes.
constexpr float kRollShare = 0.35f, kRollMost = 6 * kDeg, kPitchShare = 0.30f, kPitchMost = 3 * kDeg;
constexpr float kSlowPitch = 1.5f, kRollLag = 0.2f, kPitchLag = 0.15f, kLongestStep = 0.1f; // s

float Ease(float dt, float tau) { return 1.0f - std::exp(-dt / tau); }

} // namespace

Turn Step(State &s, float viewRoll, float viewPitch, bool active, float dt)
{
	dt = std::clamp(dt, 0.0f, kLongestStep);
	if (!s.primed) {
		s.slowPitch = viewPitch;
		s.primed = true;
	}
	s.slowPitch += (viewPitch - s.slowPitch) * Ease(dt, kSlowPitch);
	const float roll = active ? std::clamp(-kRollShare * viewRoll, -kRollMost, kRollMost) : 0.0f;
	const float pitch = active ? std::clamp(-kPitchShare * (viewPitch - s.slowPitch), -kPitchMost, kPitchMost) : 0.0f;
	s.roll += (roll - s.roll) * Ease(dt, kRollLag);
	s.pitch += (pitch - s.pitch) * Ease(dt, kPitchLag);
	return {s.pitch, s.roll};
}

} // namespace headbalance
