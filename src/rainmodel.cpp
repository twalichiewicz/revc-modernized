#include "rainmodel.h"
#include <algorithm>
#include <cmath>

namespace rainmodel {

Weather Model::Update(float gameRain, float gameWetRoads, float dt)
{
	w_.rain = std::clamp(gameRain, 0.0f, 1.0f);
	const float target = std::clamp(gameWetRoads, 0.0f, 1.0f);
	w_.wetness += (target - w_.wetness) * (1 - std::exp(-std::max(dt, 0.0f) / kWetEase));
	w_.puddles += w_.rain > kRaining ? w_.rain * dt / kPuddleFill : -dt / kPuddleDrain;
	w_.puddles = std::clamp(w_.puddles, 0.0f, 1.0f);
	return w_;
}

int StreakCount(float rain)
{
	if (rain < kRaining)
		return 0;
	return int(std::lround(kMaxStreaks * std::min(1.0f, (rain - kRaining) / (1 - kRaining))));
}

} // namespace rainmodel
