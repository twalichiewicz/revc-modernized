#pragma once
// GPS guidance: picks a destination (your waypoint, else the mission
// objective) and routes to it over the game's car path network.
#include "game.h"
#include "scene.h"

namespace gps {

// Call once per frame while the minimap is drawn.
void Update(const CVector &player);

// The route along road centres, starting level with the player; nullptr when
// there is nowhere to guide to (or the minimap hasn't been drawn lately).
const Route *Current();

} // namespace gps
