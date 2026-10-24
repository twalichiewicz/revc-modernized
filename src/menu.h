#pragma once
// GTA IV-style pause menu: it opens straight on the map, and the other pages
// sit in tabs across the top (Q/E, LB/RB or a click); Back on a tab resumes
// the game. Circle (B) is the back button instead of Triangle (Y). On the map,
// LT zooms in instead of RB, and Cross/A only places the waypoint. The map
// legend is drawn at a fraction of its size in a corner.
//
// Like the HUD, the frontend is changed through the primitives it calls: the
// legend's drawing calls are recognised by return address or by running
// inside CRadar::DrawLegend, and moved/scaled into place.
#include "types.h"

struct CSprite2d;
struct CRect;

namespace menu {

using SpriteDrawFn = void (*)(const CSprite2d *, const CRect &, const CRGBA &);

// `spriteDraw` runs the original CSprite2d::Draw. `fontHooks`: hud's CFont hooks
// are in, which the legend's text needs.
bool Install(SpriteDrawFn spriteDraw, bool fontHooks);

// From CHud::Draw: gameplay is on screen, so the next menu opening is fresh.
void OnGameplayFrame();

// From the CSprite2d::Draw hook: true if the call was handled (drawn moved or deferred).
bool SpriteDraw(const CSprite2d *sprite, const CRect &rect, const CRGBA &col, const void *ret);
// From the CFont hooks: the legend's text.
void AdjustText(const void *ret, float &x, float &y);
void AdjustScale(const void *ret, float &sx, float &sy);

} // namespace menu
