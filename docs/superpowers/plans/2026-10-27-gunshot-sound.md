# Gunshot sound (Plan 7) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Gunshots that hit like Counter-Strike's while still being Vice City's: the game's own samples cleaned up
and given low end, played louder; his own gun with a close, centred body (a synthesized thump, crack and
mechanism) and a tail that knows indoors from out.

**Research:** `docs/research/2026-10-27-gunshot-audio.md` (the playback path, the bank, the hooks; cited).
**Spec:** the player's feedback (2026-10-27): "The sounds need an overhaul — they sound very tinny and not punchy".

## Global Constraints

- No Rockstar audio in the repo: the game's samples are processed in memory at runtime only; the mod's own
  layers are synthesized in code.
- Sample lengths and rates never change (the game's pitch divides by the stored rate).
- All audio work on the main thread (the game's sound effects run there).
- Don't commit; install only with the game closed.

## Review Focus

1. The bank processed exactly once per load, only the gunshot samples, never past a sample's end; a bank whose
   table doesn't look like macVC 1.0.5217's is left alone (logged).
2. Every shooter's gunshot changes (shared samples), the player's own gets the layers.
3. Loudness: a gunshot never clips the mix (AL gain ≤ 1), the SFX slider still scales it, 0 still mutes.
4. Bullet time (the game's time scale) and the pause menu: the layers don't play paused, and follow the slider.
5. A gun the mod doesn't know (a melee weapon, a thrown one): no layers.

---

### Task 1: The game's gunshots cleaned up and given low end (`sfxdsp`, `gunbank`)

- `sfxdsp::Punch(int16_t *pcm, size_t n, int rate)`: DC removed (one-pole high-pass, 20 Hz), a low shelf +5 dB
  below 150 Hz, the first 8 ms lifted +4 dB easing to none by 25 ms, gentle saturation, peak to −1 dBFS.
  Tests: `TestDcRemoved`, `TestLowShelfLiftsLows` (an 80 Hz tone gains 4–6 dB on a 3 kHz one), `TestAttackLifted`,
  `TestPeakUnderMinusOneDb`, `TestSilenceStaysSilent`.
- `gunbank`: when bank 0 is loaded (polled each frame; keyed to its address), checks the table (gunshot ids'
  stored rates as the research measured) and processes the gunshot samples (LEFT/RIGHT ids for every gun, and
  the tails 56, 73, 76, 87) in place. Logs what it did, or why not.

### Task 2: His own gun's body (`shotsynth`, `shotsound`)

- `shotsynth::Make(Kind, int rate) -> std::vector<int16_t>`: a decaying 45–90 Hz thump with a falling pitch, a
  5–15 ms noise crack, a short mechanism click; by kind (pistol, SMG, rifle, shotgun, heavy, sniper).
  Tests: `TestLengthAndPeak` (80–250 ms, peak ≤ −1 dBFS), `TestThumpIsLow` (most of the first 40 ms's energy
  under 150 Hz), `TestDecays` (the last 10 ms 40 dB down), `TestKindsDiffer`.
- `shotsound`: from `CWeapon::Fire`'s hook, the player's shot of a gun: the layer for its kind, centred
  (relative source at his head), CS-style pitch ±8%, gain from the SFX slider; a tail through the mod's own EFX
  EAX-reverb slot (a room indoors — `CCullZones::CurrentFlags_Camera & 0x200` — a city outside).

### Task 3: Louder, and the M4 its own pitch

- Hooks `cSampleManager::InitialiseChannel` (records each channel's sample), `SetChannelEmittingVolume`
  (gunshot samples doubled, into the 6 dB the game never uses), `SetChannelFrequency` (the M4's Ruger sample at
  1.15× instead of 1.66×). Pure rules in `gunbank` with tests: `TestGunshotIds`, `TestLouderCapped`,
  `TestM4Pitch`.

### Task 4: No headphone colouring

- The game process's OpenAL Soft reads a mod-written config (`ALSOFT_CONF`, set before the device opens):
  `stereo-encoding = basic` (no HRTF on hard-panned close shots).
