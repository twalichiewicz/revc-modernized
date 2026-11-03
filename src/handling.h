#pragma once
// GTA IV-style weight for the cars (#37), in the game (handlingmodel.h has the
// numbers): each car's handling.cfg line is retuned by its type as the game
// converts it (cHandlingDataMgr::ConvertDataToGameUnits), so the game derives
// its turn mass, top speed and gears from the new values; its turn mass is then
// raised. Once, at load, before any car exists. With CarHandling=0, nothing.
#include <cstdint>

namespace handling {

bool Install();

// How many cars this run's conversions have retuned.
int RetunedCount();
// A retuned car's handling.cfg identifier by handling id; nullptr if not one.
const char *NameOf(int id);

} // namespace handling
