#include "heat.h"

#include <algorithm>
#include <cmath>

namespace heat {

namespace {

int Stars(int n)
{
	return std::clamp(n, 1, kMaxStars);
}

float Distance2D(Vec3 a, Vec3 b)
{
	return std::hypot(a.x - b.x, a.y - b.y);
}

// Linear from `a` at `d0` to `b` at `d1`.
float Ramp(float d, float d0, float a, float d1, float b)
{
	return a + (b - a) * (d - d0) / (d1 - d0);
}

} // namespace

float SearchRadius(int stars)
{
	return 60.0f + 25.0f * stars;
}

float FillRate(float distance, bool matches, bool helicopter)
{
	if (helicopter) {
		if (!matches) return 0;
		distance /= 2.5f;
	}
	if (matches) {
		if (distance <= 15) return 1 / 1.2f;
		if (distance <= 70) return Ramp(distance, 15, 1 / 1.2f, 70, 1 / 5.0f);
		return 0;
	}
	if (distance <= 5) return 1 / 3.0f;
	if (distance < 10) return Ramp(distance, 5, 1 / 3.0f, 10, 0);
	return 0;
}

Drop ClassifyDrop(const DropFacts &f)
{
	if (f.suspended) return Drop::Respray;
	if (f.level == 0 && f.chaos > 0) return Drop::Timeout;
	if (f.onMission) return Drop::Script;
	if (f.outfitChanged) return Drop::Clothes;
	return f.waited ? Drop::Script : Drop::Wait;
}

State Model::state() const
{
	if (state_ == State::Clear && calling_) return State::Reported;
	return state_;
}

int Model::stars() const
{
	return state_ == State::Clear ? 0 : stars_;
}

int Model::gameStars() const
{
	return state_ == State::Wanted || state_ == State::Searching ? stars_ : 0;
}

bool Model::Knows(bool seenNow, uint32_t now) const
{
	switch (state_) {
	case State::Wanted: return seenNow || now - lastSeenAt_ <= kLostAfterMs;
	case State::Searching: return false; // (only a full eye: CopSees, and Wanted)
	default: return true;                // (nobody's after him)
	}
}

bool Model::Matches(const Looks &looks) const
{
	const Description &d = description_;
	if (d.haveOutfit && d.outfit == looks.outfit) return true;
	return d.haveVehicle && looks.vehicleModel == d.vehicleModel && looks.colour1 == d.colour1 &&
	       looks.colour2 == d.colour2;
}

bool Model::OutfitStruck(const Looks &looks) const
{
	return description_.haveOutfit && description_.outfit != looks.outfit;
}

bool Model::VehicleStruck(const Looks &looks) const
{
	const Description &d = description_;
	return d.haveVehicle && looks.vehicleModel >= 0 &&
	       (looks.vehicleModel != d.vehicleModel || looks.colour1 != d.colour1 || looks.colour2 != d.colour2);
}

void Model::CopSees(const Looks &looks, Vec3 at, uint32_t now, int gameStars)
{
	Identified(looks, gameStars);
	lastSeen_ = at;
	lastSeenAt_ = now;
}

// They know him: Wanted, at least at the stars he escaped at, the description now `looks`.
void Model::Identified(const Looks &looks, int stars)
{
	stars_ = Stars(std::max({state_ == State::Clear ? 0 : stars_, stars, escaped_}));
	state_ = State::Wanted;
	description_.outfit = looks.outfit;
	description_.haveOutfit = true;
	// On foot they still remember the car they last saw him in.
	if (looks.vehicleModel >= 0) {
		description_.vehicleModel = looks.vehicleModel;
		description_.colour1 = looks.colour1;
		description_.colour2 = looks.colour2;
		description_.haveVehicle = true;
	}
}

void Model::Reported(const Looks *looks, Vec3 at, uint32_t when, uint32_t now, int stars)
{
	// Under the APB, someone not on its description is a stranger to them: only where.
	if (looks && state_ == State::Apb && !Matches(*looks))
		looks = nullptr;
	if (looks) {
		// Where he was then: never over a later sighting.
		const bool later = state_ == State::Clear || state_ == State::Reported || when >= lastSeenAt_;
		Identified(*looks, stars);
		if (later) {
			lastSeen_ = at;
			lastSeenAt_ = when;
		}
		return;
	}
	// Only where: a search at the call's own stars (an APB's stars wait for a recognition).
	stars_ = Stars(std::max(state_ == State::Clear || state_ == State::Apb ? 0 : stars_, stars));
	if (state_ == State::Wanted) return;
	state_ = State::Searching;
	searchCentre_ = at;
	searchSince_ = now;
}

void Model::Calling(bool anyone)
{
	calling_ = anyone;
}

void Model::Tick(Vec3 him, uint32_t now)
{
	if (state_ == State::Wanted && now - lastSeenAt_ > kLostAfterMs) {
		state_ = State::Searching;
		searchCentre_ = lastSeen_;
		searchSince_ = now;
		return;
	}
	if (state_ != State::Searching) return;
	if (now - searchSince_ <= kSearchMs[stars_]) return;
	if (Distance2D(him, searchCentre_) <= searchRadius()) return;
	// An APB if they know what he looks like, or it was serious (two stars or more).
	if (description_.haveOutfit || description_.haveVehicle || stars_ >= kApbStars) {
		state_ = State::Apb;
		escaped_ = stars_ = std::max(escaped_, stars_);
	} else {
		Clear();
	}
}

void Model::Lowered(int stars)
{
	if (stars <= 0) {
		Clear();
		return;
	}
	stars_ = std::min(stars_, stars);
	escaped_ = std::min(escaped_, stars);
}

void Model::Clear()
{
	*this = Model();
}

} // namespace heat
