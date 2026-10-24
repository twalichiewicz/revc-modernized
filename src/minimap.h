#pragma once
// The GTA VI-style minimap (minimap.cpp).

namespace minimap {

// GTA V's expanded radar: zooms the minimap out (and makes it taller) for a
// few seconds; called again, it goes back straight away.
void ToggleExpanded();

} // namespace minimap
