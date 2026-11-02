#pragma once
// Who's spotted Tommy (sight.h): the red eye over a cop or police helicopter
// that has just recognised him, or seen a crime of his while the police
// weren't on him. It's there long enough to notice (a few seconds at least)
// and goes as the chase gets going: once he's reached him, lost sight of him
// a moment, is shooting at him, or it's gone on a while. A crime seen once
// the police are on him spots nobody: that's the chase, not a sighting (every
// shot of his in a firefight was one, and kept the eyes red). No game in it;
// unit-tested (tests/test_spotters.cpp).
#include "types.h"

#include <vector>

namespace spotters {

constexpr float kAtLeast = 2.5f;  // s: shown at least this long, and till
constexpr float kLost = 1.5f;     // ... he's lost him this long,
constexpr float kReached = 10.0f; // ... has him this close (m),
constexpr float kAtMost = 8.0f;   // ... or it's been this long;
constexpr float kFade = 0.6f;     // then it fades in this long (s)

// A cop or police helicopter, as of a look.
struct Look {
	int handle;
	bool helicopter;
	Vec3 eyes;
	bool sees;      // him
	bool shooting;  // at him
	float distance; // m to him
};

struct Spotter {
	int handle;
	bool helicopter;
	Vec3 eyes;
	float alpha = 1.0f; // fading out
	float since = 0.0f, lost = 0.0f, fade = 1.0f, distance = 1e9f;
	bool ending = false;
};

class Spotters {
public:
	// Each look: whether the police are on him (wanted, not searching or under
	// the APB), and every cop and helicopter there is.
	void Follow(bool onHim, const std::vector<Look> &looks, float dt);
	// One whose eye has filled.
	void Recognised(const Look &look);
	// One who saw a crime of his: spotted, unless the police were on him already.
	void SawCrime(const Look &look);
	void Clear();
	const std::vector<Spotter> &all() const { return spotters_; }

private:
	void Spot(const Look &look);
	std::vector<Spotter> spotters_;
	bool onHim_ = false; // as of the last look
};

} // namespace spotters
