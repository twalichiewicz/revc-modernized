#include "shotsound.h"
#include "config.h"
#include "gunbank.h"
#include "log.h"
#include "shotsynth.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <vector>

namespace shotsound {
namespace {

// OpenAL and its effects extension, from the game's OpenAL Soft.
using ALuint = unsigned int;
void *(*alcGetCurrentContext)();
void (*alGenSources)(int, ALuint *);
void (*alGenBuffers)(int, ALuint *);
void (*alBufferData)(ALuint, int, const void *, int, int);
void (*alSourcei)(ALuint, int, int);
void (*alSourcef)(ALuint, int, float);
void (*alSource3f)(ALuint, int, float, float, float);
void (*alSource3i)(ALuint, int, int, int, int);
void (*alSourcePlay)(ALuint);
void (*alSourceStop)(ALuint);
void (*alGenEffects)(int, ALuint *);
void (*alEffecti)(ALuint, int, int);
void (*alEffectf)(ALuint, int, float);
void (*alGenAuxiliaryEffectSlots)(int, ALuint *);
void (*alAuxiliaryEffectSloti)(ALuint, int, int);
void (*alAuxiliaryEffectSlotf)(ALuint, int, float);
constexpr int kAlFormatMono16 = 0x1101, kAlBuffer = 0x1009, kAlSourceRelative = 0x202, kAlPosition = 0x1004,
              kAlGain = 0x100A, kAlPitch = 0x1003, kAlAuxiliarySend = 0x20006, kAlFilterNull = 0;
constexpr int kAlEffectType = 0x8001, kAlEffectReverb = 0x0001, kAlSlotEffect = 0x0001, kAlSlotGain = 0x0002;
// AL_REVERB_*: density, diffusion, gain, gain HF, decay time, decay HF ratio,
// reflections gain and delay, late reverb gain and delay.
constexpr int kReverbParams[] = {0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xA};
constexpr float kRoom[] = {1.0f, 1.0f, 0.5f, 0.6f, 0.9f, 0.7f, 0.5f, 0.007f, 1.2f, 0.011f};
constexpr float kCity[] = {0.5f, 0.6f, 0.32f, 0.4f, 1.6f, 0.5f, 0.15f, 0.03f, 0.8f, 0.06f};

constexpr int kRate = 32000;       // the game's mixing rate
constexpr int kVariants = 4;       // shots of each kind, taken in turn
constexpr int kVoices = 8;         // sources, taken in turn (automatic fire overlaps)
constexpr float kGain = 0.8f;      // at full effects volume
constexpr float kTailGain = 0.45f; // the reverb's share
constexpr float kPitchLow = 0.94f, kPitchHigh = 1.09f; // Counter-Strike's 94..109
constexpr uint32_t kRoomForAudio = 0x200;              // CCullZones' ATTRZONE_ROOMFORAUDIO

const uint8_t *gSampleManager;     // its effects and master volume (alert.cpp reads them so)
const int32_t *gZoneFlags;         // CCullZones::CurrentFlags_Camera
bool gReady, gMade, gIndoors;
ALuint gBuffers[7][kVariants], gSources[kVoices], gEffect, gSlot;
int gNextVoice, gNextVariant[7];

float Volume()
{
	if (!gSampleManager)
		return 1.0f;
	return gSampleManager[0] / 127.0f * std::min(gSampleManager[3], uint8_t(127)) / 127.0f;
}

void SetReverb(const float *preset)
{
	alEffecti(gEffect, kAlEffectType, kAlEffectReverb);
	for (size_t i = 0; i < std::size(kReverbParams); i++)
		alEffectf(gEffect, kReverbParams[i], preset[i]);
	alAuxiliaryEffectSloti(gSlot, kAlSlotEffect, int(gEffect)); // (re-attached: a slot copies its effect)
}

// The buffers, sources and reverb, once the game's OpenAL is up.
bool Make()
{
	if (gMade)
		return true;
	if (!alcGetCurrentContext())
		return false;
	for (int k = 1; k < 7; k++) {
		alGenBuffers(kVariants, gBuffers[k]);
		for (int v = 0; v < kVariants; v++) {
			const std::vector<int16_t> pcm = shotsynth::Make(shotsynth::Kind(k), kRate, uint32_t(k * 16 + v));
			alBufferData(gBuffers[k][v], kAlFormatMono16, pcm.data(), int(pcm.size() * 2), kRate);
		}
	}
	alGenSources(kVoices, gSources);
	for (ALuint s : gSources) {
		alSourcei(s, kAlSourceRelative, 1); // on him
		alSource3f(s, kAlPosition, 0.0f, 0.0f, 0.0f);
	}
	if (alGenEffects && alGenAuxiliaryEffectSlots) {
		alGenEffects(1, &gEffect);
		alGenAuxiliaryEffectSlots(1, &gSlot);
		alAuxiliaryEffectSlotf(gSlot, kAlSlotGain, kTailGain);
		SetReverb(gIndoors ? kRoom : kCity);
	}
	gMade = true;
	Log("shotsound: his guns' bodies made (%d kinds × %d), %s", 6, kVariants, gSlot ? "with a reverb tail" : "no reverb (no EFX)");
	return true;
}

} // namespace

void BeforeAudio()
{
	if (getenv("ALSOFT_CONF"))
		return;
	const std::string path = SupportDir() + "/vcmodernhud-openal.conf";
	if (FILE *f = fopen(path.c_str(), "w")) {
		fputs("# Written by VC Modern HUD at each start: the game's OpenAL Soft in plain stereo (no headphone HRTF),\n"
		      "# with its limiter (the game's shots, his gun's body and their tails can sum past full scale).\n"
		      "[general]\nstereo-encoding = basic\noutput-limiter = true\n",
		      f);
		fclose(f);
		setenv("ALSOFT_CONF", path.c_str(), 0);
	}
}

bool Install()
{
	auto al = [](const char *name) { return dlsym(RTLD_DEFAULT, name); };
	alcGetCurrentContext = reinterpret_cast<decltype(alcGetCurrentContext)>(al("alcGetCurrentContext"));
	alGenSources = reinterpret_cast<decltype(alGenSources)>(al("alGenSources"));
	alGenBuffers = reinterpret_cast<decltype(alGenBuffers)>(al("alGenBuffers"));
	alBufferData = reinterpret_cast<decltype(alBufferData)>(al("alBufferData"));
	alSourcei = reinterpret_cast<decltype(alSourcei)>(al("alSourcei"));
	alSourcef = reinterpret_cast<decltype(alSourcef)>(al("alSourcef"));
	alSource3f = reinterpret_cast<decltype(alSource3f)>(al("alSource3f"));
	alSource3i = reinterpret_cast<decltype(alSource3i)>(al("alSource3i"));
	alSourcePlay = reinterpret_cast<decltype(alSourcePlay)>(al("alSourcePlay"));
	alSourceStop = reinterpret_cast<decltype(alSourceStop)>(al("alSourceStop"));
	alGenEffects = reinterpret_cast<decltype(alGenEffects)>(al("alGenEffects"));
	alEffecti = reinterpret_cast<decltype(alEffecti)>(al("alEffecti"));
	alEffectf = reinterpret_cast<decltype(alEffectf)>(al("alEffectf"));
	alGenAuxiliaryEffectSlots = reinterpret_cast<decltype(alGenAuxiliaryEffectSlots)>(al("alGenAuxiliaryEffectSlots"));
	alAuxiliaryEffectSloti = reinterpret_cast<decltype(alAuxiliaryEffectSloti)>(al("alAuxiliaryEffectSloti"));
	alAuxiliaryEffectSlotf = reinterpret_cast<decltype(alAuxiliaryEffectSlotf)>(al("alAuxiliaryEffectSlotf"));
	if (!(alGenEffects && alEffecti && alEffectf && alGenAuxiliaryEffectSlots && alAuxiliaryEffectSloti && alAuxiliaryEffectSlotf && alSource3i))
		alGenEffects = nullptr, alGenAuxiliaryEffectSlots = nullptr; // no tail, the body still
	gSampleManager = static_cast<const uint8_t *>(dlsym(RTLD_MAIN_ONLY, "SampleManager"));
	gZoneFlags = static_cast<const int32_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN10CCullZones19CurrentFlags_CameraE"));
	gReady = alcGetCurrentContext && alGenSources && alGenBuffers && alBufferData && alSourcei && alSourcef && alSource3f &&
	         alSourcePlay && alSourceStop;
	if (!gReady)
		Log("shotsound: no OpenAL; his guns sound as the game has them");
	return gReady;
}

void Shot(int weapon)
{
	const shotsynth::Kind kind = shotsynth::KindOf(weapon);
	if (!gReady || kind == shotsynth::Kind::kNone || Volume() <= 0.0f || !Make())
		return;
	// Indoors or out: the tail's room.
	const bool indoors = gZoneFlags && (*gZoneFlags & kRoomForAudio);
	if (gSlot && indoors != gIndoors) {
		gIndoors = indoors;
		SetReverb(indoors ? kRoom : kCity);
	}
	const int k = int(kind);
	const ALuint source = gSources[gNextVoice];
	gNextVoice = (gNextVoice + 1) % kVoices;
	alSourceStop(source);
	alSourcei(source, kAlBuffer, int(gBuffers[k][gNextVariant[k]]));
	gNextVariant[k] = (gNextVariant[k] + 1) % kVariants;
	alSourcef(source, kAlGain, kGain * Volume());
	// Counter-Strike's pitch spread, slowed with the game's own sounds in bullet time.
	alSourcef(source, kAlPitch, (kPitchLow + (kPitchHigh - kPitchLow) * float(rand() % 1000) / 999.0f) * gunbank::AudioTimeScale());
	if (gSlot)
		alSource3i(source, kAlAuxiliarySend, int(gSlot), 0, kAlFilterNull);
	alSourcePlay(source);
}

} // namespace shotsound
