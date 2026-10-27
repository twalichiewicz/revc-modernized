// Unit tests for the table of what each action is on: make test
#include "bindings.h"

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

using namespace bindings;
static void TestOnFootModern()
{
	CHECK(PadFor(kAll, Situation::kOnFoot, Action::kPedFireweapon).pad == Pad::kRT);
	CHECK(PadFor(kAll, Situation::kOnFoot, Action::kPedLockTarget).pad == Pad::kLT);
	CHECK(PadFor(kAll, Situation::kOnFoot, Action::kPedCycleWeaponLeft).pad == Pad::kLeft);
	CHECK(PadFor(kAll, Situation::kOnFoot, Action::kWeaponWheel).press == Press::kHold);
	CHECK(PadFor(kAll, Situation::kOnFoot, Action::kPedAnswerPhone).press == Press::kTap);
	CHECK(PadFor(kAll, Situation::kOnFoot, Action::kAnswerCall).pad == Pad::kUp);
	CHECK(PadFor(kAll, Situation::kOnFoot, Action::kBulletTime).press == Press::kWhileAiming);
	CHECK(!PadFor(kAll, Situation::kOnFoot, Action::kPedSprint).ours); // the game's own A
}

static void TestSwitchOffFallsBackToGame()
{
	CHECK(!PadFor(0, Situation::kOnFoot, Action::kPedFireweapon).ours);
	CHECK(!PadFor(kAll & ~kTriggers, Situation::kDriving, Action::kVehicleAccelerate).ours);
	CHECK(Reachable(0, Situation::kDriving, Action::kVehicleLookleft)); // nothing taken with everything off
}

// controls.cpp only remaps the face buttons inside ModernTriggers: off, they're the game's.
static void TestButtonsNeedTriggers()
{
	const uint16_t noTriggers = kAll & ~kTriggers;
	CHECK(!PadFor(noTriggers, Situation::kArmedVehicle, Action::kPedFireweapon).ours);
	CHECK(!PadFor(noTriggers, Situation::kDriving, Action::kPedFireweapon).ours);
	CHECK(!PadFor(noTriggers, Situation::kOnFoot, Action::kReload).ours);
	CHECK(!PadFor(noTriggers, Situation::kHunter, Action::kPedFireweapon).ours);
	CHECK(!PadFor(noTriggers, Situation::kDriving, Action::kCinematicCamera).ours);
	CHECK(Reachable(noTriggers, Situation::kDriving, Action::kVehicleAccelerate)); // A drives again
}

// Moved to no button (look left in a car without drive-bys): a hint names nothing, not the game's old one.
static void TestHintTokenForNoButton()
{
	bool game = true;
	CHECK(std::string(HintToken(kAll & ~kDriveBy, Situation::kDriving, Action::kVehicleLookleft, game)).empty());
	CHECK(!game);
	CHECK(std::string(HintToken(kAll, Situation::kOnFoot, Action::kPedFireweapon, game)) == "~V~" && !game);
	CHECK(std::string(HintToken(kAll, Situation::kOnFoot, Action::kPedSprint, game)).empty() && game);
}

static void TestDrivingAndArmed()
{
	CHECK(PadFor(kAll, Situation::kDriving, Action::kVehicleChangeRadioStation).pad == Pad::kLeftRight);
	CHECK(PadFor(kAll, Situation::kDriving, Action::kPedFireweapon).pad == Pad::kA); // a car's own gun: a car bomb, a bike's guns
	CHECK(PadFor(kAll, Situation::kDriving, Action::kDriveByFire).press == Press::kWhileDriveByAim);
	CHECK(PadFor(kAll, Situation::kArmedVehicle, Action::kPedFireweapon).pad == Pad::kRB);
	CHECK(PadFor(kAll, Situation::kArmedVehicle, Action::kDriveByAim).pad == Pad::kNone);
	CHECK(PadFor(kAll, Situation::kDriving, Action::kVehicleLookbehind).pad == Pad::kR3);
}

static void TestAircraft()
{
	CHECK(PadFor(kAll, Situation::kHunter, Action::kPedFireweapon).pad == Pad::kX);
	CHECK(PadFor(kAll, Situation::kHunter, Action::kVehicleHandbrake).pad == Pad::kA);
	CHECK(PadFor(kAll, Situation::kHelicopter, Action::kVehicleLookleft).pad == Pad::kLB);
}

static void TestForVehicleAndHint()
{
	CHECK(ForVehicle(155, true) == Situation::kHunter);
	CHECK(ForVehicle(231, true) == Situation::kHelicopter);
	CHECK(ForVehicle(190, true) == Situation::kPlane);     // Skimmer
	CHECK(ForVehicle(162, false) == Situation::kArmedVehicle);
	CHECK(ForVehicle(160, false) == Situation::kArmedVehicle); // the Predator's cannons
	CHECK(ForVehicle(130, false) == Situation::kDriving);
	CHECK(ForHint(Situation::kOnFoot, Action::kVehicleHandbrake) == Situation::kDriving);
	CHECK(ForHint(Situation::kHunter, Action::kPedFireweapon) == Situation::kHunter);
}

// The audit: every action a hint names is pressable where it can be named.
static void TestHintActionsReachable()
{
	const Action onFoot[] = {Action::kPedFireweapon, Action::kPedLockTarget, Action::kPedCycleWeaponLeft,
	    Action::kPedCycleWeaponRight, Action::kPedCycleTargetLeft, Action::kPedCycleTargetRight, Action::kPedSprint,
	    Action::kPedJumping, Action::kPedDuck, Action::kPedLookbehind, Action::kPedAnswerPhone,
	    Action::kPedSniperZoomIn, Action::kPedSniperZoomOut, Action::kVehicleEnterExit, Action::kGoLeft,
	    Action::kGoRight, Action::kCameraChangeViewAllSituations};
	for (Action a : onFoot)
		CHECK(Reachable(kAll, Situation::kOnFoot, a));
	const Action driving[] = {Action::kVehicleAccelerate, Action::kVehicleBrake, Action::kVehicleHandbrake,
	    Action::kVehicleHorn, Action::kVehicleEnterExit, Action::kToggleSubmissions,
	    Action::kVehicleChangeRadioStation, Action::kVehicleLookbehind, Action::kPedFireweapon};
	for (Action a : driving)
		CHECK(Reachable(kAll, Situation::kDriving, a));
	CHECK(Reachable(kAll, Situation::kArmedVehicle, Action::kPedFireweapon));
	CHECK(Reachable(kAll, Situation::kArmedVehicle, Action::kVehicleTurretleft));
	CHECK(Reachable(kAll, Situation::kArmedVehicle, Action::kVehicleHandbrake));
	const Situation air[] = {Situation::kPlane, Situation::kHelicopter, Situation::kHunter, Situation::kSeaSparrow,
	                         Situation::kRcBaron};
	for (Situation s : air) {
		CHECK(Reachable(kAll, s, Action::kVehicleAccelerate));
		CHECK(Reachable(kAll, s, Action::kVehicleBrake));
		CHECK(Reachable(kAll, s, Action::kVehicleEnterExit));
	}
	for (Situation s : {Situation::kPlane, Situation::kHelicopter}) {
		CHECK(Reachable(kAll, s, Action::kPedFireweapon));
		CHECK(Reachable(kAll, s, Action::kVehicleHandbrake));
	}
	CHECK(Reachable(kAll, Situation::kPlane, Action::kVehicleLookleft));
	CHECK(Reachable(kAll, Situation::kPlane, Action::kVehicleLookright));
}

static void TestNamesRoundTrip()
{
	for (int i = 0; i < int(Action::kGameCount); i++) {
		if (i == 39) continue;
		Action a;
		CHECK(Name(Action(i)) && FromName(Name(Action(i)), a) && a == Action(i));
	}
	Action a;
	CHECK(FromName("MOD_ANSWER_CALL", a) && a == Action::kAnswerCall);
	CHECK(!FromName("NOT_AN_ACTION", a));
	CHECK(std::string(Token(Pad::kRT)) == "~V~" && std::string(Token(Pad::kUp)) == "~@u~");
	Action game;
	CHECK(KeyAlias(Action::kAnswerCall, game) && game == Action::kPedAnswerPhone && !HasOwnKeys(Action::kAnswerCall));
}

static void TestDefaultKeySkipsGameKey()
{
	auto none = [](uint16_t, Scope) { return false; };
	CHECK(DefaultKeys(Action::kWeaponWheel, none).key[0] == 'Q');
	auto qOnFoot = [](uint16_t k, Scope s) { return k == 'Q' && s == Scope::kOnFoot; };
	CHECK(DefaultKeys(Action::kWeaponWheel, qOnFoot).Empty());
	auto qInCar = [](uint16_t k, Scope s) { return k == 'Q' && s == Scope::kVehicle; };
	CHECK(DefaultKeys(Action::kWeaponWheel, qInCar).key[0] == 'Q'); // look left in a car doesn't clash on foot
}

static void TestFamilyForName()
{
	CHECK(FamilyForName("DualSense Wireless Controller") == PadFamily::kPlayStation);
	CHECK(FamilyForName("Nintendo Switch Pro Controller") == PadFamily::kNintendo);
	CHECK(FamilyForName("Xbox Wireless Controller") == PadFamily::kXbox);
	CHECK(FamilyForName("") == PadFamily::kXbox);
}

int main()
{
	TestOnFootModern();
	TestSwitchOffFallsBackToGame();
	TestButtonsNeedTriggers();
	TestHintTokenForNoButton();
	TestDrivingAndArmed();
	TestAircraft();
	TestForVehicleAndHint();
	TestHintActionsReachable();
	TestNamesRoundTrip();
	TestDefaultKeySkipsGameKey();
	TestFamilyForName();
	if (gFailures) {
		printf("%d bindings test(s) failed\n", gFailures);
		return 1;
	}
	printf("all bindings tests passed\n");
	return 0;
}
