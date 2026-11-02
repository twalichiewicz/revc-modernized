#pragma once
#include "mapdata.h"
// The GTA VI-style minimap (minimap.cpp).

namespace minimap {

// GTA V's expanded radar: zooms the minimap out (and makes it taller) for a
// few seconds; called again, it goes back straight away.
void ToggleExpanded();
bool Expanded();
// The baked map data, when it has street names (map.bin 5); else null.
const mapdata::Map *BakedMap();
// The baked map with the rain's wet ground (map.bin 6), loaded once; null without.
const mapdata::Map *RainMap();

} // namespace minimap
