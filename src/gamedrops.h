#pragma once
// The game's own drops on the screen give way to the mod's (raindrops.h) while
// RainDrops is on: CWeather::AddRain's RAINDROP_2D particles (in third person
// as well: none added, any cleared), and the port's screen droplets (reVC's
// ScreenDroplets, on the camera on foot and in cars alike: not drawn).
namespace gamedrops {

bool Install();
void Hook_AddRain();
void Hook_ScreenDropletsRender();

} // namespace gamedrops
