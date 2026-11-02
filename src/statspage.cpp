#include "statspage.h"
#include "achievements.h"
#include "config.h"
#include "controlsmodel.h"
#include "draw.h"
#include "game.h"
#include "text.h"
#include <algorithm>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <vector>

using namespace game;
namespace sm = statsmodel;
namespace statspage {
namespace {

constexpr int32_t kActionGoBack = 35;
constexpr CRGBA kWhite{255, 255, 255, 255}, kPink{255, 150, 225, 255}, kDim{255, 255, 255, 153}, kBox{0, 0, 0, 158},
    kTrack{255, 255, 255, 56};

bool gAvailable, gFrontendRelease;
int32_t (*gConstructStatLine)(int32_t row);
const uint16_t *gLabelBuffer, *gValueBuffer; // gUString, gUString2
sm::Focus gFocus;
controlsmodel::MouseGate gMouse;
Im2DCanvas gCanvas, gNoticeCanvas;

using Wide = std::vector<uint16_t>; // the game's 16-bit text, 0-ended
struct StatLine { Wide label, value; };
std::vector<StatLine> gLines;

Wide Upper(const uint16_t *s)
{
	Wide out;
	for (; s && *s; ++s)
		out.push_back(*s >= 'a' && *s <= 'z' ? uint16_t(*s - 32) : *s);
	out.push_back(0);
	return out;
}
Wide FromAscii(const std::string &s)
{
	Wide out(s.begin(), s.end());
	out.push_back(0);
	return out;
}
std::string UpperAscii(std::string s)
{
	for (char &c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
	return s;
}

// The game's own lines, as its page lists them (and only those that apply).
void ReadLines()
{
	gLines.clear();
	const int count = gConstructStatLine(99999);
	for (int i = 0; i < count && i < 400; ++i)
		if (gConstructStatLine(i) == 0)
			gLines.push_back({Upper(gLabelBuffer), Upper(gValueBuffer)});
}

// The approved mockup's 1600x900 stage on the screen.
struct Stage {
	float s, ox;
	Vec2 P(float x, float y) const { return {ox + x * s, y * s}; }
	float L(float v) const { return v * s; }
	float X(float x) const { return ox + x * s; }
};
Stage Now()
{
	const float W = float(ScreenWidth()), H = float(ScreenHeight()), s = H / 900.0f;
	return {s, (W - 1600.0f * s) / 2};
}
// The game's font, its line `px` stage pixels tall (font 1 the wide capitals, 2 the heading).
text::Style Font(int16_t font, float px, CRGBA col, const Stage &st)
{
	static float unit[3];
	if (!unit[font]) {
		float t, b;
		text::LineExtent({font, 1.0f, 1.0f, col}, 0.0f, t, b);
		unit[font] = b - t > 0 ? b - t : 1.0f;
	}
	const controlsmodel::TextScale scale = controlsmodel::ScaleFor(st.L(px), unit[font]);
	return {font, scale.x, scale.y, col};
}

// Labels go after every panel (menulabels.h has why); these may be the game's 16-bit text.
struct Label {
	float x, cy;
	Wide text;
	text::Style style;
};
std::vector<Label> gLabels;
void Text(float x, float cy, const Wide &s, const text::Style &style) { gLabels.push_back({x, cy, s, style}); }
void Text(float x, float cy, const std::string &s, const text::Style &style) { Text(x, cy, FromAscii(s), style); }
float Width(const Wide &s, const text::Style &style) { return text::Width(s.data(), style); }
float Width(const std::string &s, const text::Style &style) { return text::Width(s.c_str(), style); }
void Heading(float x, float cy, const std::string &s, float px, CRGBA col, const Stage &st)
{
	const float shadow = st.L(px * 0.05f);
	Text(x + shadow, cy + shadow, s, Font(2, px, {20, 20, 20, uint8_t(230 * col.a / 255)}, st));
	Text(x, cy, s, Font(2, px, col, st));
}
void FlushLabels()
{
	gCanvas.Flush();
	for (const Label &label : gLabels) {
		float top, bottom;
		text::LineExtent(label.style, 0, top, bottom);
		text::Draw(label.x, label.cy - (top + bottom) / 2, label.text.data(), label.style);
	}
	gLabels.clear();
	text::Flush();
}
void Box(float x0, float y0, float x1, float y1, CRGBA col, const Stage &st)
{
	gCanvas.Quad(st.P(x0, y0), st.P(x1, y0), st.P(x1, y1), st.P(x0, y1), col);
}
// A thin bar's share of a track: the view's place in a longer list.
void ScrollThumb(float x, float y0, float y1, int first, int shown, int total, const Stage &st)
{
	if (total <= shown)
		return;
	const float h = y1 - y0, top = y0 + h * float(first) / float(total), len = h * float(shown) / float(total);
	Box(x, top, x + 5, top + len, kPink, st);
}
void Bar(float x0, float y0, float x1, float y1, float share, const Stage &st)
{
	Box(x0, y0, x1, y1, kTrack, st);
	if (share > 0)
		Box(x0, y0, x0 + (x1 - x0) * std::min(share, 1.0f), y1, kPink, st);
}

struct Target {
	float x0, y0, x1, y1;
	enum Kind { kTab, kRow } kind;
	int index;
};
std::vector<Target> gTargets;
const Target *Hit(uint8_t *menu)
{
	const float x = float(MenuMouseX(menu)), y = float(MenuMouseY(menu));
	for (const Target &t : gTargets)
		if (x >= t.x0 && x < t.x1 && y >= t.y0 && y < t.y1)
			return &t;
	return nullptr;
}

// The Controls page's pill row: the chosen one white, centred at stage y `top`.
void Pills(const std::vector<std::string> &items, int chosen, float top, float px, float h, bool focused, const Stage &st)
{
	const text::Style style = Font(1, px, kWhite, st);
	const float pad = st.L(20), gap = st.L(10), edge = st.L(7);
	std::vector<float> widths;
	float total = 0;
	for (const std::string &s : items) {
		widths.push_back(Width(s, style) + 2 * pad);
		total += widths.back();
	}
	total += gap * float(items.size() - 1);
	float x = st.X(800) - total / 2;
	const float y = st.L(top), ph = st.L(h);
	gCanvas.Pill(x - edge, y - edge, total + 2 * edge, ph + 2 * edge, {0, 0, 0, 171});
	if (focused)
		gCanvas.RoundRectOutline(x - edge, y - edge, total + 2 * edge, ph + 2 * edge, (ph + 2 * edge) / 2, st.L(2), kPink);
	for (size_t i = 0; i < items.size(); i++) {
		const bool on = int(i) == chosen;
		gTargets.push_back({x, y, x + widths[i], y + ph, Target::kTab, int(i)});
		if (on)
			gCanvas.Pill(x, y, widths[i], ph, kWhite);
		Text(x + pad, y + ph / 2, items[i], Font(1, px, on ? CRGBA{0, 0, 0, 255} : CRGBA{255, 255, 255, 171}, st));
		x += widths[i] + gap;
	}
}

sm::Sizes Sizes() { return {int(gLines.size()), achievements::kCount}; }

void StatsTab(const Stage &st)
{
	const int lines = int(gLines.size());
	if (!lines) {
		Box(120, 196, 1490, 300, kBox, st);
		Text(st.X(146), st.L(248), std::string("NO STATS YET"), Font(1, 22, kDim, st));
		return;
	}
	for (int column = 0; column < 2; ++column) {
		const float x0 = column ? 820 : 120, x1 = x0 + 670;
		Box(x0, 196, x1, 740, kBox, st);
		const sm::Span span = sm::Column(column, lines, gFocus.statScroll);
		for (int r = 0; r < span.count; ++r) {
			const StatLine &line = gLines[size_t(span.first + r)];
			const float cy = 227 + 52.0f * r;
			if (r % 2)
				Box(x0 + 4, cy - 26, x1 - 4, cy + 26, {255, 255, 255, 12}, st);
			const text::Style value = Font(1, 22, kPink, st);
			const bool valueOnly = line.label.size() <= 1;
			if (valueOnly) { // a name the game lists on its own (one of the gangs, say)
				Text(st.X(x0 + 26), st.L(cy), line.value, Font(1, 22, kPink, st));
				continue;
			}
			const float valueW = Width(line.value, value);
			text::Style label = Font(1, 22, kWhite, st);
			const float room = st.L(670 - 52 - 24) - valueW, labelW = Width(line.label, label);
			if (labelW > room && room > 0)
				label.scaleX *= room / labelW; // a long one, squeezed rather than over its value
			Text(st.X(x0 + 26), st.L(cy), line.label, label);
			Text(st.X(x1 - 26) - valueW, st.L(cy), line.value, value);
		}
	}
	const int half = (lines + 1) / 2;
	ScrollThumb(802, 196, 740, gFocus.statScroll, sm::kStatRows, half, st);
}

void AchievementsTab(const Stage &st)
{
	using namespace achievements;
	const Record &record = Current();
	const Facts facts = Snapshot();
	int unlocked = 0, others = 0;
	for (int i = 0; i < kCount; ++i) {
		unlocked += record.Unlocked(Id(i));
		others += i != kKingpin && record.Unlocked(Id(i));
	}
	// The header: how many, a bar, how far to Kingpin (or why nothing more unlocks).
	Box(120, 196, 1490, 248, kBox, st);
	Heading(st.X(150), st.L(222), std::to_string(unlocked) + " of " + std::to_string(kCount) + " unlocked", 34, kPink, st);
	Bar(540, 214, 1290, 226, float(unlocked) / kCount, st);
	const std::string kingpin = facts.cheated                ? "CHEATS USED: NONE UNLOCK"
	                            : record.Unlocked(kKingpin) ? "KINGPIN"
	                                                        : std::to_string(kCount - 1 - others) + " TO KINGPIN";
	const text::Style small = Font(1, 19, facts.cheated ? kPink : kDim, st);
	Text(st.X(1470) - Width(kingpin, small), st.L(221), kingpin, small);

	// The list.
	Box(120, 264, 900, 740, kBox, st);
	for (int r = 0; r < sm::kAchievementRows; ++r) {
		const int i = gFocus.achievementScroll + r;
		if (i >= kCount)
			break;
		const Id id = Id(i);
		const float cy = 290 + 42.0f * r;
		const bool chosen = i == gFocus.achievement, done = record.Unlocked(id);
		if (chosen)
			Box(124, cy - 20, 896, cy + 20, gFocus.area == sm::Area::kList ? CRGBA{255, 150, 225, 60} : CRGBA{255, 255, 255, 30}, st);
		gTargets.push_back({st.X(124), st.L(cy - 20), st.X(896), st.L(cy + 20), Target::kRow, i});
		const float dot = st.L(9);
		gCanvas.Pill(st.X(151) - dot, st.L(cy) - dot, 2 * dot, 2 * dot, done ? kPink : CRGBA{255, 255, 255, 48});
		Text(st.X(178), st.L(cy), UpperAscii(Definition(id).name), Font(1, 22, done ? kWhite : kDim, st));
		const std::string status = sm::RowStatus(id, Measure(id, facts, record), record.unlockedAt[size_t(i)]);
		const text::Style statusStyle = Font(1, 19, done ? kPink : kDim, st);
		Text(st.X(880) - Width(status, statusStyle), st.L(cy), status, statusStyle);
	}
	ScrollThumb(905, 264, 740, gFocus.achievementScroll, sm::kAchievementRows, kCount, st);

	// The chosen one's panel: its name, what it asks, how far, and where that comes from.
	const Id id = Id(std::clamp(gFocus.achievement, 0, kCount - 1));
	const Progress progress = Measure(id, facts, record);
	const int64_t at = record.unlockedAt[size_t(id)];
	const float left = 966, width = 500;
	const std::string heading = sm::HeadingText(Definition(id).name);
	float headingPx = 52;
	const float headingW = Width(heading, Font(2, headingPx, kPink, st));
	if (headingW > st.L(width))
		headingPx *= st.L(width) / headingW;
	auto measure = [&](const text::Style &style) {
		return [&, style](const std::string &s) { return Width(s, style); };
	};
	const text::Style body = Font(1, 20, kWhite, st), dim = Font(1, 19, at ? kPink : kDim, st),
	                  note = Font(1, 16, kDim, st);
	const auto description = sm::Wrap(UpperAscii(Definition(id).description), st.L(width), measure(body));
	const std::string detail = sm::Detail(id, progress, at);
	const auto notes = sm::Wrap(sm::Note(id, facts.cheated), st.L(width), measure(note));
	float y = 371 + 30.0f * float(description.size() - 1) + 60;
	const float detailY = y;
	const bool bar = progress.target > 1;
	if (!detail.empty())
		y += bar ? 70 : 40;
	const float noteY = y;
	const float bottom = std::max(620.0f, noteY + 28.0f * float(notes.size()) + 10);
	Box(940, 264, 1490, bottom, kBox, st);
	Heading(st.X(left), st.L(312), heading, headingPx, kPink, st);
	for (size_t i = 0; i < description.size(); ++i)
		Text(st.X(left), st.L(371 + 30.0f * float(i)), description[i], body);
	if (!detail.empty()) {
		Text(st.X(left), st.L(detailY), detail, dim);
		if (bar)
			Bar(left, detailY + 26, left + 494, detailY + 42, at ? 1.0f : progress.current / progress.target, st);
	}
	for (size_t i = 0; i < notes.size(); ++i)
		Text(st.X(left), st.L(noteY + 28.0f * float(i)), notes[i], note);
}

} // namespace

bool Install()
{
	gConstructStatLine = reinterpret_cast<int32_t (*)(int32_t)>(dlsym(RTLD_MAIN_ONLY, "_ZN6CStats17ConstructStatLineEi"));
	gLabelBuffer = static_cast<const uint16_t *>(dlsym(RTLD_MAIN_ONLY, "gUString"));
	gValueBuffer = static_cast<const uint16_t *>(dlsym(RTLD_MAIN_ONLY, "gUString2"));
	return gConstructStatLine && gLabelBuffer && gValueBuffer;
}
void SetAvailable(bool available) { gAvailable = available; }
bool Available() { return gAvailable && achievements::Ready(); }

void Enter(uint8_t *menu)
{
	if (!Available())
		return;
	ReadLines();
	gFocus.area = sm::Area::kTabs;
	sm::Clamp(gFocus, Sizes());
	gMouse = {};
	gFrontendRelease = false;
	gTargets.clear();
	// The page's one entry, Back, from its line in the middle (the old list's) to
	// where the map page has its own, under the panels.
	auto backOf = [](int32_t page) -> uint8_t * {
		for (int i = 0; i < kMenuEntries; i++) {
			const int32_t action = *reinterpret_cast<const int32_t *>(MenuPageEntry(page, i));
			if (action == 0)
				break;
			if (action == kActionGoBack)
				return MenuPageEntry(page, i);
		}
		return nullptr;
	};
	uint8_t *back = backOf(kPageStats);
	const uint8_t *map = backOf(kPageMap);
	if (back && map)
		std::memcpy(back + 32, map + 32, 5); // x, y (uint16), align
	else if (back)
		*reinterpret_cast<uint16_t *>(back + 34) = 396; // under the panels, as the Controls page's
	MenuOption(menu) = 0;
}

void Draw(uint8_t *)
{
	if (!Available())
		return;
	const Stage st = Now();
	gTargets.clear();
	gLabels.clear();
	text::Flush(); // the frontend's title and Back, before our panels
	SetHudRenderStates();
	gCanvas.SetTexture(nullptr);
	auto &wrapX = *reinterpret_cast<float *>(api.fontDetails + 0x2c);
	const float wrap = wrapX;
	api.FontSetWrapx(float(ScreenWidth()));
	Pills({"STATS", "ACHIEVEMENTS"}, int(gFocus.tab), 128, 26, 36, gFocus.area == sm::Area::kTabs, st);
	const bool stats = gFocus.tab == sm::Tab::kStats;
	if (stats)
		StatsTab(st);
	else
		AchievementsTab(st);
	const bool scrolls = !stats || sm::MaxStatScroll(int(gLines.size())) > 0;
	const std::string help = scrolls ? "UP / DOWN TO SCROLL - LEFT / RIGHT: STATS OR ACHIEVEMENTS"
	                                 : "LEFT / RIGHT: STATS OR ACHIEVEMENTS";
	const text::Style style = Font(1, 20, kWhite, st);
	Text(st.X(1580) - Width(help, style), st.L(872), help, style);
	FlushLabels();
	api.FontSetWrapx(wrap);
}

bool UserInput(uint8_t *menu)
{
	if (!Available())
		return false;
	// The frontend selects on the left button's release: a click the page took keeps it.
	gFrontendRelease = false;
	if (!api.newMouseState[0] && api.oldMouseState[0]) {
		gFrontendRelease = gMouse.Release();
		if (!gFrontendRelease)
			api.oldMouseState[0] = 0;
	}
	if (LeftClickJustDown()) {
		const Target *hit = Hit(menu);
		gMouse.Press(hit != nullptr);
		if (hit) {
			api.oldMouseState[0] = 1;
			if (hit->kind == Target::kTab) {
				gFocus.tab = sm::Tab(hit->index);
				gFocus.area = sm::Area::kTabs;
			} else {
				gFocus.area = sm::Area::kList;
				gFocus.achievement = hit->index;
			}
		}
	}
	// The wheel (CMouseControllerState: up at +3, down at +4).
	const int ticks = (api.newMouseState[4] && !api.oldMouseState[4]) - (api.newMouseState[3] && !api.oldMouseState[3]);
	if (ticks)
		sm::Scroll(gFocus, ticks, Sizes());
	return false;
}

void ProcessInput(uint8_t *, uint8_t &down, uint8_t &up, uint8_t &select, uint8_t &back, int8_t &change)
{
	if (!Available() || back || (select && gFrontendRelease))
		return; // Back resumes the game, as on every tab; so does a click on the page's Back
	sm::Input input;
	if (up)
		input = sm::Input::kUp;
	else if (down)
		input = sm::Input::kDown;
	else if (change < 0)
		input = sm::Input::kLeft;
	else if (change > 0)
		input = sm::Input::kRight;
	else if (select)
		input = sm::Input::kSelect;
	else
		return;
	if (sm::Apply(gFocus, input, Sizes()))
		down = up = select = 0, change = 0;
}

void DrawNotice()
{
	achievements::Shown shown;
	if (!achievements::Notice(shown) || !shown.def)
		return;
	const float alpha = sm::NoticeAlpha(shown.age, shown.shownFor);
	if (alpha <= 0)
		return;
	const Stage st = Now();
	const float W = float(ScreenWidth()), H = float(ScreenHeight());
	const uint8_t a = uint8_t(255 * alpha);
	const bool many = shown.count > 1;
	const std::string kicker = many ? "ACHIEVEMENTS UNLOCKED" : "ACHIEVEMENT UNLOCKED";
	const std::string name = many ? std::to_string(shown.count) + " from your save" : sm::HeadingText(shown.def->name);
	const text::Style small = Font(1, 18, {255, 150, 225, a}, st), big = Font(2, 40, {255, 255, 255, a}, st),
	                  shadow = Font(2, 40, {20, 20, 20, uint8_t(230 * alpha)}, st);
	const float w = std::max(text::Width(kicker.c_str(), small), text::Width(name.c_str(), big)) + st.L(56);
	const float h = st.L(96), x = (W - w) / 2, y = gConfig.margin * H;
	SetHudRenderStates();
	gNoticeCanvas.SetTexture(nullptr);
	gNoticeCanvas.RoundRect(x, y, w, h, st.L(14), {0, 0, 0, uint8_t(200 * alpha)});
	gNoticeCanvas.Quad({x + st.L(14), y + h - st.L(5)}, {x + w - st.L(14), y + h - st.L(5)}, {x + w - st.L(14), y + h},
	                   {x + st.L(14), y + h}, {255, 150, 225, a});
	gNoticeCanvas.Flush();
	auto centred = [&](const std::string &s, float cy, const text::Style &style, float dx = 0) {
		float top, bottom;
		text::LineExtent(style, 0, top, bottom);
		text::Draw((W - text::Width(s.c_str(), style)) / 2 + dx, cy - (top + bottom) / 2 + dx, s.c_str(), style);
	};
	centred(kicker, y + st.L(28), small);
	centred(name, y + st.L(62), shadow, st.L(2));
	centred(name, y + st.L(62), big);
	text::Flush();
}

} // namespace statspage
