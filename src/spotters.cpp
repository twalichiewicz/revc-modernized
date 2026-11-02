#include "spotters.h"

#include <algorithm>

namespace spotters {

void Spotters::Follow(bool onHim, const std::vector<Look> &looks, float dt)
{
	onHim_ = onHim;
	for (Spotter &s : spotters_) {
		auto it = std::find_if(looks.begin(), looks.end(), [&](const Look &l) { return l.handle == s.handle; });
		s.since += dt;
		if (it == looks.end()) {
			s.ending = true; // dead, or gone
		} else {
			s.eyes = it->eyes;
			s.distance = it->distance;
			s.lost = it->sees ? 0.0f : s.lost + dt;
			s.ending |= s.since > kAtLeast &&
			            (s.lost > kLost || s.distance < kReached || it->shooting || s.since > kAtMost);
		}
		if (s.ending)
			s.fade -= dt / kFade;
		s.alpha = std::clamp(s.fade, 0.0f, 1.0f);
	}
	spotters_.erase(std::remove_if(spotters_.begin(), spotters_.end(), [](const Spotter &s) { return s.fade <= 0.0f; }),
	                spotters_.end());
}

void Spotters::Recognised(const Look &look) { Spot(look); }

void Spotters::SawCrime(const Look &look)
{
	if (!onHim_)
		Spot(look);
}

void Spotters::Clear()
{
	spotters_.clear();
	onHim_ = false;
}

void Spotters::Spot(const Look &look)
{
	for (Spotter &s : spotters_)
		if (s.handle == look.handle) {
			s.since = s.lost = 0.0f;
			s.fade = 1.0f;
			s.ending = false;
			return;
		}
	Spotter s;
	s.handle = look.handle;
	s.helicopter = look.helicopter;
	s.eyes = look.eyes;
	s.distance = look.distance;
	spotters_.push_back(s);
}

} // namespace spotters
