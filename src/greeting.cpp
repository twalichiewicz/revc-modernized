#include "greeting.h"
#include "log.h"
#include "voiceclip.h"

#include <AudioToolbox/AudioToolbox.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <vector>

namespace greeting {
namespace {
using ALuint = unsigned int;
void (*alGenSources)(int, ALuint *);
void (*alGenBuffers)(int, ALuint *);
void (*alBufferData)(ALuint, int, const void *, int, int);
void (*alSourcei)(ALuint, int, int);
void (*alSourcef)(ALuint, int, float);
void (*alSource3f)(ALuint, int, float, float, float);
void (*alSourcePlay)(ALuint);
void (*alGetSourcei)(ALuint, int, int *);
void *(*alcGetCurrentContext)();
ALuint gSource, gBuffer;
bool gTried;
constexpr int kRate = 22050;

bool Make()
{
	if (gSource)
		return true;
	if (gTried)
		return false;
	auto al = [](const char *name) { return dlsym(RTLD_DEFAULT, name); };
	alcGetCurrentContext = reinterpret_cast<decltype(alcGetCurrentContext)>(al("alcGetCurrentContext"));
	if (!alcGetCurrentContext || !alcGetCurrentContext())
		return false;
	gTried = true;
	alGenSources = reinterpret_cast<decltype(alGenSources)>(al("alGenSources"));
	alGenBuffers = reinterpret_cast<decltype(alGenBuffers)>(al("alGenBuffers"));
	alBufferData = reinterpret_cast<decltype(alBufferData)>(al("alBufferData"));
	alSourcei = reinterpret_cast<decltype(alSourcei)>(al("alSourcei"));
	alSourcef = reinterpret_cast<decltype(alSourcef)>(al("alSourcef"));
	alSource3f = reinterpret_cast<decltype(alSource3f)>(al("alSource3f"));
	alSourcePlay = reinterpret_cast<decltype(alSourcePlay)>(al("alSourcePlay"));
	alGetSourcei = reinterpret_cast<decltype(alGetSourcei)>(al("alGetSourcei"));
	if (!alGenSources || !alGenBuffers || !alBufferData || !alSourcei || !alSourcef || !alSource3f || !alSourcePlay ||
		!alGetSourcei)
		return false;
	// As the game opens its files, relative to GameData. This dialogue is
	// Tommy's "Hello? Hel-lo?! Hello?" (american.gxt DRUG_1); keep the first phrase.
	const char *path = "audio/drug_1.mp3";
	CFURLRef url =
		CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8 *>(path), strlen(path), false);
	ExtAudioFileRef file = nullptr;
	const OSStatus opened = url ? ExtAudioFileOpenURL(url, &file) : -1;
	if (url)
		CFRelease(url);
	if (opened != noErr || !file) {
		Log("greeting: Tommy's drug_1 dialogue unavailable (%d)", int(opened));
		return false;
	}
	AudioStreamBasicDescription format{};
	format.mSampleRate = kRate;
	format.mFormatID = kAudioFormatLinearPCM;
	format.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
	format.mBytesPerPacket = format.mBytesPerFrame = 2;
	format.mFramesPerPacket = 1;
	format.mChannelsPerFrame = 1;
	format.mBitsPerChannel = 16;
	const OSStatus configured =
		ExtAudioFileSetProperty(file, kExtAudioFileProperty_ClientDataFormat, sizeof format, &format);
	std::vector<int16_t> pcm(size_t(kRate) * 3);
	UInt32 frames = UInt32(pcm.size());
	AudioBufferList buffers{};
	buffers.mNumberBuffers = 1;
	buffers.mBuffers[0] = {1, UInt32(pcm.size() * 2), pcm.data()};
	const OSStatus read = configured == noErr ? ExtAudioFileRead(file, &frames, &buffers) : configured;
	ExtAudioFileDispose(file);
	if (read != noErr)
		return false;
	pcm.resize(frames);
	const voiceclip::Range phrase = voiceclip::FirstPhrase(pcm, kRate);
	if (phrase.last <= phrase.first)
		return false;
	alGenBuffers(1, &gBuffer);
	alBufferData(gBuffer, 0x1101, pcm.data() + phrase.first, int((phrase.last - phrase.first) * 2), kRate);
	alGenSources(1, &gSource);
	alSourcei(gSource, 0x1009, int(gBuffer));
	alSourcei(gSource, 0x202, 1);
	alSource3f(gSource, 0x1004, 0.0f, 0.0f, 0.0f);
	Log("greeting: Tommy's first hello loaded (%.2f s)", float(phrase.last - phrase.first) / kRate);
	return gSource != 0;
}
} // namespace

bool Play()
{
	if (!Make())
		return false;
	int state = 0;
	alGetSourcei(gSource, 0x1010, &state);
	if (state == 0x1012)
		return true;
	const uint8_t *samples = static_cast<const uint8_t *>(dlsym(RTLD_MAIN_ONLY, "SampleManager"));
	const float volume = samples ? float(samples[0]) * samples[3] / (127.0f * 127.0f) : 1.0f;
	alSourcef(gSource, 0x100a, volume);
	alSourcePlay(gSource);
	return true;
}
} // namespace greeting
