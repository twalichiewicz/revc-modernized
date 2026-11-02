#pragma once
#include "game.h"
#include <cstdint>

namespace fpfeedback
{
class Breath
{
      public:
	float Update(bool submerged, float dt, float maxHealth = 100.0f);
	float Seconds() const { return seconds_; }
	void Reset() { seconds_ = 20.0f; }

      private:
	double seconds_ = 20.0;
};
// macVC's InflictDamage scales player damage before its drowning armour bypass.
float DrowningWeaponDamage(float healthLoss);
struct Basis {
	CVector forward, up;
};
Basis View(float heading, float pitch);
float Pitch(float pitch);
class LookGate
{
      public:
	bool Update(bool open);

      private:
	bool wasOpen_ = false;
};
class Impact
{
      public:
	void Hit(float before, float after, float side, float ahead);
	void Tick(float dt);
	void Reset();
	float Opacity() const { return opacity_; }
	float Side() const { return side_; }
	float Ahead() const { return ahead_; }

      private:
	float opacity_ = 0, side_ = 0, ahead_ = 0;
};
bool CanPose(float health, int state, bool enabled);
// InflictDamage's native bool reports a fatal transition, not acceptance.
// Observe actual before/after values while preserving that return contract.
struct DamageFacts {
	bool nativeResult = false, accepted = false;
	float healthLost = 0, armourLost = 0;
};
DamageFacts ObserveDamage(bool nativeResult, float healthBefore, float healthAfter, float armourBefore, float armourAfter);

// Stable upper-body target across native standing/walking aim states.
class AimPose
{
      public:
	struct Output {
		int weapon = -1;
		float weight = 0;
		bool capture = false;
	};
	Output Update(int weapon, uintptr_t model, uint32_t now, bool aimed, bool standing, bool ownsPose, float dt);
	void Captured(int weapon);
	bool Known(int weapon) const;

      private:
	uintptr_t model_ = 0;
	bool known_[40] = {};
	bool standing_ = false;
	uint32_t standingAt_ = 0;
	int weapon_ = -1, standingWeapon_ = -1;
	float weight_ = 0;
};
struct MatrixRestore {
	void Save(uintptr_t owner, const float *matrix);
	bool Restore(uintptr_t owner, float *matrix);

      private:
	uintptr_t owner_ = 0;
	float matrix_[16] = {};
	bool saved_ = false;
};
} // namespace fpfeedback
