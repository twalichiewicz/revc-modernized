#pragma once
#include "types.h"
#include <cstdint>

namespace shotimpact {
struct Event {
	Vec3 position{}, direction{};
};
// Wrap the current native bullet hook once, preserving helicopter processing.
bool Install();
// Only valid during this victim/weapon's native bullet-impact call. The caller
// remains responsible for requiring accepted damage before applying any force.
bool Current(uint8_t *victim, int weapon, Event &event);
} // namespace shotimpact
