#pragma once

namespace ballistics {
// Trace ordinary player bullets throughout the currently visible world.
// Native weapon damage, accuracy, ammo and NPC engagement ranges stay native.
bool Install();
float VisibleRange(float nativeRange);
} // namespace ballistics
