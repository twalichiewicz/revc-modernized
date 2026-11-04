// The car handling retune (#37) against the game's own handling.cfg ($GAMEDATA):
// the 71 cars are exactly its cars, each still reads sensibly after the retune,
// its springs keep the ride height with travel to spare, its top speed lands on
// its type's factor, and within a type the cars keep their order.
#include "handlingmodel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace handlingmodel;
static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)

struct Line {
	std::string name;
	Record raw;
};
static std::vector<Line> gLines; // the plain lines from ;Exotic to ;------- bikes

// As LoadHandlingData parses a plain line: 33 columns, A to g.
static bool Parse(const std::string &text, Line &out)
{
	std::istringstream in(text);
	std::vector<std::string> f;
	for (std::string w; in >> w;)
		f.push_back(w);
	if (f.size() < 33)
		return false;
	Record r{};
	auto F = [&](int i) { return float(std::atof(f[i].c_str())); };
	r.mass = F(1);
	for (int i = 0; i < 3; ++i)
		r.dimension[i] = F(2 + i), r.centreOfMass[i] = F(5 + i);
	r.percentSubmerged = int8_t(std::atoi(f[8].c_str()));
	r.tractionMultiplier = F(9), r.tractionLoss = F(10), r.tractionBias = F(11);
	r.numberOfGears = int8_t(std::atoi(f[12].c_str()));
	r.maxVelocity = F(13);
	r.engineAcceleration = float(std::atof(f[14].c_str()) * 0.4);
	r.driveType = uint8_t(f[15][0]), r.engineType = uint8_t(f[16][0]);
	r.brakeDeceleration = F(17), r.brakeBias = F(18), r.abs = int8_t(std::atoi(f[19].c_str()));
	r.steeringLock = F(20), r.suspensionForce = F(21), r.suspensionDamping = F(22), r.seatOffset = F(23);
	r.collisionDamage = F(24), r.money = std::atoi(f[25].c_str());
	r.suspensionUpper = F(26), r.suspensionLower = F(27), r.suspensionBias = F(28), r.antiDive = F(29);
	r.flags = uint32_t(std::strtoul(f[30].c_str(), nullptr, 16));
	r.frontLights = int8_t(std::atoi(f[31].c_str())), r.rearLights = int8_t(std::atoi(f[32].c_str()));
	out = {f[0], r};
	return true;
}

static bool Load()
{
	const char *root = std::getenv("GAMEDATA");
	if (!root) {
		std::printf("GAMEDATA not set\n");
		return false;
	}
	std::ifstream file(std::string(root) + "/data/handling.cfg");
	if (!file) {
		std::printf("no handling.cfg under %s\n", root);
		return false;
	}
	bool inCars = false;
	for (std::string text; std::getline(file, text);) {
		if (text.rfind(";Exotic", 0) == 0)
			inCars = true;
		if (text.rfind(";------- bikes", 0) == 0)
			break;
		if (!inCars || text.empty() || std::strchr(";%!$\r", text[0]))
			continue;
		Line line;
		if (Parse(text, line))
			gLines.push_back(line);
	}
	return !gLines.empty();
}

static Kind KindOf(const Line &l) { return Find(l.name.c_str())->kind; }
static Record Retuned(const Line &l)
{
	Record r = l.raw;
	BeforeConversion(r, KindOf(l));
	return r;
}
static const char *KindName(Kind k)
{
	static const char *const names[] = {"Sports", "Car", "Van", "OffRoad", "LightTruck", "HeavyTruck", "Tank", "Small"};
	return names[int(k)];
}

static void TestListMatchesFile()
{
	const std::set<std::string> notCars = {"AIRTRAIN", "DEADDODO", "DODO", "HELI", "TRAIN", "RCBANDIT"};
	std::set<std::string> file, list;
	for (const Line &l : gLines)
		if (!notCars.count(l.name))
			file.insert(l.name);
	for (int i = 0; i < kCarCount; ++i)
		list.insert(kCars[i].name);
	CHECK(file == list);
	CHECK(int(list.size()) == kCarCount);
	for (const std::string &name : file)
		if (!list.count(name))
			std::printf("  in the file, not the list: %s\n", name.c_str());
	for (const std::string &name : list)
		if (!file.count(name))
			std::printf("  in the list, not the file: %s\n", name.c_str());
}

static void TestEveryCarPlausible()
{
	for (const Line &l : gLines)
		if (Find(l.name.c_str())) {
			CHECK(Plausible(l.raw));
			CHECK(Plausible(Retuned(l)));
		}
}

// Each axle at rest, the centre of mass midway (as the model has it): the share
// of its travel compressed, and its height.
static float AxleCompression(const Record &r, bool front)
{
	return 1.0f / (8 * r.suspensionForce * (front ? r.suspensionBias : 1 - r.suspensionBias));
}
static float AxleHeight(const Record &r, bool front)
{
	return -r.suspensionLower - AxleCompression(r, front) * (r.suspensionUpper - r.suspensionLower);
}

static void TestEveryCarsSuspension()
{
	for (const Line &l : gLines) {
		if (!Find(l.name.c_str()))
			continue;
		const Record was = l.raw, r = Retuned(l);
		bool ok = std::fabs(AxleHeight(r, true) + AxleHeight(r, false) - AxleHeight(was, true) - AxleHeight(was, false)) <=
		          0.002f; // the car as a whole, to 1 mm
		for (bool front : {true, false})
			ok = ok && AxleCompression(r, front) <= std::max(0.4f, AxleCompression(was, front)) + 1e-4f &&
			     std::fabs(AxleHeight(r, front) - AxleHeight(was, front)) <= 0.01f + 1e-4f;
		const float springs = (r.suspensionForce / (r.suspensionUpper - r.suspensionLower)) /
		                      (was.suspensionForce / (was.suspensionUpper - was.suspensionLower));
		if (!ok || springs > For(KindOf(l)).springs + 0.005f)
			std::printf("  %-9s bias %.2f: springs %.2f of %.2f wanted, compressed front %.2f rear %.2f (was %.2f %.2f)\n",
			            l.name.c_str(), was.suspensionBias, springs, For(KindOf(l)).springs, AxleCompression(r, true),
			            AxleCompression(r, false), AxleCompression(was, true), AxleCompression(was, false));
		CHECK(ok);
	}
}

static void TestEveryTopSpeed()
{
	std::printf("  %-9s %-10s %6s %6s %6s %6s  %s\n", "car", "type", "kg", "km/h", "kg", "km/h", "springs");
	for (const Line &l : gLines) {
		if (!Find(l.name.c_str()))
			continue;
		const Record r = Retuned(l);
		const float before = DragTopSpeed(l.raw), after = DragTopSpeed(r), want = For(KindOf(l)).top * before;
		std::printf("  %-9s %-10s %6.0f %6.1f %6.0f %6.1f  force %.2f -> %.2f, lower %.2f -> %.2f\n", l.name.c_str(),
		            KindName(KindOf(l)), l.raw.mass, before, r.mass, after, l.raw.suspensionForce, r.suspensionForce,
		            l.raw.suspensionLower, r.suspensionLower);
		CHECK(std::fabs(after - want) <= 2.0f);
	}
}

// Within a type, the order by a field is the same after the retune.
template <typename Get> static void SameOrder(const char *field, Get get)
{
	for (int k = 0; k <= int(Kind::Small); ++k) {
		std::vector<const Line *> cars;
		for (const Line &l : gLines)
			if (Find(l.name.c_str()) && int(KindOf(l)) == k)
				cars.push_back(&l);
		for (const Line *a : cars)
			for (const Line *b : cars) {
				const float wa = get(a->raw), wb = get(b->raw), na = get(Retuned(*a)), nb = get(Retuned(*b));
				if (wa < wb && na > nb) { // (ties allowed: the top speed search steps 1.8 km/h)
					std::printf("  %s: %s below %s before, above after\n", field, a->name.c_str(), b->name.c_str());
					++failures;
				}
			}
	}
}

static void TestCharacterKept()
{
	SameOrder("mass", [](const Record &r) { return r.mass; });
	SameOrder("top speed", [](const Record &r) { return DragTopSpeed(r); });
	SameOrder("traction", [](const Record &r) { return r.tractionMultiplier; });
	SameOrder("brakes", [](const Record &r) { return r.brakeDeceleration; });
}

static void TestModelClasses()
{
 std::ifstream file(std::string(std::getenv("GAMEDATA"))+"/data/default.ide");CHECK(bool(file));int count=0;
 for(std::string row;std::getline(file,row);){std::replace(row.begin(),row.end(),',',' ');std::istringstream in(row);int id;std::string model,texture,type,handling;
  if(!(in>>id>>model>>texture>>type>>handling)||type!="car")continue;
  const auto *expected=Find(handling.c_str()),*actual=FindModel(id);
  if(expected){++count;CHECK(actual&&actual==expected);}
  else CHECK(!actual);
 }
 CHECK(count==77); //71 primary models plus6 aliases (includes Rhino)
}

static void TestNoStabiliser()
{
	for (const Line &l : gLines)
		if (Find(l.name.c_str()))
			CHECK(!(l.raw.flags & 0x800)); // NONPLAYER_STABILISER would read dimension z for AI cars' centre of mass
}

int main()
{
	if (!Load())
		return 1;
	TestListMatchesFile();
	TestEveryCarPlausible();
	TestEveryCarsSuspension();
	TestEveryTopSpeed();
	TestCharacterKept();
	TestNoStabiliser();
 TestModelClasses();
	if (failures)
		std::printf("%d failure(s)\n", failures);
	else
		std::printf("handling assets: all passed\n");
	return failures ? 1 : 0;
}
