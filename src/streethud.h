#pragma once
// Street names on the HUD (#35), GTA IV's way: the street he's on (map.bin's
// named roads, streetmodel.h) over the area he's in (the game's own name for
// it), bottom right, on a new street, a new area or getting into a vehicle,
// and while the expanded map is up. The game's own area name is hidden while
// this draws (hud::SuppressAreaName); with StreetNames=0 or a map baked before
// street names, the game's shows as before.
namespace streethud {
bool Install();
// Every HUD frame (minimap.cpp's CHud::Draw hook).
void Draw();
} // namespace streethud
