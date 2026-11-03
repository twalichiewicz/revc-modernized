#include "drivelog.h"

#include <algorithm>
#include <cmath>

namespace drivelog {
namespace {

constexpr float kAtRest = 0.5f, k100 = 100.0f / 3.6f, kWindow = 30.0f; // m/s, m/s, s

float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

} // namespace

void Add(State &s, const Sample &x)
{
	if (s.startedAt < 0)
		s.startedAt = x.t;
	s.window.top = std::max(s.window.top, x.speed);
	if (x.speed < kAtRest) {
		s.armed = true;
		s.runFrom = -1;
	} else if (s.armed && s.runFrom < 0) {
		// Moving off: when it was at 0, from the last two samples.
		const float dv = x.speed - s.lastSpeed, dt = x.t - s.lastT;
		s.runFrom = s.any && dv > 0 && dt > 0 ? s.lastT - s.lastSpeed * dt / dv : x.t;
		s.armed = false;
	}
	if (s.runFrom >= 0 && x.speed >= k100) {
		const float dv = x.speed - s.lastSpeed, dt = x.t - s.lastT;
		const float at = s.any && dv > 0 && s.lastSpeed < k100 ? s.lastT + (k100 - s.lastSpeed) * dt / dv : x.t;
		const float time = at - s.runFrom;
		if (s.window.zeroTo100 < 0 || time < s.window.zeroTo100)
			s.window.zeroTo100 = time;
		s.runFrom = -1;
	}
	if (x.allWheelsDown) {
		s.window.rollMost = std::max(s.window.rollMost, std::fabs(x.roll));
		s.window.pitchMost = std::max(s.window.pitchMost, std::fabs(x.pitch));
	}
	s.lastT = x.t;
	s.lastSpeed = x.speed;
	s.any = true;
}

bool Due(const State &s, float t) { return s.startedAt >= 0 && t - s.startedAt >= kWindow; }

Window Take(State &s, float t)
{
	const Window w = s.window;
	s.window = Window{};
	s.startedAt = t;
	return w;
}

void BodyAngles(const float right[3], const float forward[3], const float up[3], const float contact[4][3], float &roll,
                float &pitch)
{
	float across[3], along[3];
	for (int i = 0; i < 3; ++i) {
		across[i] = 0.5f * (contact[2][i] + contact[3][i] - contact[0][i] - contact[1][i]); // right minus left
		along[i] = 0.5f * (contact[0][i] + contact[2][i] - contact[1][i] - contact[3][i]);  // front minus rear
	}
	// Rolled right side down, the road under the right wheels stands higher in
	// the body's own frame; nose down, so does the road under the front ones.
	roll = std::atan2(Dot(across, up), Dot(across, right));
	pitch = -std::atan2(Dot(along, up), Dot(along, forward));
}

} // namespace drivelog
