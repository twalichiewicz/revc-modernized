// The car handling retune (#37) through its hook on the game's conversion
// (cHandlingDataMgr::ConvertDataToGameUnits): what the game's own conversion is
// handed, and what comes out.
#include "../src/config.h"
#include "../src/game.h"
#include <cmath>
#include <cstdio>
#include <cstring>
[[maybe_unused]] static void *TestSymbol(void *, const char *);
#define dlsym TestSymbol
#include "../src/handling.cpp"
#undef dlsym

namespace game { Api api{}; }
Config gConfig;
static int gLogs;
void Log(const char *, ...) { ++gLogs; }

using handlingmodel::Record;
static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)

// The game's conversion, as far as these tests need it: it saw what it was handed.
static int gConverts;
static Record gHanded;
static void NativeConvert(void *, Record *r)
{
	++gConverts;
	gHanded = *r;
	r->invMass = 1.0f / r->mass;
	r->turnMass = (r->dimension[0] * r->dimension[0] + r->dimension[1] * r->dimension[1]) * r->mass / 12.0f;
}
static const struct {
	const char *name;
	int32_t id;
} kIds[] = {{"INFERNUS", 4}, {"BUS", 58}, {"DODO", 90}, {"RCBANDIT", 99}};
static int32_t FakeGetHandlingId(void *, const char *name)
{
	for (const auto &e : kIds)
		if (!std::strcmp(e.name, name))
			return e.id;
	return name[0] == 'A' ? 70 : 106; // a few others found, the rest not (it returns NUMHANDLINGS)
}
static void *TestSymbol(void *, const char *name)
{
	if (!std::strcmp(name, "_ZN16cHandlingDataMgr13GetHandlingIdEPKc"))
		return reinterpret_cast<void *>(FakeGetHandlingId);
	return nullptr;
}
static int gChains;
static void *gConvertHook;
namespace hooks {
void *Chain(const char *name, void *replacement)
{
	++gChains;
	if (!std::strcmp(name, "_ZN16cHandlingDataMgr22ConvertDataToGameUnitsEP13tHandlingData")) {
		gConvertHook = replacement;
		return reinterpret_cast<void *>(NativeConvert);
	}
	return nullptr;
}
} // namespace hooks

static Record Infernus()
{
	Record r{};
	r.id = 4;
	r.mass = 1500;
	r.dimension[0] = 1.9f, r.dimension[1] = 4.5f, r.dimension[2] = 1.0f;
	r.percentSubmerged = 75;
	r.tractionMultiplier = 0.85f, r.tractionLoss = 0.70f, r.tractionBias = 0.48f;
	r.numberOfGears = 5, r.maxVelocity = 300, r.engineAcceleration = 12.4f, r.driveType = 'R', r.engineType = 'P';
	r.brakeDeceleration = 4.5f, r.brakeBias = 0.65f, r.steeringLock = 30;
	r.suspensionForce = 1.10f, r.suspensionDamping = 0.14f, r.suspensionUpper = 0.15f, r.suspensionLower = -0.15f;
	r.suspensionBias = 0.5f, r.antiDive = 0.5f, r.money = 95000, r.rearLights = 1;
	return r;
}
static Record With(int32_t id)
{
	Record r = Infernus();
	r.id = id;
	return r;
}
using Convert = void (*)(void *, Record *);
static void Run(Record &r) { reinterpret_cast<Convert>(gConvertHook)(nullptr, &r); }

static void TestOffInstallsNothing()
{
	gConfig.carHandling = false;
	CHECK(handling::Install());
	CHECK(gChains == 0);
	gConfig.carHandling = true;
}

static void TestRetunesCars()
{
	for (int32_t id : {4, 58}) {
		const handlingmodel::Kind kind = id == 4 ? handlingmodel::Kind::Sports : handlingmodel::Kind::HeavyTruck;
		Record r = With(id), want = With(id);
		handlingmodel::BeforeConversion(want, kind);
		const int converts = gConverts;
		Run(r);
		CHECK(gConverts == converts + 1);
		CHECK(std::memcmp(&gHanded, &want, sizeof(Record)) == 0); // the game converts the retuned values
		NativeConvert(nullptr, &want);
		CHECK(r.turnMass == want.turnMass * 1.25f);
	}
	CHECK(handling::RetunedCount() == 2);
	CHECK(handling::NameOf(4) && !std::strcmp(handling::NameOf(4), "INFERNUS"));
	CHECK(handling::NameOf(90) == nullptr);
}

static void TestOthersUntouched()
{
	for (int32_t id : {90, 99, 104}) {
		Record r = With(id), in = r;
		Run(r);
		CHECK(std::memcmp(&gHanded, &in, sizeof(Record)) == 0);
		NativeConvert(nullptr, &in);
		CHECK(std::memcmp(&r, &in, sizeof(Record)) == 0);
	}
}

static void TestReconvertFromFresh()
{
	Record a = Infernus(), b = Infernus();
	Run(a);
	Run(b);
	CHECK(std::memcmp(&a, &b, sizeof(Record)) == 0);
	CHECK(handling::RetunedCount() == 2); // still two cars
}

static void TestImplausiblePassesThrough()
{
	Record r = With(70); // a car (FakeGetHandlingId's 'A' names) not yet seen
	r.mass = 0;
	const Record in = r;
	gLogs = 0;
	Run(r);
	CHECK(std::memcmp(&gHanded, &in, sizeof(Record)) == 0);
	CHECK(gLogs == 1);
	CHECK(handling::RetunedCount() == 2);
}

int main()
{
	TestOffInstallsNothing();
	gLogs = 0;
	CHECK(handling::Install());
	CHECK(gConvertHook != nullptr);
	TestRetunesCars();
	TestOthersUntouched();
	TestReconvertFromFresh();
	TestImplausiblePassesThrough();
	if (failures)
		std::printf("%d failure(s)\n", failures);
	else
		std::printf("handling hook: all passed\n");
	return failures ? 1 : 0;
}
