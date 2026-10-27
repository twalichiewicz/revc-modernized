#pragma once
// The places you can point at on the pause map: the game's icons (Ammu-Nation,
// safehouses, the contacts...) and the mod's (places.h). The controller's
// cursor is drawn to them (magnet.h), a tooltip names the one under it, and
// Cross/A or a right click on one makes it the destination. That's the game's
// waypoint, put exactly on the icon, so the GPS route and the waypoint
// clearing on arrival work as before; the icon gets a border instead of the
// waypoint's square, on the pause map and the minimap. In the legend the
// waypoint reads "Destination" (menu.cpp).
#include "canvas.h"
#include "game.h"

#include <functional>

namespace poi {

constexpr uint16_t kSpriteWaypoint = 40; // RADAR_SPRITE_WAYPOINT

// From CRadar::DrawBlips on the pause map, its icons drawn: where they are
// this frame, by `toScreen` (as places::Draw is given).
void Collect(const std::function<bool(float x, float y, Vec2 &out)> &toScreen);
// The pause map's cursor after the controller moved it by `move`, pulled
// onto the icons; `mapMoving`: the map pans or zooms under it.
Vec2 Steer(Vec2 cursor, Vec2 move, bool mapMoving, float dt);
// Cross/A or a right click with the cursor (`mouse`: the mouse pointer) at
// `at`: on an icon it becomes the destination, or stops being it. False if
// there was no icon there (the press is then the game's: a waypoint there).
bool Press(Vec2 at, bool mouse);
// From the pause menu's DrawBackground, every frame: the tooltip for the icon
// under the cursor, if the map was drawn.
void DrawTooltip(Vec2 at, bool mouse, bool onMap);
// Gameplay is on screen: the map's icons are gone until it's drawn again.
void Forget();

// The waypoint's index in the blip table, or -1.
int WaypointBlip();
// The destination is this game icon / this world point (one of places.h's).
bool IsDestination(const game::Blip &blip);
bool IsDestination(float x, float y);
// The destination sits on an icon: `blip` is the game's icon (null: one of
// places.h's). Then the icon shows where it is, not the waypoint's square.
bool DestinationIcon(const game::Blip *&blip);
// The destination's border round its icon at `at`; `round`: one of places.h's
// badges.
void Border(Canvas &canvas, Vec2 at, bool round);

} // namespace poi
