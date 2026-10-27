#pragma once
// The game's gunshot samples, punched up (sfxdsp) in the sample bank itself:
// bank 0 stays in memory and each play re-uploads from it, so every
// gunshot after is heard so, every shooter's. Done once each time the bank
// loads, after checking the table is the one expected (gunsfx); else left
// alone, logged. And the M4 played at its own pitch (gunsfx::Pitch), not
// the Ruger's sample 1.66 times fast.
namespace gunbank {

bool Install();
// Every frame (menus too): the bank, once it's loaded.
void Frame();
// The time scale the game plays its sounds at (bullet time slows them; 1 when it doesn't apply it).
float AudioTimeScale();

} // namespace gunbank
