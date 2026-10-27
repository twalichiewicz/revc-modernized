# Gunshot audio in macVC: playback path, sample data, hook points

Player feedback: first-person gunshots sound "very tinny and not punchy". This note traces how macVC 1.0.5217 plays a gunshot, what the game's samples contain, and where the mod can intervene. The bar is Counter-Strike 1.6 for feel and Vice City for fidelity.

Each claim carries one of these tags:

- **binary**: checked in the installed `macVC` or its `libopenal.1.dylib` with `nm -m`, `otool -L/-Iv/-l` or `llvm-objdump`. Addresses come from the installed, hook-patched binary. Its function bodies match vanilla except the first instruction of hooked functions, and none of the audio functions cited here are hooked.
- **source**: read in reVC or OpenAL Soft at the commits pinned below.
- **measured**: decoded from `GameData/audio` by a read-only scratch script. No game audio is in the repo.
- **inferred**: reasoned from the above but not tested in game.

## Sources

| Tag | Where (pinned) | Note |
|---|---|---|
| reVC | [mrxenginner/reVC @ d7dd135](https://github.com/mrxenginner/reVC/tree/d7dd1354d258762480ab7dec56fa0553fa0e951a) | `halpz/re3` returns HTTP 451 ([Take-Two DMCA, 2025-04-25](https://github.com/github/dmca/blob/master/2025/04/2025-04-25-take-two.md)). In this mirror, d7dd135 (2021-09-24, "add premake vs2022 cmd") is the last commit before the mirror's own work, which starts 2025-06-30, so it is the final upstream `miami` state. The constants spot-checked below match macVC |
| OpenAL Soft | [kcat/openal-soft @ 1.25.2 (b2c48f7)](https://github.com/kcat/openal-soft/tree/b2c48f7718ef3fcf67921a8b6534c4914e328970) | The version bundled with the app |
| cs16-client | [FWGS/cs16-client @ a238ef1](https://github.com/FWGS/cs16-client/tree/a238ef131803ed7592c4ba701499027c862889ee) | Same pin as `fps-feel-goldsrc.md` |

Links into reVC and OpenAL Soft point at the pinned commits above. Bare `file.cpp Lnn` links in the same paragraph are in the same repository.

---

## 1. How macVC plays sound

**Backend: reVC's OpenAL sample manager (`sampman_oal.cpp`) on a bundled OpenAL Soft 1.25.2, not Apple's OpenAL.framework.** (binary)
- `otool -L macVC` lists `@executable_path/../Frameworks/libopenal.1.dylib (current version 1.25.2)`, and nothing links `OpenAL.framework`. `Contents/Frameworks` holds `libopenal.1.dylib` (1.8 MB), `libmpg123`, `libglfw.3`, `libEGL`, `libGLESv2` and the mod. The dylib's strings include `1.1 ALSOFT 1.25.2` and its ALC extension list, which contains `ALC_EXT_EFX` and `ALC_SOFT_HRTF`. It exports `_alGenEffects`, `_alAuxiliaryEffectSloti` and `_alFilteri`.
- Apple's ASA extensions are irrelevant here. EFX is fully available.
- macVC imports 34 `al*`/`alc*` functions (`nm -m … (from libopenal)`), among them `alBufferData`, `alBufferiv`, `alGenSources`, `alSourcePlay`, `alcIsExtensionPresent` and `alGetProcAddress`. EFX entry points are not imported. They are fetched at runtime into pointers such as `__ZN10re3_openal9alEffectiE` and `__ZN10re3_openal9alFilteriE`.
- Present: `__GLOBAL__sub_I_sampman_oal.cpp`, the `CChannel::*` methods (reVC `audio/oal/channel.cpp`), `ALDeviceList`, and `EAX30_*_PRESETS`. Nothing from Miles is present.
- Key symbols: `cSampleManager::LoadSampleBank` `__ZN14cSampleManager14LoadSampleBankEh` @0x100044720, `LoadPedComment` `__ZN14cSampleManager14LoadPedCommentEj`, `InitialiseChannel` `__ZN14cSampleManager17InitialiseChannelEjjh` @0x1000453a8, `StartChannel` `__ZN14cSampleManager12StartChannelEj`, `CChannel::Start` `__ZN8CChannel5StartEv` @0x10003b5c0, `CChannel::SetSampleData` `__ZN8CChannel13SetSampleDataEPvmj` @0x10003b868.
- Exported globals: `_SampleManager`, `_AudioManager` (0x10037bdd0), `_nSampleBankMemoryStartAddress`, `_BankStartOffset`, `_gBankLoaded`, `_aChannel`, `_alSources` (44 IDs, so NUM_CHANNELS = 44), `__usingEFX` and `__fEffectsLevel`.

**The context.** The attribute array passed to `alcCreateContext`, read from the `__TEXT,__const` bytes at 0x100321540, is `{ALC_FREQUENCY, 32000, ALC_MONO_SOURCES, 49, ALC_STEREO_SOURCES, 1, 0}` (binary). This is the source's `MAX_FREQ` = `DIGITALRATE` 32000 ([reVC `audio/sampman_oal.cpp` L844–848](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/sampman_oal.cpp#L844-L848), [sampman.h L136](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/sampman.h#L136)). OpenAL Soft raises the total to at least 256 sources ([OpenAL Soft `alc/alc.cpp` L1625–1639](https://github.com/kcat/openal-soft/blob/b2c48f7718ef3fcf67921a8b6534c4914e328970/alc/alc.cpp#L1625-L1639)), so about 200 are free for the mod. **Inferred:** the mixer runs at 32 kHz, so nothing above 16 kHz reaches the output.

**The listener.** The listener sits at the origin, facing +Y with +Z up ([L866–870](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/sampman_oal.cpp#L866-L870); binary 0x1000430fc–0x100043150). The distance model is `AL_INVERSE_DISTANCE_CLAMPED` (0xD002 at 0x100043154). Every channel's source is `AL_SOURCE_RELATIVE` ([channel.cpp L88](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/oal/channel.cpp#L88)). Positions are in camera space: `TranslateEntity` = `MultiplyInverse(TheCamera.GetMatrix(), pos)` ([AudioManager.cpp L625–628](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioManager.cpp#L625-L628)). X is negated on the way in ([sampman_oal.cpp L1614](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/sampman_oal.cpp#L1614); binary `fneg` at 0x1000457e4).

**Threads.** All sound-effect work runs on the main thread. `Idle()` runs `CGame::Process()` and then `DMAudio.Service()` in the same frame ([reVC `core/main.cpp` L1545–1550](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/core/main.cpp#L1545-L1550)). A second thread exists only for music streams (`MULTITHREADED_AUDIO` "for streams", [core/config.h L455](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/core/config.h#L455); binary `_gAudioThread`, `_gAudioThreadQueueMutex`). OpenAL Soft locks source and buffer calls itself ([OpenAL Soft `al/source.cpp` L2738](https://github.com/kcat/openal-soft/blob/b2c48f7718ef3fcf67921a8b6534c4914e328970/al/source.cpp#L2738), [OpenAL Soft `al/buffer.cpp` L749](https://github.com/kcat/openal-soft/blob/b2c48f7718ef3fcf67921a8b6534c4914e328970/al/buffer.cpp#L749)).

**Reverb already in use.** This Mac's `~/Library/Application Support/macVC/macVC.ini` has `Provider=0` and `DynamicAcoustics=1`. Provider 0 is named `"OPENAL SOFT"` ([L182](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/sampman_oal.cpp#L182)), which selects the EFX path. That path puts the EAX **Cave** preset in the game's single auxiliary slot ([L276–290](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/sampman_oal.cpp#L276-L290)). Every 16 frames, `UpdateReverb` sets the wet send to `0.8 × ratio`, capped at 0.48. The ratio comes from eight camera raycasts ([L1438–1505](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/sampman_oal.cpp#L1438-L1505)). The binary has the strings `OPENAL SOFT`, `OPENAL SOFT EAX3` and `[DEV]: EFX`. **Inferred:** a stored 0 maps to `providers[0]`.

**HRTF.** OpenAL Soft turns HRTF on by itself when CoreAudio reports the output as headphones ([OpenAL Soft `alc/backends/coreaudio.cpp` L494–506](https://github.com/kcat/openal-soft/blob/b2c48f7718ef3fcf67921a8b6534c4914e328970/alc/backends/coreaudio.cpp#L494-L506), [OpenAL Soft `alc/panning.cpp` L1307–1311](https://github.com/kcat/openal-soft/blob/b2c48f7718ef3fcf67921a8b6534c4914e328970/alc/panning.cpp#L1307-L1311)). The dylib contains `Built-In HRTF`. This Mac has no `~/.alsoftrc` or `~/.config/alsoft.conf` to override that. **Inferred:** on wired headphones, close gunshots, which are panned hard left and right (§4), are run through 90° HRTF filters. That coloration may be part of "tinny". Testing it is a one-line config: `stereo-encoding = basic` in `~/.alsoftrc` ([OpenAL Soft `alc/alconfig.cpp` L474–492](https://github.com/kcat/openal-soft/blob/b2c48f7718ef3fcf67921a8b6534c4914e328970/alc/alconfig.cpp#L474-L492), [OpenAL Soft `alsoftrc.sample` L116](https://github.com/kcat/openal-soft/blob/b2c48f7718ef3fcf67921a8b6534c4914e328970/alsoftrc.sample#L116)).

**Gain staging.** A channel's gain is `fade × volume × sfxVolume >> 14`, divided by 127 (binary `SetChannelEmittingVolume` 0x100045758–0x10004576c, `CChannel::SetVolume` divides by 127.0f; source [L1606](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/sampman_oal.cpp#L1606), [channel.cpp L176–179](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/oal/channel.cpp#L176-L179)). The SFX slider runs 0–65 ([reVC `core/Frontend.cpp` L749](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/core/Frontend.cpp#L749)). From that, **derived:**
- A shot at the maximum emitting volume of 127, with the slider at 65, gets AL gain 63/127 ≈ **−6 dB**.
- At this Mac's `SfxVolume=37` it gets 36/127 ≈ **−11 dB**.
- Music at `MusicVolume=64` uses the same formula ([L1977–1984](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/sampman_oal.cpp#L1977-L1984)), so it sits ≈ 4.8 dB hotter than effects from the settings alone.
- `CChannel::SetDistances` caps `AL_MAX_GAIN` at 1.0 ([channel.cpp L219](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/oal/channel.cpp#L219)). The game never uses the top 6 dB of the gain range.

## 2. The path of one gunshot

1. `CWeapon::Fire` calls `DMAudio.PlayOneShot(shooterPed->m_audioEntityId, SOUND_WEAPON_SHOT_FIRED, 0.0f)` ([reVC `weapons/Weapon.cpp` L340](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/weapons/Weapon.cpp#L340)). In the binary this is `mov w2, #0x37` then `bl cDMAudio::PlayOneShot` at 0x100286038, so **SOUND_WEAPON_SHOT_FIRED = 55** ([audio/soundlist.h L61](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/soundlist.h#L61)). Vehicle guns raise the same event on the vehicle's entity, and it is handled in `ProcessVehicleOneShots` ([AudioLogic.cpp L3445](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioLogic.cpp#L3445)).
2. `cDMAudio::PlayOneShot` (`__ZN8cDMAudio11PlayOneShotEitf`) is a three-instruction thunk that branches to `cAudioManager::PlayOneShot(int32 index, uint16 sound, float vol)` (`__ZN13cAudioManager11PlayOneShotEitf` @0x1000311a8; binary 0x100033f88). That function only queues the event on the audio entity ([AudioManager.cpp L251–293](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioManager.cpp#L251-L293)). In the binary the entity array starts at `AudioManager+0x3010` with a stride of 0x30, and `m_pEntity` sits at `+0x3018 + index*0x30` (`ProcessPhysical` 0x10000e4d0).
3. In the same frame's `DMAudio.Service`, `ServiceSoundEffects` runs `InterrogateAudioEntities`, then `ProcessEntity`, `ProcessPed` and `ProcessPedOneShots` ([AudioManager.cpp L537–600](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioManager.cpp#L537-L600), [AudioLogic.cpp L4201–4216](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioLogic.cpp#L4201-L4216)). For `SOUND_WEAPON_SHOT_FIRED`, it picks the sample, frequency and volume from the ped's current weapon ([L4549–4832](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioLogic.cpp#L4549-L4832)).
4. `ProcessActiveQueues` calls `InitialiseChannel(k, sfx, bank)`, `SetChannelFrequency`, `SetChannelEmittingVolume`, `SetChannelReverbFlag`, `SetChannel3DPosition`, `SetChannel3DDistances(k, max, 0.25 × max)` and `StartChannel(k)`, in that order ([AudioManager.cpp L1273–1330](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioManager.cpp#L1273-L1330)).
5. `CChannel::Start` uploads the sample on every play: `alBufferData(…, AL_FORMAT_MONO16, Data, DataSize, Frequency)`, then `alSourcei(AL_BUFFER)` and `alSourcePlay` ([channel.cpp L114–140](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/oal/channel.cpp#L114-L140)). Binary: `bl _alBufferData` at 0x10003b6bc (the stereo branch) and 0x10003b70c (mono), and a tail `b _alSourcePlay` at 0x10003b74c.
   - `Data` points straight into the resident sample bank. `InitialiseChannel` computes it as `nSampleBankMemoryStartAddress[bank] + m_aSamples[sfx].nOffset − m_aSamples[BankStartOffset[bank]].nOffset` ([L1527–1586](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/sampman_oal.cpp#L1527-L1586)).
   - In the binary, `m_aSamples` is at `SampleManager+0x258`, 20 bytes per entry, and it passes `nSize` and `nFrequency` to `CChannel::SetSampleData` (0x1000455fc–0x100045614).
   - Pitch is `freq / nFrequency` ([channel.cpp L188–191](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/oal/channel.cpp#L188-L191)).

## 3. What each weapon plays

Each LEFT sample's RIGHT partner is the next id ([AudioSamples.h L63–104](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioSamples.h#L63-L104)).
- **source**: the played rate, volume and echoes ([AudioLogic.cpp L4554–4832](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioLogic.cpp#L4554-L4832)), and the volume constants ([L589–604](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioLogic.cpp#L589-L604)).
- **measured**: the stored rate, the length, and the last column. That column is for the LEFT sample: crest factor (peak/RMS), DC offset as % of full scale, and the count of samples at ±32700 or beyond.
- "±3%" is `RandomDisplacement(freq >> 5)` ([AudioManager.cpp L673–692](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioManager.cpp#L673-L692)).
- Every firearm has a max distance of 120 m and priority 3 (1 for the rocket). The volume scale is 0–127.

| Weapon | LEFT id | Stored rate | Played rate | Length played | Vol | Echoes | Crest / DC / clipped |
|---|---|---|---|---|---|---|---|
| Colt 45 | 50 | 22050 | ±3% | 915 ms | 90–99 | yes | 14.4 dB / −0.1% / 0 |
| Python | 88 | 17600 | ±3% | 1238 ms | 127 | yes | 14.9 / +0.7% / 19 |
| Uzi, minigun | 54 | 37000 | ±3% | 133 ms | 70–84 | no | 11.1 / +0.6% / 0 |
| MP5 | 90 | 30200 | ±3% | 132 ms | 70–84 | no | 10.2 / +0.8% / 187 |
| TEC-9 | 71 | 17000 | 17000 ±500 | 160 ms | 70–84 | no | 9.7 / **+8.6%** / 17 |
| Silenced Ingram | 71 | 17000 | **34000** ±1000 (×2.0) | ≈80 ms | 70–84 | no | (TEC sample) |
| Ruger | 74 | 26000 | ±3% | 200 ms | 90–104 | no | 8.3 / **−8.0%** / 27 |
| **M4** | 74 | 26000 | **43150** ±1000 (×1.66) | ≈120 ms | 90–104 | no | (Ruger sample) |
| M60, heli cannon | 68 | 32000 | ±3% | 121 ms | 127 | no | 8.5 / −1.9% / 1 |
| Shotgun, stubby | 66 | 22050 | ±3% | 1732 ms | 100–109 | yes | 15.5 / 0.0% / 11 |
| SPAS-12 | 85 | 28000 | ±3% | 361 ms | 100–109 | yes | 9.5 / −1.8% / 0 |
| Sniper / laser sniper | 57 | 24000 | 25472 / 20182 | 233 / 295 ms | 110–119 | yes | 8.1 / −5.5% / 0 |
| Rocket launcher | 59 | 23000 | ±3% | 786 ms | 80–99 | yes | 10.9 / – / – |

What the table says about "tinny":
- **Derived:** the **M4 is the Ruger sample played 1.66× fast.** That is +8.8 semitones and about 120 ms, so the whole spectrum moves up. The binary has the same immediates: `#0xa88e` (43150) at 0x100019554, 34000 at 0x100019228, and 25472/20182 at 0x100019060.
- **Measured:** every gunshot peaks at about 0 dBFS. The automatic weapons have crest factors of only 8–11 dB. Several samples carry large DC offsets and some clipping. **Inferred:** they are already peak-limited, so more compression will not add punch; transient shaping and low end will.
- I did not get reliable spectra, because the DC offsets contaminate the low bands, so beyond the M4 this note does not claim from data *why* the samples sound thin.

**Tails.** `SOUND_WEAPON_AK47_BULLET_ECHO` plays when an attack animation ends ([reVC `peds/PedFight.cpp` L1041–1043](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/peds/PedFight.cpp#L1041-L1043)). It is handled at [AudioLogic.cpp L4467–4530](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioLogic.cpp#L4467-L4530): a mono 3D sample at volume 80–89 with echoes.
- Uzi and MP5 get UZI_END (56).
- TEC-9 and Ingram get TEC_TAIL (73).
- M4, Ruger and both snipers get RUGER_TAIL (76). The M4 plays it at 15600 Hz against a stored rate of 9400.
- SPAS-12 gets SPAS12_TAIL (87).
- The M60 case lacks a `break`, so it falls through to UZI_END (source only, not checked in the binary).

In the rifle first-person modes (`PlayerControlM16`, `PlayerControlSniper`), the tail is sent with `PlayFrontEndSound` ([reVC `peds/PlayerPed.cpp` L706, L845](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/peds/PlayerPed.cpp#L706)). `ProcessFrontEnd` has no case for it and falls to `default: continue` ([AudioLogic.cpp L9040](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioLogic.cpp#L9040)). **Inferred:** first-person rifle fire gets no tail.

**CS 1.6 for contrast:** every weapon sound plays at `VOL_NORM`, `ATTN_NORM`, with pitch `94 + RandomLong(0, 15)`, about ±8% ([cs16-client `cl_dll/include/events.h` L59](https://github.com/FWGS/cs16-client/blob/a238ef131803ed7592c4ba701499027c862889ee/cl_dll/include/events.h#L59)). The AK alternates between two samples ([`event_ak47.cpp` L74](https://github.com/FWGS/cs16-client/blob/a238ef131803ed7592c4ba701499027c862889ee/cl_dll/events/event_ak47.cpp#L74)).

## 4. The player's own shots, and distance

The `SOUND_WEAPON_SHOT_FIRED` code has **no player branch**. The player's shot gets the same sample and volume range as any ped's (source, [L4549–4832](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioLogic.cpp#L4549-L4832)). What changes with the shooter is distance. That is measured from the **camera**, with height differences scaled by 0.2 ([AudioLogic.cpp L262–266](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioLogic.cpp#L262-L266)):

- **Under 0.2 × 120 = 24 m**, which includes your own gun in first or third person, the shot plays "stereo". The LEFT sample is queued at pan 0 and the RIGHT sample (id + 1) at pan 127 ([L5192–5212](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioLogic.cpp#L5192-L5212); binary 0x100018dac–0x100018df4, threshold 0x3E4CCCCD = 0.2).
  - Under OpenAL each half becomes a mono source at x = ±1000 m, with max distance 100000 and reference 25000, so there is no attenuation ([AudioManager.cpp L1308–1328](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioManager.cpp#L1308-L1328)).
  - **Derived:** your own shot sounds exactly like a gunman's 20 m away: same level, same hard-panned width, no proximity cue.
- **From 24 m to 120 m**, only the LEFT sample plays, as a 3D source at the shooter. OpenAL attenuates it as inverse distance with reference 30 m (0.25 × 120) and max 120 m. **Derived:** gain = 30/d, so −6 dB at 60 m and −12 dB at 120 m. The game drops the sound at 120 m or more ([L5192](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioLogic.cpp#L5192)).
  - Under OpenAL, the game's own quadratic `ComputeVolume` only ranks and culls samples. The emitting volume is what reaches the channel ([AudioManager.cpp L1115, L607–622](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioManager.cpp#L1115)).
- **There is no distant variant and no distance filtering.** `AudioSamples.h` has no far or distant gun samples; the only `_FAR` sample is `SFX_CAR_HELI_FAR`. `CChannel` sets no low-pass and no air absorption ([channel.cpp L207–222](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/oal/channel.cpp#L207-L222)).
- **Echoes are skipped for 2D samples and indoors.** `AddSampleToRequestedQueue` drops reflections when the sample is 2D or `CCullZones::InRoomForAudio()` ([AudioManager.cpp L724](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioManager.cpp#L724)), so **your own shot never gets the discrete echoes**. Those are copies placed at the raycast hits and delayed by distance ([L772–840](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioManager.cpp#L772-L840)). Your shot only gets the Cave reverb send from §1, which every shot gets (`SET_SOUND_REVERB(TRUE)`, [L5203](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioLogic.cpp#L5203)).

## 5. The sample banks

- `GameData/audio/sfx.SDT` is 198,820 bytes, exactly 9,941 entries of 20 bytes. Each entry is `tSample {u32 offset, u32 size, u32 rate, u32 loopStart, i32 loopEnd}` ([sampman.h L9–19](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/sampman.h#L9-L19), [sampman_oal.cpp L2027–2074](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/sampman_oal.cpp#L2027-L2074)) (measured).
- `sfx.RAW` is 340,245,502 bytes of headerless **mono signed 16-bit little-endian PCM**. The last SDT entry ends exactly at that size. Each sample has its own rate: 12000 Hz for 8,048 entries, 16000 for 1,267, 22050 for 185, and so on (measured). The format matches `AL_FORMAT_MONO16` in `CChannel::Start`.
- **Bank 0** is entries 0–523, which includes every gunshot. It is 15,044,960 bytes, `malloc`ed and read once in `cSampleManager::Initialise` ([L961, L985](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/sampman_oal.cpp#L961-L985)). It stays resident until `Terminate` frees it ([L1133–1137](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/sampman_oal.cpp#L1133-L1137)). In the binary, `InitialiseChannel` tests `sfx <= 0x20B` (0x1000453f8).
- `LoadPedComment` only fills the speech bank, in 79,000-byte slots. **Gunshots never pass through it.**

## 6. Hook points, by feasibility

**(a) Process the gunshot PCM in bank 0, in place, once. Needs no new hook; easiest.**
- Bind `nSampleBankMemoryStartAddress`, `gBankLoaded` and `SampleManager` with `dlsym(RTLD_MAIN_ONLY, …)`, as `alert.cpp` already does for `SampleManager` ([src/alert.cpp L133](../../src/alert.cpp#L133)).
- When `gBankLoaded[0] == 1`, sample *n* starts at `base + m_aSamples[n].nOffset`. Entry 0's offset is 0 and `BankStartOffset[0] = 0` ([AudioLogic.cpp L55](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioLogic.cpp#L55)).
- Because `CChannel::Start` re-uploads from that memory on every play, an edit there is heard from the next shot on.
- Rules:
  - Keep each sample's **length and rate**: pitch math divides by the stored rate.
  - The samples already peak at 0 dBFS, so a low-shelf boost needs an overall trim inside int16.
  - Pairs are shared: M4 with Ruger, Ingram with TEC-9, minigun with Uzi, M60 with the heli guns, and laser sniper with sniper. Every shooter changes, not just the player.
- To re-apply after a re-initialise, hook `__ZN14cSampleManager14LoadSampleBankEh` (`bool8 LoadSampleBank(uint8 nBank)`), call the original, and process when `nBank == 0`.

**(a′) Swap in longer, mod-built buffers.**
- Hook `__ZN8CChannel13SetSampleDataEPvmj` (`void SetSampleData(void *data, size_t size, uint32 freq)`). It is three instructions; the first is `stp x1, x2, [x0, #0x10]`, which is position-independent and so safe to displace.
- When `data` is a gunshot's bank address, pass the mod's own buffer and size instead, at the **same rate**. This allows a longer tail.
- Keep the buffer ≤ 79,000 bytes. The 2D stereo conversion writes `size × 2` into `tempStereoBuffer`, which holds `max(79000, playerTalk) × 2` bytes ([channel.cpp L29, L119–132](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/oal/channel.cpp#L29)). Gunshot channels are 3D, because `SetPosition` clears `bForce2D` before `Start` ([L211](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/oal/channel.cpp#L211)), but a bigger buffer would overflow if one ever ran 2D.

**(b) Extra layers on the mod's own OpenAL sources.** This gives the most feel per unit of effort: it can be per weapon, per shooter, and aware of indoors.
- **Your own shots:** the mod already hooks `_ZN7CWeapon4FireEP7CEntityP7CVector` (`bool Fire(CEntity *shooter, CVector *source)`; [tools/patch_macvc.py L128](../../tools/patch_macvc.py#L128), [src/camera.cpp L456–480](../../src/camera.cpp#L456-L480)).
  - When `orig_Fire` returns true and the shooter is `FindPlayerPed()`, play the layers. `WeaponType(weapon)` gives the weapon.
  - The game's sample starts later in the same frame, in `DMAudio.Service`. **Inferred:** the two land within one frame of each other.
  - Copy `alert.cpp`'s setup: a source with `AL_SOURCE_RELATIVE` at (0,0,0) ([src/alert.cpp L92–96](../../src/alert.cpp#L92-L96)).
- **Every shooter:** add `_ZN13cAudioManager11PlayOneShotEitf` (`void PlayOneShot(int32 index, uint16 sound, float vol)`, `this` = `&AudioManager`).
  - Filter on `sound == 55`.
  - The entity is `*(CEntity **)(AudioManager + 0x3018 + index*0x30)`; check `m_nType` at `+0x3010`.
  - Position the layer as the game does: camera-inverse × world position, then negate x. Use reference 30 m and max 120 m.
- **Sample-exact sync, optional:** hook `__ZN14cSampleManager17InitialiseChannelEjjh` to record `nSfx` for each channel, and `__ZN14cSampleManager12StartChannelEj` to fire on a LEFT id. The 2D pair calls it twice. Read the position from `__ZN14cSampleManager20SetChannel3DPositionEjfff`.
- **Indoors or outdoors:**
  - `__ZN10CCullZones19CurrentFlags_CameraE & 0x200` (ATTRZONE_ROOMFORAUDIO, [reVC `core/ZoneCull.h` L14, L62](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/core/ZoneCull.h#L14)).
  - The game's eight reflection distances (N, S, W, E, then four ceiling), refreshed every 8 frames ([AudioManager.cpp L843+](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/AudioManager.cpp#L843)). **Derived:** they are at `AudioManager+0x633C`, because `UpdateReverb`'s first read at +0x634C is index 4 (0x100045084).
- **Cheap level lever:** hook `__ZN14cSampleManager24SetChannelEmittingVolumeEjj` (`void (uint32 nChannel, uint32 nVolume)`) and raise only gunshot channels into the unused top 6 dB of the gain range (§1).

**(c) OpenAL EFX. Available, but limited to the mod's own sources.**
- The mod can create its own auxiliary slots (up to 64 by default, [OpenAL Soft `alsoftrc.sample` L225](https://github.com/kcat/openal-soft/blob/b2c48f7718ef3fcf67921a8b6534c4914e328970/alsoftrc.sample#L225)) with `AL_EFFECT_EAXREVERB` for indoor and outdoor tails. Each source has 2 sends by default ([OpenAL Soft `al/source.h` L47](https://github.com/kcat/openal-soft/blob/b2c48f7718ef3fcf67921a8b6534c4914e328970/al/source.h#L47)).
- `AL_FILTER_LOWPASS` on the mod's own layers can model distance and occlusion.
- EFX cannot boost the low end of the game's sources:
  - Direct filters only cut.
  - The equalizer is an auxiliary effect, so it runs in parallel, not as an insert.
- Leave the game's `ALEffectSlot` alone. `SetChannelReverbFlag` re-binds it on every start ([sampman_oal.cpp L1508–1523](https://github.com/mrxenginner/reVC/blob/d7dd1354d258762480ab7dec56fa0553fa0e951a/src/audio/sampman_oal.cpp#L1508-L1523)).

## 7. Recommended approach

The hooks and symbols below are the verified ones from §6. The DSP numbers are **judgment**, starting points to tune by ear.

1. **Diagnose first, with no code.** A/B with HRTF off (`stereo-encoding = basic` in `~/.alsoftrc`) and with the SFX slider at 65. If HRTF or the −11 dB setting explains much of it, everything after this gets easier.
2. **Clean up bank 0 in place, (a).** Do it once, after `gBankLoaded[0]` is set, on the main thread. For the gunshot LEFT/RIGHT ids:
   - Remove DC (a high-pass at about 20 Hz).
   - Add a low shelf of +4–6 dB below about 150 Hz.
   - Add transient emphasis on the first 5–10 ms.
   - Add gentle saturation for density.
   - Trim the result to a −1 dBFS peak.
   - Also process the tail samples (56, 73, 76, 87).
3. **Restore loudness** for gunshot channels through the `SetChannelEmittingVolume` hook, (b). This uses the headroom the game leaves unused.
4. **Layer the player's own gun** from the existing `CWeapon::Fire` hook, (b).
   - Use a synthesized, per-weapon low "thump": a decaying 45–90 Hz sine with a noise click, 60–120 ms.
   - Add a short mechanical layer.
   - Add a tail through the mod's own EAX-reverb slot, chosen by `ROOMFORAUDIO` and the reflection distances, (c).
   - Give it CS-style pitch jitter (±8%) and centre it, so your own gun reads as close and in front. The game cannot do that (§4).
5. **Optional, judgment:** give the M4 its own pitch. Hook `SetChannelFrequency` (`__ZN14cSampleManager19SetChannelFrequencyEjj`), together with the per-channel `nSfx` from `InitialiseChannel`, and scale down the frequency when the sample is RUGER and `freq > 40000`. Add NPC layers through the `PlayOneShot` hook, and a distance low-pass on them.

## 8. Risks

- **Threads:** none beyond what the mod already has. Game sound effects, `CWeapon::Fire` and the mod's frame code all run on the main thread (§1). Never touch bank memory from another thread. OpenAL copies data at `alBufferData` time, so edits between frames are safe (inferred from OpenAL semantics).
- **Formats:** mono s16 at each sample's own rate. Changing the rate breaks pitch (`freq / nFrequency`). Changing the size in place is impossible. Swapped buffers carry the 79,000-byte 2D limit.
- **Collateral:** samples shared across weapons and every NPC's shots change together.
- **CPU:** small. The one-time DSP covers about 0.8 MB of PCM. The game already re-uploads every sample on every play. Each shot adds a few sources, with about 200 spare (inferred: negligible).
- **Re-initialise:** `Terminate` frees bank 0. Re-apply from a `LoadSampleBank` hook, or keep a processed-flag keyed to the bank's base address.
- **New hooks:** each needs an entry in `DEFAULT_HOOKS` and a re-patch ([tools/patch_macvc.py L58](../../tools/patch_macvc.py#L58)). Every target named here starts with a non-PC-relative instruction or an `adrp`, which the patcher relocates (binary).
- **Rockstar data:** process the game's own files only at runtime. Commit no decoded or derived audio. The measurements above come from a scratch script that reads and prints only.

**Not verified in game:** how much HRTF actually contributes; whether OpenAL Soft honours 32 kHz on CoreAudio; the dropped first-person tail; the M60 tail fall-through in the binary; and how `Provider=0` maps to a device.
