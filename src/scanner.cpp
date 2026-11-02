#include "scanner.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include <cstring>
#include <dlfcn.h>

using namespace game;

namespace scanner {
namespace {

void (*orig_ServicePoliceRadio)(uint8_t *audio);
void (*gReset)(uint8_t *audio);                     // cAudioManager::ResetPoliceRadio: stops it, clears its queue
bool (*gUsesPoliceRadio)(void *music, uint8_t *vehicle); // cMusicManager::UsesPoliceRadio: by its model
void *gMusicManager;
bool gHearing = true; // last time round

void Hook_ServicePoliceRadio(uint8_t *audio)
{
	uint8_t *player = api.FindPlayerPed ? api.FindPlayerPed() : nullptr;
	uint8_t *vehicle = player && PedInVehicle(player) ? PedVehicle(player) : nullptr;
	const bool hears = !gConfig.scannerInPoliceVehicles || (vehicle && gUsesPoliceRadio(gMusicManager, vehicle));
	if (hears != gHearing && gConfig.scannerInPoliceVehicles)
		gReset(audio); // out: a call stops mid-sentence; in: nothing stale from meanwhile
	gHearing = hears;
	if (hears)
		orig_ServicePoliceRadio(audio);
}

} // namespace

bool Install()
{
	if (!gReset)
		gReset = reinterpret_cast<void (*)(uint8_t *)>(dlsym(RTLD_MAIN_ONLY, "_ZN13cAudioManager16ResetPoliceRadioEv"));
	if (!gUsesPoliceRadio)
		gUsesPoliceRadio = reinterpret_cast<bool (*)(void *, uint8_t *)>(
		    dlsym(RTLD_MAIN_ONLY, "_ZN13cMusicManager15UsesPoliceRadioEP8CVehicle"));
	gMusicManager = dlsym(RTLD_MAIN_ONLY, "MusicManager");
	if (!gReset || !gUsesPoliceRadio)
		return false;
	return hooks::Install("_ZN13cAudioManager18ServicePoliceRadioEv", Hook_ServicePoliceRadio, orig_ServicePoliceRadio);
}

} // namespace scanner
