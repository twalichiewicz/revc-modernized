#include "fpfeedback.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>

static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)

static void Drowning()
{
	for (float hz : {20.0f, 60.0f, 144.0f}) {
		fpfeedback::Breath b;
		float damage = 0;
		for (int i = 0; i < int(hz * 22); ++i)
			damage += fpfeedback::DrowningWeaponDamage(b.Update(true, 1 / hz)) *
				  0.33f; // exact target InflictDamage scale
		CHECK(std::fabs(damage - 100) < 0.02f);
		CHECK(b.Seconds() == 0);
		CHECK(b.Update(false, 0.1f) == 0 && b.Seconds() > 0);
	}
	fpfeedback::Breath b;
	CHECK(b.Update(true, 20.5f) == 25.0f); // only the exhausted half-second hurts
	b.Reset();
	CHECK(b.Seconds() == 20);
	CHECK(b.Update(true, 0) == 0 && b.Update(true, -1) == 0);
	b.Update(true, 20);
	CHECK(b.Update(true, 2, 200) == 200); // maximum-health upgrade still drowns in two seconds
}

static void PitchAndFreeze()
{
	using namespace fpfeedback;
	for (float p : {-100.0f, -1.55f, 0.0f, 1.55f, 100.0f}) {
		auto basis = View(0.7f, p);
		CHECK(std::isfinite(basis.forward.x) && std::isfinite(basis.up.z));
		CHECK(std::fabs(basis.forward.x * basis.up.x + basis.forward.y * basis.up.y +
				basis.forward.z * basis.up.z) < 1e-5f);
		CHECK(std::hypot(basis.forward.x, basis.forward.y) > 0.015f);
	}
	CHECK(View(0, 100).forward.z > 0.999f && View(0, -100).forward.z < -0.999f);
	LookGate g;
	CHECK(!g.Update(false));
	CHECK(g.Update(true));
	CHECK(g.Update(true));
	CHECK(g.Update(false)); // consume release-frame input too
	CHECK(!g.Update(false));
}

static void DamageAndLifecycle()
{
	fpfeedback::Impact impact;
	impact.Hit(100, 100, 1, 0);
	CHECK(impact.Opacity() == 0);
	impact.Hit(100, 90, 1, 0);
	CHECK(impact.Opacity() > 0.05f);
	auto first = impact.Opacity();
	impact.Tick(0.1f);
	CHECK(impact.Opacity() < first);
	impact.Hit(100, 0, 1, 0);
	CHECK(impact.Opacity() <= 0.55f);
	impact.Tick(1);
	CHECK(impact.Opacity() == 0);
	impact.Hit(100, 70, 1, 0);
	impact.Reset();
	CHECK(impact.Opacity() == 0);
	CHECK(fpfeedback::CanPose(100, 1, true));
	CHECK(!fpfeedback::CanPose(0, 1, true));
	CHECK(!fpfeedback::CanPose(100, 0x36, true));
	CHECK(!fpfeedback::CanPose(100, 0x37, true));
	CHECK(!fpfeedback::CanPose(100, 1, false));
	float m[16];
	for (int i = 0; i < 16; ++i)
		m[i] = float(i);
	fpfeedback::MatrixRestore restore;
	restore.Save(7, m);
	for (int i = 0; i < 16; ++i)
		m[i] = 0;
	CHECK(!restore.Restore(8, m)); // reused hierarchy cannot receive another owner's bones
	CHECK(restore.Restore(7, m));
	CHECK(m[14] == 14);
	CHECK(!restore.Restore(7, m)); // exactly once, no stale frame reapplied
}
static void ActualDamageObservation()
{
	using fpfeedback::ObserveDamage;
	const auto nonfatal = ObserveDamage(false, 100, 95, 50, 50);
	CHECK(nonfatal.accepted && nonfatal.healthLost == 5 && nonfatal.armourLost == 0);
	CHECK(!nonfatal.nativeResult); // native false still reports a real accepted hit
	const auto armour = ObserveDamage(false, 100, 100, 50, 35);
	CHECK(armour.accepted && armour.healthLost == 0 && armour.armourLost == 15);
	CHECK(!armour.nativeResult);
	const auto ignored = ObserveDamage(false, 100, 100, 50, 50);
	CHECK(!ignored.accepted && ignored.healthLost == 0 && ignored.armourLost == 0);
	const auto fatal = ObserveDamage(true, 5, 0, 0, 0);
	CHECK(fatal.accepted && fatal.nativeResult && fatal.healthLost == 5);
	const auto unchangedNativeTrue = ObserveDamage(true, 100, 100, 50, 50);
	CHECK(!unchangedNativeTrue.accepted && unchangedNativeTrue.nativeResult);
}

static void AimPoseTransitions()
{
	fpfeedback::AimPose pose;
	auto first = pose.Update(26, 10, 100, true, true, true, .016f);
	CHECK(!first.capture);
	auto learn = pose.Update(26, 10, 450, true, true, true, .016f);
	CHECK(learn.capture);
	pose.Captured(26);
	auto stood = pose.Update(26, 10, 500, true, true, true, .2f);
	CHECK(!stood.capture && stood.weight > .7f);
	auto walk = pose.Update(26, 10, 516, true, false, true, .016f);
	CHECK(walk.weapon == 26 && walk.weight >= stood.weight);
	auto lowered = pose.Update(26, 10, 532, false, false, true, .016f);
	CHECK(lowered.weight > 0 && lowered.weight < walk.weight);
	auto gone = pose.Update(26, 10, 548, true, true, false, .016f);
	CHECK(gone.weight == 0); // first person/crouch owns arms
	pose.Update(26, 10, 900, true, true, true, .016f);
	CHECK(!pose.Update(27, 10, 1300, true, true, true, .016f).capture); // new gun must settle
	pose.Update(26, 11, 564, true, true, true, .016f);
	CHECK(!pose.Known(26)); // new outfit learns its own skeleton
}
int main()
{
	ActualDamageObservation();
	AimPoseTransitions();
	Drowning();
	PitchAndFreeze();
	DamageAndLifecycle();
	if (failures)
		return 1;
	std::puts("all first-person feedback tests passed");
}
