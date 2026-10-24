#pragma once
#include "canvas.h"

// GTA VI-style layout for the rest of the HUD: money only where it can be
// spent, a small clock on the minimap, the weapon/ammo at half size in the
// top-right corner, and wanted stars at half size centred at the top (only
// while wanted).
//
// CHud::Draw is one huge function, so instead of rewriting it we hook the
// drawing primitives it calls (CFont::PrintString/SetScale, CSprite2d::Draw)
// and recognise the HUD element by the call's return address.

namespace hud {

// Installs the hooks; false (HUD left vanilla) if the binary isn't the expected one.
bool Install();

// Start of CHud::Draw. `minimapShown`: our minimap is up, so the clock moves onto it.
void BeginFrame(bool minimapShown);

// The clock in the top-right corner of the minimap rectangle.
void DrawClock(Canvas &canvas, float mapX, float mapY, float mapW, float mapH);

// Red Dead 2-style pills right of the map for contextual information: the
// money, while standing where it can be spent (Ammu-Nation, property for sale).
void DrawPills(Canvas &canvas, float mapX, float mapY, float mapW, float mapH);

} // namespace hud
