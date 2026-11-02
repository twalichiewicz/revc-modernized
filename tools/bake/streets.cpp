#include "streets.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>

namespace streets {
namespace {

constexpr float kCell = 64.0f;

struct Piece {
	Vec2 a, b;
	uint16_t street;
};

float Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
Vec2 Sub(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
float Length(Vec2 v) { return std::sqrt(Dot(v, v)); }

float DistanceToPiece(Vec2 p, const Piece &s)
{
	const Vec2 d = Sub(s.b, s.a);
	const float len2 = Dot(d, d);
	const float t = len2 > 0 ? std::clamp(Dot(Sub(p, s.a), d) / len2, 0.0f, 1.0f) : 0.0f;
	return Length(Sub(p, {s.a.x + d.x * t, s.a.y + d.y * t}));
}

std::pair<int, int> CellOf(Vec2 p) { return {int(std::floor(p.x / kCell)), int(std::floor(p.y / kCell))}; }

} // namespace

bool ParseStreets(const std::string &text, std::vector<Outline> &out, std::string &error)
{
	out.clear();
	std::istringstream in(text);
	std::string line;
	for (int number = 1; std::getline(in, line); ++number) {
		const size_t start = line.find_first_not_of(" \t\r");
		if (start == std::string::npos || line[start] == '#')
			continue;
		const size_t close = line[start] == '"' ? line.find('"', start + 1) : std::string::npos;
		if (close == std::string::npos || close == start + 1) {
			error = "line " + std::to_string(number) + ": a street starts with its \"name\"";
			return false;
		}
		Outline o{line.substr(start + 1, close - start - 1), {}};
		std::istringstream points(line.substr(close + 1));
		for (std::string point; points >> point;) {
			const size_t comma = point.find(',');
			char *endX = nullptr, *endY = nullptr;
			const float x = std::strtof(point.c_str(), &endX);
			const float y = comma == std::string::npos ? 0 : std::strtof(point.c_str() + comma + 1, &endY);
			if (comma == std::string::npos || endX != point.c_str() + comma || !endY || *endY || !std::isfinite(x) ||
			    !std::isfinite(y)) {
				error = "line " + std::to_string(number) + ": \"" + point + "\" isn't x,y";
				return false;
			}
			o.points.push_back({x, y});
		}
		if (o.points.size() < 2) {
			error = "line " + std::to_string(number) + ": " + o.name + " needs two points or more";
			return false;
		}
		out.push_back(std::move(o));
	}
	return true;
}

std::vector<uint16_t> Assign(const std::vector<Outline> &outlines, const std::vector<mapdata::Road> &roads)
{
	// Each outline's pieces, filed under every cell they come within reach of.
	std::vector<Piece> pieces;
	std::map<std::pair<int, int>, std::vector<size_t>> grid;
	for (size_t s = 0; s < outlines.size() && s < mapdata::Map::kNoStreet; s++)
		for (size_t i = 0; i + 1 < outlines[s].points.size(); i++) {
			const Piece piece{outlines[s].points[i], outlines[s].points[i + 1], uint16_t(s)};
			const Vec2 lo{std::min(piece.a.x, piece.b.x) - kSnapDistance, std::min(piece.a.y, piece.b.y) - kSnapDistance};
			const Vec2 hi{std::max(piece.a.x, piece.b.x) + kSnapDistance, std::max(piece.a.y, piece.b.y) + kSnapDistance};
			const auto c0 = CellOf(lo), c1 = CellOf(hi);
			for (int cx = c0.first; cx <= c1.first; cx++)
				for (int cy = c0.second; cy <= c1.second; cy++)
					grid[{cx, cy}].push_back(pieces.size());
			pieces.push_back(piece);
		}
	const float minCos = std::cos(kSnapAngle * 3.14159265f / 180.0f);
	std::vector<uint16_t> out(roads.size(), mapdata::Map::kNoStreet);
	for (size_t r = 0; r < roads.size(); r++) {
		const Vec2 mid{(roads[r].a.x + roads[r].b.x) / 2, (roads[r].a.y + roads[r].b.y) / 2};
		const Vec2 d = Sub(roads[r].b, roads[r].a);
		const float len = Length(d);
		const auto found = grid.find(CellOf(mid));
		if (len <= 0 || found == grid.end())
			continue;
		float best = kSnapDistance;
		for (size_t index : found->second) {
			const Piece &p = pieces[index];
			const Vec2 pd = Sub(p.b, p.a);
			const float plen = Length(pd);
			if (plen <= 0 || std::fabs(Dot(d, pd)) / (len * plen) < minCos)
				continue; // across it, not along it
			const float distance = DistanceToPiece(mid, p);
			if (distance <= best) {
				best = distance;
				out[r] = p.street;
			}
		}
	}
	return out;
}

Coverage Measure(const std::vector<mapdata::Road> &roads, const std::vector<uint16_t> &street, Vec2 lo, Vec2 hi)
{
	Coverage c;
	for (size_t r = 0; r < roads.size(); r++) {
		const Vec2 mid{(roads[r].a.x + roads[r].b.x) / 2, (roads[r].a.y + roads[r].b.y) / 2};
		if (mid.x < lo.x || mid.y < lo.y || mid.x > hi.x || mid.y > hi.y)
			continue;
		const float length = Length(Sub(roads[r].b, roads[r].a));
		c.total += length;
		if (r < street.size() && street[r] != mapdata::Map::kNoStreet)
			c.named += length;
	}
	return c;
}

} // namespace streets
