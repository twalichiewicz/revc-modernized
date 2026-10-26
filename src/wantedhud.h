#pragma once
// What the police know, on screen (RDR2 / GTA VI style): a row of round icons
// right of the minimap (a witness's call going through, with its ring; the
// search; the APB; the outfit and car on the description, struck through
// once he's changed them), an eye with a filling ring over each cop who's
// looking at him (at the screen's edge when the cop is off it), and the
// area they're searching on the minimap.
#include "canvas.h"
#include "types.h"

#include <functional>

namespace wantedhud {

// The icon row's height (0 with no icons), for what stacks above it (hud::DrawPills).
float RowHeight(float mapH);
// The icons, bottom-aligned with the map, left to right from just right of it.
void Draw(Canvas &canvas, float mapX, float mapY, float mapW, float mapH);
// The eyes over cops; after the HUD, before the font flush.
void DrawEyes(Canvas &canvas);
// On the minimap: the search area and witnesses running for a phone.
// `fillWorld` fills a convex polygon given in world XY (clipped to the map);
// `toScreen` places a world point on it (false: not on the map).
void DrawOnMap(Canvas &canvas, const std::function<void(const Vec2 *world, int n, CRGBA col)> &fillWorld,
               const std::function<bool(float x, float y, Vec2 &out)> &toScreen);

} // namespace wantedhud
