#pragma once
// First person's bob, as Counter-Strike's (V_CalcBob, cl_bob 0.01,
// cl_bobcycle 0.8, cl_bobup 0.5): the eye rises and falls once every 0.8 s
// while he moves, by a hundredth of his speed, mostly upward; in the air it
// holds where it was. And the view's sway with his steps, more as he runs,
// with Counter-Strike's roll as he strafes. Unit-tested (tests/test_fpbob.cpp).

namespace fpbob {

struct State {
	float time = 0.0f, last = 0.0f;
};

// The eye's rise this frame (m), at `speed` (m/s, horizontal).
float Bob(State &s, float speed, float dt, bool onGround);

// The view's turn with his steps (degrees: up, over to the right, left), at
// `phase` through his stride (0..2: a step each 1), `speed` (m/s, horizontal),
// `side` (m/s to his right): it dips at each footfall and sways side to side
// once a stride, barely walking, more running, most sprinting; strafing
// rolls it over that way (Half-Life's cl_rollangle 2 at cl_rollspeed 200
// units, a third: classic Counter-Strike had none).
struct Turn {
	float pitch, roll, yaw;
};
Turn Sway(float phase, float speed, float side);

// Where the eye goes as he looks down (`frontZ`: the view's forward, its
// height): forward past his chest and a little down, as a head tips forward,
// so he sees his body and legs, not his neck from inside; none until 20°
// down, 0.16 m ahead and 0.05 m down looking straight down.
struct Push {
	float ahead, down; // m
};
Push LookDown(float frontZ);

// His steps' phase (0..2, a step each 1) from his legs' walking, running and
// sprinting animations: the one blended in most, its place through its loop
// (a loop's a stride); false when none's playing (then it's worked out from
// his speed).
struct Stride {
	float blend, time, length; // its blend; s into it; its loop's length (s)
};
bool StridePhase(const Stride *anims, int count, float &phase);

// The eye's height over steps, as GoldSrc smooths stairs: on the ground it
// eases after his body (0.06 s), never more than 0.45 m behind; in the air
// it's exactly his (no trailing behind a jump or a fall).
struct Steps {
	float Height(float z, float dt, bool onGround);

private:
	float z_ = 0.0f;
	bool ready_ = false;
};

} // namespace fpbob
