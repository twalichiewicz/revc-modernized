#pragma once
// The rain's weather as the mod sees it (#32): the game's own (CWeather::Rain,
// CWeather::WetRoads) through rainmodel's easing, once per frame. Shared by the
// wet streets, the streaks and the drops.
#include "rainmodel.h"

namespace weather {

bool Install();
// This frame's weather (updated on the frame's first call; dry if not installed).
const rainmodel::Weather &Now();

// Heavy rain closes the view in: after CTimeCycle::Update sets the frame's far
// clip and fog start, they're brought in with the rain (RainHaze=1).
bool InstallHaze();
void Hook_TimeCycleUpdate();

} // namespace weather
