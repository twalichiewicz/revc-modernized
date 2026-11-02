// Achievements (#24): the Definitive Edition's console list (Greasy Palms
// deferred, #31) measured from a snapshot of the game's own values, awarded
// retroactively from what a save already meets, never while cheated.
#include "achievementsmodel.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

using namespace achievements;
static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)

static Facts Fresh()
{
	Facts f;
	f.valid = true;
	return f;
}
static bool Unlocks(Id id, const Facts &f)
{
	Record r;
	for (Id got : Evaluate(f, r, 1000))
		if (got == id)
			return true;
	return false;
}
// The value just below, at, and just above a threshold.
template <class Set> static void Threshold(Id id, Set set, float target, float below)
{
	Facts f = Fresh();
	set(f, below);
	CHECK(!Unlocks(id, f));
	f = Fresh();
	set(f, target);
	CHECK(Unlocks(id, f));
	f = Fresh();
	set(f, target * 2);
	CHECK(Unlocks(id, f));
}

int main()
{
	// The list: 42 (Greasy Palms deferred), unique stable keys, named as the DE does.
	CHECK(kCount == 42);
	std::set<std::string> keys;
	for (int i = 0; i < kCount; ++i) {
		const Def &d = Definition(Id(i));
		CHECK(d.key && *d.key && d.name && *d.name && d.description && *d.description);
		keys.insert(d.key);
	}
	CHECK(keys.size() == size_t(kCount));
	CHECK(!std::strcmp(Definition(kCitySleuth).name, "City Sleuth") && !std::strcmp(Definition(kKingpin).name, "Kingpin"));

	// Counters and ratings from the save, at the DE's thresholds.
	Threshold(kRunningRampant, [](Facts &f, float v) { f.vigilanteLevel = int(v); }, 12, 11);
	Threshold(kWambulance, [](Facts &f, float v) { f.ambulanceLevel = int(v); }, 12, 11);
	Threshold(kOneIsBetter, [](Facts &f, float v) { f.longestWheelie = int(v); }, 30, 29);
	Threshold(kPointAToB, [](Facts &f, float v) { f.taxiFares = int(v); }, 25, 24);
	Threshold(kHighQualityH2O, [](Facts &f, float v) { f.firesExtinguished = int(v); }, 10, 9);
	Threshold(kPieGuy, [](Facts &f, float v) { f.pizzas = v; }, 10, 9);
	Threshold(kBullInAChinaShop, [](Facts &f, float v) { f.propertyDestroyed = int(v); }, 1000000, 999999);
	Threshold(kBreakItUp, [](Facts &f, float v) { f.propertyDestroyed = int(v); }, 500000, 499999);
	Threshold(kViceCityMogul, [](Facts &f, float v) { f.propertiesOwned = int(v); }, 10, 9);
	Threshold(kPrimeRealEstate, [](Facts &f, float v) { f.propertiesOwned = int(v); }, 15, 14);
	Threshold(kKeepieUppy, [](Facts &f, float v) { f.keepieUppy = int(v); }, 5, 4);
	Threshold(kImFamous, [](Facts &f, float v) { f.chaseValue = v; }, 1800, 1799);
	Threshold(kDaredevil, [](Facts &f, float v) { f.uniqueJumps = int(v); }, 36, 35);
	Threshold(kBloodstainedHands, [](Facts &f, float v) { f.criminalRating = int(v); }, 10000, 9999);
	Threshold(kTakeTheCannoli, [](Facts &f, float v) { f.criminalRating = int(v); }, 100000, 99999);
	Threshold(kCitySleuth, [](Facts &f, float v) { f.packages = int(v); }, 100, 99);
	Threshold(kScouringTheCity, [](Facts &f, float v) { f.packages = int(v); }, 50, 49);
	Threshold(kDoneItAll, [](Facts &f, float v) { f.percent = v; }, 100, 99.9f);
	Threshold(kPutEmUp, [](Facts &f, float v) { f.storesKnockedOff = v; }, 15, 14);
	Threshold(kOffender, [](Facts &f, float v) { f.starsAttained = int(v); }, 50, 49);
	Threshold(kTargetPractice, [](Facts &f, float v) { f.headshots = int(v); }, 50, 49);
	Threshold(kCatchMe, [](Facts &f, float v) { f.wantedLevel = int(v); }, 6, 5);

	// Every music station heard at least 10 s; tuning past one doesn't count.
	{
		Facts f = Fresh();
		for (float &s : f.radioSeconds) s = 60;
		CHECK(Unlocks(kBornIn80s, f));
		f.radioSeconds[5] = 9.5f;
		CHECK(!Unlocks(kBornIn80s, f));
	}
	// Flags: each mission's own pass flag; all five contracts; the race won; Cherry Popper.
	const struct { Id id; bool Facts::*flag; } flags[] = {
	    {kLegalCounsel, &Facts::riot},           {kLifeOfTheParty, &Facts::allHands},
	    {kSouthAmerican, &Facts::supplyDemand}, {kBigHeat, &Facts::trojanVoodoo},
	    {kChauffeur, &Facts::publicityTour},     {kTommyTwoWheels, &Facts::hogTied},
	    {kMischiefManaged, &Facts::keepFriendsClose}, {kViceViceBaby, &Facts::cherryPopper},
	    {kJustLikeTheRealThing, &Facts::rcRaceWon},   {kDontNeedRoads, &Facts::deluxoAtTopSpeed},
	};
	for (const auto &c : flags) {
		Facts f = Fresh();
		CHECK(!Unlocks(c.id, f));
		f.*c.flag = true;
		CHECK(Unlocks(c.id, f));
	}
	{
		Facts f = Fresh();
		for (bool &c : f.contracts) c = true;
		CHECK(Unlocks(kGunForHire, f));
		f.contracts[3] = false;
		CHECK(!Unlocks(kGunForHire, f));
		// Every import list full: 24 cars.
		for (uint8_t &m : f.importLists) m = 0x3f;
		CHECK(Unlocks(kGrandTheftAuto, f));
		f.importLists[2] = 0x3e;
		CHECK(!Unlocks(kGrandTheftAuto, f));
		CHECK(Measure(kGrandTheftAuto, f, Record{}).current == 23);
	}

	// Mission conditions come only from what's seen live (Award), never from facts.
	{
		Facts f = Fresh();
		Record r;
		CHECK(!Unlocks(kIrony, f) && !Unlocks(kChopperdUp, f));
		CHECK(Award(kIrony, f, r, 50) && r.Unlocked(kIrony) && r.unlockedAt[kIrony] == 50);
		CHECK(!Award(kIrony, f, r, 60) && r.unlockedAt[kIrony] == 50); // once
		f.cheated = true;
		CHECK(!Award(kSalutations, f, r, 70) && !r.Unlocked(kSalutations));
	}

	// Retro Lewk: the 12 outfits, each worn while the mod runs, accumulate in the record.
	{
		Record r;
		Facts f = Fresh();
		for (int outfit = 0; outfit < kOutfits; ++outfit) {
			f.outfit = outfit;
			Evaluate(f, r, 100 + outfit);
			CHECK(r.Unlocked(kRetroLewk) == (outfit == kOutfits - 1));
		}
		CHECK(Measure(kRetroLewk, f, r).current == kOutfits);
	}

	// Nothing unlocks while cheated (as the DE), or before a game has settled;
	// progress still shows.
	{
		Facts f = Fresh();
		f.packages = 100;
		f.cheated = true;
		Record r;
		CHECK(Evaluate(f, r, 1).empty() && !r.Unlocked(kCitySleuth));
		CHECK(Measure(kCitySleuth, f, r).current == 100 && !Measure(kCitySleuth, f, r).unlocked);
		f.cheated = false;
		f.valid = false;
		CHECK(Evaluate(f, r, 1).empty());
		f.outfit = 3;
		Evaluate(f, r, 1);
		CHECK(r.outfitsWorn == 0);
	}

	// Retroactive: a save that already meets them awards them at once, with that date;
	// what's unlocked stays unlocked when a new game zeroes the stats.
	{
		Facts f = Fresh();
		f.packages = 100;
		f.uniqueJumps = 36;
		Record r;
		const auto got = Evaluate(f, r, 777);
		CHECK(got.size() == 3 && r.unlockedAt[kCitySleuth] == 777 && r.Unlocked(kScouringTheCity) && r.Unlocked(kDaredevil));
		CHECK(Evaluate(f, r, 778).empty());
		Facts zero = Fresh();
		Evaluate(zero, r, 900);
		CHECK(r.Unlocked(kCitySleuth) && r.unlockedAt[kCitySleuth] == 777);
	}

	// Kingpin: when the other 41 are unlocked, in the same evaluation, last.
	{
		Record r;
		for (int i = 0; i < kCount; ++i)
			if (i != kKingpin && i != kCitySleuth)
				r.unlockedAt[i] = 5;
		Facts f = Fresh();
		CHECK(!Unlocks(kKingpin, f));
		f.packages = 100;
		const auto got = Evaluate(f, r, 9);
		CHECK(got.size() == 2 && got.back() == kKingpin && r.Unlocked(kKingpin));
		CHECK(Measure(kKingpin, f, r).current == 41 && Measure(kKingpin, f, r).target == 41);
	}

	// The record on disk: round-trips; tolerates corruption, unknown keys and duplicates.
	{
		Record r;
		r.unlockedAt[kCitySleuth] = 1762000000;
		r.unlockedAt[kKingpin] = 1762000100;
		r.outfitsWorn = 0x0a5;
		const Record back = Load(Save(r));
		CHECK(back.unlockedAt == r.unlockedAt && back.outfitsWorn == r.outfitsWorn);
		const Record odd = Load("garbage\ncity_sleuth 1762000000\nno_such_one 5\ncity_sleuth -4\noutfits banana\n"
		                        "pie_guy 12x\n\nkingpin 99\n");
		CHECK(odd.unlockedAt[kCitySleuth] == 1762000000 && odd.unlockedAt[kKingpin] == 99 && !odd.Unlocked(kPieGuy) &&
		      odd.outfitsWorn == 0);
		CHECK(Load("").unlockedAt == Record{}.unlockedAt);
	}

	if (failures)
		return 1;
	std::puts("achievementsmodel: all achievement model tests passed");
}
