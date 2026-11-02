#pragma once
#include <cstdint>
namespace airworld {
// High owned-aircraft flight: native BIG terrain coverage, bounded streaming,
// and an ocean/haze horizon. Ordinary ground and scripted views stay native.
bool Install();
// Current player/camera ownership, revalidated at the query; also used by
// native structural-collision consumers. This does not cache render state.
bool OwnsFlightView(const uint8_t *vehicle);
}
