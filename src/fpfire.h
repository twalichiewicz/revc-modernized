#pragma once
// First person's firing rules, as Counter-Strike 1.6's: a shot leaves the
// frame the trigger is pulled and then at the gun's own cadence (once a pull
// for pistols and shotguns); each shot punches the view up (automatics with
// CS's KickBack: growing through a burst, a little sideways, capped), the
// punch decaying as CS's does (len -= (10 + 0.5 len) dt); and the spread
// grows with sustained fire as CS's accuracy does (shots^3/200 + 0.35), more
// running and much more in the air, less crouched or aiming, the first shot
// standing or walking exact. All on game time. Unit-tested (tests/test_fpfire.cpp).
namespace fpfire {

enum class Kind { kPistol, kShotgun, kSmg, kRifle, kHeavy };
Kind KindOf(int weaponType);
bool Automatic(Kind kind);

class Trigger {
public:
	// How many shots leave this frame (one, or a few when the cadence is
	// quicker than the frame).
	int Update(bool held, bool ready, float interval, bool automatic, float dt);
	// Another gun: its cadence fresh; a pistol or shotgun needs a new pull if RT's still `held`.
	void Reset(bool held);

private:
	float cooldown_ = 0.0f; // s till the next shot may leave
	bool armed_ = true;     // released since the last semi-automatic shot
	bool bursting_ = false; // an automatic held and firing last frame
};

// Degrees: up, and to the right.
struct Punch {
	float pitch = 0.0f, yaw = 0.0f;
	bool right = true; // the way an automatic's side kick goes
	void Decay(float dt);
	void Kick(Kind kind, int shotsInBurst, float random01);
};

// The shot's jolt through him into the view (degrees: left, up, over to the
// right): his body takes most of the recoil, so the gun kicks little and the
// view shakes a little, a quick shake gone in about a tenth of a second
// (more for a shotgun or a sniper rifle). On top of the punch.
struct Jolt {
	float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
	void Kick(Kind kind, float random01);
	void Decay(float dt);

private:
	float size_ = 0.0f, t_ = 1.0f; // degrees; s since the shot
	float yawSign_ = 1.0f, rollSign_ = 1.0f;
};

enum class Moving { kStill, kWalking, kRunning, kAir };

class Spread {
public:
	void Shot();
	void Update(float dt);
	// The cone's half angle for the next shot (radians).
	float Angle(Kind kind, Moving moving, bool crouched, bool aiming) const;
	int Burst() const { return shots_; }

private:
	int shots_ = 0;
	float since_ = 1e9f;
};

} // namespace fpfire
