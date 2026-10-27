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
#include <vector>

namespace places {

// The hospitals' and police stations' round badges, in a game icon's half sizes.
constexpr float kBadgeRadius = 0.9f;

// One of the icons Draw shows.
struct Spot {
	float x, y;
	const char *name;
	int32_t price; // for sale at this price; 0: not (yet)
};

bool Install();
// Draws them where `toScreen` puts a world point (false: not shown there);
// `prices` labels the businesses for sale. Business icons go through
// CRadar::DrawRadarSprite, so on the pause map they join its legend. The
// destination's icon gets its border (poi.h).
void Draw(const std::function<bool(float x, float y, Vec2 &out)> &toScreen, bool prices);
// The icons Draw shows, wherever they are.
void List(std::vector<Spot> &out);
// The game's for-sale blip at a business: Draw shows the business instead.
bool Replaces(const game::Blip &blip);

} // namespace places
