#include "interact.h"
#include "config.h"
#include "draw.h"
#include "game.h"
#include "log.h"
#include "phone.h"
#include "prompt.h"
#include "text.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

using namespace game;

namespace interact {
namespace {

using Clock = std::chrono::steady_clock;

enum class Kind { kNone, kHooker, kStranger };

// Lines (CPed::Say) and objectives (CPed::SetObjective) as the game uses them:
// running off (CPed::Flee), a fight taunt (CPed::Fight), road rage at the horn
// (CAutomobile::PlayCarHorn), small talk (CPed::Chat).
constexpr uint16_t kLineFlee = 120, kLineTaunt = 138, kLineRoadRage = 146, kLineChat = 157;
constexpr int kObjectiveAttack = 8, kObjectiveFlee = 10; // KILL_CHAR_ON_FOOT, FLEE_CHAR_ON_FOOT_TILL_SAFE
constexpr int kNearbyMax = 10;     // CPed::m_nearPeds
constexpr float kReach = 2.8f;     // metres
constexpr float kFacing = 0.7f;    // cosine of how far off straight ahead
constexpr float kHeightGap = 1.5f; // metres: not someone on a balcony
constexpr uint32_t kChatMs = 4500;
constexpr auto kCooldown = std::chrono::seconds(20); // before the same person is offered again
// A hooker at the window chats for a second (CPed::SeekCar's SetSolicit(1000)),
// then gets in if he has over $100 (CPed::Solicit) or walks off: here she waits
// this long for an answer, then walks off.
constexpr auto kHookerWaits = std::chrono::seconds(10);
constexpr int32_t kHookerPrice = 100;
constexpr float kFade = 0.15f;                       // seconds

Kind gKind = Kind::kNone;
uint8_t *gPed; // whom the prompt is for
uint8_t *gLastPed;
Clock::time_point gLastAt;
uint8_t *gHooker; // the one waiting at the window, since
Clock::time_point gHookerSince;
float gShow;
Kind gShownKind = Kind::kNone;
Clock::time_point gLastDraw;
Im2DCanvas gCanvas;
bool gLogged;

bool Talkable(int32_t type) { return type == 4 || type == 5 || (type >= 7 && type <= 15) || type == 18 || type == 20; }

// A hooker leaning in at the window of his car.
uint8_t *HookerAtWindow(uint8_t *player, const uint8_t *vehicle)
{
	for (int i = 0; i < std::min(PedNearbyCount(player), kNearbyMax); i++) {
		uint8_t *p = PedNearby(player, i);
		if (p && PedState(p) == kPedSolicit && PedCarInObjective(p) == vehicle)
			return p;
	}
	return nullptr;
}

// The nearest person he's facing, up close and minding their own business.
uint8_t *Stranger(uint8_t *player)
{
	const CVector &pos = EntityPos(player), &fwd = EntityForward(player);
	const float fwdLen = std::hypot(fwd.x, fwd.y);
	if (fwdLen < 1e-3f)
		return nullptr;
	uint8_t *best = nullptr;
	float bestD2 = kReach * kReach;
	for (int i = 0; i < std::min(PedNearbyCount(player), kNearbyMax); i++) {
		uint8_t *p = PedNearby(player, i);
		if (!p || (p == gLastPed && Clock::now() - gLastAt < kCooldown) || Health(p) <= 0.0f || PedInVehicle(p) ||
		    PedFromMission(p) || !Talkable(PedType(p)) || PedObjective(p) != 0 || !api.IsPedInControl(p))
			continue;
		const CVector &q = EntityPos(p);
		const float dx = q.x - pos.x, dy = q.y - pos.y, d2 = dx * dx + dy * dy;
		if (d2 > bestD2 || d2 < 1e-4f || std::fabs(q.z - pos.z) > kHeightGap ||
		    (dx * fwd.x + dy * fwd.y) / (std::sqrt(d2) * fwdLen) < kFacing)
			continue;
		best = p;
		bestD2 = d2;
	}
	return best;
}

// Insulted: gang members and criminals square up, so do some men; the rest run
// or give it back.
void React(uint8_t *ped, uint8_t *player)
{
	const int32_t type = PedType(ped);
	const int roll = rand() % 100;
	if (api.IsGangMember(ped) || type == 18 || (type == 4 && roll < 25)) {
		api.Say(ped, kLineTaunt);
		api.SetObjective(ped, kObjectiveAttack, player);
	} else if (roll < 65) {
		api.Say(ped, kLineFlee);
		api.SetObjective(ped, kObjectiveFlee, player);
	} else {
		api.Say(ped, kLineRoadRage);
	}
}

} // namespace

void Update()
{
	gKind = Kind::kNone;
	gPed = nullptr;
	uint8_t *player = api.FindPlayerPed();
	uint8_t *pad = api.GetPad(0);
	if (!gConfig.interactions || !player || Health(player) <= 0.0f || PadControlsDisabled(pad))
		return;
	if (PedInVehicle(player)) {
		if (const uint8_t *vehicle = PedVehicle(player))
			gPed = HookerAtWindow(player, vehicle);
		if (gPed && gPed == gLastPed && Clock::now() - gLastAt < kCooldown)
			gPed = nullptr; // answered: getting in, or off
		gKind = gPed ? Kind::kHooker : Kind::kNone;
		if (!gPed)
			gHooker = nullptr;
		else if (gPed != gHooker) {
			gHooker = gPed;
			gHookerSince = Clock::now();
		}
		if (gPed && Clock::now() - gHookerSince < kHookerWaits) {
			PedChatTimer(gPed) = std::max(PedChatTimer(gPed), *api.timeMs + 500); // still chatting
		} else if (gPed) { // no answer: off she goes
			Respond(false, false);
			return;
		}
	} else if (api.IsPedInControl(player) && !api.PadGetTarget(pad) && !api.PadGetWeapon(pad)) {
		gPed = Stranger(player);
		gKind = gPed ? Kind::kStranger : Kind::kNone;
	}
	if (gKind != Kind::kNone && !gLogged) {
		gLogged = true;
		Log("interact: first prompt (%s)", gKind == Kind::kHooker ? "hooker" : "someone on foot");
	}
}

bool Prompting() { return gKind != Kind::kNone; }

bool CanAffordHooker() { return *reinterpret_cast<const int32_t *>(PlayerInfo() + 0xc8) > kHookerPrice; }

void Respond(bool positive, bool spoken)
{
	uint8_t *player = api.FindPlayerPed(), *ped = gPed;
	if (gKind == Kind::kNone || !player || !ped)
		return;
	if (gKind == Kind::kHooker) {
		if (positive) {
			static int logged;
			if (logged++ < 3)
				Log("interact: picking up a hooker with $%d", *reinterpret_cast<const int32_t *>(PlayerInfo() + 0xc8));
			if (!CanAffordHooker())
				return; // she waits; the prompt says what it takes
			PedChatTimer(ped) = *api.timeMs - 1; // her chat is over: in she gets
		} else {
			uint8_t *&car = PedCarInObjective(ped); // no car, no chat: she walks off (CPed::Solicit)
			if (car) {
				api.CleanUpOldReference(car, &car);
				car = nullptr;
			}
			PedChatTimer(ped) = 0;
			if (spoken)
				api.Say(player, kLineRoadRage);
		}
	} else if (positive) {
		api.SetChat(ped, player, kChatMs);
		api.Say(ped, kLineChat);
	} else {
		api.Say(player, rand() % 2 ? kLineRoadRage : kLineTaunt);
		React(ped, player);
	}
	gLastPed = ped;
	gLastAt = Clock::now();
	gKind = Kind::kNone;
	gPed = nullptr;
}

void Draw()
{
	const Clock::time_point now = Clock::now();
	const float dt = std::min(std::chrono::duration<float>(now - gLastDraw).count(), 0.1f);
	gLastDraw = now;
	if (gKind != Kind::kNone) {
		gShownKind = gKind;
		gShow = std::min(1.0f, gShow + dt / kFade);
	} else {
		gShow = std::max(0.0f, gShow - dt / kFade);
	}
	if (gShow <= 0.0f)
		return;
	const float H = float(ScreenHeight()), right = phone::LeftEdge(), h = prompt::Height(H);
	const float bottom = H - gConfig.margin * H;
	const bool hooker = gShownKind == Kind::kHooker;
	SetHudRenderStates();
	gCanvas.SetTexture(nullptr);
	const char *yes = !hooker ? "GREET" : CanAffordHooker() ? "PICK UP" : "NEED $100";
	prompt::Draw(gCanvas, right, bottom - h * 1.75f, H, prompt::Button::kFaceWest, yes, gShow);
	prompt::Draw(gCanvas, right, bottom - h * 0.5f, H, prompt::Button::kFaceEast, hooker ? "WAVE OFF" : "INSULT", gShow);
	text::Flush();
}

} // namespace interact
