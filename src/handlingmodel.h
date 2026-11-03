#pragma once
// GTA IV-style weight for Vice City's cars (#37), as numbers: a handling.cfg
// line retuned by its vehicle type, before the game converts it to game units
// (cHandlingDataMgr::ConvertDataToGameUnits), so the game derives turn mass,
// buoyancy, the top speed and the gears from the new values itself.
//
// - Mass by type: trucks and buses weigh what such vehicles weigh; cars keep theirs.
// - Slower: less engine, and a top speed set through drag (the game's top speed
//   is where drag meets the engine; dimension z feeds only that drag).
// - Softer springs at the same ride height, so the game's own suspension rolls
//   the body in corners, dives it under braking and squats it under power;
//   damping scaled with them, a little floatier.
// - Everyone: heavier turn-in, progressive slides, slightly less grip, longer
//   braking. Within a type every factor is the same, so each car keeps its
//   place among the others.
#include <cstddef>
#include <cstdint>

namespace handlingmodel {

// tHandlingData (macVC 1.0.5217), 0xdc bytes. Before the conversion the fields
// hold the file's values (engine acceleration is the file's x0.4, max velocity
// km/h); after it, game units (per 1/50 s), with the turn mass, inverse mass,
// buoyancy, top speed and gears derived.
struct Record {
	int32_t id;                  // 0x00
	float mass;                  // 0x04 kg
	float invMass;               // 0x08 (derived)
	float turnMass;              // 0x0c (derived: (x^2 + y^2) * mass / 12)
	float dimension[3];          // 0x10 x, y (turn mass, drag) and z (drag only)
	float centreOfMass[3];       // 0x1c
	int8_t percentSubmerged;     // 0x28
	uint8_t pad29[3];            // 0x29
	float buoyancy;              // 0x2c (derived)
	float tractionMultiplier;    // 0x30
	uint8_t gears[0x48];         // 0x34 cTransmission::Gears[6] (derived)
	uint8_t driveType;           // 0x7c 'F', 'R' or '4'
	uint8_t engineType;          // 0x7d
	int8_t numberOfGears;        // 0x7e
	uint8_t transmissionFlags;   // 0x7f
	float engineAcceleration;    // 0x80
	float maxVelocity;           // 0x84 km/h; after: 1.2 x the drag-limited top speed
	float maxCruiseVelocity;     // 0x88 (derived: the drag-limited top speed)
	float maxReverseVelocity;    // 0x8c (derived)
	float currentVelocity;       // 0x90
	float brakeDeceleration;     // 0x94
	float brakeBias;             // 0x98
	int8_t abs;                  // 0x9c
	uint8_t pad9d[3];            // 0x9d
	float steeringLock;          // 0xa0 degrees
	float tractionLoss;          // 0xa4 grip kept when sliding
	float tractionBias;          // 0xa8
	float unused;                // 0xac
	float suspensionForce;       // 0xb0
	float suspensionDamping;     // 0xb4
	float suspensionUpper;       // 0xb8 m
	float suspensionLower;       // 0xbc m
	float suspensionBias;        // 0xc0
	float antiDive;              // 0xc4
	float collisionDamage;       // 0xc8 (after: x2000/mass)
	uint32_t flags;              // 0xcc
	float seatOffset;            // 0xd0
	int32_t money;               // 0xd4
	int8_t frontLights;          // 0xd8
	int8_t rearLights;           // 0xd9
	uint8_t padda[2];            // 0xda
};
static_assert(sizeof(Record) == 0xdc, "tHandlingData");
static_assert(offsetof(Record, mass) == 0x04 && offsetof(Record, turnMass) == 0x0c, "tHandlingData masses");
static_assert(offsetof(Record, dimension) == 0x10 && offsetof(Record, centreOfMass) == 0x1c, "tHandlingData shape");
static_assert(offsetof(Record, percentSubmerged) == 0x28 && offsetof(Record, buoyancy) == 0x2c, "tHandlingData water");
static_assert(offsetof(Record, tractionMultiplier) == 0x30 && offsetof(Record, gears) == 0x34, "tHandlingData grip");
static_assert(offsetof(Record, driveType) == 0x7c && offsetof(Record, numberOfGears) == 0x7e, "cTransmission bytes");
static_assert(offsetof(Record, engineAcceleration) == 0x80 && offsetof(Record, maxVelocity) == 0x84, "cTransmission");
static_assert(offsetof(Record, maxCruiseVelocity) == 0x88 && offsetof(Record, currentVelocity) == 0x90, "cTransmission");
static_assert(offsetof(Record, brakeDeceleration) == 0x94 && offsetof(Record, abs) == 0x9c, "tHandlingData brakes");
static_assert(offsetof(Record, steeringLock) == 0xa0 && offsetof(Record, tractionLoss) == 0xa4, "tHandlingData steer");
static_assert(offsetof(Record, tractionBias) == 0xa8 && offsetof(Record, suspensionForce) == 0xb0, "tHandlingData");
static_assert(offsetof(Record, suspensionUpper) == 0xb8 && offsetof(Record, suspensionLower) == 0xbc, "suspension");
static_assert(offsetof(Record, suspensionBias) == 0xc0 && offsetof(Record, antiDive) == 0xc4, "suspension");
static_assert(offsetof(Record, collisionDamage) == 0xc8 && offsetof(Record, flags) == 0xcc, "tHandlingData");
static_assert(offsetof(Record, seatOffset) == 0xd0 && offsetof(Record, money) == 0xd4, "tHandlingData");
static_assert(offsetof(Record, frontLights) == 0xd8 && offsetof(Record, rearLights) == 0xd9, "tHandlingData lights");

enum class Kind : uint8_t { Sports, Car, Van, OffRoad, LightTruck, HeavyTruck, Tank, Small };

// Multipliers of a type: mass, engine acceleration, top speed and the springs'
// stiffness per metre (1 leaves the springs as they are).
struct Factors {
	float mass, accel, top, springs;
};
const Factors &For(Kind);

struct Car {
	const char *name; // handling.cfg identifier
	Kind kind;
};
extern const Car kCars[];
extern const int kCarCount; // 71: every car of handling.cfg but the RC Bandit
const Car *Find(const char *identifier); // nullptr: not retuned

// A record as parsed (before conversion) that this layout reads sensibly.
bool Plausible(const Record &raw);
void BeforeConversion(Record &raw, Kind);
void AfterConversion(Record &converted);

// km/h: where the game's conversion puts this raw record's top speed (its
// search for where drag meets the engine, from max velocity down).
float DragTopSpeed(const Record &raw);

} // namespace handlingmodel
