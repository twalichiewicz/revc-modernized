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
	const bool inView = std::fabs(Wrap(HeadingTo(eye, seen.head) - heading)) <= kField;
	if (!inView && d > kClose)
		return 0;
	// Glances per second: Tommy often when he's close, a bystander now and then.
	float rate = seen.player ? .6f * (1 - d / kNotice) * (1 - d / kNotice) + .04f
				 : .12f * (1 - d / kOthers) * (1 - d / kOthers) + .01f;
	if (seen.armed)
		rate *= 4; // a drawn weapon draws the eye
	if (seen.fast)
		rate *= 2; // someone sprinting by
	if (!inView)
		rate *= .5f; // sensed beside or behind, not seen
	return rate;
}

Glance Choose(Point eye, float heading, const Seen *seen, int count, Memory &memory, uint32_t now, float dt, float roll,
	      float roll2)
{
	if (now < memory.until || now < memory.next || !(dt > 0) || !std::isfinite(dt))
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
	const uint32_t ms = stare ? uint32_t(3000 + roll2 * 3000) : uint32_t(1200 + roll2 * 1600);
	memory.target = best->id;
	memory.until = now + ms;
	memory.next = memory.until + uint32_t(4000 + (1 - roll2) * 6000);
	return Glance{best->id, ms};
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
