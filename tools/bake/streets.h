#pragma once
// Street names (#35): outlines from assets/streets.txt, one street a line,
//   "Name" x,y x,y ...      (world metres; # comments and blank lines skipped)
// snapped onto the game's car-path roads at bake time. A road takes the
// outline whose nearest stretch is within kSnapDistance of the road's middle
// and runs within kSnapAngle of it (either way round); the nearest wins.
#include "../../src/mapdata.h"
#include <string>
#include <vector>

namespace streets {

constexpr float kSnapDistance = 12.0f;  // m
constexpr float kSnapAngle = 30.0f;     // degrees

struct Outline {
	std::string name;
	std::vector<Vec2> points;
};

// False, with `error` naming the line, on a malformed line.
bool ParseStreets(const std::string &text, std::vector<Outline> &out, std::string &error);
// Each road's street: an index into `outlines`, or mapdata::Map::kNoStreet.
std::vector<uint16_t> Assign(const std::vector<Outline> &outlines, const std::vector<mapdata::Road> &roads);

} // namespace streets
