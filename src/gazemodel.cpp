#include "gazemodel.h"
#include <cmath>

namespace gazemodel {
namespace {
constexpr float kPi = 3.14159265358979f;
bool Finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
float Flat(Point a, Point b) { return std::hypot(b.x - a.x, b.y - a.y); }
} // namespace

float Wrap(float angle)
{
	angle = std::fmod(angle + kPi, 2 * kPi);
	if (angle < 0)
		angle += 2 * kPi;
	return angle - kPi;
}

float HeadingTo(Point from, Point to) { return std::atan2(-(to.x - from.x), to.y - from.y); }

float Rate(Point eye, float heading, const Seen &seen)
{
	if (!Finite(eye) || !Finite(seen.head) || !std::isfinite(heading))
		return 0;
	const float d = Flat(eye, seen.head);
	if (d > kNotice || (!seen.player && d > kOthers))
		return 0;
	if (std::fabs(Wrap(HeadingTo(eye, seen.head) - heading)) > kField)
		return 0;
	// Glances per second: Tommy often when he's close, a bystander now and then.
	float rate = seen.player ? 1.2f * (1 - d / kNotice) * (1 - d / kNotice) + .05f
				 : .12f * (1 - d / kOthers) * (1 - d / kOthers) + .01f;
	if (seen.armed)
		rate *= 4; // a drawn weapon draws the eye
	if (seen.fast)
		rate *= 2; // someone sprinting by
	return rate;
}

Glance Choose(Point eye, float heading, const Seen *seen, int count, Memory &memory, uint32_t now, float dt, float roll,
	      float roll2)
{
	if (now < memory.until) {
		// Under way: a look at Tommy is kept up while he stays in view, to a cap.
		for (int i = 0; seen && i < count; i++) {
			const Seen &s = seen[i];
			if (s.id != memory.target || !s.player || Rate(eye, heading, s) <= 0)
				continue;
			const uint32_t cap = memory.started + (s.armed ? kTrackArmed : kTrackCalm);
			if (memory.until - now > 500 || memory.until >= cap)
				break;
			const uint32_t end = now + 600 < cap ? now + 600 : cap;
			memory.next += end - memory.until;
			memory.until = end;
			return Glance{s.id, end - now, true};
		}
		return {};
	}
	if (now < memory.next || !(dt > 0) || !std::isfinite(dt))
		return {};
	const Seen *best = nullptr;
	float rate = 0;
	for (int i = 0; seen && i < count; i++) {
		const float r = Rate(eye, heading, seen[i]);
		if (r > rate) {
			rate = r;
			best = &seen[i];
		}
	}
	if (!best || !(roll < 1 - std::exp(-rate * dt)))
		return {};
	// A glance, or a stare at a weapon; then a pause before the next.
	const bool stare = best->player && best->armed;
	const uint32_t ms = stare ? uint32_t(4000 + roll2 * 3000) : uint32_t(1500 + roll2 * 2000);
	memory.target = best->id;
	memory.started = now;
	memory.until = now + ms;
	memory.next = memory.until + uint32_t(2000 + (1 - roll2) * 4000);
	return Glance{best->id, ms, false};
}

Follow Track(Point eye, float body, float camera, const Seen *seen, int count, int current)
{
	if (!Finite(eye) || !std::isfinite(body) || !std::isfinite(camera) ||
	    std::fabs(Wrap(camera - body)) > kCameraWindow)
		return {};
	Follow out{true, -1, camera};
	float bestOff = 1e9f;
	for (int i = 0; seen && i < count; i++) {
		const Seen &s = seen[i];
		if (!Finite(s.head))
			continue;
		const float to = HeadingTo(eye, s.head), off = std::fabs(Wrap(to - camera)), d = Flat(eye, s.head);
		if (std::fabs(Wrap(to - body)) > kCameraWindow)
			continue; // over his shoulder
		const bool kept = s.id == current && d <= kKeepRange && off <= kKeepLine;
		if (kept)
			return Follow{true, s.id, camera};
		if (d <= kFollowRange && off <= kOnLine && off < bestOff) {
			bestOff = off;
			out.target = s.id;
		}
	}
	return out;
}

} // namespace gazemodel
