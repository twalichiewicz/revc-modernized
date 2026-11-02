// Achievements (#24), the native side, over fake game memory shaped as this
// game's: values read where the game keeps them, unlocks after a load settles
// and saved, nothing while cheated, and the five mission conditions the game
// keeps no record of, watched live through their own scripts.
#include "../src/achievements.cpp"
#include "../src/achievementsmodel.cpp"
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
namespace game { Api api{}; }
Config gConfig;
static int logs;
void Log(const char *, ...) { ++logs; }
static std::string gHome;
std::string SupportDir() { return gHome; }
static bool storyKnows = true; // main.scm is the one the globals are for
namespace story { bool ScriptKnown() { return storyKnows; } }

static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)

// CStats and friends.
static int32_t vigilante, ambulance, wheelie, taxi, fires, destroyed, owned, jumps, stars, heads, cheated, scores[16],
    imports[4];
static float pizzas, stores, chase;
static int32_t rating = 0;
static float percent = 0;
static int32_t Rating() { return rating; }
static float Percent() { return percent; }
alignas(8) static uint8_t music[0x3a00], scriptSpace[40000], players[0x1a0 * 2], focus;
static uint8_t tommy[0x900], wantedBlock[0x40], car[0x6f0], handling[0x100], model0[0x40], gonzalez[0x900],
    diaz[0x900], goblin[0x6f0], hostiles[16][0x900];
static uint8_t *model0Ptr[1] = {model0};
static uint32_t now = 100000;
// The running scripts, a list linked at +0: name at +0x10, locals at +0x38.
static uint8_t script[0x200];
static uint8_t *activeScripts;
static void Run(const char *name)
{
	std::memset(script, 0, sizeof script);
	std::strncpy(reinterpret_cast<char *>(script + 0x10), name, 8);
	activeScripts = script;
}
static void Stop() { activeScripts = nullptr; }
static int32_t &Var(int i) { return *reinterpret_cast<int32_t *>(scriptSpace + 4 * i); }
static uint8_t *Tommy() { return tommy; }
static uint8_t *Ped(int h)
{
	if (h == 11) return gonzalez;
	if (h == 12) return diaz;
	if (h >= 100 && h < 116) return hostiles[h - 100];
	return h == 0 ? tommy : nullptr; // handle 0 is someone too: an unset variable must never match
}
static uint8_t *Vehicle(int h) { return h == 50 ? goblin : nullptr; }
static void Tick(int frames = 1) // each a poll: Update reads every 250 ms
{
	for (int i = 0; i < frames; ++i) {
		now += 300;
		achievements::Update();
	}
}
static bool Has(achievements::Id id) { return achievements::Current().Unlocked(id); }
static void Reset()
{
	using namespace achievements;
	gRecord = {};
	gNotices.clear();
	gSpotlight = {};
	gConvoy = {};
	gDemolition = {};
	std::memset(scriptSpace, 0, sizeof scriptSpace);
	cheated = 0;
	Stop();
	now += 20000; // a jump, as a load is: it settles first
	Tick();
	now += 4000;
}

int main()
{
	using namespace achievements;
	gHome = "/private/tmp/claude-501/achievements-test";
	mkdir("/private/tmp/claude-501", 0700);
	mkdir(gHome.c_str(), 0700);
	mkdir((gHome + "/vcmodernhud").c_str(), 0700);
	Native &n = gNative;
	n.vigilante = &vigilante; n.ambulance = &ambulance; n.longestWheelie = &wheelie; n.taxiFares = &taxi;
	n.fires = &fires; n.propertyDestroyed = &destroyed; n.propertiesOwned = &owned; n.uniqueJumps = &jumps;
	n.starsAttained = &stars; n.headshots = &heads; n.cheated = &cheated; n.highestScores = scores;
	n.importLists = imports; n.pizzas = &pizzas; n.stores = &stores; n.chase = &chase; n.musicManager = music;
	n.activeScripts = &activeScripts; n.criminalRating = Rating; n.percent = Percent;
	api.timeMs = &now;
	api.FindPlayerPed = Tommy;
	api.PoolsGetPed = Ped;
	api.PoolsGetVehicle = Vehicle;
	api.scriptSpace = scriptSpace;
	api.worldPlayers = players;
	api.playerInFocus = &focus;
	api.modelInfoPtrs = model0Ptr;
	gConfig.achievements = true;
	gInstalled = gScriptKnown = true;
	*reinterpret_cast<uint8_t **>(tommy + 0x778) = wantedBlock;
	std::strcpy(reinterpret_cast<char *>(model0 + 8), "PLAYER");
	*reinterpret_cast<CVector *>(tommy + 0x38) = {0, 0, 20};

	// Read: the game's values where it keeps them.
	Reset();
	vigilante = 7; scores[3] = 4; imports[2] = 0x7f; pizzas = 3; rating = 12345; percent = 41.5f;
	reinterpret_cast<float *>(music + 0x39a0)[4] = 61000; // ms
	*reinterpret_cast<int32_t *>(players + 0xd0) = 61;
	*reinterpret_cast<int32_t *>(wantedBlock + 0x20) = 4;
	Var(227) = 1; Var(298) = 1; Var(302) = 1;
	std::strcpy(reinterpret_cast<char *>(model0 + 8), "Play11");
	Facts f = Read(tommy, now);
	CHECK(f.valid && f.vigilanteLevel == 7 && f.keepieUppy == 4 && f.importLists[2] == 0x3f && f.pizzas == 3);
	CHECK(f.criminalRating == 12345 && f.percent == 41.5f && f.radioSeconds[4] == 61 && f.packages == 61);
	CHECK(f.wantedLevel == 4 && f.riot && f.contracts[0] && !f.contracts[1] && f.contracts[4] && f.outfit == 10);
	gScriptKnown = false;
	CHECK(!Read(tommy, now).riot); // another main.scm: its globals aren't these
	gScriptKnown = true;
	// The Deluxo flat out: its handling's top speed (game units), 95% of it.
	tommy[0x4c0] = 1;
	*reinterpret_cast<uint8_t **>(tommy + 0x4b8) = car;
	*reinterpret_cast<int16_t *>(car + 0x6a) = 211;
	*reinterpret_cast<uint8_t **>(car + 0x160) = handling;
	*reinterpret_cast<float *>(handling + 0x84) = 300.0f / 180;
	EntitySpeed(car) = {0, 300.0f / 180 * .96f, 0};
	CHECK(Read(tommy, now).deluxoAtTopSpeed);
	EntitySpeed(car) = {0, 300.0f / 180 * .9f, 0};
	CHECK(!Read(tommy, now).deluxoAtTopSpeed);
	*reinterpret_cast<int16_t *>(car + 0x6a) = 210;
	EntitySpeed(car) = {0, 9, 0};
	CHECK(!Read(tommy, now).deluxoAtTopSpeed);
	tommy[0x4c0] = 0;
	vigilante = 0; scores[3] = 0; imports[2] = 0; pizzas = 0; rating = 0; percent = 0;
	reinterpret_cast<float *>(music + 0x39a0)[4] = 0;
	*reinterpret_cast<int32_t *>(wantedBlock + 0x20) = 0;
	std::strcpy(reinterpret_cast<char *>(model0 + 8), "PLAYER");

	// Update: a load settles first; then the save's progress unlocks, is saved, and notices.
	Reset();
	*reinterpret_cast<int32_t *>(players + 0xd0) = 100;
	now += 20000; // load
	Tick();
	CHECK(!Has(kCitySleuth));
	Tick(12); // 3.6 s
	CHECK(Has(kCitySleuth) && Has(kScouringTheCity));
	Shown shown;
	CHECK(Notice(shown) && shown.count == 1 && shown.def == &Definition(kCitySleuth)); // in the list's order
	Tick(17); // 5.1 s
	CHECK(Notice(shown) && shown.def == &Definition(kScouringTheCity) && shown.age == 0); // then the next
	Tick(17); // 5.1 s
	CHECK(!Notice(shown));
	std::ifstream saved(RecordPath());
	CHECK(Load(std::string((std::istreambuf_iterator<char>(saved)), {})).Unlocked(kCitySleuth));
	// Cheated: nothing more unlocks.
	cheated = 1;
	jumps = 36;
	Tick(5);
	CHECK(!Has(kDaredevil));
	cheated = 0;
	Tick(5);
	CHECK(Has(kDaredevil));
	jumps = 0;
	*reinterpret_cast<int32_t *>(players + 0xd0) = 0;
	// A save that meets many at once: one notice, all of them counted.
	Reset();
	vigilante = ambulance = 12;
	fires = 10;
	Tick();
	CHECK(Has(kRunningRampant) && Has(kWambulance) && Has(kHighQualityH2O));
	CHECK(Notice(shown) && shown.count == 3 && shown.def == &Definition(kRunningRampant));
	Tick(17); // 5.1 s
	CHECK(!Notice(shown));
	vigilante = ambulance = fires = 0;

	// main.scm isn't loaded at startup (the title screen): it's checked once a game is.
	Reset();
	gScriptKnown = false; // as Install, at the title screen, found it
	Var(227) = 1;         // Riot passed
	Tick();
	CHECK(Has(kLegalCounsel));
	// Another main.scm: its globals aren't these, so none of them count.
	Reset();
	storyKnows = false;
	Var(227) = 1;
	Tick(3);
	CHECK(!Has(kLegalCounsel));
	storyKnows = true;
	Var(227) = 0;

	// Iron-y: Four Iron (serg1), Gonzalez ($3166) killed by Tommy with a golf club.
	Reset();
	Damaged(gonzalez, tommy, 3, true);
	CHECK(!Has(kIrony)); // the mission isn't running
	Run("serg1");
	Var(3166) = 11;
	Damaged(gonzalez, tommy, 17, true);
	Damaged(gonzalez, tommy, 3, false);
	CHECK(!Has(kIrony)); // a Colt; not fatal
	Damaged(gonzalez, tommy, 3, true);
	CHECK(Has(kIrony));
	Reset();
	Run("serg1"); // Gonzalez not created yet: his variable's 0, and handle 0 is Tommy
	Damaged(tommy, tommy, 3, true);
	CHECK(!Has(kIrony));

	// Salutations: Rub Out (baron5), Diaz (its local 0) taken out with the M4.
	Reset();
	Run("baron5");
	*reinterpret_cast<int32_t *>(script + 0x38) = 12;
	Damaged(diaz, tommy, 22, true);
	CHECK(!Has(kSalutations));
	Damaged(diaz, tommy, 26, true);
	CHECK(Has(kSalutations));

	// Chopper'd Up: Demolition Man (serg3), all 16 hostiles by the RC Goblin's blades.
	// spoil: -1 none; 0 one killed by Tommy; 1 the mission restarted mid-run;
	// 2 one killed by the Goblin, but not its blades (an explosion credited to it).
	for (int spoil = -1; spoil < 3; ++spoil) {
		Reset();
		Run("serg3");
		for (int i = 0; i < 16; ++i) Var(3311 + i) = 100 + i;
		Var(3327) = 50;
		for (int i = 0; i < 16; ++i) {
			const bool byTommy = spoil == 0 && i == 7, blast = spoil == 2 && i == 7;
			Damaged(hostiles[i], byTommy ? tommy : goblin, byTommy || blast ? 41 : 40, true);
			if (spoil == 1 && i == 7) { // the mission restarted after a failure: a fresh tally
				Stop();
				Tick();
				Run("serg3");
			}
		}
		CHECK(Has(kChopperdUp) == (spoil == -1));
	}

	// Not my First Time: G-Spotlight (porno4), its course's rooftop stages 7-25.
	for (bool fall : {false, true}) {
		Reset();
		Run("porno4");
		Var(4525) = 10;
		Tick();
		*reinterpret_cast<CVector *>(tommy + 0x38) = {0, 0, fall ? 11.0f : 22.0f};
		Tick();
		*reinterpret_cast<CVector *>(tommy + 0x38) = {0, 0, 22};
		Var(4525) = 26;
		Var(260) = 1; // passed
		Tick();
		CHECK(Has(kNotMyFirstTime) == !fall);
	}

	// As main.scm passes it: $260 set and the script terminated in one tick (no WAIT
	// between), so no poll sees porno4 running with it set: seen as the script's end.
	Reset();
	Run("porno4");
	Var(4525) = 20;
	Tick();
	Var(260) = 1;
	Stop();
	Tick();
	CHECK(Has(kNotMyFirstTime));
	// Failed (Tommy wasted or busted): the script ends with $260 still 0.
	Reset();
	Run("porno4");
	Tick();
	Stop();
	Tick(3);
	CHECK(!Has(kNotMyFirstTime));
	// Fell and failed, then tried again cleanly: the retry's own.
	Reset();
	Run("porno4");
	Var(4525) = 12;
	*reinterpret_cast<CVector *>(tommy + 0x38) = {0, 0, 10};
	Tick();
	Stop(); // failed
	Tick();
	*reinterpret_cast<CVector *>(tommy + 0x38) = {0, 0, 22};
	Run("porno4");
	Var(4525) = 12;
	Tick();
	Var(260) = 1;
	Stop();
	Tick();
	CHECK(Has(kNotMyFirstTime));
	Var(260) = 0;
	// A save loaded mid-mission, one where G-Spotlight is passed: porno4 is gone and
	// $260 reads 1, but it wasn't passed now.
	Reset();
	Run("porno4");
	Tick();
	Stop();
	Var(260) = 1;
	now += 20000; // the load
	Tick(15);
	CHECK(!Has(kNotMyFirstTime));
	Var(260) = 0;

	// Baked Distractions: Sir, Yes Sir! (col_4), in the tank ($2519) before the
	// army's alerted ($2525; the theft itself sets it just after).
	for (bool alerted : {false, true}) {
		Reset();
		Run("col_4");
		Tick();
		if (alerted) {
			Var(2525) = 1;
			Tick();
		}
		Var(2519) = 1;
		Tick();
		Var(2525) = 1;
		Tick();
		CHECK(Has(kBakedDistractions) == !alerted);
	}

	if (failures)
		return 1;
	std::puts("achievements: all native achievement tests passed");
}
