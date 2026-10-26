#pragma once
// The army's Hunter joins at six stars (HeliGunners): it comes in from out
// of sight, hovers off where the police think he is (wanted::Target), turned
// to face him, and when it can see him fires its cannon close in and its
// rockets further off (the game fires them only for the player, so they're
// fired for it here). It's three times as tough as a car, and once the stars
// drop below six it flies off.
#include <cstdint>

namespace hunter {

bool Install();
void Update(uint32_t now); // from heli::Update, each frame after the world

} // namespace hunter
