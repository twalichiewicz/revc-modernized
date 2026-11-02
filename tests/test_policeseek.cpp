// Police foot-chase pace (#23) through the actual CPed::Seek hook: a cop
// chasing Tommy far behind is raised from the game's run to a sprint (with
// its move animation re-set), lowered to a run within reach, and handed back
// to the game's own pace once it stops chasing. Others keep the game's pace.
#include "../src/police.cpp"
#include <cstdio>
namespace game
{
Api api{};
}
Config gConfig;
void Log(const char *, ...) {}

static uint8_t cop[0x900]{}, player[0x900]{}, civilian[0x900]{};
static int moveAnims, seeks, failures;
static bool seekResult = true;
static void MoveAnim(uint8_t *) { ++moveAnims; }
static void (*vtable[32])(uint8_t *){};
static bool NativeSeek(uint8_t *ped)
{
	++seeks;
	int32_t &move = *reinterpret_cast<int32_t *>(ped + 0x304);
	if (move < 4)
		move = 4; // the game's run for police objectives, only ever raised
	return seekResult;
}
static uint8_t *Player() { return player; }
static int Ref(uint8_t *p) { return p == cop ? 7 << 8 : p == civilian ? 8 << 8 : -1; }
static uint8_t *Lookup(int h) { return h == 7 << 8 ? cop : h == 8 << 8 ? civilian : nullptr; }
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)
static int32_t &Move(uint8_t *p) { return *reinterpret_cast<int32_t *>(p + 0x304); }
static void Place(uint8_t *p, float y) { *reinterpret_cast<CVector *>(p + 0x38) = {0, y, 1}; }
static void Chase(uint8_t *p, int32_t objective)
{
	*reinterpret_cast<int32_t *>(p + 0x19c) = objective;
	*reinterpret_cast<uint8_t **>(p + 0x1a8) = player;
}

int main()
{
	using namespace police;
	vtable[0x98 / 8] = MoveAnim;
	for (uint8_t *p : {cop, civilian}) {
		*reinterpret_cast<void (***)(uint8_t *)>(p) = vtable;
		*reinterpret_cast<float *>(p + 0x44c) = 100;
	}
	*reinterpret_cast<int32_t *>(cop + 0x4f8) = kCop;
	*reinterpret_cast<int32_t *>(civilian + 0x4f8) = 4;
	api.FindPlayerPed = Player;
	api.PoolsGetPed = Lookup;
	gPedRef = Ref;
	orig_Seek = NativeSeek;
	gConfig.smarterPolice = true;
	Place(player, 0);

	// Far behind, chasing: a sprint, its animation re-set; the game's result kept.
	Chase(cop, kKillOnFoot);
	Place(cop, -30);
	Move(cop) = 4;
	seekResult = false;
	CHECK(Hook_Seek(cop) == false && seeks == 1);
	CHECK(Move(cop) == policemove::kMoveSprint && moveAnims == 1);
	seekResult = true;
	CHECK(Hook_Seek(cop) == true && Move(cop) == policemove::kMoveSprint && moveAnims == 1); // stays, no re-set
	// Within reach: the game's run again.
	Place(cop, -5);
	Hook_Seek(cop);
	CHECK(Move(cop) == policemove::kMoveRun && moveAnims == 2);
	// Far again, then no longer chasing (another objective): handed back still,
	// for the game's Seek to re-pick next frame.
	Place(cop, -30);
	Hook_Seek(cop);
	CHECK(Move(cop) == policemove::kMoveSprint);
	*reinterpret_cast<int32_t *>(cop + 0x19c) = 0;
	Hook_Seek(cop);
	CHECK(Move(cop) == policemove::kMoveStill);
	Hook_Seek(cop);
	CHECK(Move(cop) == 4); // the game's own pace, ours forgotten

	// Never raised: a mission cop, one in a vehicle, one chasing someone else,
	// a civilian, a dead cop, or with the police overhaul off.
	for (int kind = 0; kind < 6; ++kind) {
		uint8_t *p = kind == 3 ? civilian : cop;
		Chase(p, kKillAnyMeans);
		Place(p, -30);
		Move(p) = 4;
		p[0x19a] = kind == 0 ? 2 : 0;
		p[0x4c0] = kind == 1;
		if (kind == 2)
			*reinterpret_cast<uint8_t **>(p + 0x1a8) = civilian;
		*reinterpret_cast<float *>(p + 0x44c) = kind == 4 ? 0 : 100;
		gConfig.smarterPolice = kind != 5;
		Hook_Seek(p);
		CHECK(Move(p) == 4);
		p[0x19a] = 0;
		p[0x4c0] = 0;
		*reinterpret_cast<float *>(p + 0x44c) = 100;
	}
	gConfig.smarterPolice = true;
	if (failures)
		return 1;
	std::puts("policeseek: all police chase-pace hook tests passed");
}
