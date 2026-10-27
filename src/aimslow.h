#pragma once
// Aim slowdown in first person: the right stick turns a little slower while
// the crosshair is over someone fighting him (fpgait::Enemy: out to kill him,
// his gun on him, or a cop while he's wanted), in sight, within 60 m. Never a pull.
// The amounts are fplook.cpp's; this finds the targets.
namespace aimslow {

bool Install();
// The strongest slowdown over the targets near the crosshair this frame (a
// turn-rate multiplier, 1: none); `push` is the right stick's (0..1).
float Multiplier(bool aiming, float push);

} // namespace aimslow
