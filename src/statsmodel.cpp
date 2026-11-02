#include "statsmodel.h"
#include <algorithm>
#include <cmath>
#include <ctime>

namespace statsmodel {
namespace {
using namespace achievements;

int Half(int lines) { return (std::max(lines, 0) + 1) / 2; }
int MaxAchievementScroll(const Sizes &s) { return std::max(0, s.achievements - kAchievementRows); }
void Follow(Focus &f) // the chosen achievement in view
{
	if (f.achievement < f.achievementScroll)
		f.achievementScroll = f.achievement;
	if (f.achievement >= f.achievementScroll + kAchievementRows)
		f.achievementScroll = f.achievement - kAchievementRows + 1;
}
// 1234567 as "1,234,567"; never rounded up (9.99 of 10 isn't done).
std::string Number(float v)
{
	std::string digits = std::to_string(static_cast<long long>(std::floor(std::max(v, 0.0f))));
	for (int i = int(digits.size()) - 3; i > 0; i -= 3)
		digits.insert(size_t(i), ",");
	return digits;
}
std::string Date(int64_t at, bool year)
{
	const std::time_t t = std::time_t(at);
	std::tm local{};
	localtime_r(&t, &local);
	char text[32];
	std::strftime(text, sizeof text, year ? "%d %b %Y" : "%d %b", &local);
	std::string s = text[0] == '0' ? text + 1 : text;
	for (char &c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
	return s;
}
bool Money(Id id) { return id == kBullInAChinaShop || id == kBreakItUp; }
bool Live(Id id)
{
	return id == kIrony || id == kNotMyFirstTime || id == kSalutations || id == kChopperdUp || id == kBakedDistractions;
}
// Passed or not, by the save's own record of the mission.
bool Mission(Id id)
{
	switch (id) {
	case kJustLikeTheRealThing:
	case kLegalCounsel:
	case kLifeOfTheParty:
	case kSouthAmerican:
	case kBigHeat:
	case kChauffeur:
	case kTommyTwoWheels:
	case kMischiefManaged:
	case kViceViceBaby: return true;
	default: return false;
	}
}
// How a count reads in the panel: "61 OF 100 FOUND".
const char *Unit(Id id)
{
	switch (id) {
	case kBornIn80s: return "STATIONS";
	case kPointAToB: return "FARES";
	case kHighQualityH2O: return "FIRES";
	case kPieGuy: return "PIZZAS";
	case kViceCityMogul:
	case kPrimeRealEstate: return "OWNED";
	case kGunForHire: return "CONTRACTS";
	case kDaredevil: return "JUMPS";
	case kGrandTheftAuto: return "CARS";
	case kCitySleuth:
	case kScouringTheCity: return "FOUND";
	case kPutEmUp: return "STORES";
	case kOffender: return "STARS";
	case kRetroLewk: return "OUTFITS";
	case kTargetPractice: return "HEADSHOTS";
	case kKingpin: return "UNLOCKED";
	default: return "";
	}
}

} // namespace

bool Apply(Focus &f, Input in, const Sizes &sizes)
{
	const bool stats = f.tab == Tab::kStats;
	switch (in) {
	case Input::kLeft: f.tab = Tab::kStats; return true;
	case Input::kRight: f.tab = Tab::kAchievements; return true;
	case Input::kSelect: return true;
	case Input::kDown:
		if (f.area == Area::kTabs)
			f.area = Area::kList;
		else if (stats)
			f.statScroll = std::min(f.statScroll + 1, MaxStatScroll(sizes.stats));
		else
			f.achievement = std::min(f.achievement + 1, sizes.achievements - 1);
		Follow(f);
		return true;
	case Input::kUp:
		if (f.area == Area::kList) {
			int &at = stats ? f.statScroll : f.achievement;
			if (at == 0)
				f.area = Area::kTabs;
			else
				--at;
		}
		Follow(f);
		return true;
	}
	return false;
}

void Scroll(Focus &f, int ticks, const Sizes &sizes)
{
	if (f.tab == Tab::kStats) {
		f.statScroll = std::clamp(f.statScroll + ticks, 0, MaxStatScroll(sizes.stats));
		return;
	}
	f.achievementScroll = std::clamp(f.achievementScroll + ticks, 0, MaxAchievementScroll(sizes));
	f.achievement = std::clamp(f.achievement, f.achievementScroll, f.achievementScroll + kAchievementRows - 1);
}

void Clamp(Focus &f, const Sizes &sizes)
{
	f.statScroll = std::clamp(f.statScroll, 0, MaxStatScroll(sizes.stats));
	f.achievement = std::clamp(f.achievement, 0, std::max(0, sizes.achievements - 1));
	f.achievementScroll = std::clamp(f.achievementScroll, 0, MaxAchievementScroll(sizes));
	Follow(f);
}

int MaxStatScroll(int lines) { return std::max(0, Half(lines) - kStatRows); }

Span Column(int column, int lines, int scroll)
{
	const int half = Half(lines), start = column == 0 ? 0 : half, end = column == 0 ? half : std::max(lines, 0);
	const int first = start + scroll;
	return {first, std::clamp(end - first, 0, kStatRows)};
}

std::string RowStatus(Id id, const Progress &p, int64_t unlockedAt)
{
	if (unlockedAt > 0)
		return Date(unlockedAt, false);
	if (p.target <= 1)
		return "";
	if (Money(id))
		return "$" + Number(p.current);
	if (id == kDoneItAll)
		return Number(p.current) + "%";
	const std::string of = Number(p.current) + "/" + Number(p.target);
	if (id == kRunningRampant || id == kWambulance)
		return "LEVEL " + of;
	if (id == kOneIsBetter)
		return "BEST " + of + " S";
	if (id == kKeepieUppy)
		return "BEST " + of;
	return of;
}

std::string Detail(Id id, const Progress &p, int64_t unlockedAt)
{
	if (unlockedAt > 0)
		return "UNLOCKED " + Date(unlockedAt, true);
	if (p.target <= 1)
		return "";
	if (Money(id))
		return "$" + Number(p.current) + " OF $" + Number(p.target);
	if (id == kDoneItAll)
		return Number(p.current) + "% COMPLETE";
	const std::string of = Number(p.current) + " OF " + Number(p.target);
	switch (id) {
	case kRunningRampant:
	case kWambulance: return "LEVEL " + of;
	case kOneIsBetter: return "BEST " + of + " SECONDS";
	case kKeepieUppy: return "BEST " + of;
	case kBloodstainedHands:
	case kTakeTheCannoli: return "RATING " + of;
	case kImFamous: return "MEDIA ATTENTION " + of;
	default: break;
	}
	const std::string unit = Unit(id);
	return unit.empty() ? of : of + " " + unit;
}

std::string Note(Id id, bool cheated)
{
	if (cheated)
		return "CHEATS WERE USED ON THIS SAVE: NOTHING MORE UNLOCKS ON IT.";
	if (Live(id))
		return "THE GAME KEEPS NO RECORD OF THIS ONE: IT UNLOCKS ONLY AS IT HAPPENS, PLAYING THE MISSION.";
	switch (id) {
	case kRetroLewk: return "COUNTS THE OUTFITS WORN SINCE THE MOD WAS INSTALLED.";
	case kCatchMe:
	case kDontNeedRoads: return "UNLOCKS THE MOMENT IT HAPPENS.";
	case kKingpin: return "UNLOCKS WITH ALL THE OTHERS.";
	default: break;
	}
	return Mission(id) ? "COUNTS FROM YOUR SAVE: A MISSION PASSED BEFORE THE MOD STILL COUNTS."
	                   : "COUNTS FROM YOUR SAVE: PROGRESS FROM BEFORE THE MOD STILL COUNTS.";
}

std::string HeadingText(const std::string &name)
{
	std::string out;
	for (char c : name) {
		const char lower = char(std::tolower(static_cast<unsigned char>(c)));
		if ((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9') || lower == ':')
			out += lower;
		else if ((c == ' ' || c == '-') && !out.empty() && out.back() != ' ')
			out += ' ';
	}
	while (!out.empty() && out.back() == ' ')
		out.pop_back();
	return out;
}

std::vector<std::string> Wrap(const std::string &text, float width, const std::function<float(const std::string &)> &measure)
{
	std::vector<std::string> lines;
	std::string line;
	size_t at = 0;
	while (at < text.size()) {
		const size_t space = text.find(' ', at), end = space == std::string::npos ? text.size() : space;
		const std::string word = text.substr(at, end - at);
		at = end + 1;
		if (word.empty())
			continue;
		const std::string longer = line.empty() ? word : line + " " + word;
		if (!line.empty() && measure(longer) > width) {
			lines.push_back(line);
			line = word;
		} else {
			line = longer;
		}
	}
	if (!line.empty())
		lines.push_back(line);
	return lines;
}

float NoticeAlpha(uint32_t age, uint32_t shownFor)
{
	constexpr float kIn = 250, kOut = 600;
	if (age >= shownFor)
		return 0;
	return std::clamp(std::min(float(age) / kIn, float(shownFor - age) / kOut), 0.0f, 1.0f);
}

} // namespace statsmodel
