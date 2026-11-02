#pragma once
// The game's own drops on the screen (CWeather::AddRain: RAINDROP_2D particles,
// in third person as well) give way to the mod's (raindrops.h) while RainDrops
// is on: none are added and any it had are cleared.
namespace gamedrops {

bool Install();
void Hook_AddRain();

} // namespace gamedrops
