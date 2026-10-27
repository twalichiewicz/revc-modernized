#include "vmgun.h"

#include <cstring>

// Each gun in assets/guns/ (build/gunlist.h and build/gunassets.S, made by tools/guns/embed.py).
#define GUN(name) extern "C" const uint8_t vmgun_##name[], vmgun_##name##_end[];
#include "gunlist.h"
#undef GUN

namespace vm {
namespace {

// The game's weapon types (eWeaponType) by their models' names.
struct Named {
	const char *name;
	int weapon;
};
constexpr Named kWeapons[] = {{"colt45", 17}, {"python", 18},   {"chromegun", 19}, {"shotgspa", 20}, {"buddyshot", 21},
                              {"tec9", 22},   {"uzi", 23},      {"ingramsl", 24},  {"mp5lng", 25},   {"m4", 26},
                              {"ruger", 27},  {"sniper", 28},   {"laser", 29},     {"rocketla", 30}, {"flame", 31},
                              {"m60", 32},    {"minigun", 33}};

} // namespace

int WeaponNamed(const char *name)
{
	for (const Named &n : kWeapons)
		if (strcmp(n.name, name) == 0)
			return n.weapon;
	return -1;
}

namespace {

const Embedded kEmbedded[] = {
#define GUN(name) {WeaponNamed(#name), #name, vmgun_##name, vmgun_##name##_end},
#include "gunlist.h"
#undef GUN
    {-1, "", nullptr, nullptr}, // none built in still compiles
};

} // namespace

const Embedded *FindEmbedded(int weapon)
{
	for (const Embedded &e : kEmbedded)
		if (e.weapon == weapon)
			return &e;
	return nullptr;
}

} // namespace vm
