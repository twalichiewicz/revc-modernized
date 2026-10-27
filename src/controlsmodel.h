#pragma once
// What the Controls page shows and how it's moved about, apart from the game
// (controlspage.h draws it): the pad's legend for each situation, the actions
// whose keys can be set, the pad's and the keyboard's status, the rule for a
// key that's already taken, and the page's focus. Unit-tested
// (tests/test_controlsmodel.cpp).
#include "bindings.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace controlsmodel {

enum class PageSituation : uint8_t { kOnFoot, kDriving, kFlying, kPauseMap, kCount };

struct LegendRow {
	bindings::Pad pad;
	std::string label;
	std::string more;                          // a second line ("CLICK WHILE AIMING: BULLET TIME")
	bindings::Pad pad2 = bindings::Pad::kNone; // a second button (LB / RB)
};
// The pad's buttons and what they do there, from bindings::PadFor (the sticks and the pause map: fixed).
std::vector<LegendRow> Legend(uint16_t switches, PageSituation s);

struct KeySection {
	const char *title; // "on foot", "driving"
	std::vector<bindings::Action> actions;
};
const std::vector<KeySection> &KeySections();
// An action's name on the page ("FIRE", "WEAPON WHEEL"); "" for one it doesn't list.
const char *Label(bindings::Action a);

enum class Dot : uint8_t { kReady, kAttention, kMissing };
struct Joystick {
	int id;
	std::string name;
	bool gamepad; // GLFW has a button layout for it
};
struct Status {
	Dot dot;
	std::string name;
	std::string why;  // why it needs attention
	int useThis = -1; // the pad USE THIS ONE switches the game to
};
Status PadStatus(const std::vector<Joystick> &connected, int gameJoystick);
Status KeyStatus(int unbound);

// The keys of every listed action, a slot each: the game's [0] keyboard key,
// [1] the second key, [2] the mouse button; the mod's [0] and [1] (either may
// be a mouse button). 0: none.
using KeyTable = std::map<bindings::Action, std::array<uint16_t, 3>>;
int Unbound(const KeyTable &table); // actions with no key at all
// `key` goes to `a`'s `slot`, taken from every other action whose situations
// overlap (bindings::ScopeOf); returns those it was taken from.
std::vector<bindings::Action> Rebind(KeyTable &table, bindings::Action a, int slot, uint16_t key);

enum class Tab : uint8_t { kController, kKeyboard };
enum class Area : uint8_t { kTabs, kSituations, kSettings, kKeys };
struct Focus {
	Tab tab = Tab::kController;
	Area area = Area::kTabs;
	PageSituation situation = PageSituation::kOnFoot;
	int item = 0; // the setting, or the key row (across the sections, in order)
	int slot = 0; // the key cap in a row
	bool capturing = false;
};
enum class Input : uint8_t { kUp, kDown, kLeft, kRight, kSelect, kBack, kEscape };
struct Sizes {
	int settings;    // on the Controller tab
	int keyRows;     // KeySections' rows, all of them
	int slider = -1; // the setting that's a slider: left / right step it, not the focus
};
// What the input does to the focus; false: it isn't the page's (Back leaves it).
bool Apply(Focus &f, Input in, const Sizes &sizes);
// Left / right on the focused slider: -1 or +1 bar; else 0.
int SliderStep(const Focus &f, Input in, const Sizes &sizes);

// CFont's scale for text whose glyphs are `height` screen pixels tall, as the
// mockup drew them; `lineAtOne` is CFont::GetTextRect's line at scale 1.
struct TextScale {
	float x, y;
};
TextScale ScaleFor(float height, float lineAtOne);

// A left click while a key is wanted: on the cap that wants it, it's the key
// (LEFT); on another of the page's targets, it's that target's click; elsewhere,
// it cancels (a click meant for something else never takes FIRE's button).
enum class CaptureClick : uint8_t { kBind, kRedirect, kCancel };
CaptureClick OnClickWhileCapturing(bool onCap, bool onTarget);

// The left button, shared with the frontend, which selects on its release: a
// press the page took keeps its release from the frontend (else the page's
// select runs twice, or a click on BACK is lost).
class MouseGate {
public:
	void Press(bool page) { page_ = page; }
	// The release: true if it's the frontend's.
	bool Release()
	{
		const bool frontend = !page_;
		page_ = false;
		return frontend;
	}

private:
	bool page_ = false;
};

} // namespace controlsmodel
