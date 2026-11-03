#include "handling.h"
#include "config.h"
#include "drivelog.h"
#include "game.h"
#include "handlingmodel.h"
#include "hooks.h"
#include "log.h"

#include <cmath>
#include <cstdio>
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

// CAutomobile (macVC 1.0.5217): its matrix's rows (right, forward, up) from +0x8,
// its move speed (per 1/50 s), handling, wheels' contact points (CColPoint,
// point first) and spring ratios (1: the wheel hangs free). ProcessControl ends
// by copying this frame's ratios (+0x580) to +0x590 and resetting them to 1, so
// after it they're read from +0x590.
constexpr size_t kMatrix = 0x8, kMoveSpeed = 0x84, kHandling = 0x160, kWheelContacts = 0x4e0, kContactStride = 0x28,
                 kSpringRatios = 0x590;
constexpr float kRadToDeg = 57.2957795f;

void (*orig_Control)(uint8_t *car);
drivelog::State gDrive;
const uint8_t *gDriven; // the car the log is about

void Sample(uint8_t *car)
{
	const auto *handlingRecord = *reinterpret_cast<const Record *const *>(car + kHandling);
	const char *name = handlingRecord ? NameOf(handlingRecord->id) : nullptr;
	if (!name || !game::api.timeMs)
		return;
	if (car != gDriven) { // a new car: a new log
		gDriven = car;
		gDrive = drivelog::State{};
	}
	const float *m = reinterpret_cast<const float *>(car + kMatrix);
	const float *right = m, *forward = m + 4, *up = m + 8;
	const float *v = reinterpret_cast<const float *>(car + kMoveSpeed);
	const float *ratio = reinterpret_cast<const float *>(car + kSpringRatios);
	float contact[4][3];
	bool allDown = true;
	for (int w = 0; w < 4; ++w) {
		const float *p = reinterpret_cast<const float *>(car + kWheelContacts + w * kContactStride);
		contact[w][0] = p[0], contact[w][1] = p[1], contact[w][2] = p[2];
		allDown = allDown && ratio[w] < 1.0f;
	}
	drivelog::Sample sample{float(*game::api.timeMs) / 1000.0f,
	                        50.0f * (v[0] * forward[0] + v[1] * forward[1] + v[2] * forward[2]), allDown, 0, 0};
	if (allDown)
		drivelog::BodyAngles(right, forward, up, contact, sample.roll, sample.pitch);
	drivelog::Add(gDrive, sample);
	if (drivelog::Due(gDrive, sample.t)) {
		const drivelog::Window w = drivelog::Take(gDrive, sample.t);
		char zeroTo100[16] = "-";
		if (w.zeroTo100 >= 0)
			std::snprintf(zeroTo100, sizeof zeroTo100, "%.1f s", w.zeroTo100);
		Log("car handling: %s last 30 s: top %.0f km/h, 0-100 %s, body roll up to %.1f deg, pitch %.1f deg", name,
		    w.top * 3.6f, zeroTo100, w.rollMost * kRadToDeg, w.pitchMost * kRadToDeg);
	}
}

void Hook_Control(uint8_t *car)
{
	orig_Control(car);
	uint8_t *ped = game::api.FindPlayerPed ? game::api.FindPlayerPed() : nullptr;
	if (ped && game::PedInVehicle(ped) && game::PedVehicle(ped) == car)
		Sample(car);
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
	orig_Control = reinterpret_cast<decltype(orig_Control)>(
	    hooks::Chain("_ZN11CAutomobile14ProcessControlEv", reinterpret_cast<void *>(Hook_Control)));
	if (!orig_Control)
		Log("car handling: ProcessControl hook missing; no drive log");
	return true;
}

int RetunedCount() { return gRetunedCount; }

const char *NameOf(int id) { return id >= 0 && id < kHandlings && gById[id] ? gById[id]->name : nullptr; }

} // namespace handling
