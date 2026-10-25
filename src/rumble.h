#pragma once
// Controller rumble. The game asks for it (CPad::StartShake: crashes,
// explosions, punches, thunder) but only the PS2 and Windows builds drive a
// motor; the macOS port leaves it unplayed. Here each request plays through
// the GameController framework's haptics on the connected pad (DualSense,
// DualShock 4, Xbox).
#include <cstdint>

namespace rumble {

bool Install();
// Every frame from the pad hook: the car he's driving (null on foot) and its
// pedals (0-1), for the rumble of flooring it, braking hard or a burnout.
void Drive(const uint8_t *vehicle, float gas, float brake);
// From CPed::InflictDamage (player.cpp), when it hurt: his punches rumble.
void Damaged(const uint8_t *ped, const uint8_t *by, int32_t weapon, float damage);

} // namespace rumble
