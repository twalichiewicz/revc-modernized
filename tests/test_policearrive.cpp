// Police arriving where Tommy is on foot (#21), through the actual hook after
// CCarAI::MellowOutChaseSpeed: a chasing police car's cruise speed is lowered
// onto the braking curve to PullUp's line, the game's own slower speed kept;
// ramming his car, mission cars, other cars and other missions untouched.
#include "../src/police.cpp"
#include <cstdio>
namespace game
{
Api api{};
}
Config gConfig;
void Log(const char *, ...) {}

static uint8_t car[0x6f0]{}, cop[0x900]{}, civilian[0x900]{}, player[0x900]{};
static int failures, mellows;
static uint8_t nativeCruise = 60;
static void NativeMellow(uint8_t *c)
{
	++mellows;
	c[0x1a8] = nativeCruise; // the game's star-level chase speed, as UpdateCarAI left it
}
static uint8_t *Player() { return player; }
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)
static void Place(uint8_t *e, float y) { *reinterpret_cast<CVector *>(e + 0x38) = {0, y, 1}; }
static void Reset(float gap)
{
	car[0x295] = 1;                   // law enforcer
	car[0x294] = 1;                   // created at random
	car[0x19e] = 3;                   // a chase mission (ram/block 2..6)
	*reinterpret_cast<uint8_t **>(car + 0x218) = cop;
	player[0x4c0] = 0;                // on foot
	gConfig.smarterPolice = true;
	nativeCruise = 60;
	Place(car, -gap);
}

int main()
{
	using namespace police;
	*reinterpret_cast<int32_t *>(cop + 0x4f8) = kCop;
	*reinterpret_cast<int32_t *>(civilian + 0x4f8) = 4;
	api.FindPlayerPed = Player;
	orig_MellowOutChaseSpeed = NativeMellow;
	Place(player, 0);

	// Close in on him: on the braking curve, below the game's chase speed.
	Reset(40);
	Hook_MellowOutChaseSpeed(car);
	CHECK(mellows == 1 && car[0x1a8] == policedrive::Limit(60, 40) && car[0x1a8] < 60);
	// At the line: the arrival speed; far away: the game's speed; the game's
	// own slower speed (MellowOutChaseSpeed's caps) is never raised.
	Reset(policedrive::kArriveAt);
	Hook_MellowOutChaseSpeed(car);
	CHECK(car[0x1a8] == policedrive::CruiseFor(policedrive::kArriveSpeed));
	Reset(400);
	Hook_MellowOutChaseSpeed(car);
	CHECK(car[0x1a8] == 60);
	Reset(40);
	nativeCruise = 5;
	Hook_MellowOutChaseSpeed(car);
	CHECK(car[0x1a8] == 5);

	// Untouched: him in a car (they ram it), a mission car, not a police car,
	// a non-cop driver, no driver, a mission outside the chase, the overhaul off.
	for (int kind = 0; kind < 7; ++kind) {
		Reset(40);
		switch (kind) {
		case 0: player[0x4c0] = 1; break;
		case 1: car[0x294] = 2; break;
		case 2: car[0x295] = 0; break;
		case 3: *reinterpret_cast<uint8_t **>(car + 0x218) = civilian; break;
		case 4: *reinterpret_cast<uint8_t **>(car + 0x218) = nullptr; break;
		case 5: car[0x19e] = 10; break;
		case 6: gConfig.smarterPolice = false; break;
		}
		Hook_MellowOutChaseSpeed(car);
		CHECK(car[0x1a8] == 60);
	}
	if (failures)
		return 1;
	std::puts("policearrive: all police arrival-speed hook tests passed");
}
