#include "dropmodel.h"

#include <algorithm>
#include <cmath>

namespace dropmodel {
namespace {

constexpr float kLandRate = 30.0f;        // drops a second on a lens in a downpour
constexpr float kSmallest = 0.005f, kBiggest = 0.014f; // as they land (5 to 15 pixels at 1080)
constexpr float kHeavy = 0.017f;          // from here they run down
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
	if (interior)
		return DropMode::None;
	if (firstPersonOnFoot)
		return DropMode::Lens;
	if (!seatView)
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

float Drops::Random()
{
	seed_ ^= seed_ << 13;
	seed_ ^= seed_ >> 17;
	seed_ ^= seed_ << 5;
	return float(seed_ & 0xffffff) / float(0x1000000);
}

void Drops::Land(Vec2 pos, float radius)
{
	Drop drop{pos, radius, 0};
	// Take in every drop it touches (and what those touch, as it grows).
	for (bool merged = true; merged;) {
		merged = false;
		for (size_t i = 0; i < drops_.size(); i++) {
			const Drop &o = drops_[i];
			if (std::hypot(o.pos.x - drop.pos.x, o.pos.y - drop.pos.y) >= o.radius + drop.radius)
				continue;
			const float a = drop.radius * drop.radius * drop.radius, b = o.radius * o.radius * o.radius;
			drop.pos = {(drop.pos.x * a + o.pos.x * b) / (a + b), (drop.pos.y * a + o.pos.y * b) / (a + b)};
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
