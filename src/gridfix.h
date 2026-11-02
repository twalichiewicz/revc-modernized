#pragma once
// A fix of the game's: CWorld's lines of sight (GetIsLineOfSightClear,
// ProcessLineOfSight) look up the sectors a line crosses without checking
// they're on the world's grid (gridclip.h), so a line with an end off it
// read before or past CWorld::ms_aSectors, and wrote there too (each
// entity's scan code). The game rarely asks it: CCoronas::Render looks from
// every corona on screen to the camera every 2 s, and the airliners, whose
// lights it shows only within 240 m, fly off the grid's south edge. Their
// lights seen from 4 km (planes.cpp) asked it, and crashed. Now every line
// (the game's and the mod's) is cut at the grid's edge first: there's
// nothing built beyond it.
// CPed::BuildPedLists also has an upper-only sector check. Its one verified
// CRect constructor call now bounds that temporary query to the grid, including
// empty queries for nonfinite bounds. Other rectangles and real actor positions
// retain native behavior. VerticalLine and TestSphere already bound their grids.
// Physical Add/RemoveAndAdd also continued past negative-bound assertions and
// stored off-grid list addresses. Only their two GetBoundRect callers now use
// bounded sector intervals, protecting collision/shift membership at its source.
namespace gridfix {

bool Install();

} // namespace gridfix
