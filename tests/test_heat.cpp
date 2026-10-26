// Unit tests for what the police know (the wanted overhaul's model): make test
#include "heat.h"

#include <cmath>
#include <cstdio>

using heat::Looks;
using heat::Model;
using heat::State;

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

static bool near(float a, float b)
{
	return std::fabs(a - b) < 0.001f;
}

static const Vec3 kHere = {0, 0, 0};

// Seen at `stars` at 1000 ms, lost, and escaped far away: under the APB.
static Model Escaped(int stars)
{
	Model m;
	m.CopSees({"player"}, kHere, 1000, stars);
	m.Tick(kHere, 5001);
	m.Tick({500, 0, 0}, 5001 + heat::kSearchMs[stars] + 1);
	return m;
}

static void TestCopSeesMakesWanted()
{
	Model m;
	m.CopSees({"player"}, kHere, 1000, 2);
	CHECK(m.state() == State::Wanted);
	CHECK(m.stars() == 2);
	CHECK(m.gameStars() == 2);
	CHECK(m.description().haveOutfit);
	CHECK(m.description().outfit == "player");
}

static void TestLostAfterFourSeconds()
{
	Model m;
	m.CopSees({"player"}, kHere, 1000, 2);
	m.Tick(kHere, 5000);
	CHECK(m.state() == State::Wanted);
	m.Tick(kHere, 5001);
	CHECK(m.state() == State::Searching);
	CHECK(near(m.searchRadius(), 110));
}

static void TestSearchEndsIntoApb()
{
	Model m;
	m.CopSees({"player"}, kHere, 1000, 2);
	m.Tick(kHere, 5001);
	m.Tick({200, 0, 0}, 35002);
	CHECK(m.state() == State::Apb);
	CHECK(m.escapedStars() == 2);
	CHECK(m.gameStars() == 0);
	CHECK(m.stars() == 2);
}

static void TestSearchHoldsInsideArea()
{
	Model m;
	m.CopSees({"player"}, kHere, 1000, 2);
	m.Tick(kHere, 5001);
	m.Tick({50, 0, 0}, 35002);
	CHECK(m.state() == State::Searching);
}

static void TestLocationOnlyEndsClear()
{
	Model m;
	m.Reported(nullptr, kHere, 0, 0, 1);
	CHECK(m.state() == State::Searching);
	CHECK(!m.description().haveOutfit);
	m.Tick({200, 0, 0}, 20001);
	CHECK(m.state() == State::Clear);
}

static void TestWitnessWithLooks()
{
	Model m;
	Looks l{"player"};
	m.Reported(&l, kHere, 0, 0, 1);
	CHECK(m.state() == State::Wanted);
	CHECK(m.description().outfit == "player");
}

static void TestRecognisedUnderApb()
{
	Model m = Escaped(3);
	CHECK(m.state() == State::Apb);
	m.CopSees({"play2"}, kHere, 60000, 0);
	CHECK(m.state() == State::Wanted);
	CHECK(m.stars() == 3);
	CHECK(m.gameStars() == 3);
	CHECK(m.description().outfit == "play2");
}

static void TestNewCrimeUnderApb()
{
	Model m = Escaped(2);
	CHECK(m.state() == State::Apb);
	m.CopSees({"player"}, kHere, 60000, 4);
	CHECK(m.stars() == 4);
}

static void TestOutfitStruck()
{
	Model m;
	m.CopSees({"player"}, kHere, 1000, 1);
	CHECK(!m.Matches({"play2"}));
	CHECK(m.Matches({"player"}));
	CHECK(m.OutfitStruck({"play2"}));
}

static void TestResprayedCarStruck()
{
	Model m;
	m.CopSees({"player", 145, 1, 2}, kHere, 1000, 1);
	CHECK(m.VehicleStruck({"play2", 145, 3, 2}));
	CHECK(!m.VehicleStruck({"play2", -1, -1, -1}));
	CHECK(m.Matches({"play2", 145, 1, 2}));
}

static void TestOnFootKeepsCar()
{
	Model m;
	m.CopSees({"player", 145, 1, 2}, kHere, 1000, 1);
	m.CopSees({"player", -1, -1, -1}, kHere, 2000, 1);
	CHECK(m.description().haveVehicle);
	CHECK(m.description().vehicleModel == 145);
}

static void TestLoweredFollowsScript()
{
	Model m;
	m.CopSees({"player"}, kHere, 1000, 3);
	m.Lowered(1);
	CHECK(m.stars() == 1);
	m.Lowered(0);
	CHECK(m.state() == State::Clear);
}

static void TestClearForgetsEverything()
{
	Model m = Escaped(3);
	CHECK(m.state() == State::Apb);
	m.Clear();
	CHECK(m.state() == State::Clear);
	CHECK(m.stars() == 0);
	CHECK(m.escapedStars() == 0);
	CHECK(!m.description().haveOutfit && !m.description().haveVehicle);
}

static void TestCallingShowsReported()
{
	Model m;
	m.Calling(true);
	CHECK(m.state() == State::Reported);
	m.CopSees({"player"}, kHere, 1000, 1);
	CHECK(m.state() == State::Wanted);
	m.Clear();
	m.Calling(false);
	CHECK(m.state() == State::Clear);
}

static void TestFillRates()
{
	using heat::FillRate;
	CHECK(near(FillRate(10, true, false), 1 / 1.2f));
	CHECK(near(FillRate(70, true, false), 0.2f));
	CHECK(FillRate(71, true, false) == 0);
	CHECK(near(FillRate(4, false, false), 1 / 3.f));
	CHECK(FillRate(10, false, false) == 0);
	CHECK(near(FillRate(25, true, true), 1 / 1.2f));
	CHECK(FillRate(4, false, true) == 0);
}

// Why the game's stars went down, and what the bridge does about it.
static heat::Drop Classify(int level, int chaos, bool suspended, bool onMission, bool outfitChanged, bool waited)
{
	return heat::ClassifyDrop({level, chaos, suspended, onMission, outfitChanged, waited});
}

static void TestResprayKeepsStars()
{
	CHECK(Classify(0, 0, true, false, false, false) == heat::Drop::Respray);
	CHECK(Classify(0, 0, true, true, false, false) == heat::Drop::Respray);
}

static void TestOneStarTimeoutKeepsStars()
{
	// CWanted::Update lets one star run down (chaos under 50, not to 0) with no cop near.
	CHECK(Classify(0, 49, false, false, false, false) == heat::Drop::Timeout);
}

static void TestMissionClearFollowsScript()
{
	// A mission clearing the stars and then dressing Tommy is the story's, not a clothes change.
	CHECK(Classify(0, 0, false, true, true, true) == heat::Drop::Script);
	CHECK(Classify(1, 70, false, true, false, false) == heat::Drop::Script);
}

static void TestClothesOffMissionKeepsStars()
{
	CHECK(Classify(0, 0, false, false, true, false) == heat::Drop::Clothes);
	CHECK(Classify(1, 70, false, false, true, true) == heat::Drop::Clothes);
}

static void TestUnexplainedDropWaitsForClothes()
{
	CHECK(Classify(0, 0, false, false, false, false) == heat::Drop::Wait);
	CHECK(Classify(0, 0, false, false, false, true) == heat::Drop::Script);
}

// Review fixes: a look isn't a recognition, a call isn't a sighting, an alarm isn't the APB.
static void TestSearchingDoesNotKnowWhereHeIs()
{
	Model m;
	m.CopSees({"player"}, kHere, 1000, 2);
	m.Tick(kHere, 5001);
	CHECK(m.state() == State::Searching);
	CHECK(!m.Knows(true, 5100)); // a cop sees him, but hasn't recognised him
	CHECK(!m.Knows(false, 5100));
}

static void TestWantedKnowsWhileSeenOrJustLost()
{
	Model m;
	m.CopSees({"player"}, kHere, 1000, 2);
	CHECK(m.Knows(false, 5000));
	CHECK(!m.Knows(false, 5001));
	CHECK(m.Knows(true, 5001));
}

static void TestWitnessReportDoesNotRevealHim()
{
	Model m;
	Looks l{"player"};
	m.Reported(&l, {10, 0, 0}, 1000, 7000, 1); // the crime at 1000, the call through at 7000
	CHECK(!m.Knows(false, 7000));
	CHECK(m.description().haveOutfit);
	CHECK(near(m.lastSeen().x, 10));
}

static void TestLateCallDoesNotMoveLastSeenBack()
{
	Model m;
	m.CopSees({"player"}, {100, 0, 0}, 5000, 2);
	Looks l{"player"};
	m.Reported(&l, {0, 0, 0}, 1000, 9000, 2); // a call about the first crime, through late
	CHECK(near(m.lastSeen().x, 100));
	CHECK(m.lastSeenAt() == 5000);
}

static void TestLocationOnlyUnderApbSearchesAtItsOwnStars()
{
	Model m = Escaped(5);
	CHECK(m.state() == State::Apb);
	m.Reported(nullptr, kHere, 62000, 70000, 1); // a car alarm, called in 8 s on
	CHECK(m.state() == State::Searching);
	CHECK(m.stars() == 1);
	CHECK(m.gameStars() == 1);
	CHECK(m.escapedStars() == 5);
	m.Tick({900, 0, 0}, 70000 + heat::kSearchMs[1] + 1);
	CHECK(m.state() == State::Apb);
	CHECK(m.stars() == 5);
	m.CopSees({"player"}, kHere, 100000, 0);
	CHECK(m.stars() == 5);
}

// Feedback: two stars or more always end in an APB, a description or not.
static void TestTwoStarsAlwaysApb()
{
	Model m;
	m.Reported(nullptr, kHere, 0, 0, 2); // heard, only where
	CHECK(m.state() == State::Searching);
	m.Tick({900, 0, 0}, heat::kSearchMs[2] + 1);
	CHECK(m.state() == State::Apb);
	CHECK(m.stars() == 2);
	CHECK(m.gameStars() == 0);
}

// Feedback: under the APB, a witness who saw someone not matching it saw a stranger.
static void TestUnmatchedWitnessUnderApbIsAStranger()
{
	Model m = Escaped(3); // the APB has outfit "player"
	Looks other{"play2", 192, 44, 78};
	m.Reported(&other, {10, 0, 0}, 70000, 70000, 1);
	CHECK(m.state() == State::Searching); // a search for whoever it was, at its own stars
	CHECK(m.stars() == 1);
	CHECK(m.escapedStars() == 3);
	CHECK(m.description().outfit == "player"); // (the APB's, not the stranger's)
	CHECK(!m.description().haveVehicle);
}

static void TestMatchingWitnessUnderApbBringsItBack()
{
	Model m = Escaped(3);
	Looks same{"player"};
	m.Reported(&same, {10, 0, 0}, 70000, 70000, 1);
	CHECK(m.state() == State::Wanted);
	CHECK(m.stars() == 3);
}

int main()
{
	TestCopSeesMakesWanted();
	TestLostAfterFourSeconds();
	TestSearchEndsIntoApb();
	TestSearchHoldsInsideArea();
	TestLocationOnlyEndsClear();
	TestWitnessWithLooks();
	TestRecognisedUnderApb();
	TestNewCrimeUnderApb();
	TestOutfitStruck();
	TestResprayedCarStruck();
	TestOnFootKeepsCar();
	TestLoweredFollowsScript();
	TestClearForgetsEverything();
	TestCallingShowsReported();
	TestFillRates();
	TestResprayKeepsStars();
	TestOneStarTimeoutKeepsStars();
	TestMissionClearFollowsScript();
	TestClothesOffMissionKeepsStars();
	TestUnexplainedDropWaitsForClothes();
	TestSearchingDoesNotKnowWhereHeIs();
	TestWantedKnowsWhileSeenOrJustLost();
	TestWitnessReportDoesNotRevealHim();
	TestLateCallDoesNotMoveLastSeenBack();
	TestLocationOnlyUnderApbSearchesAtItsOwnStars();
	TestTwoStarsAlwaysApb();
	TestUnmatchedWitnessUnderApbIsAStranger();
	TestMatchingWitnessUnderApbBringsItBack();
	if (gFailures) {
		printf("%d heat test(s) failed\n", gFailures);
		return 1;
	}
	printf("all heat tests passed\n");
	return 0;
}
