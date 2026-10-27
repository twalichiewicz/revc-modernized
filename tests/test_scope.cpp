// Unit tests for the sniper scope's rules (zoom stops, sway and breath, the bolt, spread): make test
#include "scope.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

using namespace scope;

constexpr float kStep = 1.0f / 60.0f;

static Input Scoped()
{
	Input in{};
	in.wanted = in.inView = true;
	in.dt = kStep;
	return in;
}

// Runs `seconds` with `in`; the largest sway (its size) from `from` seconds on.
static float Sway(Scope &s, Input in, float seconds, float from = 0.0f, Output *last = nullptr)
{
	float most = 0.0f;
	Output out{};
	for (float t = 0.0f; t < seconds; t += kStep) {
		out = s.Update(in);
		if (t >= from)
			most = std::max(most, out.sway);
		CHECK(std::fabs(out.swayYaw) <= out.sway + 1e-5f && std::fabs(out.swayPitch) <= out.sway + 1e-5f);
	}
	if (last)
		*last = out;
	return most;
}

static float FovAt(float magnification) { return 2.0f * std::atan(std::tan(35.0f * 3.14159265f / 180.0f) / magnification) * 180.0f / 3.14159265f; }

// RB steps between 3× and 8× and back; the field of view eases there.
static void TestStopsCycleOnRb()
{
	Scope s;
	Output out{};
	Sway(s, Scoped(), 1.0f, 0.0f, &out);
	CHECK(std::fabs(out.fov - FovAt(3.0f)) < 0.1f);
	Input press = Scoped();
	press.zoomPressed = true;
	const Output next = s.Update(press);
	CHECK(next.fov < out.fov && next.fov > FovAt(8.0f) + 1.0f); // easing, not jumped
	Sway(s, Scoped(), 0.5f, 0.0f, &out);
	CHECK(std::fabs(out.fov - FovAt(8.0f)) < 0.1f);
	s.Update(press);
	Sway(s, Scoped(), 0.5f, 0.0f, &out);
	CHECK(std::fabs(out.fov - FovAt(3.0f)) < 0.1f);
}

// Holding his breath steadies the sway; held too long, he gasps (more sway), then it settles.
static void TestBreathSteadiesThenGasps()
{
	Scope s;
	const float normal = Sway(s, Scoped(), 8.0f);
	CHECK(normal > 0.2f && normal < 0.6f);
	Input hold = Scoped();
	hold.breathHeld = true;
	Output out{};
	const float held = Sway(s, hold, 4.0f, 0.5f, &out);
	CHECK(held < 0.1f * normal && out.holding && !out.gasping && out.breath < 0.2f && out.breath > 0.0f);
	const float gasp = Sway(s, hold, 1.0f, 0.6f, &out);
	CHECK(gasp > 2.0f * normal);
}

// Still holding L3 after a gasp, he can't hold it again till he lets go: no
// gasp after gasp, and his breath comes back meanwhile.
static void TestNoGaspAfterGasp()
{
	Scope s;
	Input hold = Scoped();
	hold.breathHeld = true;
	Output out{};
	Sway(s, hold, 6.0f, 0.0f, &out); // held out, gasped
	bool again = false;
	for (float t = 0.0f; t < 6.0f; t += kStep) {
		out = s.Update(hold);
		again = again || out.holding || (t > 0.5f && out.gasping);
	}
	CHECK(!again && out.breath > 0.99f);
	s.Update(Scoped()); // let go
	CHECK(s.Update(hold).holding);
}

// It comes back: a few seconds breathing and he can hold it as long again.
static void TestBreathRefills()
{
	Scope s;
	Input hold = Scoped();
	hold.breathHeld = true;
	Output out{};
	Sway(s, hold, 6.0f, 0.0f, &out); // held out, gasped
	Sway(s, Scoped(), 4.5f, 0.0f, &out);
	CHECK(out.breath > 0.99f && !out.gasping);
	Sway(s, hold, 0.2f, 0.0f, &out);
	CHECK(out.holding);
}

// A shot drops the scope while the bolt cycles (0.9 s), then it's back at the same stop.
static void TestShotDropsForTheBolt()
{
	Scope s;
	Input press = Scoped();
	press.zoomPressed = true;
	s.Update(press);
	Output out{};
	Sway(s, Scoped(), 1.0f, 0.0f, &out);
	Input shot = Scoped();
	shot.shot = true;
	shot.boltAction = true;
	const Output fired = s.Update(shot);
	CHECK(fired.bolting && fired.overlay > 0.99f); // the scope's art stays while the game's view still is
	Input out_of_view = Scoped();
	out_of_view.inView = false; // the game leaves its scope for the bolt
	Sway(s, out_of_view, 0.8f, 0.0f, &out);
	CHECK(out.bolting && out.overlay == 0.0f);
	Sway(s, out_of_view, 0.15f, 0.0f, &out);
	CHECK(!out.bolting);
	Sway(s, Scoped(), 1.0f, 0.0f, &out);
	CHECK(std::fabs(out.fov - FovAt(8.0f)) < 0.1f && out.overlay > 0.99f);
}

// The laser rifle is semi-automatic: it keeps its zoom after a shot.
static void TestSemiAutomaticKeepsTheScope()
{
	Scope s;
	Sway(s, Scoped(), 1.0f);
	Input shot = Scoped();
	shot.shot = true;
	CHECK(!s.Update(shot).bolting);
}

// Creeping while scoped spreads his shots; standing, they go where the reticle is.
static void TestMovingSpreads()
{
	Scope s;
	CHECK(s.Update(Scoped()).spread == 0.0f);
	Input creep = Scoped();
	creep.moving = 0.6f;
	CHECK(s.Update(creep).spread > 0.005f);
}

// Letting go of the scope puts it all back: the first stop, no bolt, no overlay.
static void TestLeavingResets()
{
	Scope s;
	Input press = Scoped();
	press.zoomPressed = true;
	s.Update(press);
	Input shot = Scoped();
	shot.shot = true;
	s.Update(shot);
	Input gone{};
	gone.dt = kStep;
	const Output out = s.Update(gone);
	CHECK(!out.bolting && out.overlay == 0.0f && !out.holding);
	Output back{};
	Sway(s, Scoped(), 1.0f, 0.0f, &back);
	CHECK(std::fabs(back.fov - FovAt(3.0f)) < 0.1f);
}

// Into the scope and out, the view stays where it was: a direction as the
// sniper camera's pitch and heading, and as the follow camera's (whose view
// first person raises by `level`), each giving the direction back.
static Dir Sniper(Angles a) { return {std::cos(a.alpha) * std::cos(a.beta), std::cos(a.alpha) * std::sin(a.beta), std::sin(a.alpha)}; }
static Dir FollowRaised(Angles a, float level)
{
	const float pitch = a.alpha + level;
	return {std::cos(pitch) * -std::cos(a.beta), std::cos(pitch) * -std::sin(a.beta), std::sin(pitch)};
}
static bool Near(Dir a, Dir b) { return std::fabs(a.x - b.x) < 1e-4f && std::fabs(a.y - b.y) < 1e-4f && std::fabs(a.z - b.z) < 1e-4f; }

static void TestViewCarriesAcross()
{
	const float level = 0.17f;
	for (Dir f : {Dir{1, 0, 0}, Dir{0.6f, -0.6f, 0.53f}, Dir{-0.3f, 0.8f, -0.52f}, Dir{0, -1, 0}}) {
		const float l = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
		f = {f.x / l, f.y / l, f.z / l};
		CHECK(Near(Sniper(SniperAngles(f)), f));
		CHECK(Near(FollowRaised(FollowAngles(f, level), level), f));
	}
	// Past each camera's own limits, as far as it goes.
	CHECK(SniperAngles({0, 0, 1}).alpha <= 60.0f * 3.14159265f / 180.0f + 1e-5f);
	CHECK(FollowAngles({0, 0, 1}, level).alpha <= 45.0f * 3.14159265f / 180.0f + 1e-5f);
}

int main()
{
	TestViewCarriesAcross();
	TestStopsCycleOnRb();
	TestBreathSteadiesThenGasps();
	TestBreathRefills();
	TestNoGaspAfterGasp();
	TestSemiAutomaticKeepsTheScope();
	TestShotDropsForTheBolt();
	TestMovingSpreads();
	TestLeavingResets();
	if (gFailures) {
		printf("%d scope test(s) failed\n", gFailures);
		return 1;
	}
	printf("all scope tests passed\n");
	return 0;
}
