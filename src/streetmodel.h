#pragma once
// Street names (#35), without the game: which street he's on, from map.bin's
// named roads, and when the name changes.
#include "mapdata.h"
#include <cstddef>
#include <map>
#include <utility>
#include <vector>

namespace streetmodel {

constexpr float kPavement = 6.0f;   // m past a road's half-width that's still on it (on foot)
constexpr float kHeading = 35.0f;   // degrees: driving, a road this close to his heading (either way)
constexpr float kHeight = 4.0f;     // m: a road further above or below him isn't his (a bridge)
constexpr float kSwitchAfter = 1.0f, kSwitchFar = 15.0f, kNoneAfter = 2.0f; // s, m, s

// The roads by 64 m cells, each in every cell it reaches into.
class Index {
public:
	explicit Index(const mapdata::Map &map);
	const std::vector<size_t> *Near(Vec2 p) const;

private:
	std::map<std::pair<int, int>, std::vector<size_t>> cells_;
};

// The street (an index into map.streets) under `pos`, or -1.
int StreetAt(const Index &index, const mapdata::Map &map, Vec3 pos, Vec2 heading, bool driving);

// The street shown: a new one takes over after kSwitchAfter s or kSwitchFar m
// on it; none (-1) after kNoneAfter s. Flipping back and forth keeps the current.
class Tracker {
public:
	int Update(int seen, Vec3 pos, float dt);
	int Current() const { return current_; }

private:
	int current_ = -1, candidate_ = -1;
	float candidateTime_ = 0;
	Vec3 candidateFrom_{};
	bool started_ = false;
};

// When the street and area show, GTA IV's way: on a new street, a new area
// (`area`: anything that changes when it does) or getting into a vehicle,
// rising over kRise, held kHold, fading over kFade; held while the expanded
// map is up; back on the street it last showed within kBack, not again.
constexpr float kRise = 0.25f, kHold = 4.0f, kFade = 0.6f, kBack = 3.0f; // s
class Banner {
public:
	void Update(int street, int area, bool gotIntoVehicle, bool expandedMap, float dt);
	float Alpha() const;

private:
	bool started_ = false, expanded_ = false;
	int area_ = 0, current_ = -1, lastShown_ = -1;
	float age_ = 1e9f, away_ = 0;
};

} // namespace streetmodel
