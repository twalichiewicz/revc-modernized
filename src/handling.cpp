#include "handling.h"
#include "config.h"
#include "handlingmodel.h"
#include "hooks.h"
#include "log.h"

#include <dlfcn.h>

namespace handling {
namespace {

using handlingmodel::Record;
constexpr int kHandlings = 106; // NUMHANDLINGS: GetHandlingId returns it for an unknown name

void (*orig_Convert)(void *manager, Record *);
int32_t (*gGetHandlingId)(void *manager, const char *name);

const handlingmodel::Car *gById[kHandlings]; // by handling id, once mapped
bool gMapped, gRetuned[kHandlings], gToldImplausible, gToldDone;
int gKnown, gRetunedCount;

void Map(void *manager)
{
	gMapped = true;
	for (int i = 0; i < handlingmodel::kCarCount; ++i) {
		const handlingmodel::Car &car = handlingmodel::kCars[i];
		const int32_t id = gGetHandlingId(manager, car.name);
		if (id < 0 || id >= kHandlings) {
			Log("car handling: %s isn't in the game's handling; left as it is", car.name);
			continue;
		}
		gById[id] = &car;
		++gKnown;
	}
}

void Hook_Convert(void *manager, Record *record)
{
	if (!gMapped)
		Map(manager);
	const handlingmodel::Car *car = record && record->id >= 0 && record->id < kHandlings ? gById[record->id] : nullptr;
	if (!car) {
		orig_Convert(manager, record);
		return;
	}
	if (!handlingmodel::Plausible(*record)) {
		if (!gToldImplausible)
			Log("car handling: %s doesn't read as a handling line should (mass %.1f); it and any like it are left "
			    "as they are",
			    car->name, record->mass);
		gToldImplausible = true;
		orig_Convert(manager, record);
		return;
	}
	handlingmodel::BeforeConversion(*record, car->kind);
	orig_Convert(manager, record);
	handlingmodel::AfterConversion(*record);
	if (!gRetuned[record->id]) {
		gRetuned[record->id] = true;
		++gRetunedCount;
	}
	if (gRetunedCount == gKnown && !gToldDone) {
		gToldDone = true;
		Log("car handling: retuned %d cars (by type: weight, acceleration, top speed, grip, brakes, softer springs)",
		    gRetunedCount);
	}
}

} // namespace

bool Install()
{
	if (!gConfig.carHandling)
		return true;
	gGetHandlingId =
	    reinterpret_cast<decltype(gGetHandlingId)>(dlsym(RTLD_MAIN_ONLY, "_ZN16cHandlingDataMgr13GetHandlingIdEPKc"));
	if (!gGetHandlingId) {
		Log("car handling: GetHandlingId missing; the game's handling as it is");
		return false;
	}
	orig_Convert = reinterpret_cast<decltype(orig_Convert)>(hooks::Chain(
	    "_ZN16cHandlingDataMgr22ConvertDataToGameUnitsEP13tHandlingData", reinterpret_cast<void *>(Hook_Convert)));
	if (!orig_Convert) {
		Log("car handling: conversion hook missing (re-run install.sh); the game's handling as it is");
		return false;
	}
	return true;
}

int RetunedCount() { return gRetunedCount; }

const char *NameOf(int id) { return id >= 0 && id < kHandlings && gById[id] ? gById[id]->name : nullptr; }

} // namespace handling
