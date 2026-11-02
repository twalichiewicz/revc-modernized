// Business hints and buying on credit (#36), over fake game memory: the game
// loads main.scm into its script space only when a game starts or loads, after
// the mod installs, so whether it's the one their script variables are for is
// checked as they're used, not at startup. Another main.scm: neither does
// anything, and a business sells for its full price.
#include "../src/story.cpp"
#include "../src/credit.cpp"
#include <cstdarg>
#include <string>
#include <vector>

namespace game { Api api{}; }
Config gConfig;
static std::vector<std::string> gLogs;
void Log(const char *format, ...)
{
	char line[512];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof line, format, args);
	va_end(args);
	gLogs.push_back(line);
}
namespace hud { void FormatMoney(int32_t money, char *out, size_t size) { snprintf(out, size, "$%d", money); } }
namespace phone { bool Text(const char *, const char *, const char *) { return true; } }
static uint8_t gImage[16];
static int gNativeBuys;
static int32_t gPriceSeen;
static bool NativePickupUpdate(uint8_t *pickup, uint8_t *, uint8_t *, int);
static void NativeHelp(uint16_t *, bool, bool) {}
namespace hooks {
void *Install(const char *name, void *)
{
	if (!std::strcmp(name, "_ZN7CPickup6UpdateEP10CPlayerPedP8CVehiclei"))
		return reinterpret_cast<void *>(NativePickupUpdate);
	if (!std::strcmp(name, "_ZN4CHud14SetHelpMessageEPtbb"))
		return reinterpret_cast<void *>(NativeHelp);
	return nullptr;
}
const void *ImageBase() { return gImage; }
} // namespace hooks
// CTheScripts::IsPlayerOnAMission, as story::Install binds it.
struct CTheScripts { static bool IsPlayerOnAMission(); };
bool CTheScripts::IsPlayerOnAMission() { return false; }

static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)

alignas(8) static uint8_t gScript[0x30000], gPickups[game::kNumPickups * 64], gPlayers[0x1a0], gFocus;
static uint8_t gTommy[0x900];
static uint32_t gNow = 1000;
static uint8_t *Tommy() { return gTommy; }
static int32_t &Money() { return *reinterpret_cast<int32_t *>(gPlayers + 0xc8); }
// The game's own purchase: takes the price when he has it.
static bool NativePickupUpdate(uint8_t *pickup, uint8_t *, uint8_t *, int)
{
	++gNativeBuys;
	gPriceSeen = *reinterpret_cast<int32_t *>(pickup + 0x20);
	if (Money() >= gPriceSeen) {
		Money() -= gPriceSeen;
		return true;
	}
	return false;
}
static void LoadMainScm(bool ours)
{
	std::memset(gScript, 0, sizeof gScript);
	std::memcpy(gScript + story::kCheckAt, story::kCheck, sizeof story::kCheck);
	if (!ours)
		gScript[story::kCheckAt + 3] ^= 0xff;
}
// Sunshine Autos' for-sale sign, at $50,000.
static uint8_t *Sign()
{
	uint8_t *p = gPickups;
	*reinterpret_cast<CVector *>(p) = {-1007.3f, -869.9f, 10};
	p[0x36] = game::kPickupPropertyForSale;
	*reinterpret_cast<int32_t *>(p + 0x20) = 50000;
	return p;
}

int main()
{
	using namespace game;
	api.scriptSpace = gScript; // as at startup: empty, the title screen's
	api.pickups = gPickups;
	api.worldPlayers = gPlayers;
	api.playerInFocus = &gFocus;
	api.timeMs = &gNow;
	api.FindPlayerPed = Tommy;
	gConfig.credit = gConfig.storyHints = true;

	// Startup: main.scm isn't loaded yet. Both still install.
	CHECK(story::Install());
	CHECK(credit::Install());
	CHECK(!story::Active());
	for (const std::string &line : gLogs)
		CHECK(line.find("hooks missing") == std::string::npos && line.find("not the main.scm") == std::string::npos);

	// A game loads with this main.scm: credit works.
	LoadMainScm(true);
	CHECK(story::Active());
	uint8_t *sign = Sign();
	Money() = 20000; // a quarter's 12,500: enough to buy on credit
	credit::Hook_PickupUpdate(sign, gTommy, nullptr, 0);
	CHECK(gPriceSeen == 20000 && Money() == 0 && credit::Owed() == 30000);
	CHECK(*reinterpret_cast<int32_t *>(sign + 0x20) == 50000); // the sign's price, as it was
	// Paid off from the business's income.
	uint8_t *revenue = gPickups + 64;
	revenue[0x36] = 16; // asset revenue
	*reinterpret_cast<float *>(revenue + 0xc) = 30000;
	credit::Update();
	CHECK(credit::Owed() == 0);

	// Another main.scm: its variables aren't these. No credit: the full price.
	LoadMainScm(false);
	CHECK(!story::Active());
	ScriptGlobal(credit::kVarOwed) = 777;
	CHECK(credit::Owed() == 0);
	Money() = 20000;
	credit::Hook_PickupUpdate(sign, gTommy, nullptr, 0);
	CHECK(gPriceSeen == 50000 && Money() == 20000);
	gLogs.clear();
	gNow += 100;
	story::Update();
	story::Update();
	CHECK(gLogs.size() == 1 && gLogs[0].find("not the main.scm") != std::string::npos); // said once

	if (failures)
		return 1;
	std::puts("storycredit: all business hint and credit tests passed");
}
