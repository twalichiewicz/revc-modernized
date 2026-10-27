#include "sniper.h"
#include "camera.h"
#include "game.h"
#include "hooks.h"
#include "log.h"

#include <cmath>
#include <cstdlib>

using namespace game;

namespace sniper {
namespace {

constexpr int32_t kSniperRifle = 28, kLaserScope = 29;
constexpr uint16_t kModeSniper = 7;                       // CCam::Mode
constexpr size_t kCamFront = 0x168, kCamFov = 0xb4;      // CCam::Front, FOV
constexpr size_t kCamAlpha = 0xac, kCamBeta = 0xbc;      // CCam::Alpha, Beta (its pitch and heading: Process_Sniper's reset)
constexpr size_t kCamUp = 0x18c;                         // CCam::Up
constexpr float kDeg = 3.14159265f / 180.0f;

void (*orig_ProcessSniper)(uint8_t *, const CVector &, float, float, float);
scope::Scope gScope;
scope::Output gNow;
bool gLt, gRbPressed, gL3, gShot, gInstalled;
bool gL3Kept;                  // L3 held since the rifle was up: not the game's
bool gGave;                    // the game had its target button last frame
constexpr int32_t kStateSniper = 0xc; // CPed::m_nPedState: PED_SNIPER_MODE
bool gWasScoped;               // the scope up last frame
int gLeftFor;                  // frames first person may still take the scope's view (TakeLeftView)
constexpr int kLeftFrames = 15;
CVector gScopeFront;           // its last view
CVector gFrontBefore;
uint8_t *gShotCam;

bool Rifle(uint8_t *ped)
{
	const int32_t type = WeaponType(PedWeapon(ped, PedCurrentWeaponSlot(ped)));
	return type == kSniperRifle || type == kLaserScope;
}

bool InView() { return *reinterpret_cast<const uint16_t *>(ActiveCam() + 0xc) == kModeSniper; }

CVector Normalised(const CVector &v)
{
	const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
	return l > 1e-6f ? CVector{v.x / l, v.y / l, v.z / l} : v;
}

// After the game's sniper camera: first person's eye, swayed, at the scope's zoom.
void Hook_ProcessSniper(uint8_t *cam, const CVector &target, float a, float b, float c)
{
	orig_ProcessSniper(cam, target, a, b, c);
	uint8_t *ped = api.FindPlayerPed();
	if (!Active() || !ped || cam != ActiveCam())
		return;
	*reinterpret_cast<CVector *>(cam + 0x174) = camera::ScopeEye(ped);
	CVector &front = *reinterpret_cast<CVector *>(cam + kCamFront);
	if (!gWasScoped) { // coming up: where first person looked (the camera's reset levels it)
		const CVector v = Normalised(camera::ViewForward());
		const scope::Angles a = scope::SniperAngles({v.x, v.y, v.z});
		*reinterpret_cast<float *>(cam + kCamAlpha) = a.alpha;
		*reinterpret_cast<float *>(cam + kCamBeta) = a.beta;
		front = {std::cos(a.alpha) * std::cos(a.beta), std::cos(a.alpha) * std::sin(a.beta), std::sin(a.alpha)};
		gWasScoped = true;
	}
	const CVector up{0.0f, 0.0f, 1.0f};
	const CVector right = Normalised({front.y * up.z - front.z * up.y, front.z * up.x - front.x * up.z, front.x * up.y - front.y * up.x});
	const float yaw = gNow.swayYaw * kDeg, pitch = gNow.swayPitch * kDeg; // left, up
	front = Normalised({front.x - right.x * yaw, front.y - right.y * yaw, front.z + pitch});
	gScopeFront = front;
	// Its up square to it again (as aimcam.cpp keeps it).
	*reinterpret_cast<CVector *>(cam + kCamUp) = Normalised({-front.x * front.z, -front.y * front.z, 1.0f - front.z * front.z});
	*reinterpret_cast<float *>(cam + kCamFov) = gNow.fov;
}

} // namespace

bool Install()
{
	gInstalled = hooks::Install("_ZN4CCam14Process_SniperERK7CVectorfff", Hook_ProcessSniper, orig_ProcessSniper);
	if (!gInstalled)
		Log("sniper: no CCam::Process_Sniper hook (re-run install.sh); the sniper rifles keep the game's scope");
	return gInstalled;
}

bool Wanted()
{
	uint8_t *ped = api.FindPlayerPed();
	return gInstalled && gLt && ped && camera::FirstPersonChosen() && Rifle(ped);
}

bool Active() { return Wanted() && InView(); }

bool KeepsL3() { return gL3Kept; }

const scope::Output &Now() { return gNow; }

bool GameAims(bool lt, bool rbPressed, bool l3)
{
	gLt = lt;
	gRbPressed = gRbPressed || rbPressed; // (till the camera's frame takes it)
	gL3 = l3;
	gL3Kept = l3 && (gL3Kept || Wanted());
	if (!Wanted()) {
		gGave = false;
		return lt;
	}
	// Only with the gun at the eye, and not while the bolt cycles. The game goes
	// into its scope on the button's press alone: until he's in its sniper state,
	// it's pressed afresh (a press it missed, jumping or on fire, would leave him
	// out with LT held).
	uint8_t *ped = api.FindPlayerPed();
	bool give = camera::AimBlend() >= 1.0f && !gNow.bolting;
	if (give && gGave && ped && PedState(ped) != kStateSniper && !InView())
		give = false;
	gGave = give;
	return give;
}

bool TakeLeftView(CVector &front)
{
	if (gLeftFor <= 0)
		return false;
	gLeftFor = 0;
	front = gScopeFront;
	return true;
}

void Frame(float dt)
{
	// The scope gone: first person carries its view on (TakeLeftView), if it's back within a few frames.
	if (gWasScoped && !InView())
		gLeftFor = kLeftFrames;
	else if (gLeftFor > 0)
		gLeftFor = InView() ? 0 : gLeftFor - 1;
	if (!InView())
		gWasScoped = false;
	uint8_t *ped = api.FindPlayerPed();
	scope::Input in{};
	in.wanted = Wanted();
	in.inView = in.wanted && InView();
	in.zoomPressed = gRbPressed && in.inView;
	in.breathHeld = gL3 && in.inView;
	in.shot = gShot;
	in.boltAction = ped && WeaponType(PedWeapon(ped, PedCurrentWeaponSlot(ped))) == kSniperRifle;
	if (ped) {
		const CVector v = EntitySpeed(ped);
		in.moving = std::sqrt(v.x * v.x + v.y * v.y) * 50.0f;
	}
	in.dt = dt;
	gNow = gScope.Update(in);
	gRbPressed = gShot = false;
}

void BeforeShot()
{
	gShotCam = ActiveCam();
	CVector &front = *reinterpret_cast<CVector *>(gShotCam + kCamFront);
	gFrontBefore = front;
	if (gNow.spread <= 0.0f)
		return;
	// Somewhere in the cone (uniform over its disc).
	const float r = gNow.spread * std::sqrt(float(rand() % 1000) / 999.0f), a = float(rand() % 1000) / 999.0f * 6.2831853f;
	const CVector right = Normalised({front.y, -front.x, 0.0f});
	front = Normalised({front.x + right.x * r * std::cos(a), front.y + right.y * r * std::cos(a), front.z + r * std::sin(a)});
}

void AfterShot(bool fired)
{
	if (gShotCam)
		*reinterpret_cast<CVector *>(gShotCam + kCamFront) = gFrontBefore;
	gShotCam = nullptr;
	gShot = gShot || fired;
}

} // namespace sniper
