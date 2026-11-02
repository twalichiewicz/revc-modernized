#include "achievementsmodel.h"
#include <cstdlib>
#include <sstream>

namespace achievements {
namespace {

// gta.wiki / GTABase wording (docs/research/2026-11-02-achievements-native-map.md).
constexpr Def kDefs[kCount] = {
    {"born_in_the_80s", "Born in the 80's", "Listen to all music stations at least once."},
    {"running_rampant", "Running Rampant", "Complete Vigilante mission level 12."},
    {"iron_y", "Iron-y", "Kill the property developer with a Golf Club during \"Four Iron\"."},
    {"not_my_first_time", "Not my First Time", "Complete \"G-Spotlight\" without falling from the rooftops."},
    {"one_is_better_than_two", "One is Better Than Two", "Perform a 30 second wheelie."},
    {"point_a_to_point_b", "Point A to Point B", "Drop off 25 passengers driving the taxi."},
    {"high_quality_h2o", "High Quality H2O", "Extinguish 10 fires."},
    {"pie_guy", "Pie Guy", "Deliver 10 pizzas."},
    {"bull_in_a_china_shop", "Bull in a China Shop", "Cause $1,000,000 in property damage."},
    {"vice_city_mogul", "Vice City Mogul", "Own 10 properties."},
    {"salutations", "Salutations My Little Friend", "Use the M4 to take out Diaz during the mission \"Rub Out\"."},
    {"catch_me_if_you_can", "Catch Me if You Can", "Achieve a six-star wanted level."},
    {"wambulance", "Somebody Call the Wambulance?", "Complete Paramedic Mission level 12."},
    {"just_like_the_real_thing", "Just Like the Real Thing", "Win the RC Bandit Race."},
    {"keepie_uppy", "Keepie-Uppy Okie Dokie", "Earn a high score of 5 with the Keepie-Uppy Beach Ball."},
    {"gun_for_hire", "Gun for Hire", "Complete all assassination contracts."},
    {"im_famous", "I'm Famous!", "Earn the Stuff of Legends media attention rank."},
    {"dont_need_roads", "Don't Need Roads", "Hit max speed in a Deluxo."},
    {"daredevil", "Daredevil", "Complete 36 unique jumps."},
    {"legal_counsel", "Legal Counsel", "Complete the \"Riot\" mission."},
    {"life_of_the_party", "Life of the Party", "Complete the \"All Hands on Deck!\" mission."},
    {"south_american_connection", "South American Connection", "Complete the \"Supply & Demand\" mission."},
    {"big_heat", "Big Heat from Little Havana", "Complete the \"Trojan Voodoo\" mission."},
    {"chauffeur", "Chauffeur", "Complete the \"Publicity Tour\" mission."},
    {"tommy_two_wheels", "Tommy Two-Wheels", "Complete the \"Hog Tied\" mission."},
    {"grand_theft_auto", "Grand Theft Auto", "Retrieve every wanted vehicle at Sunshine Autos Import Garage."},
    {"bloodstained_hands", "Bloodstained Hands", "Earn the Butcher criminal rating."},
    {"take_the_cannoli", "Take the Cannoli", "Earn the Godfather criminal rating."},
    {"mischief_managed", "Mischief Managed", "Complete the \"Keep Your Friends Close...\" mission."},
    {"chopperd_up", "Chopper'd Up", "Kill all hostile NPCs during \"Demolition Man\" using the RC chopper's blades."},
    {"city_sleuth", "City Sleuth", "Find 100 hidden packages."},
    {"done_it_all", "Done it All", "Earn 100% completion."},
    {"put_em_up", "Put 'Em Up", "Knock off 15 stores."},
    {"offender", "Offender", "Attain 50 wanted stars."},
    {"retro_lewk", "Retro Lewk", "Wear every outfit in the game at least once."},
    {"scouring_the_city", "Scouring the City!", "Find 50 hidden packages."},
    {"break_it_up", "Break it Up", "Cause $500,000 in property damage."},
    {"target_practice", "Target Practice", "Perform 50 headshots."},
    {"prime_real_estate", "Prime Real Estate", "Purchase all properties."},
    {"vice_vice_baby", "Vice Vice Baby", "Complete the Cherry Popper dealer subgame."},
    {"baked_distractions", "Baked Distractions", "Hijack the tank without alerting the army first, during \"Sir, Yes Sir!\"."},
    {"kingpin", "Kingpin", "Unlock all achievements."},
};

constexpr float kHeardSeconds = 10;     // a station "listened to", not tuned past
constexpr int kImportCars = 6, kPropertiesAll = 15;

float Flag(bool b) { return b ? 1.0f : 0.0f; }
int Bits(unsigned v)
{
	int n = 0;
	for (; v; v &= v - 1)
		++n;
	return n;
}
// Seen only live, through Award.
bool Live(Id id)
{
	return id == kIrony || id == kNotMyFirstTime || id == kSalutations || id == kChopperdUp || id == kBakedDistractions;
}
// {current, target} from the facts (and, for Retro Lewk and Kingpin, the record).
Progress Value(Id id, const Facts &f, const Record &r)
{
	switch (id) {
	case kBornIn80s: {
		int heard = 0;
		for (float s : f.radioSeconds) heard += s >= kHeardSeconds;
		return {float(heard), float(kStations)};
	}
	case kRunningRampant: return {float(f.vigilanteLevel), 12};
	case kOneIsBetter: return {float(f.longestWheelie), 30};
	case kPointAToB: return {float(f.taxiFares), 25};
	case kHighQualityH2O: return {float(f.firesExtinguished), 10};
	case kPieGuy: return {f.pizzas, 10};
	case kBullInAChinaShop: return {float(f.propertyDestroyed), 1000000};
	case kViceCityMogul: return {float(f.propertiesOwned), 10};
	case kCatchMe: return {float(f.wantedLevel), 6};
	case kWambulance: return {float(f.ambulanceLevel), 12};
	case kJustLikeTheRealThing: return {Flag(f.rcRaceWon), 1};
	case kKeepieUppy: return {float(f.keepieUppy), 5};
	case kGunForHire: {
		int done = 0;
		for (bool c : f.contracts) done += c;
		return {float(done), float(kContracts)};
	}
	case kImFamous: return {f.chaseValue, 1800};                 // the game's Stuff of Legends
	case kDontNeedRoads: return {Flag(f.deluxoAtTopSpeed), 1};
	case kDaredevil: return {float(f.uniqueJumps), 36};
	case kLegalCounsel: return {Flag(f.riot), 1};
	case kLifeOfTheParty: return {Flag(f.allHands), 1};
	case kSouthAmerican: return {Flag(f.supplyDemand), 1};
	case kBigHeat: return {Flag(f.trojanVoodoo), 1};
	case kChauffeur: return {Flag(f.publicityTour), 1};
	case kTommyTwoWheels: return {Flag(f.hogTied), 1};
	case kGrandTheftAuto: {
		int cars = 0;
		for (uint8_t m : f.importLists) cars += Bits(m & 0x3f);
		return {float(cars), float(kImportLists * kImportCars)};
	}
	case kBloodstainedHands: return {float(f.criminalRating), 10000};
	case kTakeTheCannoli: return {float(f.criminalRating), 100000}; // the DE's patched Godfather
	case kMischiefManaged: return {Flag(f.keepFriendsClose), 1};
	case kCitySleuth: return {float(f.packages), 100};
	case kDoneItAll: return {f.percent, 100};
	case kPutEmUp: return {f.storesKnockedOff, 15};
	case kOffender: return {float(f.starsAttained), 50};
	case kRetroLewk: return {float(Bits(r.outfitsWorn)), float(kOutfits)};
	case kScouringTheCity: return {float(f.packages), 50};
	case kBreakItUp: return {float(f.propertyDestroyed), 500000};
	case kTargetPractice: return {float(f.headshots), 50};
	case kPrimeRealEstate: return {float(f.propertiesOwned), float(kPropertiesAll)};
	case kViceViceBaby: return {Flag(f.cherryPopper), 1};
	case kKingpin: {
		int others = 0;
		for (int i = 0; i < kCount; ++i) others += i != kKingpin && r.Unlocked(Id(i));
		return {float(others), float(kCount - 1)};
	}
	default: return {0, 1}; // the live ones
	}
}
bool Allowed(const Facts &f) { return f.valid && !f.cheated; }

} // namespace

const Def &Definition(Id id) { return kDefs[size_t(id)]; }

Progress Measure(Id id, const Facts &facts, const Record &record)
{
	Progress p = Value(id, facts, record);
	p.unlocked = record.Unlocked(id);
	if (Live(id) && p.unlocked)
		p.current = p.target;
	return p;
}

std::vector<Id> Evaluate(const Facts &facts, Record &record, int64_t now)
{
	std::vector<Id> got;
	if (!Allowed(facts))
		return got;
	if (facts.outfit >= 0 && facts.outfit < kOutfits)
		record.outfitsWorn = uint16_t(record.outfitsWorn | (1u << facts.outfit));
	for (int i = 0; i < kCount; ++i) {
		const Id id = Id(i);
		if (id == kKingpin || Live(id) || record.Unlocked(id))
			continue;
		const Progress p = Value(id, facts, record);
		if (p.current >= p.target) {
			record.unlockedAt[size_t(id)] = now;
			got.push_back(id);
		}
	}
	const Progress king = Value(kKingpin, facts, record);
	if (!record.Unlocked(kKingpin) && king.current >= king.target) {
		record.unlockedAt[kKingpin] = now;
		got.push_back(kKingpin);
	}
	return got;
}

bool Award(Id id, const Facts &facts, Record &record, int64_t now)
{
	if (!Allowed(facts) || id < 0 || id >= kCount || record.Unlocked(id) || now <= 0)
		return false;
	record.unlockedAt[size_t(id)] = now;
	return true;
}

std::string Save(const Record &record)
{
	std::ostringstream out;
	out << "# VC Modern HUD achievements: key, then when it unlocked (seconds since 1970)\n";
	for (int i = 0; i < kCount; ++i)
		if (record.Unlocked(Id(i)))
			out << kDefs[i].key << ' ' << record.unlockedAt[size_t(i)] << '\n';
	out << "outfits " << record.outfitsWorn << '\n';
	return out.str();
}

Record Load(const std::string &text)
{
	Record record;
	std::istringstream in(text);
	for (std::string line; std::getline(in, line);) {
		std::istringstream row(line);
		std::string key, value;
		if (!(row >> key >> value) || key[0] == '#')
			continue;
		char *end = nullptr;
		const long long number = std::strtoll(value.c_str(), &end, 10);
		if (!end || *end || number <= 0)
			continue;
		if (key == "outfits") {
			record.outfitsWorn = uint16_t(number & ((1 << kOutfits) - 1));
			continue;
		}
		for (int i = 0; i < kCount; ++i)
			if (key == kDefs[i].key)
				record.unlockedAt[size_t(i)] = number;
	}
	return record;
}

} // namespace achievements
