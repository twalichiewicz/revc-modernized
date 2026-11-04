#include "handlingmodel.h"

#include <algorithm>
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
    kVan{1.20f, 0.85f, 0.90f, 0.60f}, kOffRoad{1.15f, 1.10f, 0.90f, 0.60f}, kLightTruck{1.50f, 1.10f, 0.90f, 0.70f},
    kHeavyTruck{1.60f, 1.15f, 0.85f, 0.75f}, kTank{1.00f, 1.00f, 0.60f, 1.00f}, kSmall{1.00f, 0.85f, 0.60f, 0.80f};

// ConvertDataToGameUnits: engine acceleration and max velocity into game units,
// and its search's step (game units per 1/50 s; 1 = 180 km/h).
constexpr float kAccelUnits = 0.0004f, kVelocityUnits = 0.0055556f, kSearchStep = 0.01f, kKmhPerUnit = 180.0f;

bool In(float v, float lo, float hi) { return v >= lo && v <= hi; }

// At rest each axle may be compressed this share of its travel at most (so it
// takes 2.5x its static load before bottoming), or no more than it was; and it
// may stand this far (m) from where it stood.
constexpr float kMostCompression = 0.4f, kMostAxleShift = 0.01f, kSofteningStep = 0.01f;

// How far each axle's wheels sag at rest (m), the centre of mass midway between
// the axles: each wheel carries a quarter, and the game scales the front springs
// by the bias, the rear ones by 1 - bias (CAutomobile::ProcessControl).
struct Sag {
	float front, rear;
};
Sag RestSag(const Record &r)
{
	const float travel = r.suspensionUpper - r.suspensionLower, force = r.suspensionForce, bias = r.suspensionBias;
	return {travel / (8.0f * force * bias), travel / (8.0f * force * (1.0f - bias))};
}

// Softer by `springs` per metre, the lower limit moved down by the axles' mean
// extra sag: does each axle stay within the limits?
bool Fits(const Record &r, float springs)
{
	const float travel = r.suspensionUpper - r.suspensionLower;
	const Sag sag = RestSag(r);
	const float worst = std::max(sag.front, sag.rear), extra = 0.5f * (sag.front + sag.rear) * (1.0f / springs - 1.0f);
	const float compressed = worst / (springs * (travel + extra));
	const float shift = 0.5f * std::fabs(sag.front - sag.rear) * (1.0f / springs - 1.0f);
	return compressed <= std::max(kMostCompression, worst / travel) + 1e-6f && shift <= kMostAxleShift + 1e-6f;
}

// The softening a type wants, or as near it as this car's springs allow (1: none).
float Softening(const Record &r, float wanted)
{
	for (float springs = wanted; springs < 1.0f; springs += kSofteningStep)
		if (Fits(r, springs))
			return springs;
	return 1.0f;
}

} // namespace

const Car kCars[] = {
    {"PHEONIX", Kind::Sports, 207},     {"HOTRING", Kind::Sports, 224},     {"SABRETUR", Kind::Sports, 206},
    {"INFERNUS", Kind::Sports, 141},    {"CHEETAH", Kind::Sports, 145},     {"BANSHEE", Kind::Sports, 159},
    {"DELUXO", Kind::Sports, 211},      {"COMET", Kind::Sports, 210},       {"STINGER", Kind::Sports, 132},
    {"ADMIRAL", Kind::Car, 175},        {"SENTINEL", Kind::Car, 135},       {"MAFIA", Kind::Car, 174},
    {"WASHING", Kind::Car, 151},        {"LOVEFIST", Kind::Car, 201},       {"STRETCH", Kind::Car, 139},
    {"ZEBRA", Kind::Car, 188},          {"CABBIE", Kind::Car, 168},         {"ROMERO", Kind::Car, 172},
    {"GLENDALE", Kind::Car, 196},       {"VIRGO", Kind::Car, 221},          {"IDAHO", Kind::Car, 131},
    {"ESPERANT", Kind::Car, 149},       {"SABRE1", Kind::Car, 205},         {"STALLION", Kind::Car, 169},
    {"VOODOO", Kind::Car, 142},         {"OCEANIC", Kind::Car, 197},        {"GREENWOO", Kind::Car, 222},
    {"HERMES", Kind::Car, 204},         {"CUBAN", Kind::Car, 164},          {"BLOODRA", Kind::Car, 234},
    {"BLOODRB", Kind::Car, 235},        {"REGINA", Kind::Car, 209},         {"PEREN", Kind::Car, 134},
    {"TAXI", Kind::Car, 150},           {"MANANA", Kind::Car, 140},         {"BLISTAC", Kind::Car, 226},
    {"POLICE", Kind::Car, 156},         {"FBICAR", Kind::Car, 147},         {"MOONBEAM", Kind::Van, 148},
    {"GANGBUR", Kind::Van, 179},        {"BURRITO", Kind::Van, 212},        {"PONY", Kind::Van, 143},
    {"RUMPO", Kind::Van, 170},          {"MRWHOOP", Kind::Van, 153},        {"SANDKING", Kind::OffRoad, 225},
    {"BFINJECT", Kind::OffRoad, 154},   {"WALTON", Kind::OffRoad, 208},     {"BOBCAT", Kind::OffRoad, 152},
    {"LANDSTAL", Kind::OffRoad, 130},   {"RANCHER", Kind::OffRoad, 219},    {"DESPERAD", Kind::OffRoad, 230},
    {"PATRIOT", Kind::OffRoad, 200},    {"FBIRANCH", Kind::OffRoad, 220},   {"BENSON", Kind::LightTruck, 229},
    {"MULE", Kind::LightTruck, 144},    {"BOXVILLE", Kind::LightTruck, 228}, {"YANKEE", Kind::LightTruck, 186},
    {"SECURICA", Kind::LightTruck, 158}, {"AMBULAN", Kind::LightTruck, 146}, {"ENFORCER", Kind::LightTruck, 157},
    {"LINERUN", Kind::HeavyTruck, 133}, {"PACKER", Kind::HeavyTruck, 173},  {"BUS", Kind::HeavyTruck, 161},
    {"FLATBED", Kind::HeavyTruck, 185}, {"TRASH", Kind::HeavyTruck, 138},   {"COACH", Kind::HeavyTruck, 167},
    {"FIRETRUK", Kind::HeavyTruck, 137}, {"BARRACKS", Kind::HeavyTruck, 163}, {"RHINO", Kind::Tank, 162},
    {"GOLFCART", Kind::Small, 187},     {"BAGGAGE", Kind::Small, 215},
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

const Car *FindModel(int model)
{
 // Native default.ide aliases share a handling row, including work-vehicle
 // mission variants and the two additional Hotrings. Keep one Kind per row.
 switch(model){case 189:model=143;break;case 213:model=144;break;case 216:model=168;break;
 case 232:case 233:model=224;break;case 236:model=145;break;}
 for(const Car &car:kCars)if(car.model==model)return &car;
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
	const float springs = Softening(r, f.springs);
	if (springs < 1.0f) {
		// Softer per metre, a wheel sags 1/springs as far; the lower limit moves
		// down by the axles' mean difference, so the car stands as high as it
		// did with the same travel left above.
		const float travel = r.suspensionUpper - r.suspensionLower;
		const Sag sag = RestSag(r);
		r.suspensionLower -= 0.5f * (sag.front + sag.rear) * (1.0f / springs - 1.0f);
		r.suspensionForce *= springs * (r.suspensionUpper - r.suspensionLower) / travel;
		// The damping ratio goes as damping x sqrt(travel / force).
		r.suspensionDamping *= kDampingRatio * std::sqrt(springs);
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
