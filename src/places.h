#pragma once
// Places Vice City never marks on its maps: the hospitals and police stations
// (where you come back after being wasted or busted: CRestart's respawn
// points, as the scripts set them), and the businesses you can buy: dimmed
// until they go on sale, then in full with their price on the pause map, in
// place of the game's for-sale house (the same one every safehouse gets).
// Once bought the game's own icon takes over. The ones the story still needs
// get a gold ring (story.h).
#include "game.h"

#include <functional>

namespace places {

bool Install();
// Draws them where `toScreen` puts a world point (false: not shown there);
// `prices` labels the businesses for sale. Business icons go through
// CRadar::DrawRadarSprite, so on the pause map they join its legend.
void Draw(const std::function<bool(float x, float y, Vec2 &out)> &toScreen, bool prices);
// The game's for-sale blip at a business: Draw shows the business instead.
bool Replaces(const game::Blip &blip);

} // namespace places
