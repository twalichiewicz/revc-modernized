#pragma once
// The Stats page (#24), without the game: its two tabs, where the focus is,
// how its lists scroll, and what each achievement's row and panel say.
#include "achievementsmodel.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace statsmodel {

enum class Tab : uint8_t { kStats, kAchievements };
enum class Area : uint8_t { kTabs, kList };
enum class Input : uint8_t { kUp, kDown, kLeft, kRight, kSelect };

// The approved mockup's rows: STATS in two columns of 10, ACHIEVEMENTS 11 at a time.
constexpr int kStatRows = 10, kAchievementRows = 11;

struct Focus {
	Tab tab = Tab::kStats;
	Area area = Area::kTabs;
	int statScroll = 0; // first row of each column
	int achievement = 0, achievementScroll = 0;
};
struct Sizes {
	int stats = 0; // the game's stat lines, this time
	int achievements = achievements::kCount;
};

// Left / right switch the tab from anywhere; up / down move down the list, or
// up out of it to the tabs. Back isn't the page's: on a tab it resumes the game.
// Returns whether it was the page's (then the frontend mustn't see it).
bool Apply(Focus &f, Input in, const Sizes &sizes);
// A wheel tick over the list: +1 down, -1 up.
void Scroll(Focus &f, int ticks, const Sizes &sizes);
// After the stat lines are read again (fewer of them, say): back in range.
void Clamp(Focus &f, const Sizes &sizes);

// The STATS tab's columns: the lines in order, down the left then the right.
// Rows [first, first + count) of the list, for column 0 or 1.
struct Span { int first, count; };
Span Column(int column, int lines, int scroll);
int MaxStatScroll(int lines);

// An achievement's row: the date it unlocked ("2 NOV"), or how far along
// ("LEVEL 7/12", "$612,400", "61/100"), or nothing (it's done or not).
std::string RowStatus(achievements::Id id, const achievements::Progress &p, int64_t unlockedAt);
// Its panel's progress line ("61 OF 100 FOUND"), "" when it has none, and its
// note: where its progress comes from, or why nothing more unlocks.
std::string Detail(achievements::Id id, const achievements::Progress &p, int64_t unlockedAt);
std::string Note(achievements::Id id, bool cheated);
// The heading font has letters, digits and ':' only: "Born in the 80's" is "born in the 80s".
std::string HeadingText(const std::string &name);
// Words to lines no wider than `width` (as `measure` has them).
std::vector<std::string> Wrap(const std::string &text, float width, const std::function<float(const std::string &)> &measure);
// The unlock notice's opacity at `age` ms of `shownFor`: in quickly, out slowly.
float NoticeAlpha(uint32_t age, uint32_t shownFor);

} // namespace statsmodel
