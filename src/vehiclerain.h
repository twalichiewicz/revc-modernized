#pragma once
// Vehicles in the rain (#32), without the game. Soft ground (grass, mud,
// gravel, sand) soaks into mud as it rains: under each wheel on it the car has
// less grip and less drive and is held back by rolling resistance, according
// to its tyre/vehicle purpose (offroad, work, sports or ordinary street). The game's own wet roads already
// have less grip, so paving is left alone. Aircraft are thrown about: smooth
// turbulence (stronger in a gust), and a little lift lost.
#include "types.h"
#include "handlingmodel.h"

#include <cstdint>

namespace vehiclerain {

struct Ground {
	float grip, drive; // scales on the wheel's grip and drive
	float drag;        // m/s² the ground holds the car back by (all its wheels on it)
};
// Under a wheel: the ground's eSurfaceType, and how soaked it is (0..1).
Ground OnGround(uint8_t surface, float soak, handlingmodel::Kind kind = handlingmodel::Kind::Car);

struct Air {
	Vec3 shove; // m/s²
	Vec3 twist; // rad/s² about the aircraft's own axes (right, forward, up)
};
// An aircraft in the rain: the gust's strength (rainmodel::GustAt), game time,
// and a seed of its own (each aircraft its own bumps).
Air InTheAir(float rain, float gust, float seconds, uint32_t seed);

} // namespace vehiclerain
