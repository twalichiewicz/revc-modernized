#pragma once
// What the police know about Tommy, and where that leaves the chase: the
// wanted overhaul's model, with no game in it (wanted.cpp feeds it from the
// game every frame, tests/test_heat.cpp pins its rules).
//
//   Clear ─ a cop sees a crime, or a witness who saw him gets through ─► Wanted
//   Clear ─ a witness calling ─► Reported (only shown; it's still Clear)
//   Wanted ─ no cop has seen him for kLostAfterMs ─► Searching (stars dim)
//   Searching ─ a cop recognises him ─► Wanted, same stars
//   Searching ─ the search runs out, he's outside the area ─► APB (hollow
//               stars, the game's level 0) if they have a description or
//               there were two stars or more, else Clear
//   APB ─ a cop recognises him ─► Wanted, at the stars he escaped at
//   anything ─ a save, wasted, busted, a script clearing the stars ─► Clear
#include "types.h"

#include <cstdint>
#include <string>

namespace heat {

enum class State : uint8_t { Clear, Reported, Wanted, Searching, Apb };

// Tommy as a cop would see him now. vehicleModel -1: on foot.
struct Looks {
	std::string outfit;
	int vehicleModel = -1;
	int colour1 = -1, colour2 = -1;
};

// What the police have on him: the outfit and the last vehicle they saw him in.
struct Description {
	std::string outfit;
	int vehicleModel = -1, colour1 = -1, colour2 = -1;
	bool haveOutfit = false, haveVehicle = false;
};

constexpr int kMaxStars = 6;
constexpr uint32_t kLostAfterMs = 4000; // unseen this long and they've lost him
// How long they search after losing him, by stars, before giving up.
constexpr uint32_t kSearchMs[kMaxStars + 1] = {0, 20000, 30000, 40000, 50000, 60000, 75000};
constexpr float kEyeDecayPerSecond = 0.35f; // an eye empties this fast once it can't see him
constexpr int kApbStars = 2; // a search over at this many stars or more ends in an APB, a description or not

// The search area round where they lost him, m.
float SearchRadius(int stars);
// How fast a cop's eye fills, per second, at `distance` m: from far off if he
// matches the description, only from close up if not; a helicopter sees 2.5
// times as far but never knows him without a match.
float FillRate(float distance, bool matches, bool helicopter);

// Why the game's stars went down. The game drops them itself in ways the
// overhaul undoes (a Pay 'n' Spray: CWanted::Suspend; one star running down
// with no cop near: CWanted::Update leaves chaos under 50, never 0; the
// clothes-change script); everything else (a script's SetWantedLevel, the
// cheat, wasted or busted: CWanted::Reset) the model follows. Off a mission an
// unexplained drop waits a moment for the outfit to change with it.
enum class Drop : uint8_t { Respray, Timeout, Clothes, Wait, Script };
struct DropFacts {
	int level;          // the game's stars now
	int chaos;          // CWanted::m_nChaos now
	bool suspended;     // CWanted::Suspend ran just now
	bool onMission;     // CTheScripts::IsPlayerOnAMission
	bool outfitChanged; // the player model's name changed within the clothes window
	bool waited;        // the drop has waited the clothes window out
};
Drop ClassifyDrop(const DropFacts &facts);

class Model {
public:
	State state() const;
	int stars() const;     // shown: filled, dimmed while searching, hollow under the APB
	int gameStars() const; // the game's wanted level should be: stars while Wanted/Searching, else 0
	int escapedStars() const { return escaped_; }
	const Description &description() const { return description_; }
	Vec3 lastSeen() const { return lastSeen_; }
	uint32_t lastSeenAt() const { return lastSeenAt_; }
	Vec3 searchCentre() const { return searchCentre_; }
	float searchRadius() const { return SearchRadius(stars_); }
	uint32_t searchSince() const { return searchSince_; }

	// They know where he is now: a cop sees him (seenNow) or only just lost him, while Wanted.
	bool Knows(bool seenNow, uint32_t now) const;
	bool Matches(const Looks &looks) const;      // the outfit or the vehicle on the description, and the same now
	bool OutfitStruck(const Looks &looks) const;  // on the description, but he's changed it
	bool VehicleStruck(const Looks &looks) const; // in another vehicle, or the same one resprayed

	// A cop (or helicopter) has him: Wanted, the description now `looks`.
	void CopSees(const Looks &looks, Vec3 at, uint32_t now, int gameStars);
	// A call about a crime at `when` got through `now`: with looks if the
	// witness saw him, null for only where (a search there, from now). Under
	// the APB, looks not matching it are a stranger's: only where.
	void Reported(const Looks *looks, Vec3 at, uint32_t when, uint32_t now, int stars);
	void Calling(bool anyone);
	void Tick(Vec3 him, uint32_t now);
	void Lowered(int stars); // a script lowered the game's level
	void Clear();

private:
	void Identified(const Looks &looks, int stars);

	State state_ = State::Clear;
	int stars_ = 0, escaped_ = 0;
	bool calling_ = false;
	Description description_;
	Vec3 lastSeen_ = {0, 0, 0}, searchCentre_ = {0, 0, 0};
	uint32_t lastSeenAt_ = 0, searchSince_ = 0;
};

} // namespace heat
