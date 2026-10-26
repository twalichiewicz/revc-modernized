#include "alert.h"
#include "game.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <dlfcn.h>
#include <vector>

using namespace game;

namespace alert {
namespace {

// OpenAL, from the game's OpenAL Soft.
using ALuint = unsigned int;
using ALint = int;
void (*alGenSources)(int, ALuint *);
void (*alGenBuffers)(int, ALuint *);
void (*alBufferData)(ALuint, int, const void *, int, int);
void (*alSourcei)(ALuint, int, int);
void (*alSourcef)(ALuint, int, float);
void (*alSource3f)(ALuint, int, float, float, float);
void (*alSourcePlay)(ALuint);
void (*alSourcePause)(ALuint);
void (*alGetSourcei)(ALuint, int, ALint *);
void *(*alcGetCurrentContext)();
constexpr int kAlFormatMono16 = 0x1101, kAlBuffer = 0x1009, kAlLooping = 0x1007, kAlSourceRelative = 0x202,
              kAlPosition = 0x1004, kAlGain = 0x100A, kAlPitch = 0x1003, kAlSourceState = 0x1010, kAlPlaying = 0x1012;

// cSampleManager (SampleManager): the effects volume (0-127) at +0, its fade (in cutscenes) at +3.
uint8_t *gSampleManager;

constexpr int kRate = 44000;   // (110 Hz is 400 samples: the swell loops without a click)
constexpr float kLowPitch = 0.8f, kHighPitch = 1.6f;  // empty to full: up most of an octave
constexpr float kQuiet = 0.10f, kLoud = 0.42f;         // its gain, empty to full
constexpr float kStingGain = 0.55f;
constexpr float kFollow = 10.0f; // 1/s: how fast the sound follows the eye
constexpr uint32_t kFresh = 250; // ms: an eye not heard of in this long has emptied

ALuint gSwell, gSting;
bool gReady, gMade;
float gFill, gLevel;
uint32_t gFillAt, gLastNow;

std::vector<int16_t> Synth(int samples, float (*wave)(float t))
{
	std::vector<int16_t> out(static_cast<size_t>(samples));
	for (int i = 0; i < samples; i++)
		out[size_t(i)] = int16_t(std::clamp(wave(float(i) / kRate), -1.0f, 1.0f) * 32000.0f);
	return out;
}

// A low, uneasy string-ish tone on 110 Hz (its 2nd to 6th, the 5th leaning on
// the 3rd), pulsing six times a second: half a second, looping cleanly.
float SwellWave(float t)
{
	constexpr float kTau = 6.2831853f;
	const float tone = 0.50f * std::sin(kTau * 220 * t) + 0.28f * std::sin(kTau * 330 * t) +
	                   0.16f * std::sin(kTau * 440 * t) + 0.10f * std::sin(kTau * 550 * t) +
	                   0.06f * std::sin(kTau * 660 * t);
	return tone * (0.75f + 0.25f * std::sin(kTau * 6 * t)) * 0.7f;
}

// A sharp stab, a minor second against a fifth, dying away.
float StingWave(float t)
{
	constexpr float kTau = 6.2831853f;
	const float attack = std::min(1.0f, t / 0.006f), decay = std::exp(-t * 4.5f);
	const float tone = 0.45f * std::sin(kTau * 440 * t) + 0.35f * std::sin(kTau * 466.16f * t) +
	                   0.30f * std::sin(kTau * 659.26f * t) + 0.20f * std::sin(kTau * 880 * t);
	return tone * attack * decay * 0.8f;
}

// The sources and their sounds, once the game's audio is up.
bool Make()
{
	if (gMade)
		return true;
	if (!alcGetCurrentContext())
		return false;
	const std::vector<int16_t> swell = Synth(kRate / 2, SwellWave), sting = Synth(kRate * 9 / 10, StingWave);
	ALuint buffers[2];
	alGenBuffers(2, buffers);
	alBufferData(buffers[0], kAlFormatMono16, swell.data(), int(swell.size() * 2), kRate);
	alBufferData(buffers[1], kAlFormatMono16, sting.data(), int(sting.size() * 2), kRate);
	ALuint sources[2];
	alGenSources(2, sources);
	gSwell = sources[0];
	gSting = sources[1];
	for (int i = 0; i < 2; i++) {
		alSourcei(sources[i], kAlBuffer, int(buffers[i]));
		alSourcei(sources[i], kAlSourceRelative, 1); // (on his head, not in the world)
		alSource3f(sources[i], kAlPosition, 0.0f, 0.0f, 0.0f);
	}
	alSourcei(gSwell, kAlLooping, 1);
	gMade = true;
	Log("alert: the spotted sound's ready");
	return true;
}

// The game's effects volume as a share (0 while it fades out for a cutscene).
float Volume()
{
	if (!gSampleManager)
		return 1.0f;
	return gSampleManager[0] / 127.0f * std::min(gSampleManager[3], uint8_t(127)) / 127.0f;
}

bool Playing(ALuint source)
{
	ALint state = 0;
	alGetSourcei(source, kAlSourceState, &state);
	return state == kAlPlaying;
}

} // namespace

bool Install()
{
	auto al = [](const char *name) { return dlsym(RTLD_DEFAULT, name); };
	alGenSources = reinterpret_cast<decltype(alGenSources)>(al("alGenSources"));
	alGenBuffers = reinterpret_cast<decltype(alGenBuffers)>(al("alGenBuffers"));
	alBufferData = reinterpret_cast<decltype(alBufferData)>(al("alBufferData"));
	alSourcei = reinterpret_cast<decltype(alSourcei)>(al("alSourcei"));
	alSourcef = reinterpret_cast<decltype(alSourcef)>(al("alSourcef"));
	alSource3f = reinterpret_cast<decltype(alSource3f)>(al("alSource3f"));
	alSourcePlay = reinterpret_cast<decltype(alSourcePlay)>(al("alSourcePlay"));
	alSourcePause = reinterpret_cast<decltype(alSourcePause)>(al("alSourcePause"));
	alGetSourcei = reinterpret_cast<decltype(alGetSourcei)>(al("alGetSourcei"));
	alcGetCurrentContext = reinterpret_cast<decltype(alcGetCurrentContext)>(al("alcGetCurrentContext"));
	gSampleManager = static_cast<uint8_t *>(dlsym(RTLD_MAIN_ONLY, "SampleManager"));
	gReady = alGenSources && alGenBuffers && alBufferData && alSourcei && alSourcef && alSource3f && alSourcePlay &&
	         alSourcePause && alGetSourcei && alcGetCurrentContext;
	if (!gReady)
		Log("alert: no OpenAL; no spotted sound");
	return gReady;
}

void Spotting(float fill)
{
	gFill = std::clamp(fill, 0.0f, 1.0f);
	gFillAt = *api.timeMs;
}

void Spotted()
{
	if (!gReady || !Make())
		return;
	alSourcef(gSting, kAlGain, kStingGain * Volume());
	alSourcePlay(gSting);
	gFill = 0.0f;
	gLevel = 0.0f; // (the swell gives way to it)
}

void Frame()
{
	if (!gReady || !api.timeMs)
		return;
	const uint32_t now = *api.timeMs;
	const float dt = now >= gLastNow ? std::min(now - gLastNow, 100u) / 1000.0f : 0.0f;
	// Paused (the game's clock stopped, or the menu up): silent at once.
	const bool paused = now == gLastNow || (api.frontEndMenuManager && MenuActive(api.frontEndMenuManager));
	gLastNow = now;
	const float target = now - gFillAt < kFresh ? gFill : 0.0f;
	gLevel += (target - gLevel) * std::min(1.0f, dt * kFollow);
	if (paused || gLevel < 0.01f) {
		if (gMade && Playing(gSwell))
			alSourcePause(gSwell);
		return;
	}
	if (!Make())
		return;
	alSourcef(gSwell, kAlPitch, kLowPitch + (kHighPitch - kLowPitch) * gLevel);
	alSourcef(gSwell, kAlGain, (kQuiet + (kLoud - kQuiet) * gLevel) * std::min(1.0f, gLevel * 4.0f) * Volume());
	if (!Playing(gSwell))
		alSourcePlay(gSwell);
}

} // namespace alert
