// The street name in the HUD (#35): with a v5 map and StreetNames=1 it draws
// the street over the area, bottom right, and hides the game's own area name;
// with an old map or the setting off, it leaves the game's area name alone.
#include "../src/streethud.cpp"
#include "../src/streetmodel.cpp"
#include <cstdio>
#include <string>

namespace game { Api api{}; }
Config gConfig;
void Log(const char *, ...) {}
static bool gSuppressed;
static mapdata::Map gMapData;
static const mapdata::Map *gBaked;
namespace hud { void SuppressAreaName(bool on) { gSuppressed = on; } }
namespace minimap {
const mapdata::Map *BakedMap() { return gBaked; }
bool Expanded() { return false; }
} // namespace minimap
static std::vector<std::string> gDrawn;
namespace text {
float Width(const char *s, const Style &) { return float(std::strlen(s)) * 10; }
float Width(const uint16_t *s, const Style &) { float w = 0; for (; *s; ++s) w += 10; return w; }
void LineExtent(const Style &, float y, float &top, float &bottom) { top = y - 10, bottom = y; }
void Draw(float, float, const char *s, const Style &) { gDrawn.push_back(s); }
void Draw(float, float, const uint16_t *s, const Style &)
{
	std::string out;
	for (; *s; ++s) out += char(*s);
	gDrawn.push_back(out);
}
void Flush() {}
} // namespace text

static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)

static uint8_t gTommy[0x900], gScreen[16], gCamera[0x100];
static bool gWantsHud = true;
static uint32_t gNow = 1000;
static uint16_t gArea[] = {'O', 'c', 'e', 'a', 'n', ' ', 'B', 'e', 'a', 'c', 'h', 0};
static const uint16_t *gAreaName = gArea;
static uint8_t *Tommy() { return gTommy; }
static void At(float x, float y)
{
	*reinterpret_cast<CVector *>(gTommy + 0x38) = {x, y, 1};
	*reinterpret_cast<CVector *>(gTommy + 0x18) = {1, 0, 0};
}
static void Frames(int n)
{
	for (int i = 0; i < n; ++i) {
		gNow += 33;
		gDrawn.clear();
		streethud::Draw();
	}
}
static bool Drew(const char *what)
{
	for (const std::string &s : gDrawn)
		if (s == what) return true;
	return false;
}

int main()
{
	game::api.FindPlayerPed = Tommy;
	game::api.timeMs = &gNow;
	game::api.rsGlobal = gScreen;
	*reinterpret_cast<int32_t *>(gScreen + 8) = 1920;
	*reinterpret_cast<int32_t *>(gScreen + 0xc) = 1080;
	gConfig.streetNames = true;
	gMapData.streets = {"Ocean Drive", "5th Street"};
	gMapData.roads = {{{0, 0}, {100, 0}, 10, 10, 10}, {{1000, 1000}, {1100, 1000}, 10, 10, 10}};
	gMapData.roadStreet = {0, 1};
	gMapData.roadZ = {0, 0, 0, 0};
	streethud::gZoneName = &gAreaName;
	game::api.theCamera = gCamera;
	streethud::gWantsHud = &gWantsHud;

	// TestSuppressesWhileActive: on Ocean Drive, both lines; the game's own hidden.
	gBaked = &gMapData;
	At(50, 2);
	Frames(10);
	CHECK(gSuppressed && Drew("Ocean Drive") && Drew("Ocean Beach"));

	// TestNoStreetShowsAreaOnly: off every road, a new area: the area line alone.
	At(50, 60);
	Frames(90); // past the 2 s it takes to leave the street
	static uint16_t haiti[] = {'L', 'i', 't', 't', 'l', 'e', ' ', 'H', 'a', 'i', 't', 'i', 0};
	gAreaName = haiti;
	Frames(10);
	CHECK(Drew("Little Haiti") && !Drew("Ocean Drive") && gDrawn.size() == 1);

	// TestTeleportShowsNewStreetAtOnce: a respawn or a script's teleport onto
	// another street: that street, not a second of the old one under it.
	At(50, 2);
	Frames(200);
	At(1050, 1002);
	bool oldShown = false, newShown = false;
	for (int i = 0; i < 15; ++i) { // half a second
		Frames(1);
		oldShown |= Drew("Ocean Drive");
		newShown |= Drew("5th Street");
	}
	CHECK(newShown && !oldShown);

	// TestHiddenHudDrawsNothing: in a cutscene (widescreen), or with the game's HUD off, no banner.
	gCamera[0x75] = 1;
	At(50, 2);
	Frames(20);
	CHECK(gDrawn.empty());
	gCamera[0x75] = 0;
	gWantsHud = false;
	Frames(5);
	CHECK(gDrawn.empty());
	gWantsHud = true;

	// TestOldMapLeavesGameAreaName: no v5 map: nothing drawn, the game's shown.
	gBaked = nullptr;
	Frames(5);
	CHECK(!gSuppressed && gDrawn.empty());

	// TestSettingOffLeavesGameAreaName
	gBaked = &gMapData;
	gConfig.streetNames = false;
	Frames(5);
	CHECK(!gSuppressed && gDrawn.empty());

	if (failures)
		return 1;
	std::puts("streethud: all street HUD tests passed");
}
