// Police cars pulling up where Tommy is on foot (#21, #22), through the actual
// PullUp over a native-shaped vehicle pool: a car stuck in the game's
// ram-reverse-ram loop near him lets its crew out even unseen; a fence or
// railing between them counts as hidden; the whole close chase (50 m) is
// covered; and a car stops straight, never in a handbrake swing at speed.
#include "../src/police.cpp"
#include <cstdio>
namespace game
{
Api api{};
}
Config gConfig;
void Log(const char *, ...) {}

static uint8_t car[0x6f0]{}, cop[0x900]{}, player[0x900]{}, flags[1]{}, pool[0x18]{};
static uint8_t *poolPointer = pool;
static int failures, leaves;
static bool fenceBetween;
static uint8_t *Player() { return player; }
static int Ref(uint8_t *v) { return v == car ? 5 << 8 : -1; }
static uint8_t *Vehicle(int h) { return h == 5 << 8 ? car : nullptr; }
static void Leave(uint8_t *) { ++leaves; }
static uint8_t Chase() { return 3; }
static int32_t ChaseSpeed(uint8_t *) { return 77; } // distinct from Reset's 30
// Buildings and walls are never in the way here; a fence is, unless see-through is ignored.
static bool Sight(const CVector &, const CVector &, bool, bool, bool, bool, bool, bool ignoreSeeThrough, bool)
{
	return !(fenceBetween && !ignoreSeeThrough);
}
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)
static void Reset(float gap, float speed)
{
	using namespace police;
	for (Stopping &s : gStopping)
		s = {};
	car[kStatusByte] = uint8_t(2 | kPhysics << 3);
	car[kLawFlags] = 1;
	car[kCreatedBy] = kRandomVehicle;
	car[kMission] = 3;
	car[kTempAction] = 0;
	car[kCruiseSpeed] = 30;
	*reinterpret_cast<uint8_t **>(car + kDriver) = cop;
	*reinterpret_cast<CVector *>(car + 0x38) = {0, -gap, 1};
	game::EntitySpeed(car) = {0, speed / 50, 0};
	player[0x4c0] = 0;
	fenceBetween = false;
	leaves = 0;
}
static bool PulledUp() { return car[police::kMission] == police::kEmergencyStop; }

int main()
{
	using namespace police;
	*reinterpret_cast<uint8_t **>(pool) = car;
	*reinterpret_cast<uint8_t **>(pool + 0x8) = flags;
	*reinterpret_cast<int32_t *>(pool + 0x10) = 1;
	gVehiclePool = &poolPointer;
	*reinterpret_cast<int32_t *>(cop + 0x4f8) = kCop;
	api.FindPlayerPed = Player;
	api.PoolsGetVehicle = Vehicle;
	api.GetIsLineOfSightClear = Sight;
	gVehicleRef = Ref;
	gLeaveCar = Leave;
	gChaseMission = Chase;
	gChaseSpeed = ChaseSpeed;
	*reinterpret_cast<CVector *>(player + 0x38) = {0, 0, 1};
	const CVector him{0, 0, 1};

	// In plain view 40 m off, nothing between: it keeps closing in (unchanged).
	Reset(40, 12);
	PullUp(player, him, true, 1000);
	CHECK(!PulledUp());
	// A fence or railing between them: it can't drive through, so it pulls up.
	Reset(40, 12);
	fenceBetween = true;
	PullUp(player, him, true, 1000);
	CHECK(PulledUp());
	// Hidden at 48 m: inside the game's 50 m close chase, so it pulls up too.
	Reset(48, 12);
	fenceBetween = true;
	PullUp(player, him, true, 1000);
	CHECK(PulledUp());
	Reset(55, 12);
	fenceBetween = true;
	PullUp(player, him, true, 1000);
	CHECK(!PulledUp());
	// Stuck: the game reversing it off a wall near where they think he is.
	// It pulls up even with nobody seeing him, and its crew get out once stopped.
	Reset(30, 4);
	car[kTempAction] = 2;
	PullUp(player, him, false, 1000);
	CHECK(PulledUp());
	game::EntitySpeed(car) = {};
	PullUp(player, him, false, 1100);
	CHECK(leaves == 1);
	Reset(80, 4);
	car[kTempAction] = 2;
	PullUp(player, him, false, 1000);
	CHECK(!PulledUp()); // far off: still finding its way
	// Pulled up at speed: it stops straight, no handbrake swing (#21).
	Reset(20, 25);
	PullUp(player, him, true, 1000);
	CHECK(PulledUp() && car[kTempAction] == 0);
	// Him in a car: they keep after it.
	Reset(20, 12);
	player[0x4c0] = 1;
	car[kTempAction] = 2;
	PullUp(player, him, true, 1000);
	CHECK(!PulledUp() && car[kCruiseSpeed] == 30); // never stopped, so never handed back
	if (failures)
		return 1;
	std::puts("policepullup: all police pull-up tests passed");
}
