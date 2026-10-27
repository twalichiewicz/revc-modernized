#pragma once
// The sniper scope's rules, as modern console shooters have them on the
// game's own sniper camera: two zoom stops (3× and 8×) on RB, eased; a
// gentle sway (two slow sines), more creeping; L3 holds his breath, steadying
// it for 4.5 s, then a gasp that sways more; it refills over 3 s (L3 let go
// before it's held again); a bolt-action shot drops the scope while the
// bolt cycles (0.9 s, as Counter-Strike's AWP), then it's back at the same stop; creeping spreads his shots. Letting go of
// the scope puts it all back. Unit-tested (tests/test_scope.cpp).
namespace scope {

struct Input {
	bool wanted;      // LT held with a sniper rifle, first person
	bool inView;      // the game's scope view is up
	bool zoomPressed; // RB's press
	bool breathHeld;  // L3
	bool shot;        // he fired
	bool boltAction;  // the gun's (the sniper rifle; the laser rifle is semi-automatic)
	float moving;     // m/s
	float dt;         // s
};

struct Output {
	float fov;                // degrees (of the view: 70 is the game's)
	float swayYaw, swayPitch; // degrees, left and up
	float sway;               // degrees, how far it sways now (their size)
	float breath;             // 0..1 left
	bool holding, gasping;
	bool bolting;             // the scope dropped for the bolt
	float spread;             // radians, a shot now
	float overlay;            // 0..1, the scope's art faded in
};

// A view's direction as each camera holds it (radians): the sniper camera's
// pitch and heading (its Front: cos a cos b, cos a sin b, sin a), and the
// follow camera's, whose view first person raises by `level` (its Front:
// -cos a cos b, -cos a sin b, sin a). So the view carries across, into the
// scope and out (each camera's reset would otherwise level it, or raise it).
struct Dir {
	float x, y, z; // unit
};
struct Angles {
	float alpha, beta;
};
Angles SniperAngles(Dir front);
Angles FollowAngles(Dir front, float level);

class Scope {
public:
	Output Update(const Input &in);

private:
	int stop_ = 0;
	float fov_ = 0.0f;
	float t_ = 0.0f;
	float steady_ = 1.0f; // the sway's scale: 0 holding breath, 4.5 gasping
	float breath_ = 1.0f, gasp_ = 0.0f;
	bool letGo_ = true;   // L3 let go since the last gasp
	float bolt_ = 0.0f, overlay_ = 0.0f;
	float moving_ = 0.0f;
};

} // namespace scope
