// Unit tests for the Controls page's model: make test
#include "controlsmodel.h"

#include <cmath>
#include <cstdio>
#include <string>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

using namespace controlsmodel;
using bindings::Action;
using bindings::Pad;
static const LegendRow *Row(const std::vector<LegendRow> &rows, Pad pad)
{
	for (const LegendRow &r : rows)
		if (r.pad == pad)
			return &r;
	return nullptr;
}

static void TestLegendOnFoot()
{
	const auto rows = Legend(bindings::kAll, PageSituation::kOnFoot);
	CHECK(Row(rows, Pad::kRT) && Row(rows, Pad::kRT)->label == "FIRE");
	CHECK(Row(rows, Pad::kLeftRight) && Row(rows, Pad::kLeftRight)->label == "PREVIOUS / NEXT WEAPON");
	CHECK(Row(rows, Pad::kRightStick) && Row(rows, Pad::kRightStick)->more == "CLICK WHILE AIMING: BULLET TIME");
	CHECK(rows.size() == 13);
}

static void TestLegendFollowsSwitches()
{
	const auto rows = Legend(bindings::kAll & ~bindings::kTriggers, PageSituation::kOnFoot);
	CHECK(Row(rows, Pad::kB) && Row(rows, Pad::kB)->label == "FIRE"); // the game's own B
}

static void TestLegendPauseMap()
{
	const auto rows = Legend(bindings::kAll, PageSituation::kPauseMap);
	CHECK(Row(rows, Pad::kA) && Row(rows, Pad::kA)->label == "SET DESTINATION");
	CHECK(rows.size() == 7);
}

static void TestKeySections()
{
	CHECK(KeySections().size() == 2);
	CHECK(std::string(KeySections()[0].title) == "on foot" && std::string(KeySections()[1].title) == "driving");
	CHECK(std::string(Label(Action::kWeaponWheel)) == "WEAPON WHEEL");
}

static void TestStatusMissing()
{
	const Status s = PadStatus({}, -1);
	CHECK(s.dot == Dot::kMissing && s.name == "NO CONTROLLER CONNECTED");
}

static void TestStatusReady()
{
	const Status s = PadStatus({{0, "DualSense Wireless Controller", true}}, 0);
	CHECK(s.dot == Dot::kReady && s.name == "DUALSENSE WIRELESS CONTROLLER" && s.why.empty());
}

static void TestStatusNoLayout()
{
	const Status s = PadStatus({{0, "8BitDo Pro 2", false}}, 0);
	CHECK(s.dot == Dot::kAttention && s.why == "NO BUTTON LAYOUT FOR THIS PAD");
}

static void TestStatusSteamVirtual()
{
	const Status s = PadStatus({{0, "Steam Virtual Gamepad", true}, {1, "DualSense Wireless Controller", true}}, 0);
	CHECK(s.dot == Dot::kAttention && s.why == "THE GAME IS READING STEAM'S VIRTUAL PAD" && s.useThis == 1);
}

static void TestKeyStatus()
{
	CHECK(KeyStatus(0).dot == Dot::kReady && KeyStatus(0).name == "KEYBOARD AND MOUSE");
	CHECK(KeyStatus(2).why == "2 ACTIONS HAVE NO KEY" && KeyStatus(1).why == "1 ACTION HAS NO KEY");
}

static void TestRebindTakesKeyFromOverlap()
{
	KeyTable t;
	t[Action::kPedJumping] = {' ', 0, 0};
	t[Action::kWeaponWheel] = {'Q', 0, 0};
	const auto taken = Rebind(t, Action::kWeaponWheel, 0, ' ');
	CHECK(taken.size() == 1 && taken[0] == Action::kPedJumping);
	CHECK(t[Action::kPedJumping][0] == 0 && t[Action::kWeaponWheel][0] == ' ');
	CHECK(Unbound(t) == 1);
}

static void TestRebindKeepsNonOverlap()
{
	KeyTable t;
	t[Action::kVehicleHandbrake] = {' ', 0, 0}; // in a vehicle
	t[Action::kPedJumping] = {'J', 0, 0};       // on foot
	CHECK(Rebind(t, Action::kPedJumping, 0, ' ').empty());
	CHECK(t[Action::kVehicleHandbrake][0] == ' ');
}

// The game checks ENTER/EXIT against the on-foot actions too (DeleteMatchingVehicle_3rdPersonControls).
static void TestRebindTakesEnterExitOnFoot()
{
	KeyTable t;
	t[Action::kVehicleEnterExit] = {'F', 0, 0};
	t[Action::kPedJumping] = {' ', 0, 0};
	const auto taken = Rebind(t, Action::kPedJumping, 0, 'F');
	CHECK(taken.size() == 1 && taken[0] == Action::kVehicleEnterExit);
}

// First-person look is on foot only: the horn can share its key.
static void TestRebindHornKeepsFirstPersonLook()
{
	KeyTable t;
	t[Action::kPed1rstPersonLookUp] = {'U', 0, 0};
	t[Action::kVehicleHorn] = {'H', 0, 0};
	CHECK(Rebind(t, Action::kVehicleHorn, 0, 'U').empty());
	CHECK(t[Action::kPed1rstPersonLookUp][0] == 'U');
}

static void TestSliderStepsWithLeftRight()
{
	Focus f;
	f.area = Area::kSettings;
	f.item = 2;
	const Sizes s{3, 10, 2};
	CHECK(SliderStep(f, Input::kLeft, s) == -1);
	CHECK(SliderStep(f, Input::kRight, s) == 1);
	CHECK(SliderStep(f, Input::kUp, s) == 0);
	CHECK(Apply(f, Input::kLeft, s) && f.item == 2); // stays on the slider
	f.item = 1;
	CHECK(SliderStep(f, Input::kLeft, s) == 0);
	f.tab = Tab::kKeyboard;
	f.area = Area::kKeys;
	f.item = 2;
	CHECK(SliderStep(f, Input::kLeft, s) == 0); // the keys' rows aren't the settings
}

static void TestClickWhileCapturing()
{
	CHECK(OnClickWhileCapturing(true, true) == CaptureClick::kBind);      // the cap that wants a key: LEFT
	CHECK(OnClickWhileCapturing(false, true) == CaptureClick::kRedirect); // another of the page's targets
	CHECK(OnClickWhileCapturing(false, false) == CaptureClick::kCancel);  // elsewhere
}

// The frontend selects on the release: a press the page took keeps its release from it.
static void TestMouseGate()
{
	MouseGate g;
	g.Press(true);
	CHECK(!g.Release());
	g.Press(false);
	CHECK(g.Release()); // its Back
	CHECK(g.Release()); // no press seen
}

static void TestFocusMovesThroughAreas()
{
	Focus f;
	const Sizes sizes{3, 40};
	CHECK(Apply(f, Input::kDown, sizes) && f.area == Area::kSituations);
	CHECK(Apply(f, Input::kRight, sizes) && f.situation == PageSituation::kDriving);
	CHECK(Apply(f, Input::kDown, sizes) && f.area == Area::kSettings);
	CHECK(Apply(f, Input::kUp, sizes) && Apply(f, Input::kUp, sizes) && f.area == Area::kTabs);
	CHECK(Apply(f, Input::kRight, sizes) && f.tab == Tab::kKeyboard);
	CHECK(Apply(f, Input::kDown, sizes) && f.area == Area::kKeys && f.item == 0);
}

static void TestCaptureEscapeCancels()
{
	Focus f;
	f.tab = Tab::kKeyboard;
	f.area = Area::kKeys;
	const Sizes sizes{3, 40};
	CHECK(Apply(f, Input::kSelect, sizes) && f.capturing);
	CHECK(Apply(f, Input::kEscape, sizes) && !f.capturing); // the page keeps the Escape
	CHECK(!Apply(f, Input::kEscape, sizes));                 // now it's the frontend's: back to Options
}

static void TestSituationsWrap()
{
	Focus f;
	f.area = Area::kSituations;
	CHECK(Apply(f, Input::kLeft, {3, 40}) && f.situation == PageSituation::kPauseMap);
}

// The mockup drew a glyph's cell (64x80 of the font sheet) px tall and 0.8 px
// across; CFont::PrintChar draws it 32 x wide and 20 y tall.
static void TestTextMatchesMockup()
{
	const float height = 26 * 1.2f;                  // 26 stage pixels, 1080 lines
	const TextScale t = ScaleFor(height, 20.0f);     // GetTextRect's line at scale 1
	CHECK(std::fabs(20 * t.y - height) < 0.01f);
	CHECK(std::fabs(32 * t.x - 0.8f * height) < 0.01f);
}

int main()
{
	TestLegendOnFoot();
	TestLegendFollowsSwitches();
	TestLegendPauseMap();
	TestKeySections();
	TestStatusMissing();
	TestStatusReady();
	TestStatusNoLayout();
	TestStatusSteamVirtual();
	TestKeyStatus();
	TestRebindTakesKeyFromOverlap();
	TestRebindKeepsNonOverlap();
	TestRebindTakesEnterExitOnFoot();
	TestRebindHornKeepsFirstPersonLook();
	TestSliderStepsWithLeftRight();
	TestClickWhileCapturing();
	TestMouseGate();
	TestFocusMovesThroughAreas();
	TestCaptureEscapeCancels();
	TestSituationsWrap();
	TestTextMatchesMockup();
	if (gFailures) {
		printf("%d controlsmodel test(s) failed\n", gFailures);
		return 1;
	}
	printf("all controlsmodel tests passed\n");
	return 0;
}
