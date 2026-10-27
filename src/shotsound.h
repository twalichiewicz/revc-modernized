#pragma once
// His own gun's body under the game's shot (shotsynth's thump, crack and
// click): centred on him, as Counter-Strike plays your own gun (the game plays
// his shot exactly as a gunman's 20 m off), pitched a little differently each
// shot, at the effects volume, with a tail through the mod's own reverb: a room
// indoors, a city's outside. Every gun he fires (not the rocket or the
// flamethrower: their own sounds stand).
namespace shotsound {

// At load, before the game opens its sound device: the game's OpenAL Soft
// reads a config of the mod's (ALSOFT_CONF) with plain stereo, no HRTF on
// headphones (it colours the game's hard-panned close shots: Vice City was
// mixed for stereo). Left alone if ALSOFT_CONF is already set.
void BeforeAudio();
bool Install();
// From CWeapon::Fire's hook: a shot of his, of this weapon type.
void Shot(int weapon);

} // namespace shotsound
