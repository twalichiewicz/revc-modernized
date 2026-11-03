// The car handling retune (#37), without the game: a handling.cfg line as the
// loader leaves it, retuned by its vehicle type before the game converts it.
#include "handlingmodel.h"
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace handlingmodel;
static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)
#define NEAR(a, b, e) CHECK(std::fabs((a) - (b)) <= (e))

// INFERNUS as LoadHandlingData stores it (engine acceleration is the file's 31 x 0.4).
static Record Infernus()
{
	Record r{};
	r.id = 4;
	r.mass = 1500;
	r.dimension[0] = 1.9f, r.dimension[1] = 4.5f, r.dimension[2] = 1.0f;
	r.centreOfMass[1] = 0.02f, r.centreOfMass[2] = 0.20f;
	r.percentSubmerged = 75;
	r.tractionMultiplier = 0.85f, r.tractionLoss = 0.70f, r.tractionBias = 0.48f;
	r.numberOfGears = 5, r.maxVelocity = 300, r.engineAcceleration = 12.4f, r.driveType = 'R', r.engineType = 'P';
	r.brakeDeceleration = 4.5f, r.brakeBias = 0.65f, r.steeringLock = 30;
	r.suspensionForce = 1.10f, r.suspensionDamping = 0.14f, r.suspensionUpper = 0.15f, r.suspensionLower = -0.15f;
	r.suspensionBias = 0.5f, r.antiDive = 0.5f, r.seatOffset = 0.24f, r.money = 95000, r.flags = 0x1000183;
	r.frontLights = 0, r.rearLights = 1;
	return r;
}
static Record Bus()
{
	Record r = Infernus();
	r.id = 58;
	r.mass = 8000;
	r.dimension[0] = 2.3f, r.dimension[1] = 9.0f, r.dimension[2] = 3.6f;
	r.engineAcceleration = 2.8f, r.numberOfGears = 4, r.driveType = 'R', r.engineType = 'D';
	r.tractionMultiplier = 0.5f, r.tractionLoss = 0.6f, r.brakeDeceleration = 2.0f;
	r.suspensionForce = 1.3f, r.suspensionDamping = 0.17f, r.suspensionUpper = 0.40f, r.suspensionLower = -0.25f;
	r.money = 15000;
	return r;
}
static Record Rhino()
{
	Record r = Bus();
	r.id = 72;
	r.mass = 30000;
	r.dimension[0] = 3.5f, r.dimension[1] = 8.0f, r.dimension[2] = 3.9f;
	r.engineAcceleration = 2.4f, r.driveType = '4';
	r.suspensionForce = 1.0f, r.suspensionDamping = 0.10f, r.suspensionUpper = 0.35f, r.suspensionLower = -0.10f;
	return r;
}
static float Sag(const Record &r) { return (r.suspensionUpper - r.suspensionLower) / (4 * r.suspensionForce); }
static float RideHeight(const Record &r) { return -r.suspensionLower - Sag(r); }
static float BumpLeft(const Record &r) { return r.suspensionUpper - r.suspensionLower - Sag(r); }
static float PerMetre(const Record &r) { return r.suspensionForce / (r.suspensionUpper - r.suspensionLower); }
static float DampingRatio(const Record &r)
{
	return r.suspensionDamping * std::sqrt((r.suspensionUpper - r.suspensionLower) / r.suspensionForce);
}

static void TestLayout()
{
	CHECK(sizeof(Record) == 0xdc);
	CHECK(offsetof(Record, engineAcceleration) == 0x80);
	CHECK(offsetof(Record, tractionLoss) == 0xa4);
	CHECK(offsetof(Record, suspensionForce) == 0xb0);
	CHECK(offsetof(Record, money) == 0xd4);
}

static void TestRetuneValues()
{
	Record r = Infernus();
	BeforeConversion(r, Kind::Sports);
	NEAR(r.engineAcceleration, 10.54f, 1e-4f);
	NEAR(r.dimension[2], 1.09158f, 1e-4f);
	NEAR(r.tractionLoss, 0.805f, 1e-4f);
	NEAR(r.tractionMultiplier, 0.8245f, 1e-4f);
	NEAR(r.brakeDeceleration, 3.96f, 1e-4f);
	NEAR(r.suspensionLower, -0.186713f, 1e-4f);
	NEAR(r.suspensionForce, 0.80250f, 1e-4f);
	NEAR(r.suspensionDamping, 0.095941f, 1e-4f);
	CHECK(r.mass == 1500 && r.dimension[0] == 1.9f && r.dimension[1] == 4.5f);
}

static void TestRetuneKeepsTheRest()
{
	Record before = Infernus();
	for (size_t i = 0; i < sizeof(before.gears); ++i)
		before.gears[i] = uint8_t(i * 7 + 3);
	Record after = before;
	BeforeConversion(after, Kind::HeavyTruck);
	// Mask out the fields the retune is meant to change.
	for (Record *r : {&before, &after}) {
		r->mass = r->engineAcceleration = r->dimension[2] = 0;
		r->tractionLoss = r->tractionMultiplier = r->brakeDeceleration = 0;
		r->suspensionLower = r->suspensionForce = r->suspensionDamping = 0;
	}
	CHECK(std::memcmp(&before, &after, sizeof(Record)) == 0);
}

static void TestTopSpeedFactor()
{
	Record inf = Infernus(), bus = Bus();
	const float infBefore = DragTopSpeed(inf), busBefore = DragTopSpeed(bus);
	NEAR(infBefore, 195.6f, 2.0f);
	BeforeConversion(inf, Kind::Sports);
	BeforeConversion(bus, Kind::HeavyTruck);
	NEAR(DragTopSpeed(inf), For(Kind::Sports).top * infBefore, 2.0f);
	NEAR(DragTopSpeed(bus), For(Kind::HeavyTruck).top * busBefore, 2.0f);
}

static void TestRideHeightKept()
{
	for (Record r : {Infernus(), Bus()}) {
		const Record was = r;
		BeforeConversion(r, r.id == 4 ? Kind::Sports : Kind::HeavyTruck);
		NEAR(RideHeight(r), RideHeight(was), 1e-5f);
		NEAR(BumpLeft(r), BumpLeft(was), 1e-5f);
	}
}

static void TestRollSofter()
{
	for (Record r : {Infernus(), Bus()}) {
		const Record was = r;
		const Kind kind = r.id == 4 ? Kind::Sports : Kind::HeavyTruck;
		BeforeConversion(r, kind);
		NEAR(PerMetre(r) / PerMetre(was), For(kind).springs, 1e-4f);
	}
}

static void TestDampingRatio()
{
	for (Record r : {Infernus(), Bus()}) {
		const Record was = r;
		BeforeConversion(r, r.id == 4 ? Kind::Sports : Kind::HeavyTruck);
		NEAR(DampingRatio(r) / DampingRatio(was), 0.85f, 1e-4f);
	}
}

static void TestTank()
{
	Record r = Rhino();
	const Record was = r;
	BeforeConversion(r, Kind::Tank);
	CHECK(r.suspensionForce == was.suspensionForce);
	CHECK(r.suspensionUpper == was.suspensionUpper && r.suspensionLower == was.suspensionLower);
	NEAR(r.suspensionDamping, 0.85f * was.suspensionDamping, 1e-6f);
}

static void TestAfterConversion()
{
	Record r = Infernus();
	r.turnMass = 2982.5f;
	AfterConversion(r);
	CHECK(r.turnMass == 3728.125f);
}

static void TestFind()
{
	CHECK(kCarCount == 71);
	const Car *c = Find("INFERNUS");
	CHECK(c && c->kind == Kind::Sports);
	CHECK((c = Find("BUS")) && c->kind == Kind::HeavyTruck);
	CHECK((c = Find("RHINO")) && c->kind == Kind::Tank);
	CHECK((c = Find("GOLFCART")) && c->kind == Kind::Small);
	for (const char *other : {"RCBANDIT", "DODO", "HELI", "AIRTRAIN", "DEADDODO", "TRAIN", "BIKE", "SEAPLANE", "SPARROW",
	                          "HUNTER"})
		CHECK(Find(other) == nullptr);
}

static void TestPlausible()
{
	CHECK(Plausible(Infernus()));
	Record r = Infernus();
	r.mass = 0;
	CHECK(!Plausible(r));
	r = Infernus();
	r.suspensionUpper = -0.2f;
	CHECK(!Plausible(r));
	r = Infernus();
	r.numberOfGears = 7;
	CHECK(!Plausible(r));
	r = Infernus();
	r.money = -1;
	CHECK(!Plausible(r));
}

int main()
{
	TestLayout();
	TestRetuneValues();
	TestRetuneKeepsTheRest();
	TestTopSpeedFactor();
	TestRideHeightKept();
	TestRollSofter();
	TestDampingRatio();
	TestTank();
	TestAfterConversion();
	TestFind();
	TestPlausible();
	if (failures)
		std::printf("%d failure(s)\n", failures);
	else
		std::printf("handling model: all passed\n");
	return failures ? 1 : 0;
}
