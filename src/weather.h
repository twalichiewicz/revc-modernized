#pragma once
// The rain's weather as the mod sees it (#32): the game's own (CWeather::Rain,
// CWeather::WetRoads) through rainmodel's easing, once per frame. Shared by the
// wet streets, the streaks and the drops.
#include "rainmodel.h"

namespace weather {

bool Install();
// This frame's weather (updated on the frame's first call; dry if not installed).
const rainmodel::Weather &Now();

} // namespace weather
