#pragma once
#include <cstdint>

namespace planeflight {
bool Install();
// Called by the shared native CVehicle::FlyingControl hook. True owns this
// fixed-wing force evaluation; false leaves the original controller in charge.
bool Apply(uint8_t *vehicle, int flightModel);
// Actual spooled throttle times current engine/health power,0..1. Zero for
// foreign, stale/recycled, stopped or wrecked aircraft; never raw pedal input.
float EnginePower(const uint8_t *vehicle);
// Read-only identity of the separately allocated, generation-valid native Dodo.
bool Owns(const uint8_t *vehicle);
// A separately managed native CPlane roster supplies its generation-checked
// identity; its own hooks retain control, entry and lifetime responsibility.
void SetAirportOwner(bool (*query)(const uint8_t *));
// True only while that airport aircraft's native rolling-wheel controller
// owns ground braking/steering. Other plane and airborne controls are retained.
void SetAirportWheelControl(bool (*query)(const uint8_t *));
// Bounded airport roster and native boarding, once per game frame.
void Frame(uint32_t now);
}
