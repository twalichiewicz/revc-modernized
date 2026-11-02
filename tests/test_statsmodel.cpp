// The Stats page (#24) without the game: tabs, focus, scrolling, the two
// columns, and what an achievement's row and panel say.
#include "statsmodel.h"
#include <cstdio>
#include <ctime>

using namespace statsmodel;
using namespace achievements;
static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)
#define EQ(a, b)                                                                                                       \
	do {                                                                                                           \
		const std::string got = (a), want = (b);                                                               \
		if (got != want) {                                                                                     \
			std::printf("FAIL line %d: \"%s\" != \"%s\"\n", __LINE__, got.c_str(), want.c_str());          \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)

static int64_t Day(int y, int m, int d)
{
	std::tm t{};
	t.tm_year = y - 1900, t.tm_mon = m - 1, t.tm_mday = d, t.tm_hour = 12;
	return int64_t(std::mktime(&t));
}

int main()
{
	// Focus: starts on the tabs; left / right switch tab from anywhere.
	Sizes sizes{57};
	Focus f;
	CHECK(f.tab == Tab::kStats && f.area == Area::kTabs);
	CHECK(Apply(f, Input::kRight, sizes) && f.tab == Tab::kAchievements);
	CHECK(Apply(f, Input::kRight, sizes) && f.tab == Tab::kAchievements); // no wrap
	CHECK(Apply(f, Input::kLeft, sizes) && f.tab == Tab::kStats);
	// Select's the page's too: on a root tab the frontend would take it as Back's (resume).
	CHECK(Apply(f, Input::kSelect, sizes));

	// STATS: down into the list, then it scrolls both columns; up past the top is the tabs.
	CHECK(Apply(f, Input::kDown, sizes) && f.area == Area::kList && f.statScroll == 0);
	Apply(f, Input::kDown, sizes);
	CHECK(f.statScroll == 1);
	for (int i = 0; i < 50; ++i) Apply(f, Input::kDown, sizes);
	CHECK(f.statScroll == MaxStatScroll(57));
	CHECK(MaxStatScroll(57) == 29 - 10);
	CHECK(MaxStatScroll(12) == 0 && MaxStatScroll(0) == 0);
	CHECK(Apply(f, Input::kLeft, sizes) == true && f.tab == Tab::kStats); // already there: still the page's
	for (int i = 0; i < 30; ++i) Apply(f, Input::kUp, sizes);
	CHECK(f.area == Area::kTabs && f.statScroll == 0);
	CHECK(Apply(f, Input::kUp, sizes) && f.area == Area::kTabs); // nothing above the tabs, but still the page's
	// Columns: down the left, then the right; the left one is the longer.
	Span left = Column(0, 57, 0), right = Column(1, 57, 0);
	CHECK(left.first == 0 && left.count == 10 && right.first == 29 && right.count == 10);
	left = Column(0, 57, 19), right = Column(1, 57, 19);
	CHECK(left.first == 19 && left.count == 10 && right.first == 48 && right.count == 9);
	right = Column(1, 7, 0);
	CHECK(Column(0, 7, 0).count == 4 && right.first == 4 && right.count == 3);
	CHECK(Column(1, 0, 0).count == 0 && Column(0, 1, 0).count == 1 && Column(1, 1, 0).count == 0);

	// ACHIEVEMENTS: down moves the choice, the list follows it.
	f = {};
	Apply(f, Input::kRight, sizes);
	Apply(f, Input::kDown, sizes);
	CHECK(f.area == Area::kList && f.achievement == 0);
	for (int i = 0; i < 11; ++i) Apply(f, Input::kDown, sizes);
	CHECK(f.achievement == 11 && f.achievementScroll == 1);
	for (int i = 0; i < 60; ++i) Apply(f, Input::kDown, sizes);
	CHECK(f.achievement == kCount - 1 && f.achievementScroll == kCount - kAchievementRows);
	for (int i = 0; i < 31; ++i) Apply(f, Input::kUp, sizes);
	CHECK(f.achievement == 10 && f.achievementScroll == 10);
	// The tabs keep each tab's place.
	Apply(f, Input::kLeft, sizes);
	Apply(f, Input::kRight, sizes);
	CHECK(f.achievement == 10 && f.area == Area::kList);
	for (int i = 0; i < 11; ++i) Apply(f, Input::kUp, sizes);
	CHECK(f.area == Area::kTabs && f.achievement == 0);
	// The wheel: scrolls the view, the choice stays in it.
	f = {};
	f.tab = Tab::kAchievements;
	f.area = Area::kList;
	Scroll(f, 3, sizes);
	CHECK(f.achievementScroll == 3 && f.achievement == 3);
	Scroll(f, 100, sizes);
	CHECK(f.achievementScroll == kCount - kAchievementRows && f.achievement == kCount - kAchievementRows);
	Scroll(f, -100, sizes);
	CHECK(f.achievementScroll == 0 && f.achievement == kAchievementRows - 1); // pulled back into view
	f.tab = Tab::kStats;
	Scroll(f, 4, sizes);
	CHECK(f.statScroll == 4);
	Scroll(f, -9, sizes);
	CHECK(f.statScroll == 0);
	// Fewer lines next time: back in range.
	f.statScroll = 19;
	Clamp(f, Sizes{20});
	CHECK(f.statScroll == 0);

	// A row: the date it unlocked, or how far along.
	const int64_t nov2 = Day(2026, 11, 2);
	EQ(RowStatus(kCitySleuth, {61, 100}, 0), "61/100");
	EQ(RowStatus(kCitySleuth, {100, 100, true}, nov2), "2 NOV");
	EQ(RowStatus(kRunningRampant, {7, 12}, 0), "LEVEL 7/12");
	EQ(RowStatus(kOneIsBetter, {18, 30}, 0), "BEST 18/30 S");
	EQ(RowStatus(kBullInAChinaShop, {612400, 1000000}, 0), "$612,400");
	EQ(RowStatus(kDoneItAll, {34.6f, 100}, 0), "34%");
	EQ(RowStatus(kTakeTheCannoli, {12345, 100000}, 0), "12,345/100,000");
	EQ(RowStatus(kIrony, {0, 1}, 0), "");       // a mission's: done or not
	EQ(RowStatus(kLegalCounsel, {0, 1}, 0), "");
	EQ(RowStatus(kPieGuy, {9.99f, 10}, 0), "9/10"); // never rounded up to done
	// The panel.
	EQ(Detail(kCitySleuth, {61, 100}, 0), "61 OF 100 FOUND");
	EQ(Detail(kCitySleuth, {100, 100, true}, nov2), "UNLOCKED 2 NOV 2026");
	EQ(Detail(kBullInAChinaShop, {612400, 1000000}, 0), "$612,400 OF $1,000,000");
	EQ(Detail(kIrony, {0, 1}, 0), "");
	EQ(Detail(kBornIn80s, {3, 9}, 0), "3 OF 9 STATIONS");
	CHECK(Note(kCitySleuth, false).find("SAVE") != std::string::npos);
	CHECK(Note(kIrony, false).find("NO RECORD") != std::string::npos);
	CHECK(Note(kRetroLewk, false).find("SINCE") != std::string::npos);
	CHECK(Note(kCitySleuth, true).find("CHEAT") != std::string::npos);
	CHECK(Note(kKingpin, false) != Note(kCitySleuth, false));
	CHECK(Note(kLegalCounsel, false).find("MISSION PASSED") != std::string::npos);
	CHECK(Note(kViceViceBaby, false).find("MISSION PASSED") != std::string::npos);
	CHECK(Note(kPieGuy, false).find("MISSION") == std::string::npos);

	// The heading font's letters.
	EQ(HeadingText("Born in the 80's"), "born in the 80s");
	EQ(HeadingText("Iron-y"), "iron y");
	EQ(HeadingText("I'm Famous!"), "im famous");
	EQ(HeadingText("Put 'Em Up"), "put em up");
	EQ(HeadingText("High Quality H2O"), "high quality h2o");
	EQ(HeadingText("Somebody Call the Wambulance?"), "somebody call the wambulance");

	// Wrap: words, no line wider than asked (a word wider than it on its own line).
	auto chars = [](const std::string &s) { return float(s.size()); };
	auto lines = Wrap("COUNTS FROM YOUR SAVE: PACKAGES FOUND BEFORE THE MOD STILL COUNT.", 22, chars);
	CHECK(lines.size() == 3 && lines[0] == "COUNTS FROM YOUR SAVE:" && lines[2] == "THE MOD STILL COUNT.");
	lines = Wrap("A SUPERCALIFRAGILISTIC WORD", 10, chars);
	CHECK(lines.size() == 3 && lines[1] == "SUPERCALIFRAGILISTIC");
	CHECK(Wrap("", 10, chars).empty());

	// The notice fades in and out.
	CHECK(NoticeAlpha(0, 5000) == 0 && NoticeAlpha(2500, 5000) == 1 && NoticeAlpha(5000, 5000) == 0);
	CHECK(NoticeAlpha(100, 5000) > 0 && NoticeAlpha(100, 5000) < 1 && NoticeAlpha(4800, 5000) < 1);
	CHECK(NoticeAlpha(9000, 5000) == 0);

	if (failures)
		return 1;
	std::puts("statsmodel: all stats page model tests passed");
}
