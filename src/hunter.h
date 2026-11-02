#pragma once
// The army's Hunter joins at six stars (HeliGunners): it comes in from out
// of sight, hovers off where the police think he is (wanted::Target), turned
// to face him, with an animated Army soldier in its single native seat. While
// that soldier is alive and aboard it fires its owned native cannon close in and its
// rockets further off (the game fires them only for the player, so they're
// fired for it here). It's three times as tough as a car, and once the stars
// drop below six it flies off.
#include <cstdint>

namespace hunter {

bool Install();
void Update(uint32_t now); // from heli::Update, each frame after the world

} // namespace hunter
