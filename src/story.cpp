#include "story.h"
#include "config.h"
#include "credit.h"
#include "hooks.h"
#include "hud.h"
#include "log.h"
#include "phone.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <string>

using namespace game;

namespace story {

// From the scripts: the for-sale signs (the blips "Shakedown" adds), the
// owned icons, and the flags each storyline's last mission sets.
const Business kBusinesses[8] = {
    {"the Print Works", "Print Works", -1059.6f, -274.5f, 24, 273},
    {"Sunshine Autos", "Sunshine Autos", -1007.3f, -869.9f, 26, 628},
    {"InterGlobal Films", "InterGlobal Films", 15.2f, 962.6f, 15, 633},
    {"the Cherry Popper", "Cherry Popper", -864.3f, -576.6f, 21, 612},
    {"Kaufman Cabs", "Kaufman Cabs", -1011.7f, 203.9f, 22, 623},
    {"the Malibu", "Malibu Club", 487.2f, -81.5f, 13, 642},
    {"the boatyard", "Boatyard", -685.8f, -1495.6f, 12, 607},
    {"Pole Position", "Pole Position", 99.5f, -1468.5f, 20, 1096},
};

namespace {

const Business &kPrintWorks = kBusinesses[0];
constexpr int kVarStorylinesDone = 1175; // Cop Land's counts too
constexpr int kVarCopLand = 268;
constexpr int kVarKenCalled = 1130;      // his call about the Print Works came: the finale's on
constexpr int kStorylinesForFinale = 7;  // the script asks for more than 6
// That check in the thread, IS_INT_VAR_GREATER_THAN_NUMBER $1175 6: the
// main.scm these globals are for.
constexpr size_t kCheckAt = 0x2621c;
constexpr uint8_t kCheck[7] = {0x18, 0x00, 0x02, 0x5c, 0x12, 0x04, 0x06};

constexpr float kSignNear = 30.0f;  // its for-sale pickup is by the sign
constexpr float kOwnedNear = 55.0f; // its owned icon can be further off (the boatyard's is 45 m away)
// Game time (it stops in the pause menu), ms: after a load, after news, then again.
constexpr uint32_t kFirstAfter = 25000, kNewsAfter = 12000, kRepeatAfter = 10 * 60000, kRetryAfter = 5000;

bool gInstalled;
bool (*gOnAMission)();
bool gHaveState;
uint32_t gState, gNextAt, gLastNow;

bool Near(const CVector &p, float x, float y, float d) { return (p.x - x) * (p.x - x) + (p.y - y) * (p.y - y) < d * d; }

// How many more storylines it needs besides the Print Works' and Cop Land's.
int OthersNeeded()
{
	const int left = kStorylinesForFinale - ScriptGlobal(kVarStorylinesDone);
	return std::max(0, left - !Done(kPrintWorks) - !ScriptGlobal(kVarCopLand));
}

// Changes when there's news: something bought, finished, or paid off.
uint32_t State()
{
	uint32_t s = uint32_t(ScriptGlobal(kVarStorylinesDone)) & 0xf;
	for (int i = 0; i < 8; i++) {
		s |= (OwnedBlip(kBusinesses[i]) ? 1u : 0u) << (4 + i);
		s |= (Done(kBusinesses[i]) ? 1u : 0u) << (12 + i);
	}
	return s | (ScriptGlobal(kVarCopLand) ? 1u << 20 : 0) | (credit::Owed() > 0 ? 1u << 21 : 0);
}

const char *Count(int n)
{
	static const char *const kWords[] = {"no", "one", "two", "three", "four", "five", "six", "seven"};
	return kWords[std::clamp(n, 0, 7)];
}

// What Ken says is left.
std::string Advice()
{
	std::string s;
	char money[24], line[160];
	if (!Done(kPrintWorks)) {
		if (const uint8_t *sign = SaleSign(kPrintWorks)) {
			hud::FormatMoney(Price(sign), money, sizeof money);
			snprintf(line, sizeof line, "The Print Works is up for sale, %s. We need that place, Tommy!", money);
			s += line;
		} else if (OwnedBlip(kPrintWorks)) {
			s += "Get over to the Print Works, Tommy. There's work to do there.";
		}
	}
	if (const int others = OthersNeeded()) {
		snprintf(line, sizeof line, "%s%s %s more of your businesses going. They're on your map.", s.empty() ? "" : " ",
		         s.empty() ? "Get" : "And get", Count(others));
		s += line;
	}
	if (!ScriptGlobal(kVarCopLand))
		s += " They still need you at the Vercetti Estate, too.";
	if (const int32_t owed = credit::Owed(); owed > 0) {
		hud::FormatMoney(owed, money, sizeof money);
		snprintf(line, sizeof line, " You still owe %s on that credit.", money);
		s += line;
	}
	return s;
}

} // namespace

const uint8_t *SaleSign(const Business &b)
{
	for (int i = 0; i < kNumPickups; i++) {
		const uint8_t *p = Pickup(i);
		if (p[0x36] == kPickupPropertyForSale && !p[0x37] /* removed */ && Near(PickupPos(p), b.x, b.y, kSignNear))
			return p;
	}
	return nullptr;
}

const Blip *OwnedBlip(const Business &b)
{
	for (int i = 0; i < kNumBlips; i++) {
		const Blip &blip = api.blips[i];
		if (blip.inUse && blip.sprite == b.sprite && Near(blip.pos, b.x, b.y, kOwnedNear))
			return &blip;
	}
	return nullptr;
}

const Business *SoldAt(const uint8_t *pickup)
{
	if (pickup[0x36] != kPickupPropertyForSale)
		return nullptr;
	for (const Business &b : kBusinesses)
		if (Near(PickupPos(pickup), b.x, b.y, kSignNear))
			return &b;
	return nullptr;
}

int32_t Price(const uint8_t *sign) { return *reinterpret_cast<const int32_t *>(sign + 0x20); } // m_nQuantity

bool Done(const Business &b) { return gInstalled && ScriptGlobal(b.doneVar) != 0; }

bool Waiting()
{
	if (!gInstalled || ScriptGlobal(kVarKenCalled))
		return false;
	if (ScriptGlobal(kVarStorylinesDone) >= kStorylinesForFinale && Done(kPrintWorks) && ScriptGlobal(kVarCopLand))
		return false; // his call's coming
	for (const Business &b : kBusinesses)
		if (SaleSign(b) || OwnedBlip(b))
			return true; // "Shakedown" has put them up for sale
	return false;
}

bool Needed(const Business &b)
{
	return gConfig.storyHints && !Done(b) && Waiting() && (&b == &kPrintWorks || OthersNeeded() > 0);
}

bool Install()
{
	gOnAMission = reinterpret_cast<bool (*)()>(dlsym(RTLD_MAIN_ONLY, "_ZN11CTheScripts18IsPlayerOnAMissionEv"));
	gInstalled = api.scriptSpace && gOnAMission && memcmp(api.scriptSpace + kCheckAt, kCheck, sizeof kCheck) == 0;
	if (!gInstalled)
		Log("story: not the main.scm this was made for; no business hints or credit");
	return gInstalled;
}

void Update()
{
	if (!gInstalled || !gConfig.storyHints)
		return;
	const uint32_t now = *api.timeMs;
	if (now < gLastNow) // a save loaded: start over
		gHaveState = false;
	gLastNow = now;
	if (!Waiting()) {
		gHaveState = false;
		return;
	}
	const uint32_t state = State();
	if (!gHaveState) {
		gHaveState = true;
		gState = state;
		gNextAt = now + kFirstAfter;
	} else if (state != gState) {
		gState = state;
		gNextAt = now + kNewsAfter;
	}
	if (now < gNextAt)
		return;
	const uint8_t *ped = api.FindPlayerPed();
	const std::string advice = Advice();
	if (!ped || gOnAMission() || advice.empty() || !phone::Text("KEN", "Ken Rosenberg", advice.c_str())) {
		gNextAt = now + kRetryAfter;
		return;
	}
	gNextAt = now + kRepeatAfter;
}

} // namespace story
