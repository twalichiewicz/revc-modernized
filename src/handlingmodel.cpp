#include "handlingmodel.h"

#include <cmath>
#include <cstring>

namespace handlingmodel {
namespace {

// Every type: turn mass (yaw, and with it roll and pitch: the game has one
// number for all three), the share of the sliding grip loss kept, peak grip,
// brakes and the damping ratio.
constexpr float kTurnMass = 1.25f, kSlideLossKept = 0.65f, kTraction = 0.97f, kBrakes = 0.88f, kDampingRatio = 0.85f;

// By type (tuned in play from the drive log):            mass  accel  top   springs
constexpr Factors kSports{1.00f, 0.85f, 0.92f, 0.65f}, kCar{1.00f, 0.85f, 0.90f, 0.55f},
    kVan{1.20f, 0.85f, 0.90f, 0.60f}, kOffRoad{1.15f, 0.85f, 0.90f, 0.60f}, kLightTruck{1.50f, 0.85f, 0.90f, 0.70f},
    kHeavyTruck{1.60f, 0.85f, 0.85f, 0.75f}, kTank{1.00f, 1.00f, 0.60f, 1.00f}, kSmall{1.00f, 0.85f, 0.60f, 0.80f};

// ConvertDataToGameUnits: engine acceleration and max velocity into game units,
// and its search's step (game units per 1/50 s; 1 = 180 km/h).
constexpr float kAccelUnits = 0.0004f, kVelocityUnits = 0.0055556f, kSearchStep = 0.01f, kKmhPerUnit = 180.0f;

bool In(float v, float lo, float hi) { return v >= lo && v <= hi; }

} // namespace

const Car kCars[] = {
    {"PHEONIX", Kind::Sports},     {"HOTRING", Kind::Sports},     {"SABRETUR", Kind::Sports},
    {"INFERNUS", Kind::Sports},    {"CHEETAH", Kind::Sports},     {"BANSHEE", Kind::Sports},
    {"DELUXO", Kind::Sports},      {"COMET", Kind::Sports},       {"STINGER", Kind::Sports},
    {"ADMIRAL", Kind::Car},        {"SENTINEL", Kind::Car},       {"MAFIA", Kind::Car},
    {"WASHING", Kind::Car},        {"LOVEFIST", Kind::Car},       {"STRETCH", Kind::Car},
    {"ZEBRA", Kind::Car},          {"CABBIE", Kind::Car},         {"ROMERO", Kind::Car},
    {"GLENDALE", Kind::Car},       {"VIRGO", Kind::Car},          {"IDAHO", Kind::Car},
    {"ESPERANT", Kind::Car},       {"SABRE1", Kind::Car},         {"STALLION", Kind::Car},
    {"VOODOO", Kind::Car},         {"OCEANIC", Kind::Car},        {"GREENWOO", Kind::Car},
    {"HERMES", Kind::Car},         {"CUBAN", Kind::Car},          {"BLOODRA", Kind::Car},
    {"BLOODRB", Kind::Car},        {"REGINA", Kind::Car},         {"PEREN", Kind::Car},
    {"TAXI", Kind::Car},           {"MANANA", Kind::Car},         {"BLISTAC", Kind::Car},
    {"POLICE", Kind::Car},         {"FBICAR", Kind::Car},         {"MOONBEAM", Kind::Van},
    {"GANGBUR", Kind::Van},        {"BURRITO", Kind::Van},        {"PONY", Kind::Van},
    {"RUMPO", Kind::Van},          {"MRWHOOP", Kind::Van},        {"SANDKING", Kind::OffRoad},
    {"BFINJECT", Kind::OffRoad},   {"WALTON", Kind::OffRoad},     {"BOBCAT", Kind::OffRoad},
    {"LANDSTAL", Kind::OffRoad},   {"RANCHER", Kind::OffRoad},    {"DESPERAD", Kind::OffRoad},
    {"PATRIOT", Kind::OffRoad},    {"FBIRANCH", Kind::OffRoad},   {"BENSON", Kind::LightTruck},
    {"MULE", Kind::LightTruck},    {"BOXVILLE", Kind::LightTruck}, {"YANKEE", Kind::LightTruck},
    {"SECURICA", Kind::LightTruck}, {"AMBULAN", Kind::LightTruck}, {"ENFORCER", Kind::LightTruck},
    {"LINERUN", Kind::HeavyTruck}, {"PACKER", Kind::HeavyTruck},  {"BUS", Kind::HeavyTruck},
    {"FLATBED", Kind::HeavyTruck}, {"TRASH", Kind::HeavyTruck},   {"COACH", Kind::HeavyTruck},
    {"FIRETRUK", Kind::HeavyTruck}, {"BARRACKS", Kind::HeavyTruck}, {"RHINO", Kind::Tank},
    {"GOLFCART", Kind::Small},     {"BAGGAGE", Kind::Small},
};
const int kCarCount = int(sizeof(kCars) / sizeof(kCars[0]));

const Factors &For(Kind kind)
{
	switch (kind) {
	case Kind::Sports: return kSports;
	case Kind::Car: return kCar;
	case Kind::Van: return kVan;
	case Kind::OffRoad: return kOffRoad;
	case Kind::LightTruck: return kLightTruck;
	case Kind::HeavyTruck: return kHeavyTruck;
	case Kind::Tank: return kTank;
	case Kind::Small: return kSmall;
	}
	return kCar;
}

const Car *Find(const char *identifier)
{
	for (const Car &car : kCars)
		if (!std::strcmp(car.name, identifier))
			return &car;
	return nullptr;
}

bool Plausible(const Record &r)
{
	for (float d : r.dimension)
		if (!(d > 0.0f && d <= 20.0f))
			return false;
	return In(r.mass, 50, 50000) && r.engineAcceleration > 0 && r.engineAcceleration <= 20 &&
	       In(r.maxVelocity, 10, 400) && In(r.brakeDeceleration, 0.5f, 15) && r.tractionMultiplier > 0 &&
	       r.tractionMultiplier <= 3 && r.tractionLoss > 0 && r.tractionLoss <= 1 && r.numberOfGears >= 1 &&
	       r.numberOfGears <= 5 && In(r.suspensionForce, 0.3f, 6) && In(r.suspensionDamping, 0.01f, 1) &&
	       r.suspensionUpper > r.suspensionLower && r.suspensionBias > 0 && r.suspensionBias < 1 &&
	       r.percentSubmerged >= 1 && r.percentSubmerged <= 100 && r.money >= 0 && r.money <= 1000000 &&
	       r.frontLights >= 0 && r.frontLights <= 3 && r.rearLights >= 0 && r.rearLights <= 3;
}

void BeforeConversion(Record &r, Kind kind)
{
	const Factors &f = For(kind);
	r.mass *= f.mass;
	r.engineAcceleration *= f.accel;
	// The top speed goes as the cube root of engine x mass / drag area, so this
	// drag area lands it at f.top of what it was.
	r.dimension[2] *= f.accel * f.mass / (f.top * f.top * f.top);
	r.tractionLoss = 1.0f - kSlideLossKept * (1.0f - r.tractionLoss);
	r.tractionMultiplier *= kTraction;
	r.brakeDeceleration *= kBrakes;
	if (f.springs < 1.0f) {
		// A wheel sags (upper - lower) / (4 force) at rest. Softer per metre it
		// sags 1/springs as far; the lower limit moves down by the difference,
		// so the car stands as high as it did with the same travel left above.
		const float travel = r.suspensionUpper - r.suspensionLower, sag = travel / (4.0f * r.suspensionForce);
		r.suspensionLower -= sag * (1.0f / f.springs - 1.0f);
		r.suspensionForce *= f.springs * (r.suspensionUpper - r.suspensionLower) / travel;
		// The damping ratio goes as damping x sqrt(travel / force).
		r.suspensionDamping *= kDampingRatio * std::sqrt(f.springs);
	} else {
		r.suspensionDamping *= kDampingRatio;
	}
}

void AfterConversion(Record &r) { r.turnMass *= kTurnMass; }

float DragTopSpeed(const Record &r)
{
	const float accel = r.engineAcceleration * kAccelUnits / 6.0f;
	const float drag = 0.5f * r.dimension[0] * r.dimension[2] / r.mass;
	float v = r.maxVelocity * kVelocityUnits;
	while (v > 0.0f) {
		v -= kSearchStep;
		if (accel >= v * (1.0f - 1.0f / (v * v * drag + 1.0f)))
			break;
	}
	return v * kKmhPerUnit;
}

} // namespace handlingmodel
