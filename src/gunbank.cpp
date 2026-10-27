#include "gunbank.h"
#include "gunsfx.h"
#include "hooks.h"
#include "log.h"
#include "sfxdsp.h"

#include <cstdint>
#include <dlfcn.h>

namespace gunbank {
namespace {

constexpr size_t kSampleTable = 0x258; // cSampleManager::m_aSamples (20 bytes an entry)

const uintptr_t *gBankStart; // nSampleBankMemoryStartAddress[]
const uint8_t *gBankLoaded;  // gBankLoaded[]
const uint8_t *gSampleManager;
const float *gTimeScale;      // CTimer::ms_fTimeScale
const uint8_t *gAudioManager; // its byte at +0x6ecd set: sounds at full speed (cAudioManager::ProcessActiveQueues)
constexpr size_t kUnscaled = 0x6ecd;
uintptr_t gDone;             // the bank processed (its address)
bool gRefused;

// Each channel's sample (the M4's pitch).
constexpr uint32_t kChannels = 44;
uint32_t gChannelSample[kChannels];
bool (*orig_InitialiseChannel)(void *, uint32_t, uint32_t, uint8_t);
void (*orig_SetChannelFrequency)(void *, uint32_t, uint32_t);

bool Hook_InitialiseChannel(void *self, uint32_t channel, uint32_t sample, uint8_t bank)
{
	if (channel < kChannels)
		gChannelSample[channel] = bank == 0 ? sample : ~0u;
	return orig_InitialiseChannel(self, channel, sample, bank);
}

void Hook_SetChannelFrequency(void *self, uint32_t channel, uint32_t freq)
{
	orig_SetChannelFrequency(self, channel, channel < kChannels ? gunsfx::Pitch(gChannelSample[channel], freq, AudioTimeScale()) : freq);
}

} // namespace

bool Install()
{
	gBankStart = static_cast<const uintptr_t *>(dlsym(RTLD_MAIN_ONLY, "nSampleBankMemoryStartAddress"));
	gBankLoaded = static_cast<const uint8_t *>(dlsym(RTLD_MAIN_ONLY, "gBankLoaded"));
	gSampleManager = static_cast<const uint8_t *>(dlsym(RTLD_MAIN_ONLY, "SampleManager"));
	gTimeScale = static_cast<const float *>(dlsym(RTLD_MAIN_ONLY, "_ZN6CTimer13ms_fTimeScaleE"));
	gAudioManager = static_cast<const uint8_t *>(dlsym(RTLD_MAIN_ONLY, "AudioManager"));
	if (!gBankStart || !gBankLoaded || !gSampleManager) {
		Log("gunbank: the sample bank's symbols aren't there; gunshots stay as the game has them");
		gBankStart = nullptr; // (Frame checks this one)
	}
	for (uint32_t &s : gChannelSample)
		s = ~0u;
	if (!hooks::Install("_ZN14cSampleManager17InitialiseChannelEjjh", Hook_InitialiseChannel, orig_InitialiseChannel) ||
	    !hooks::Install("_ZN14cSampleManager19SetChannelFrequencyEjj", Hook_SetChannelFrequency, orig_SetChannelFrequency))
		Log("gunbank: no sample-channel hooks (re-run install.sh); the M4 keeps the game's pitch");
	return gBankStart && gBankLoaded && gSampleManager;
}

float AudioTimeScale()
{
	if (!gTimeScale || (gAudioManager && gAudioManager[kUnscaled]))
		return 1.0f;
	return *gTimeScale;
}

void Frame()
{
	if (!gBankStart || gRefused || !gBankLoaded[0])
		return;
	const uintptr_t base = gBankStart[0];
	if (!base || base == gDone)
		return;
	const auto *table = reinterpret_cast<const gunsfx::Sample *>(gSampleManager + kSampleTable);
	if (!gunsfx::TableLooksRight(table, gunsfx::kBankSamples)) {
		Log("gunbank: the sample table isn't macVC 1.0.5217's; gunshots stay as the game has them");
		gRefused = true;
		return;
	}
	size_t bytes = 0;
	for (uint32_t id : gunsfx::GunshotIds()) {
		const gunsfx::Sample &s = table[id];
		sfxdsp::Punch(reinterpret_cast<int16_t *>(base + s.offset - table[0].offset), s.size / 2, int(s.rate));
		bytes += s.size;
	}
	gDone = base;
	Log("gunbank: %zu gunshot samples punched up (%zu KB)", gunsfx::GunshotIds().size(), bytes / 1024);
}

} // namespace gunbank
