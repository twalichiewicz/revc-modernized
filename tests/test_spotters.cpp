// Unit tests for who's spotted Tommy (the red eyes): make test
#include "spotters.h"

#include <cstdio>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

using spotters::Look;
using spotters::Spotters;

constexpr float kStep = 0.05f; // s: sight looks every 50 ms

static Look Cop(int handle, float distance, bool sees = true, bool shooting = false)
{
	return Look{handle, false, {0, distance, 1.6f}, sees, shooting, distance};
}

static bool Red(const Spotters &s, int handle)
{
	for (const spotters::Spotter &e : s.all())
		if (e.handle == handle)
			return true;
	return false;
}

// How long the eye over `look` stays (s), the police on him and him seen as `look` has it throughout.
static float Shown(Spotters &s, Look look, float approach = 0.0f, float crimesEvery = 0.0f)
{
	float t = 0.0f, sinceCrime = 0.0f;
	while (t < 60.0f && Red(s, look.handle)) {
		s.Follow(true, {look}, kStep);
		t += kStep;
		look.distance = look.distance - approach * kStep;
		look.eyes.y = look.distance;
		sinceCrime += kStep;
		if (crimesEvery > 0.0f && sinceCrime >= crimesEvery) {
			s.SawCrime(look);
			sinceCrime = 0.0f;
		}
	}
	return t;
}

// The player's report: several cops shooting at him and the red never went.
// Every shot of his in front of them was a crime they saw, and each one
// spotted them again. Once the police are on him, a crime spots nobody.
static void TestAFirefightDoesntKeepItRed()
{
	Spotters s;
	s.Follow(false, {}, kStep); // clear: nobody on him
	const Look cop = Cop(7, 30.0f);
	s.SawCrime(cop);
	CHECK(Red(s, 7));
	const float shown = Shown(s, cop, 0.0f, 0.1f); // a crime of his every 0.1 s
	CHECK(shown < spotters::kAtMost + spotters::kFade + 0.1f);
	CHECK(!Red(s, 7));
}

// And nobody new turns red in the middle of the chase.
static void TestCrimesInTheChaseSpotNobody()
{
	Spotters s;
	s.Follow(true, {Cop(7, 30.0f), Cop(8, 40.0f)}, kStep);
	s.SawCrime(Cop(8, 40.0f));
	CHECK(s.all().empty());
}

// A cop shooting at him is the chase: his eye goes once it's been seen.
static void TestShootingAtHimEndsIt()
{
	Spotters s;
	s.Follow(false, {}, kStep);
	s.Recognised(Cop(7, 35.0f));
	const float shown = Shown(s, Cop(7, 35.0f, true, true));
	CHECK(shown >= spotters::kAtLeast);
	CHECK(shown < spotters::kAtLeast + spotters::kFade + 0.2f);
}

// Long enough to notice: even if he's lost him at once, or is right there.
static void TestLongEnoughToNotice()
{
	Spotters s;
	s.Follow(false, {}, kStep);
	s.Recognised(Cop(7, 5.0f));
	CHECK(Shown(s, Cop(7, 5.0f, false)) >= spotters::kAtLeast);
}

// Coming for him: red while he closes in, gone once he's there.
static void TestRedWhileHeComes()
{
	Spotters s;
	s.Follow(false, {}, kStep);
	const Look cop = Cop(7, 40.0f);
	s.Recognised(cop);
	const float shown = Shown(s, cop, 6.0f); // within 10 m in 5 s
	CHECK(shown > 4.8f && shown < 5.0f + spotters::kFade + 0.2f);
}

// Lost and found again: a new sighting, a new red eye.
static void TestFoundAgain()
{
	Spotters s;
	s.Follow(false, {}, kStep);
	s.Recognised(Cop(7, 30.0f));
	Shown(s, Cop(7, 30.0f, false));
	CHECK(!Red(s, 7));
	s.Follow(false, {Cop(7, 30.0f)}, kStep); // searching again
	s.SawCrime(Cop(7, 30.0f));
	CHECK(Red(s, 7));
}

// Gone from the world: his eye goes with him.
static void TestGoneTakesItsEye()
{
	Spotters s;
	s.Follow(false, {}, kStep);
	s.Recognised(Cop(7, 30.0f));
	for (int i = 0; i < 100; i++)
		s.Follow(true, {}, kStep);
	CHECK(!Red(s, 7));
}

int main()
{
	TestAFirefightDoesntKeepItRed();
	TestCrimesInTheChaseSpotNobody();
	TestShootingAtHimEndsIt();
	TestLongEnoughToNotice();
	TestRedWhileHeComes();
	TestFoundAgain();
	TestGoneTakesItsEye();
	if (gFailures)
		printf("%d failure(s)\n", gFailures);
	else
		printf("spotters: all tests passed\n");
	return gFailures ? 1 : 0;
}
