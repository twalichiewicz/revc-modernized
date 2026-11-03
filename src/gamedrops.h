#pragma once
// The game's own drops on the screen give way to the mod's (raindrops.h) while
// RainDrops is on: CWeather::AddRain's RAINDROP_2D particles (none added, any
// cleared), and the port's screen droplets (reVC's ScreenDroplets) in first
// person and a vehicle's seat view. In third person the camera's a thing in
// the world, rained on, and the port's droplets show (the user's call).
namespace gamedrops {

bool Install();
void Hook_AddRain();
void Hook_ScreenDropletsRender();

} // namespace gamedrops
