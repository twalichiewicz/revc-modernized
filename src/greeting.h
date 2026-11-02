#pragma once
namespace greeting {
// Plays Tommy's first "Hello?" from the player's own drug_1 dialogue file.
// Returns false if the game's audio/assets are unavailable; never invents speech.
bool Play();
} // namespace greeting
