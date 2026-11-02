// Unit tests for melee weapons in first person (ready, swung, his arm to them): make test
#include "vmmelee.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

using namespace vm;

constexpr MeleeKind kKinds[] = {MeleeKind::kKnuckles, MeleeKind::kKnife, MeleeKind::kBlade,
                                MeleeKind::kClub,     MeleeKind::kLong,  MeleeKind::kChainsaw};
constexpr float kStrike = 0.47f; // weapon.dat's frame 14

static float Apart(const Grip &a, const Grip &b)
{
	return Length(Sub(a.wrist, b.wrist)) + Length(Sub(Normal(a.along), Normal(b.along))) +
	       Length(Sub(Normal(a.knuckles), Normal(b.knuckles)));
}

static void TestTheMeleeWeapons()
{
	CHECK(!IsMelee(0) && !IsMelee(12) && !IsMelee(17));
	for (int w = 1; w <= 11; w++)
		CHECK(IsMelee(w));
	CHECK(MeleeKindFor(1) == MeleeKind::kKnuckles && MeleeKindFor(4) == MeleeKind::kClub &&
	      MeleeKindFor(6) == MeleeKind::kLong && MeleeKindFor(5) == MeleeKind::kKnife &&
	      MeleeKindFor(10) == MeleeKind::kBlade && MeleeKindFor(11) == MeleeKind::kChainsaw);
}

static void TestGripFrameSquare()
{
	const Frame f = GripFrame({{0.1f, 0.3f, -0.2f}, {-0.3f, 0.6f, 0.7f}, {-0.3f, 0.8f, -0.5f}});
	CHECK(std::fabs(Length(f.x) - 1.0f) < 1e-4f && std::fabs(Length(f.y) - 1.0f) < 1e-4f && std::fabs(Length(f.z) - 1.0f) < 1e-4f);
	CHECK(std::fabs(Dot(f.x, f.y)) < 1e-4f && std::fabs(Dot(f.y, f.z)) < 1e-4f && std::fabs(Dot(f.x, f.z)) < 1e-4f);
	CHECK(Length(Sub(Cross(f.x, f.y), f.z)) < 1e-4f); // right-handed, as his hand bone is
	CHECK(Length(Sub(f.z, Normal({-0.3f, 0.6f, 0.7f}))) < 1e-4f);
}

// Held up ready: in front of him on the right, below the eye, in view.
static void TestReadyInView()
{
	for (MeleeKind k : kKinds) {
		const Grip g = ReadyGrip(k);
		CHECK(Apart(AttackGrip(k, Attack{}), g) < 1e-5f);
		CHECK(g.wrist.x > 0.1f && g.wrist.y > 0.0f && g.wrist.z < 0.0f);
	}
}

// Every swing reaches out further than ready at its hit, and is back to ready long after.
static void TestSwingsReachOutAndComeBack()
{
	for (MeleeKind k : kKinds)
		for (int which = 1; which <= 3; which++) {
			const Grip hit = AttackGrip(k, Attack{which, kStrike, kStrike});
			CHECK(hit.wrist.y > ReadyGrip(k).wrist.y + 0.05f);
			CHECK(Apart(AttackGrip(k, Attack{which, kStrike + 2.0f, kStrike}), ReadyGrip(k)) < 1e-4f);
		}
}

// Into the hit quicker than drawing back: the last 30 ms before it cover more than the first 30 ms of the swing.
static void TestQuickestIntoTheHit()
{
	for (MeleeKind k : {MeleeKind::kClub, MeleeKind::kLong, MeleeKind::kBlade, MeleeKind::kKnife}) {
		const float into = Length(Sub(AttackGrip(k, Attack{1, kStrike, kStrike}).wrist, AttackGrip(k, Attack{1, kStrike - 0.03f, kStrike}).wrist));
		const float drawing = Length(Sub(AttackGrip(k, Attack{1, 0.03f, kStrike}).wrist, AttackGrip(k, Attack{1, 0.0f, kStrike}).wrist));
		CHECK(into > 2.0f * drawing);
	}
}

// A swing that starts over (the game loops its animation while the button's
// held) carries on from where his hand is: no quicker than the swing itself.
static void TestHandNeverJumps()
{
	for (MeleeKind k : kKinds) {
		MeleeHand hand;
		const float dt = 1.0f / 60.0f;
		Grip last = hand.Update(k, Attack{1, 0.0f, kStrike}, dt);
		float swinging = 0.0f, restarting = 0.0f;
		for (float t = dt; t < 0.66f; t += dt) {
			const Grip g = hand.Update(k, Attack{1, t, kStrike}, dt);
			swinging = std::fmax(swinging, Length(Sub(g.wrist, last.wrist)));
			last = g;
		}
		for (int i = 0; i < 20; i++) { // looped back to its start
			const Grip g = hand.Update(k, Attack{1, 0.17f + float(i) * dt, kStrike}, dt);
			restarting = std::fmax(restarting, Length(Sub(g.wrist, last.wrist)));
			last = g;
		}
		CHECK(restarting <= 1.15f * swinging);
	}
}

// Left alone, his hand settles on ready; the first frame it's there already.
static void TestHandSettlesReady()
{
	MeleeHand hand;
	CHECK(Apart(hand.Update(MeleeKind::kBlade, Attack{}, 1.0f / 60.0f), ReadyGrip(MeleeKind::kBlade)) < 1e-5f);
	for (float t = 0.0f; t < 0.3f; t += 1.0f / 60.0f)
		hand.Update(MeleeKind::kBlade, Attack{1, t, kStrike}, 1.0f / 60.0f);
	Grip g{};
	for (int i = 0; i < 60; i++)
		g = hand.Update(MeleeKind::kBlade, Attack{}, 1.0f / 60.0f);
	CHECK(Apart(g, ReadyGrip(MeleeKind::kBlade)) < 2e-3f);
}

// His arm from one shoulder to wherever the hand goes, the elbow out and below.
static void TestArmToReaches()
{
	const float fore = 0.26f, upper = 0.295f;
	Vec3 shoulders[2];
	const Vec3 wrists[2] = {ReadyGrip(MeleeKind::kClub).wrist, {0.0f, 0.42f, -0.10f}};
	for (int i = 0; i < 2; i++) {
		Vec3 f, u;
		ArmTo(wrists[i], fore, upper, f, u);
		CHECK(std::fabs(Length(f) - 1.0f) < 1e-4f && std::fabs(Length(u) - 1.0f) < 1e-4f);
		const Vec3 elbow = Add(wrists[i], Mul(f, fore));
		shoulders[i] = Add(elbow, Mul(u, upper));
		CHECK(elbow.z < (wrists[i].z + shoulders[i].z) * 0.5f); // below the line from shoulder to wrist
	}
	CHECK(Length(Sub(shoulders[0], shoulders[1])) < 1e-3f);
	CHECK(shoulders[0].x > 0.1f && shoulders[0].z < -0.1f && shoulders[0].y < 0.0f); // right, below and behind the eye
}

int main()
{
	TestTheMeleeWeapons();
	TestGripFrameSquare();
	TestReadyInView();
	TestSwingsReachOutAndComeBack();
	TestQuickestIntoTheHit();
	TestHandNeverJumps();
	TestHandSettlesReady();
	TestArmToReaches();
	if (gFailures) {
		printf("%d vmmelee test(s) failed\n", gFailures);
		return 1;
	}
	printf("all vmmelee tests passed\n");
	return 0;
}
