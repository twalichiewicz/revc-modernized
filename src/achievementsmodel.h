#pragma once
// Achievements (#24): Vice City – The Definitive Edition's console list (Greasy
// Palms deferred until bribes return, #31), measured from a snapshot of the
// game's own values (achievements.cpp reads them), awarded retroactively from
// what a save already meets, never while cheated. Pure: unit-tested
// (tests/test_achievementsmodel.cpp).
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace achievements {

enum Id : int {
	kBornIn80s, kRunningRampant, kIrony, kNotMyFirstTime, kOneIsBetter, kPointAToB, kHighQualityH2O, kPieGuy,
	kBullInAChinaShop, kViceCityMogul, kSalutations, kCatchMe, kWambulance, kJustLikeTheRealThing, kKeepieUppy,
	kGunForHire, kImFamous, kDontNeedRoads, kDaredevil, kLegalCounsel, kLifeOfTheParty, kSouthAmerican, kBigHeat,
	kChauffeur, kTommyTwoWheels, kGrandTheftAuto, kBloodstainedHands, kTakeTheCannoli, kMischiefManaged,
	kChopperdUp, kCitySleuth, kDoneItAll, kPutEmUp, kOffender, kRetroLewk, kScouringTheCity, kBreakItUp,
	kTargetPractice, kPrimeRealEstate, kViceViceBaby, kBakedDistractions, kKingpin, kCount
};

struct Def {
	const char *key;         // stable, for the record on disk
	const char *name;        // as the Definitive Edition names it
	const char *description; // its requirement
};
const Def &Definition(Id id);

constexpr int kStations = 9, kOutfits = 12, kContracts = 5, kImportLists = 4;

// The game's values now (achievements.cpp), as plain numbers.
struct Facts {
	bool valid = false;   // a game in progress, settled after any load
	bool cheated = false; // CStats::CheatedCount > 0: as in the DE, nothing unlocks
	float radioSeconds[kStations]{}; // each music station's listen time
	int vigilanteLevel = 0, ambulanceLevel = 0, longestWheelie = 0, taxiFares = 0, firesExtinguished = 0;
	float pizzas = 0, storesKnockedOff = 0, chaseValue = 0, percent = 0;
	int propertyDestroyed = 0, propertiesOwned = 0, uniqueJumps = 0, packages = 0, starsAttained = 0, headshots = 0,
	    keepieUppy = 0, criminalRating = 0, wantedLevel = 0;
	bool contracts[kContracts]{};
	uint8_t importLists[kImportLists]{}; // each list's six cars, a bit each
	bool riot = false, allHands = false, supplyDemand = false, trojanVoodoo = false, publicityTour = false,
	     hogTied = false, keepFriendsClose = false, cherryPopper = false, rcRaceWon = false;
	bool deluxoAtTopSpeed = false; // live
	int outfit = -1;               // which of the 12 he wears now, -1 unknown
};

// Kept by the mod across saves and games (a profile's, as consoles keep them).
struct Record {
	std::array<int64_t, kCount> unlockedAt{}; // when (seconds since the epoch); 0 locked
	uint16_t outfitsWorn = 0;                 // Retro Lewk: each outfit seen worn, a bit each
	bool Unlocked(Id id) const { return unlockedAt[size_t(id)] != 0; }
};

struct Progress {
	float current = 0, target = 1;
	bool unlocked = false;
};
Progress Measure(Id id, const Facts &facts, const Record &record);
// Unlocks everything the facts now meet (Kingpin last); returns those newly unlocked.
std::vector<Id> Evaluate(const Facts &facts, Record &record, int64_t now);
// A mission condition seen live (Iron-y, Salutations, Chopper'd Up, Not my First
// Time, Baked Distractions): unlocks it if allowed and not yet unlocked.
bool Award(Id id, const Facts &facts, Record &record, int64_t now);

std::string Save(const Record &record);
Record Load(const std::string &text); // tolerates corruption, unknown keys, blank lines

} // namespace achievements
