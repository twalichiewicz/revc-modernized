#include "phone.h"
#include "brickphone.h"
#include "config.h"
#include "draw.h"
#include "driveby.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "text.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

using namespace game;

namespace phone {
namespace {

using Clock = std::chrono::steady_clock;

// --- the CELL script (the original main.scm) ------------------------------------

// Its call subroutine (at 0x28cc6) rings, waits for L1 and raises the phone;
// each of the 41 calls GOSUBs it, then plays its lines if it was picked up.
// $1179 says how the call is going: 0 ringing (or no call), 1 picked up,
// 2 dropped; $1173 counts the rings.
constexpr uint8_t kGosubCall[7] = {0x50, 0x00, 0x01, 0xc6, 0x8c, 0x02, 0x00}; // GOSUB 0x28cc6
constexpr int kVarCallState = 1179, kVarRings = 1173;

struct Patch {
	uint32_t at;
	uint8_t size;
	uint8_t from[10], to[10];
};
const Patch kPatches[] = {
    // A call only starts while he's on foot: IS_CHAR_ON_FOOT $3 -> IS_PLAYER_PLAYING $2
    // (no mission, no wanted level and the 20 s since the last one still apply).
    {0x25265, 5, {0x4b, 0x04, 0x02, 0x0c, 0x00}, {0x56, 0x02, 0x02, 0x08, 0x00}},
    // ... and hangs up when he isn't: NOT IS_CHAR_ON_FOOT $3 -> NOT IS_PLAYER_PLAYING $2.
    {0x28ee7, 5, {0x4b, 0x84, 0x02, 0x0c, 0x00}, {0x56, 0x82, 0x02, 0x08, 0x00}},
    {0x28f7f, 5, {0x4b, 0x84, 0x02, 0x0c, 0x00}, {0x56, 0x82, 0x02, 0x08, 0x00}},
    {0x28ffe, 5, {0x4b, 0x84, 0x02, 0x0c, 0x00}, {0x56, 0x82, 0x02, 0x08, 0x00}},
    // "Press L1 to answer your cell phone" (PRINT_HELP, PRINT_HELP_FOREVER): NOPs, the phone says it.
    {0x25409, 10, {0xe5, 0x03, 'A', 'N', 'S', 'W', 'E', 'R', 0, 0}, {}},
    {0x28cd4, 10, {0x12, 0x05, 'A', 'N', 'S', 'W', 'E', 'R', 0, 0}, {}},
    // SET_EVERYONE_IGNORE_PLAYER $2 1 -> 0: he isn't standing defenceless on the phone any more.
    {0x28e15, 7, {0xbf, 0x03, 0x02, 0x08, 0x00, 0x04, 0x01}, {0xbf, 0x03, 0x02, 0x08, 0x00, 0x04, 0x00}},
};

// Who is calling, by where the call's GOSUB returns to.
struct Caller {
	uint32_t ret;
	const char *name;
};
constexpr const char *kUnknown = "UNKNOWN";
const Caller kCallers[] = {
    {0x2541a, kUnknown}, // for Leo, the phone's last owner
    {0x255f7, "COLONEL CORTEZ"}, {0x257d7, "COLONEL CORTEZ"}, {0x258b8, "COLONEL CORTEZ"},
    {0x259e6, "KENT PAUL"},      {0x25b75, "COLONEL CORTEZ"}, {0x25e4e, "RICARDO DIAZ"},
    {0x25fd6, "KENT PAUL"},      {0x26118, "KENT PAUL"},      {0x26231, "KEN ROSENBERG"},
    {0x26334, "KENT PAUL"},      {0x26457, "KENT PAUL"},      {0x2657a, "MITCH BAKER"},
    {0x26684, "MITCH BAKER"},    {0x267b8, kUnknown},         {0x26934, "AUNTIE POULET"},
    {0x26a55, "AUNTIE POULET"},  {0x26b0b, "KEN ROSENBERG"},  {0x26c9a, "AVERY CARRINGTON"},
    {0x26df8, "KENT PAUL"},      {0x26faa, "UMBERTO ROBINA"}, {0x27108, "PHIL CASSIDY"},
    {0x2722b, "PHIL CASSIDY"},   {0x2734e, "PHIL CASSIDY"},   {0x2742e, "KENT PAUL"},
    {0x275f9, "LANCE VANCE"},    {0x277ab, "SONNY FORELLI"},  {0x27999, "SONNY FORELLI"},
    {0x27b64, "SONNY FORELLI"},  {0x27cb1, "KEN ROSENBERG"},  {0x27e63, "LANCE VANCE"},
    {0x28111, "LANCE VANCE"},    {0x28299, "LANCE VANCE"},    {0x28366, "EARNEST KELLY"},
    {0x2858c, "KENT PAUL"},      {0x287d2, "STEVE SCOTT"},
    // the payphone jobs
    {0x28930, kUnknown}, {0x289e8, kUnknown}, {0x28aa0, kUnknown}, {0x28b58, kUnknown}, {0x28c10, kUnknown},
};

// --- the phone in his hand --------------------------------------------------------

constexpr int kAnimPhoneIn = 0xa4, kAnimPhoneOut = 0xa5, kAnimPhoneTalk = 0xa6; // standard group, all partial
constexpr int kPhoneModel = 258;    // the cellphone CPed::AnswerMobile puts in his hand
constexpr float kInHandAt = 0.85f;  // seconds into PHONE_IN when it's out of his pocket (CPed::AnswerMobile)
constexpr float kDropFade = 8.0f;   // blend per second when something else takes his hand
constexpr int32_t kWeaponFist = 0;
constexpr auto kAnswerHold = std::chrono::milliseconds(1000); // L1 for the script, at most
constexpr auto kSettle = std::chrono::milliseconds(1000);      // after getting in or out of a vehicle

float AnimTime(const uint8_t *assoc) { return *reinterpret_cast<const float *>(assoc + 0x30); }
void FadeOut(uint8_t *assoc)
{
	*reinterpret_cast<float *>(assoc + 0x2c) = -kDropFade;
	*reinterpret_cast<uint16_t *>(assoc + 0x3e) |= 0x4; // ASSOC_DELETEFADEDOUT
}
// Streamed in (the script requests it before raising the phone): CSimpleModelInfo's first atomic.
bool PhoneModelLoaded()
{
	const uint8_t *mi = api.modelInfoPtrs[kPhoneModel];
	return mi && *reinterpret_cast<void *const *>(mi + 0x30);
}
// The weapon the game puts back when he gets out of a vehicle (CPed::m_storedWeapon).
int32_t &StoredWeapon(uint8_t *ped) { return *reinterpret_cast<int32_t *>(ped + 0x640); }

void (*orig_ScriptsProcess)();
void (*orig_SetAnswerMobile)(uint8_t *ped);
void (*orig_ClearAnswerMobile)(uint8_t *ped);

bool gInstalled;
bool gScriptOk, gScriptChecked; // main.scm is the one the patches and call sites are for

// The call, as the script has it.
bool gCallOn, gRinging, gConnected;
const char *gCaller = kUnknown;
int gRings;
Clock::time_point gCallStart, gRingAt, gConnectedAt, gAnswerUntil;

// The phone in his hand, once picked up.
enum class Hand { kEmpty, kRaising, kTalking, kLowering };
Hand gHand = Hand::kEmpty;
uint8_t *gPed;
bool gOurCall;      // the script's SET_CHAR_ANSWERING_MOBILE came to us, not to CPed
int gHeldSlot = -1; // the weapon he had out, back in his hand after the call
bool gWasInVehicle;
Clock::time_point gSettleUntil; // just got in or out: the game is still handing his weapons around

// VCHUD_TEST_PHONE: a pretend call 10 s after each one ends, ringing until
// answered and lasting 15 s, to try the phone on foot and in vehicles.
bool gTest;
enum class TestCall { kIdle, kRinging, kOn };
TestCall gTestCall = TestCall::kIdle;
Clock::time_point gTestAt;
constexpr auto kTestGap = std::chrono::seconds(10), kTestRing = std::chrono::seconds(3),
               kTestLength = std::chrono::seconds(15);

// Applies the patches once main.scm is loaded, and again whenever it's reloaded.
void PatchScript()
{
	uint8_t *code = api.scriptSpace;
	if (memcmp(code + kPatches[0].at, kPatches[0].to, kPatches[0].size) == 0)
		return;
	bool original = true;
	for (const Patch &p : kPatches)
		original = original && memcmp(code + p.at, p.from, p.size) == 0;
	if (!original) {
		if (!gScriptChecked || gScriptOk)
			Log("phone: main.scm isn't the original, calls left as they are");
		gScriptChecked = true;
		gScriptOk = false;
		return;
	}
	for (const Patch &p : kPatches)
		memcpy(code + p.at, p.to, p.size);
	if (!gScriptChecked)
		Log("phone: calls ring in vehicles too, D-pad up answers");
	gScriptChecked = gScriptOk = true;
}

uint8_t *CellScript()
{
	for (uint8_t *s = *api.activeScripts; s; s = ScriptNext(s))
		if (strncasecmp(ScriptName(s), "cell", 8) == 0)
			return s;
	return nullptr;
}

// The caller while the CELL script is in its call subroutine, else nullptr.
const char *CallerOnTheLine()
{
	const uint8_t *cell = CellScript();
	if (!cell)
		return nullptr;
	const uint32_t *stack = ScriptStack(cell);
	for (int i = std::min(ScriptStackDepth(cell), 6) - 1; i >= 0; i--) {
		const uint32_t ret = stack[i];
		if (ret < sizeof kGosubCall || ret > 0x40000 || memcmp(api.scriptSpace + ret - sizeof kGosubCall, kGosubCall,
		                                                        sizeof kGosubCall) != 0)
			continue;
		for (const Caller &c : kCallers)
			if (c.ret == ret)
				return c.name;
		return kUnknown;
	}
	return nullptr;
}

void OnPhoneRaised(uint8_t *, void *ped)
{
	if (gHand == Hand::kRaising && ped == gPed) {
		api.BlendAnimation(EntityClump(gPed), 0, kAnimPhoneTalk, 4.0f);
		gHand = Hand::kTalking;
	}
}

void Pocket()
{
	if (PedHandModel(gPed) == kPhoneModel)
		api.RemoveWeaponModel(gPed, kPhoneModel);
	gHand = Hand::kEmpty;
}

void OnPhoneLowered(uint8_t *, void *ped)
{
	if (gHand == Hand::kLowering && ped == gPed)
		Pocket();
}

// On foot the phone takes his right hand: whatever he holds goes, as the game
// does it, and comes back after.
void FreeHand(uint8_t *ped)
{
	const int slot = PedCurrentWeaponSlot(ped);
	if (slot != 0) {
		gHeldSlot = slot;
		api.SetCurrentWeapon(ped, 0);
	}
}

void GiveBackWeapon(uint8_t *ped)
{
	if (gHeldSlot < 0 || gHand != Hand::kEmpty || PedInVehicle(ped))
		return;
	if (PedCurrentWeaponSlot(ped) == 0 && WeaponType(PedWeapon(ped, gHeldSlot)) != 0)
		api.SetCurrentWeapon(ped, gHeldSlot);
	gHeldSlot = -1;
}

// What SET_CHAR_ANSWERING_MOBILE does (CPed::SetAnswerMobile), without the
// PED_ANSWER_MOBILE state that keeps him standing there: the animations only
// move his head and right arm, so he can walk or drive with it.
void RaisePhone(uint8_t *ped)
{
	gPed = ped;
	gHeldSlot = -1;
	gWasInVehicle = PedInVehicle(ped);
	if (!gWasInVehicle)
		FreeHand(ped);
	if (uint8_t *in = api.BlendAnimation(EntityClump(ped), 0, kAnimPhoneIn, 4.0f))
		api.SetFinishCallback(in, OnPhoneRaised, ped);
	gHand = Hand::kRaising;
}

// Back in his pocket at the end of the call (CPed::ClearAnswerMobile).
void LowerPhone()
{
	if (gHand != Hand::kRaising && gHand != Hand::kTalking)
		return;
	if (uint8_t *out = api.BlendAnimation(EntityClump(gPed), 0, kAnimPhoneOut, 8.0f)) {
		api.SetFinishCallback(out, OnPhoneLowered, gPed);
		gHand = Hand::kLowering;
	} else {
		Pocket();
	}
}

// He needs his hand for something else (aiming, a punch, a weapon): the phone
// goes at once, and the call carries on.
void DropPhone()
{
	uint8_t *clump = EntityClump(gPed);
	for (int anim : {kAnimPhoneIn, kAnimPhoneTalk})
		if (uint8_t *assoc = api.GetAssociation(clump, anim))
			FadeOut(assoc);
	Pocket();
}

// Every frame of a picked-up call: the phone into his hand once it's out of
// his pocket, the talking loop back when something else blended it away (an
// idle fidget, getting in a car), his weapon handed over when he gets in or out.
void HoldPhone()
{
	if (gHand == Hand::kEmpty)
		return;
	uint8_t *ped = gPed;
	if (ped != api.FindPlayerPed() || Health(ped) <= 0.0f) { // wasted: the game empties his hands
		gHand = Hand::kEmpty;
		gHeldSlot = -1;
		gPed = nullptr;
		return;
	}
	uint8_t *clump = EntityClump(ped);
	if (gHand == Hand::kLowering) {
		if (!api.GetAssociation(clump, kAnimPhoneOut)) // cut short
			Pocket();
		return;
	}
	const bool inVehicle = PedInVehicle(ped);
	if (inVehicle != gWasInVehicle)
		gSettleUntil = Clock::now() + kSettle;
	if (inVehicle && !gWasInVehicle && gHeldSlot > 0 && StoredWeapon(ped) == kWeaponFist) {
		// Getting in with an SMG stored his empty hand to give back on the way out.
		StoredWeapon(ped) = WeaponType(PedWeapon(ped, gHeldSlot));
		gHeldSlot = -1;
	}
	gWasInVehicle = inVehicle;
	if (!inVehicle && Clock::now() < gSettleUntil)
		FreeHand(ped); // getting out gave him a weapon back (CPed::ReplaceWeaponWhenExitingVehicle)
	uint8_t *pad = api.GetPad(0);
	const bool busy = inVehicle ? driveby::Aiming()
	                            : api.PadGetTarget(pad) || api.PadGetWeapon(pad) || PedCurrentWeaponSlot(ped) != 0;
	if (busy) {
		DropPhone();
		return;
	}
	if (!api.IsPedInControl(ped) && PedState(ped) != kPedDriving)
		return; // getting in or out: carry on after
	uint8_t *in = api.GetAssociation(clump, kAnimPhoneIn);
	if (!in && !api.GetAssociation(clump, kAnimPhoneTalk)) {
		api.BlendAnimation(clump, 0, kAnimPhoneTalk, 4.0f);
		gHand = Hand::kTalking;
	}
	if (PedHandModel(ped) != kPhoneModel && (!in || AnimTime(in) >= kInHandAt) && PhoneModelLoaded()) {
		api.RemoveWeaponModel(ped, -1);
		api.AddWeaponModel(ped, kPhoneModel);
	}
}

void UpdateTestCall(Clock::time_point now)
{
	uint8_t *ped = api.FindPlayerPed();
	if (gTestCall == TestCall::kIdle && ped && now >= gTestAt) {
		gTestCall = TestCall::kRinging;
		gCallStart = now;
		gCaller = "TEST CALL";
		api.RequestModel(kPhoneModel, 1); // as the script does before picking up
	}
	if (gTestCall == TestCall::kRinging && now >= gTestAt) {
		gRingAt = now;
		gTestAt = now + kTestRing;
	}
	if (gTestCall == TestCall::kOn && (now >= gTestAt || !ped)) {
		LowerPhone();
		gTestCall = TestCall::kIdle;
		gTestAt = now + kTestGap;
	}
	gRinging = gTestCall == TestCall::kRinging;
	gConnected = gTestCall == TestCall::kOn;
	gCallOn = gRinging || gConnected;
}

void Update()
{
	if (!api.scriptSpace || !*api.activeScripts)
		return;
	PatchScript();
	if (!gScriptOk)
		return;
	const Clock::time_point now = Clock::now();
	if (gTest) {
		UpdateTestCall(now);
		HoldPhone();
		if (uint8_t *ped = api.FindPlayerPed())
			GiveBackWeapon(ped);
		return;
	}
	const int state = ScriptGlobal(kVarCallState);
	const char *caller = CallerOnTheLine();
	const bool ringing = caller && state == 0, connected = state == 1;
	if ((ringing || connected) && !gCallOn) {
		gCallStart = now;
		gRings = 0;
		gCaller = caller ? caller : kUnknown;
		gOurCall = false;
	}
	if (caller)
		gCaller = caller;
	gCallOn = ringing || connected;
	gRinging = ringing;
	if (connected && !gConnected)
		gConnectedAt = now;
	gConnected = connected;
	const int rings = ScriptGlobal(kVarRings);
	if (ringing && rings > gRings)
		gRingAt = now;
	gRings = rings;

	// Dropped, or wasted (no SET_CHAR_ANSWERING_MOBILE to end it). gOurCall stays
	// until the next call: a late one from the script's clean-up is still ours.
	if (gOurCall && !connected)
		LowerPhone();
	HoldPhone();
	if (uint8_t *ped = api.FindPlayerPed())
		GiveBackWeapon(ped);
}

void Hook_ScriptsProcess()
{
	Update();
	orig_ScriptsProcess();
}

void Hook_SetAnswerMobile(uint8_t *ped)
{
	if (!gScriptOk || ped != api.FindPlayerPed() || !gConnected || !CallerOnTheLine()) {
		gOurCall = false; // a mission's own call: theirs to end too
		orig_SetAnswerMobile(ped);
		return;
	}
	gOurCall = true;
	RaisePhone(ped);
}

void Hook_ClearAnswerMobile(uint8_t *ped)
{
	if (!gOurCall || ped != gPed) {
		orig_ClearAnswerMobile(ped);
		return;
	}
	gOurCall = false;
	LowerPhone();
}

// --- on screen ---------------------------------------------------------------------

constexpr float kUnit = 0.095f;                                          // the phone's width, in screen heights
constexpr float kSlideIn = 0.35f, kSlideOut = 0.3f;                      // seconds
constexpr float kShakeTime = 0.6f, kShakeHz = 16.0f, kShakeAngle = 0.05f; // per ring

float gShow; // 0 hidden .. 1 up
Clock::time_point gLastDraw;
Im2DCanvas gCanvas;

// The D-pad with its up arm lit, then "ANSWER", on a dark pill.
void AnswerPrompt(float right, float cy, float h)
{
	const text::Style style{1, h * 0.3f / 480, h * 0.42f / 448, {255, 255, 255, 235}};
	float top, bottom;
	text::LineExtent(style, 0.0f, top, bottom);
	const float lineH = bottom - top, pillH = lineH * 1.9f, icon = pillH * 0.56f, pad = pillH * 0.38f;
	const float textW = text::Width("ANSWER", style), w = pad + icon + pad * 0.7f + textW + pad * 1.2f;
	const float x = right - w, y = cy - pillH / 2;
	gCanvas.Pill(x, y, w, pillH, {0, 0, 0, 175});
	const float cx = x + pad + icon / 2, arm = icon * 0.36f, half = icon / 2;
	const CRGBA dim{255, 255, 255, 70}, lit{255, 255, 255, 255};
	gCanvas.Quad({cx - arm / 2, cy - half}, {cx + arm / 2, cy - half}, {cx + arm / 2, cy + half}, {cx - arm / 2, cy + half},
	             dim);
	gCanvas.Quad({cx - half, cy - arm / 2}, {cx + half, cy - arm / 2}, {cx + half, cy + arm / 2}, {cx - half, cy + arm / 2},
	             dim);
	gCanvas.Quad({cx - arm / 2, cy - half}, {cx + arm / 2, cy - half}, {cx + arm / 2, cy - arm / 2},
	             {cx - arm / 2, cy - arm / 2}, lit);
	gCanvas.Flush();
	text::Draw(x + pad + icon + pad * 0.7f, cy - (top + bottom) / 2, "ANSWER", style);
}

} // namespace

bool Ringing() { return gInstalled && gScriptOk && gRinging; }

void Answer()
{
	if (!Ringing())
		return;
	if (gTest) {
		const Clock::time_point now = Clock::now();
		gTestCall = TestCall::kOn;
		gConnectedAt = now;
		gTestAt = now + kTestLength;
		if (uint8_t *ped = api.FindPlayerPed())
			RaisePhone(ped);
		return;
	}
	gAnswerUntil = Clock::now() + kAnswerHold;
}

void HoldAnswer(uint8_t *pad)
{
	if (!Ringing() || Clock::now() >= gAnswerUntil)
		return;
	// Held rather than just pressed: the script only asks IS_BUTTON_PRESSED, and
	// L1 going down would also retune the radio or pick things up.
	PadNew(pad, kPadLB) = PadOld(pad, kPadLB) = 255;
}

void Draw()
{
	const Clock::time_point now = Clock::now();
	const float dt = std::min(std::chrono::duration<float>(now - gLastDraw).count(), 0.1f);
	gLastDraw = now;
	const bool up = gInstalled && gScriptOk && gCallOn;
	gShow = up ? std::min(1.0f, gShow + dt / kSlideIn) : std::max(0.0f, gShow - dt / kSlideOut);
	if (gShow <= 0.0f)
		return;

	// Up from below the bottom-right corner, rocking for a moment at each ring.
	const float W = float(ScreenWidth()), H = float(ScreenHeight()), m = gConfig.margin * H;
	const float ease = 1.0f - (1.0f - gShow) * (1.0f - gShow) * (1.0f - gShow);
	BrickPhone phone;
	phone.unit = kUnit * H;
	phone.x = W - m - phone.unit;
	phone.y = H - m - kBrickPhoneHeight * phone.unit +
	          (1.0f - ease) * ((kBrickPhoneHeight + kBrickPhoneAntenna + 0.1f) * phone.unit + m);
	const float sinceRing = std::chrono::duration<float>(now - gRingAt).count();
	if (gRinging && sinceRing < kShakeTime)
		phone.tilt = kShakeAngle * sinf(2.0f * float(M_PI) * kShakeHz * sinceRing) * (1.0f - sinceRing / kShakeTime);

	// The caller, over "CALL" flashing while it rings, then the call's length.
	const float t = std::chrono::duration<float>(now - gCallStart).count();
	char status[8];
	if (gConnected) {
		const int secs = int(std::chrono::duration<float>(now - gConnectedAt).count());
		snprintf(status, sizeof status, "%d:%02d", std::min(secs / 60, 9), secs % 60);
	} else {
		snprintf(status, sizeof status, "%s", fmodf(t, 0.8f) < 0.5f ? "CALL" : "");
	}
	phone.line1 = gCaller;
	phone.line2 = status;
	phone.scroll = t;
	phone.sendGlow = gRinging ? 0.5f + 0.5f * sinf(2.0f * float(M_PI) * 2.0f * t) : 0.0f;

	SetHudRenderStates();
	gCanvas.SetTexture(nullptr);
	DrawBrickPhone(gCanvas, phone);
	gCanvas.Flush();
	if (gRinging)
		AnswerPrompt(phone.x - 0.2f * phone.unit, BrickPhoneLcdCentre(phone), H);
	text::Flush();
}

bool Install()
{
	if (!gConfig.phone)
		return true;
	if (!api.scriptSpace || !api.activeScripts || !api.BlendAnimation || !api.GetAssociation ||
	    !api.SetFinishCallback || !api.AddWeaponModel || !api.RemoveWeaponModel)
		return false;
	// The script is only patched from CTheScripts::Process, so that one goes in
	// last: calls never ring in a vehicle without the phone that works there.
	gInstalled = hooks::Install("_ZN4CPed15SetAnswerMobileEv", Hook_SetAnswerMobile, orig_SetAnswerMobile) &&
	             hooks::Install("_ZN4CPed17ClearAnswerMobileEv", Hook_ClearAnswerMobile, orig_ClearAnswerMobile) &&
	             hooks::Install("_ZN11CTheScripts7ProcessEv", Hook_ScriptsProcess, orig_ScriptsProcess);
	if (!gInstalled)
		Log("phone: hooks missing (re-run install.sh); calls left as they are");
	gLastDraw = Clock::now();
	gTest = gInstalled && getenv("VCHUD_TEST_PHONE") != nullptr;
	gTestAt = gLastDraw + kTestGap;
	return true;
}

} // namespace phone
