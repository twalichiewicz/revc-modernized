#include "dropmodel.h"

#include <algorithm>
#include <cmath>

namespace dropmodel {
namespace {

constexpr float kLandRate = 22.0f;        // drops a second on a lens in a downpour
constexpr float kSmallest = 0.010f, kBiggest = 0.026f; // as they land (11 to 28 pixels at 1080)
constexpr float kHeavy = 0.032f;          // from here they run down
constexpr float kRun = 0.08f;             // screens a second a heavy drop runs
constexpr float kDryOut = 0.0012f;        // radius a second lost under cover
constexpr float kDryIn = 0.0002f;         // ...and out in the rain
constexpr float kTrailFade = 0.06f;       // a second
constexpr float kLongestTrail = 0.18f;
constexpr size_t kMost = 150;

// How hard the air pushes them off a windscreen, 0..1, by speed (m/s).
float Push(float speed) { return std::clamp((speed - 4.0f) / 26.0f, 0.0f, 1.0f); }

} // namespace

DropMode ModeFor(bool firstPersonOnFoot, bool seatView, int vehicleType, bool topDown, bool interior)
{
	(void)firstPersonOnFoot; // on foot his view is his own eyes: no drops on it (the user's call)
	if (interior || !seatView)
		return DropMode::None;
	switch (vehicleType) {
	case kCar:
		return topDown ? DropMode::Lens : DropMode::Windscreen;
	case kHeli:
	case kPlane:
		return DropMode::Windscreen;
	case kBike:
	case kBoat:
		return DropMode::Lens;
	default:
		return DropMode::None;
	}
}

Vec3 WindscreenDirection(Vec2 pos)
{
	const float across = (pos.x - 0.5f) * kWindscreenWide, up = kWindscreenTop - pos.y * kWindscreenHigh;
	return {std::sin(across) * std::cos(up), std::cos(across) * std::cos(up), std::sin(up)};
}

float WindscreenAngle(float size) { return size * kWindscreenHigh; }

bool HitGlass(const std::vector<Pane> &glass, Vec3 from, Vec3 dir, float &t, Vec3 &normal)
{
	bool hit = false;
	for (const Pane &p : glass) { // Moller-Trumbore
		const Vec3 e1{p.b.x - p.a.x, p.b.y - p.a.y, p.b.z - p.a.z}, e2{p.c.x - p.a.x, p.c.y - p.a.y, p.c.z - p.a.z};
		const Vec3 h{dir.y * e2.z - dir.z * e2.y, dir.z * e2.x - dir.x * e2.z, dir.x * e2.y - dir.y * e2.x};
		const float det = e1.x * h.x + e1.y * h.y + e1.z * h.z;
		if (std::fabs(det) < 1e-9f)
			continue;
		const Vec3 s{from.x - p.a.x, from.y - p.a.y, from.z - p.a.z};
		const float u = (s.x * h.x + s.y * h.y + s.z * h.z) / det;
		if (u < 0 || u > 1)
			continue;
		const Vec3 q{s.y * e1.z - s.z * e1.y, s.z * e1.x - s.x * e1.z, s.x * e1.y - s.y * e1.x};
		const float v = (dir.x * q.x + dir.y * q.y + dir.z * q.z) / det;
		if (v < 0 || u + v > 1)
			continue;
		const float d = (e2.x * q.x + e2.y * q.y + e2.z * q.z) / det;
		if (d <= 1e-4f || (hit && d >= t))
			continue;
		Vec3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
		const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
		if (!(len > 0))
			continue;
		const float side = (n.x * dir.x + n.y * dir.y + n.z * dir.z) > 0 ? -1.0f : 1.0f; // towards him
		normal = {n.x / len * side, n.y / len * side, n.z / len * side};
		t = d;
		hit = true;
	}
	return hit;
}

float Drops::Random()
{
	seed_ ^= seed_ << 13;
	seed_ ^= seed_ >> 17;
	seed_ ^= seed_ << 5;
	return float(seed_ & 0xffffff) / float(0x1000000);
}

void Drops::Land(Vec2 pos, float radius)
{
	Drop drop{pos, radius, 0, Random()};
	// Take in every drop it touches (and what those touch, as it grows).
	for (bool merged = true; merged;) {
		merged = false;
		for (size_t i = 0; i < drops_.size(); i++) {
			const Drop &o = drops_[i];
			if (std::hypot(o.pos.x - drop.pos.x, o.pos.y - drop.pos.y) >= o.radius + drop.radius)
				continue;
			const float a = drop.radius * drop.radius * drop.radius, b = o.radius * o.radius * o.radius;
			drop.pos = {(drop.pos.x * a + o.pos.x * b) / (a + b), (drop.pos.y * a + o.pos.y * b) / (a + b)};
			drop.seed = b > a ? o.seed : drop.seed;
			drop.radius = std::cbrt(a + b);
			drop.trail = std::max(drop.trail, o.trail);
			drops_.erase(drops_.begin() + long(i));
			merged = true;
			break;
		}
	}
	drops_.push_back(drop);
}

void Drops::Update(float rain, bool exposed, float speed, DropMode mode, float dt)
{
	if (mode != mode_) {
		drops_.clear();
		pending_ = 0;
		mode_ = mode;
	}
	if (mode == DropMode::None)
		return;
	const bool windscreen = mode == DropMode::Windscreen;
	const float push = windscreen ? Push(speed) : 0.0f;
	// Driving into it, more land.
	if (exposed && rain > 0) {
		pending_ += kLandRate * std::clamp(rain, 0.0f, 1.0f) * (windscreen ? 1.0f + speed / 15.0f : 1.0f) * dt;
		while (pending_ >= 1.0f) {
			pending_ -= 1.0f;
			if (drops_.size() >= kMost)
				drops_.erase(drops_.begin()); // the oldest
			Land({Random(), Random()}, kSmallest + (kBiggest - kSmallest) * Random() * Random());
		}
	} else if (!exposed) {
		pending_ = 0;
	}
	std::vector<Drop> kept;
	kept.reserve(drops_.size());
	for (Drop d : drops_) {
		d.radius -= (exposed ? kDryIn : kDryOut) * dt;
		if (d.radius < kSmallest * 0.5f)
			continue;
		float dy = 0, dx = 0;
		if (d.radius >= kHeavy)
			dy += kRun * (d.radius / kHeavy) * dt; // it runs down
		if (push > 0) {                            // the air's stronger: up and aside
			dy -= 0.45f * push * dt;
			dx += (d.pos.x < 0.5f ? -1.0f : 1.0f) * 0.25f * push * dt;
		}
		d.pos = {d.pos.x + dx, d.pos.y + dy};
		const float moved = std::hypot(dx, dy);
		d.trail = moved > 0 ? std::min(kLongestTrail, d.trail + moved) : std::max(0.0f, d.trail - kTrailFade * dt);
		if (d.pos.x < -d.radius || d.pos.x > 1 + d.radius || d.pos.y < -d.radius || d.pos.y > 1 + d.radius)
			continue; // off the glass
		kept.push_back(d);
	}
	drops_ = std::move(kept);
}

} // namespace dropmodel
