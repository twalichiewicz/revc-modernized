#pragma once
#include "canvas.h"

#include <cstddef>

// GTA VI-style layout for the rest of the HUD: money only where it can be
// spent or when it changes, a small clock on the minimap, the weapon/ammo at
// half size in the top-right corner, and outlined wanted stars at half size
// above it, fading in (and pushing it down) only while wanted.
//
// CHud::Draw is one huge function, so instead of rewriting it we hook the
// drawing primitives it calls (CFont::PrintString/SetScale, CSprite2d::Draw)
// and recognise the HUD element by the call's return address.

namespace hud {

// Installs the hooks; false (HUD left vanilla) if the binary isn't the expected one.
bool Install();

// Start of CHud::Draw. `minimapShown`: our minimap is up, so the clock moves onto it.
void BeginFrame(bool minimapShown);
// After CHud::Draw: the wanted stars.
void DrawWanted();

// The clock in the top-right corner of the minimap rectangle.
void DrawClock(Canvas &canvas, float mapX, float mapY, float mapW, float mapH);

// "$1,234,567"
void FormatMoney(int32_t money, char *out, size_t size);

// Red Dead 2-style pills right of the map for contextual information: the
// money, while standing where it can be spent (Ammu-Nation, property for sale)
// and for a few seconds after it changes, with the change above it. With the
// map `expanded` (D-pad down) they all show, what he owes on credit too.
void DrawPills(Canvas &canvas, float mapX, float mapY, float mapW, float mapH, bool expanded);

} // namespace hud
