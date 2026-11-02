#include "achievements.h"
#include "config.h"
#include "game.h"
#include "log.h"
#include "story.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <fstream>
#include <iterator>
#include <string>

using namespace game;

namespace achievements {
namespace {

// Where each value lives in this game (docs/research/2026-11-02-achievements-native-map.md).
struct Native {
	const int32_t *vigilante = nullptr, *ambulance = nullptr, *longestWheelie = nullptr, *taxiFares = nullptr,
	              *fires = nullptr, *propertyDestroyed = nullptr, *propertiesOwned = nullptr, *uniqueJumps = nullptr,
	              *starsAttained = nullptr, *headshots = nullptr, *cheated = nullptr, *highestScores = nullptr,
	              *importLists = nullptr;
	const float *pizzas = nullptr, *stores = nullptr, *chase = nullptr;
	const uint8_t *musicManager = nullptr; // listen times (ms) at +0x39a0, float[10]
	uint8_t *const *activeScripts = nullptr;
	int32_t (*criminalRating)() = nullptr;
	float (*percent)() = nullptr;
};
Native gNative;
bool gInstalled;
// main.scm is in the script space only once a game's started or loaded (at
// startup, the title screen, it isn't): checked at every poll, not at Install.
bool gScriptKnown;
int gScriptLogged = -1;

// Script globals (this main.scm only: story::ScriptKnown).
constexpr int kVarContracts = 298, kVarRiot = 227, kVarAllHands = 233, kVarSupplyDemand = 238, kVarTrojanVoodoo = 282,
              kVarPublicityTour = 290, kVarHogTied = 277, kVarKeepFriendsClose = 270, kVarCherryPopper = 612,
              kVarRcRaceWon = 8243;
// Mission conditions: their scripts and the script's own variables.
constexpr int kVarGonzalez = 3166;                          // serg1 "Four Iron"
constexpr int kVarHostiles = 3311, kHostiles = 16, kVarRcGoblin = 3327; // serg3 "Demolition Man"
constexpr int kVarSpotlightStage = 4525, kVarSpotlightPassed = 260;  // porno4 "G-Spotlight"
constexpr int kVarArmyAlerted = 2525, kVarInTheTank = 2519;          // col_4 "Sir, Yes Sir!"
constexpr int kWeaponGolfClub = 3, kWeaponM4 = 26, kWeaponBlades = 40;
constexpr float kSpotlightStreet = 13.0f; // m: below the lowest rooftop checkpoint (16.6), the street (10)
constexpr int kDeluxo = 211;
constexpr float kDeluxoTopSpeed = 0.95f; // of its handling's fMaxVelocity
constexpr const char *kOutfitNames[kOutfits] = {"player", "player2", "player3", "player4", "player5", "player6",
                                               "player7", "player8", "player9", "play10", "play11", "play12"};
constexpr uint32_t kPollMs = 250, kSettleMs = 3000, kNoticeMs = 5000;

Record gRecord;
uint32_t gLastPoll, gLastNow, gSettledAt;
// Three or more at once (a save loaded that already meets them) is one notice, not minutes of them.
constexpr size_t kBurst = 3;
struct Pending { Id id; int count; uint32_t since; };
std::vector<Pending> gNotices;
// Mission latches.
struct {
	bool fell = false, watching = false; int passed = 0;
} gSpotlight;
struct {
	bool alerted = false, watching = false; int inTank = 0;
} gConvoy;
struct {
	bool watching = false;
	int8_t kill[kHostiles] = {}; // 0 alive, 1 by the blades, -1 another way
} gDemolition;

std::string RecordPath() { return SupportDir() + "/vcmodernhud/achievements"; }
void SaveRecord()
{
	if (FILE *f = std::fopen(RecordPath().c_str(), "w")) {
		const std::string text = Save(gRecord);
		std::fwrite(text.data(), 1, text.size(), f);
		std::fclose(f);
	}
}
void Unlocked(Id id, uint32_t now)
{
	gNotices.push_back({id, 1, now});
	Log("achievements: unlocked %s", Definition(id).name);
}

uint8_t *Running(const char *name)
{
	if (!gNative.activeScripts)
		return nullptr;
	for (uint8_t *script = *gNative.activeScripts; script; script = *reinterpret_cast<uint8_t **>(script))
		if (!std::strncmp(reinterpret_cast<const char *>(script + 0x10), name, 8))
			return script;
	return nullptr;
}
int32_t Local(const uint8_t *script, int index) { return *reinterpret_cast<const int32_t *>(script + 0x38 + 4 * index); }
int32_t Global(int index) { return gScriptKnown ? ScriptGlobal(index) : 0; }
// A script's ped by its handle; an unset variable (0) is never anyone.
uint8_t *PedOf(int32_t handle) { return handle != 0 && api.PoolsGetPed ? api.PoolsGetPed(handle) : nullptr; }

int Outfit()
{
	const uint8_t *mi = api.modelInfoPtrs ? api.modelInfoPtrs[0] : nullptr;
	if (!mi)
		return -1;
	char name[25] = {};
	std::strncpy(name, reinterpret_cast<const char *>(mi + 8), 24);
	for (char &c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
	for (int i = 0; i < kOutfits; ++i)
		if (!std::strcmp(name, kOutfitNames[i]))
			return i;
	return -1;
}
bool DeluxoFlatOut(uint8_t *player)
{
	if (!PedInVehicle(player))
		return false;
	uint8_t *car = PedVehicle(player);
	const uint8_t *handling = car ? *reinterpret_cast<uint8_t *const *>(car + 0x160) : nullptr;
	if (!car || EntityModel(car) != kDeluxo || !handling)
		return false;
	const CVector v = EntitySpeed(car);
	const float top = *reinterpret_cast<const float *>(handling + 0x84); // fMaxVelocity, game units
	return top > 0 && std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z) >= kDeluxoTopSpeed * top;
}

Facts Read(uint8_t *player, uint32_t now)
{
	const Native &n = gNative;
	Facts f;
	f.valid = gInstalled && player && now - gSettledAt >= kSettleMs;
	f.cheated = n.cheated && *n.cheated > 0;
	if (n.musicManager)
		for (int i = 0; i < kStations; ++i)
			f.radioSeconds[i] = reinterpret_cast<const float *>(n.musicManager + 0x39a0)[i] / 1000.0f;
	auto get = [](const int32_t *p) { return p ? *p : 0; };
	f.vigilanteLevel = get(n.vigilante);
	f.ambulanceLevel = get(n.ambulance);
	f.longestWheelie = get(n.longestWheelie);
	f.taxiFares = get(n.taxiFares);
	f.firesExtinguished = get(n.fires);
	f.propertyDestroyed = get(n.propertyDestroyed);
	f.propertiesOwned = get(n.propertiesOwned);
	f.uniqueJumps = get(n.uniqueJumps);
	f.starsAttained = get(n.starsAttained);
	f.headshots = get(n.headshots);
	f.keepieUppy = n.highestScores ? n.highestScores[3] : 0;
	f.pizzas = n.pizzas ? *n.pizzas : 0;
	f.storesKnockedOff = n.stores ? *n.stores : 0;
	f.chaseValue = n.chase ? *n.chase : 0;
	f.criminalRating = n.criminalRating ? n.criminalRating() : 0;
	f.percent = n.percent ? n.percent() : 0;
	for (int i = 0; i < kImportLists; ++i)
		f.importLists[i] = n.importLists ? uint8_t(n.importLists[i] & 0x3f) : 0;
	if (api.worldPlayers && api.playerInFocus)
		f.packages = *reinterpret_cast<const int32_t *>(PlayerInfo() + 0xd0);
	for (int i = 0; i < kContracts; ++i)
		f.contracts[i] = Global(kVarContracts + i) == 1;
	f.riot = Global(kVarRiot) == 1;
	f.allHands = Global(kVarAllHands) == 1;
	f.supplyDemand = Global(kVarSupplyDemand) == 1;
	f.trojanVoodoo = Global(kVarTrojanVoodoo) == 1;
	f.publicityTour = Global(kVarPublicityTour) == 1;
	f.hogTied = Global(kVarHogTied) == 1;
	f.keepFriendsClose = Global(kVarKeepFriendsClose) == 1;
	f.cherryPopper = Global(kVarCherryPopper) == 1;
	f.rcRaceWon = Global(kVarRcRaceWon) == 1;
	if (player) {
		if (const uint8_t *wanted = *reinterpret_cast<uint8_t *const *>(player + 0x778))
			f.wantedLevel = *reinterpret_cast<const int32_t *>(wanted + 0x20);
		f.deluxoAtTopSpeed = DeluxoFlatOut(player);
		f.outfit = Outfit();
	}
	return f;
}

// G-Spotlight without a fall: the script keeps no such flag, so off the
// rooftops (stages 7-25 of its course) is him, or his bike, near the street.
// main.scm sets $260 and ends the script in one tick (no WAIT between), so the
// pass is seen at the first poll that finds porno4 gone: the watch lasts till then.
void WatchSpotlight(const Facts &facts, uint8_t *player, int64_t now)
{
	const bool running = gScriptKnown && Running("porno4");
	if (!running && !gSpotlight.watching)
		return;
	if (!gSpotlight.watching)
		gSpotlight = {false, true, Global(kVarSpotlightPassed)};
	if (running) {
		const int stage = Global(kVarSpotlightStage);
		const CVector at =
		    PedInVehicle(player) && PedVehicle(player) ? EntityPos(PedVehicle(player)) : EntityPos(player);
		if (stage >= 7 && stage <= 25 && at.z < kSpotlightStreet)
			gSpotlight.fell = true;
	}
	const int passed = Global(kVarSpotlightPassed);
	if (passed == 1 && gSpotlight.passed != 1 && !gSpotlight.fell && Award(kNotMyFirstTime, facts, gRecord, now))
		Unlocked(kNotMyFirstTime, *api.timeMs);
	gSpotlight.passed = passed;
	if (!running)
		gSpotlight = {}; // passed, failed or given up: the next attempt starts afresh
}
// Sir, Yes Sir!: into the tank before the army's alerted ($2525: every alarm in
// the script is guarded by it; the theft itself sets it just after $2519).
void WatchConvoy(const Facts &facts, int64_t now)
{
	if (!gScriptKnown || !Running("col_4")) {
		gConvoy = {};
		return;
	}
	if (!gConvoy.watching)
		gConvoy = {false, true, Global(kVarInTheTank)};
	const int inTank = Global(kVarInTheTank);
	if (Global(kVarArmyAlerted) != 0 && inTank == 0)
		gConvoy.alerted = true;
	if (inTank == 1 && gConvoy.inTank == 0 && !gConvoy.alerted && Award(kBakedDistractions, facts, gRecord, now))
		Unlocked(kBakedDistractions, *api.timeMs);
	gConvoy.inTank = inTank;
}

} // namespace

bool Install()
{
	auto sym = [](const char *name) { return dlsym(RTLD_MAIN_ONLY, name); };
	Native &n = gNative;
	n.vigilante = static_cast<const int32_t *>(sym("_ZN6CStats28HighestLevelVigilanteMissionE"));
	n.ambulance = static_cast<const int32_t *>(sym("_ZN6CStats28HighestLevelAmbulanceMissionE"));
	n.longestWheelie = static_cast<const int32_t *>(sym("_ZN6CStats14LongestWheelieE"));
	n.taxiFares = static_cast<const int32_t *>(sym("_ZN6CStats28PassengersDroppedOffWithTaxiE"));
	n.fires = static_cast<const int32_t *>(sym("_ZN6CStats17FiresExtinguishedE"));
	n.propertyDestroyed = static_cast<const int32_t *>(sym("_ZN6CStats17PropertyDestroyedE"));
	n.propertiesOwned = static_cast<const int32_t *>(sym("_ZN6CStats16NumPropertyOwnedE"));
	n.uniqueJumps = static_cast<const int32_t *>(sym("_ZN6CStats24NumberOfUniqueJumpsFoundE"));
	n.starsAttained = static_cast<const int32_t *>(sym("_ZN6CStats19WantedStarsAttainedE"));
	n.headshots = static_cast<const int32_t *>(sym("_ZN6CStats11HeadsPoppedE"));
	n.cheated = static_cast<const int32_t *>(sym("_ZN6CStats12CheatedCountE"));
	n.highestScores = static_cast<const int32_t *>(sym("_ZN6CStats13HighestScoresE"));
	n.importLists = static_cast<const int32_t *>(sym("_ZN8CGarages17CarTypesCollectedE"));
	n.pizzas = static_cast<const float *>(sym("_ZN6CStats15PizzasDeliveredE"));
	n.stores = static_cast<const float *>(sym("_ZN6CStats16StoresKnockedOffE"));
	n.chase = static_cast<const float *>(sym("_ZN6CStats17HighestChaseValueE"));
	n.musicManager = static_cast<const uint8_t *>(sym("MusicManager"));
	n.activeScripts = static_cast<uint8_t *const *>(sym("_ZN11CTheScripts14pActiveScriptsE"));
	n.criminalRating = reinterpret_cast<int32_t (*)()>(sym("_ZN6CStats24FindCriminalRatingNumberEv"));
	n.percent = reinterpret_cast<float (*)()>(sym("_ZN6CStats21GetPercentageProgressEv"));
	gInstalled = n.vigilante && n.ambulance && n.longestWheelie && n.taxiFares && n.fires && n.propertyDestroyed &&
	             n.propertiesOwned && n.uniqueJumps && n.starsAttained && n.headshots && n.cheated && n.highestScores &&
	             n.importLists && n.pizzas && n.stores && n.chase && n.musicManager && n.activeScripts &&
	             n.criminalRating && n.percent && api.timeMs && api.FindPlayerPed;
	if (!gInstalled) {
		Log("achievements: a native value is missing; no achievements");
		return false;
	}
	std::ifstream in(RecordPath());
	gRecord = Load(std::string((std::istreambuf_iterator<char>(in)), {}));
	int unlocked = 0;
	for (int i = 0; i < kCount; ++i) unlocked += gRecord.Unlocked(Id(i));
	Log("achievements: %d of %d unlocked", unlocked, int(kCount));
	return true;
}

void Update()
{
	if (!gInstalled || !gConfig.achievements)
		return;
	const uint32_t now = *api.timeMs;
	if (now < gLastNow || now - gLastNow > 10000) { // a save loaded, or a new game: let it settle
		gSettledAt = now;
		// What the missions were doing is gone with it: a script ended by the load
		// isn't one passed (its pass flag may read 1 from the save).
		gSpotlight = {};
		gConvoy = {};
		gDemolition = {};
	}
	gLastNow = now;
	if (now - gLastPoll < kPollMs)
		return;
	gLastPoll = now;
	gScriptKnown = story::ScriptKnown();
	uint8_t *player = api.FindPlayerPed();
	if (player && now - gSettledAt >= kSettleMs && gScriptLogged != int(gScriptKnown)) {
		gScriptLogged = int(gScriptKnown);
		Log(gScriptKnown ? "achievements: main.scm is the one this was made for; mission achievements on"
		                 : "achievements: not the main.scm this was made for, so no mission achievements");
	}
	const Facts facts = Read(player, now);
	const int64_t wall = int64_t(std::time(nullptr));
	const uint16_t outfitsWere = gRecord.outfitsWorn;
	bool changed = false;
	const std::vector<Id> got = Evaluate(facts, gRecord, wall);
	if (got.size() >= kBurst) {
		for (Id id : got) Log("achievements: unlocked %s", Definition(id).name);
		gNotices.push_back({got.front(), int(got.size()), now});
	} else {
		for (Id id : got) Unlocked(id, now);
	}
	changed = !got.empty();
	if (!gScriptKnown || !Running("serg3"))
		gDemolition = {}; // a fresh tally each time the mission starts
	if (player && facts.valid) {
		const auto before = gRecord.unlockedAt;
		WatchSpotlight(facts, player, wall);
		WatchConvoy(facts, wall);
		changed |= before != gRecord.unlockedAt;
	}
	if (changed || gRecord.outfitsWorn != outfitsWere)
		SaveRecord();
}

void Damaged(uint8_t *ped, uint8_t *by, int weapon, bool fatal)
{
	if (!gInstalled || !gConfig.achievements || !gScriptKnown || !ped || !fatal || !api.PoolsGetPed)
		return;
	uint8_t *player = api.FindPlayerPed();
	const Facts facts = Read(player, *api.timeMs);
	const int64_t wall = int64_t(std::time(nullptr));
	bool awarded = false;
	if (by == player && weapon == kWeaponGolfClub && Running("serg1") && ped == PedOf(Global(kVarGonzalez)))
		awarded |= Award(kIrony, facts, gRecord, wall) && (Unlocked(kIrony, *api.timeMs), true);
	if (const uint8_t *script = by == player && weapon == kWeaponM4 ? Running("baron5") : nullptr)
		if (ped == PedOf(Local(script, 0)))
			awarded |= Award(kSalutations, facts, gRecord, wall) && (Unlocked(kSalutations, *api.timeMs), true);
	if (Running("serg3")) {
		if (!gDemolition.watching)
			gDemolition = {true, {}};
		for (int i = 0; i < kHostiles; ++i)
			if (gDemolition.kill[i] == 0 && ped == PedOf(Global(kVarHostiles + i))) {
				const int32_t goblin = Global(kVarRcGoblin);
				const bool blades = weapon == kWeaponBlades && goblin != 0 && api.PoolsGetVehicle &&
				                    by == api.PoolsGetVehicle(goblin);
				gDemolition.kill[i] = blades ? 1 : -1;
			}
		bool all = true;
		for (int8_t k : gDemolition.kill) all &= k == 1;
		if (all)
			awarded |= Award(kChopperdUp, facts, gRecord, wall) && (Unlocked(kChopperdUp, *api.timeMs), true);
	} else {
		gDemolition = {};
	}
	if (awarded)
		SaveRecord();
}

bool Ready() { return gInstalled && gConfig.achievements; }
const Record &Current() { return gRecord; }
Facts Snapshot() { return Read(gInstalled && api.FindPlayerPed ? api.FindPlayerPed() : nullptr, api.timeMs ? *api.timeMs : 0); }

bool Notice(Shown &out)
{
	if (!api.timeMs)
		return false;
	const uint32_t now = *api.timeMs;
	while (!gNotices.empty() && (now < gNotices.front().since || now - gNotices.front().since > kNoticeMs)) {
		gNotices.erase(gNotices.begin());
		if (!gNotices.empty())
			gNotices.front().since = now; // the next one starts when this one's gone
	}
	if (gNotices.empty())
		return false;
	out = {&Definition(gNotices.front().id), gNotices.front().count, now - gNotices.front().since, kNoticeMs};
	return true;
}

} // namespace achievements
