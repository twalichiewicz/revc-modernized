#include "seatgun.h"
#include "carlights.h"
#include "config.h"
#include "consolespot.h"
#include "driveby.h"
#include "game.h"
#include "log.h"
#include "shaders.h"
#include "viewmodel.h"

#include <algorithm>
#include <cmath>
#include <dlfcn.h>
#include <strings.h>
#include <unordered_map>
#include <vector>

using namespace game;

namespace seatgun {
namespace {

// CVehicle, CPed, CBaseModelInfo (macVC 1.0.5217).
constexpr size_t kRwObject = 0x58, kModelIndex = 0x6a, kDriver = 0x218, kVehicleType = 0x348;
constexpr size_t kFrontSeat = 0x78;      // CVehicleModelInfo::m_positions[CAR_POS_FRONTSEAT] (the driver's mirrors it)
constexpr size_t kModelName = 0x8;       // CBaseModelInfo::m_name
constexpr size_t kWeaponModel = 0x290;   // CPed::m_pWeaponModel: the atomic in his hand
constexpr size_t kCreateInstance = 0x20; // CBaseModelInfo's vtable: CreateInstance() (CPed::AddWeaponModel calls it so)
constexpr size_t kWeaponModelId = 0x54;  // CWeaponInfo::m_nModelId
constexpr int32_t kCar = 0, kBoat = 1, kBike = 5; // eVehicleType
constexpr int32_t kStateDriving = 0x32;
constexpr int kBoneHandR = 24;    // ConvertPedNode2BoneTag
constexpr uint32_t kOrthonormal = 3; // rw::Matrix's flags

// How it goes (s): his hand down to it, then it up into the aim; it down onto
// the console, then his hand back to the wheel.
constexpr float kReach = 0.13f, kLift = 0.17f, kLower = 0.15f, kBack = 0.17f;
// It lies pointing this far in from straight ahead, towards the passenger's side (rad).
constexpr float kInwards = 0.35f;
constexpr float kLength = 0.4f; // m: a gun's length, for the surface being flat under it

struct V3 {
	float x, y, z;
};
struct Triangle {
	uint16_t v[3], material;
};

struct RW {
	void *(*ForAllAtomics)(void *clump, void *(*cb)(void *atomic, void *data), void *data);
	void *(*AtomicFrame)(const void *atomic);
	void *(*AtomicGeometry)(const void *atomic);
	void (*AtomicRender)(void *atomic);
	void (*AtomicDestroy)(void *atomic);
	float *(*FrameLTM)(void *frame);
	float *(*FrameMatrix)(void *frame);
	void *(*FrameUpdate)(void *frame);
	void (*FrameDestroy)(void *frame);
	const char *(*FrameName)(void *frame);
	int (*NumTriangles)(const void *geometry);
	int (*NumVertices)(const void *geometry);
	const Triangle *(*Triangles)(const void *geometry);
	void *(*MorphTarget)(const void *geometry, int index);
	const V3 *(*Vertices)(const void *morphTarget);
	void *(*Hierarchy)(void *clump);
	int (*BoneIndex)(void *hierarchy, int tag);
	float *(*BoneMatrices)(void *hierarchy);
	void (*AddRef)(void *modelInfo);
	void (*RemoveRef)(void *modelInfo);
} rw;
const float *gLod0DistSq; // CVisibilityPlugins::ms_vehicleLod0Dist
bool gReady;

V3 Add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 Sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 Mul(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Lerp(V3 a, V3 b, float t) { return Add(a, Mul(Sub(b, a), t)); }
float Smooth(float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}
// rw::Matrix: right, up, at and position, each with a pad.
V3 Apply(const float *m, V3 p)
{
	return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
	        m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
}
// The car's CMatrix: right, forward, up and position (each 16 bytes from +8).
const V3 &Axis(const uint8_t *car, size_t row) { return *reinterpret_cast<const V3 *>(car + 8 + row); }
V3 InCar(const uint8_t *car, V3 world)
{
	const V3 d = Sub(world, Axis(car, 0x30));
	return {Dot(d, Axis(car, 0)), Dot(d, Axis(car, 0x10)), Dot(d, Axis(car, 0x20))};
}
V3 FromCar(const uint8_t *car, V3 p)
{
	return Add(Add(Add(Axis(car, 0x30), Mul(Axis(car, 0), p.x)), Mul(Axis(car, 0x10), p.y)), Mul(Axis(car, 0x20), p.z));
}
V3 DirFromCar(const uint8_t *car, V3 d)
{
	return Add(Add(Mul(Axis(car, 0), d.x), Mul(Axis(car, 0x10), d.y)), Mul(Axis(car, 0x20), d.z));
}

// --- Rigid frames (rw::Matrix's layout) and blending them ----------------------------------

struct Quat {
	float w, x, y, z;
};
Quat ToQuat(const float *m) // columns right (0..2), up (4..6), at (8..10)
{
	const float r00 = m[0], r11 = m[5], r22 = m[10], tr = r00 + r11 + r22;
	Quat q;
	if (tr > 0.0f) {
		const float s = std::sqrt(tr + 1.0f) * 2.0f;
		q = {0.25f * s, (m[6] - m[9]) / s, (m[8] - m[2]) / s, (m[1] - m[4]) / s};
	} else if (r00 > r11 && r00 > r22) {
		const float s = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
		q = {(m[6] - m[9]) / s, 0.25f * s, (m[4] + m[1]) / s, (m[8] + m[2]) / s};
	} else if (r11 > r22) {
		const float s = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
		q = {(m[8] - m[2]) / s, (m[4] + m[1]) / s, 0.25f * s, (m[9] + m[6]) / s};
	} else {
		const float s = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
		q = {(m[1] - m[4]) / s, (m[8] + m[2]) / s, (m[9] + m[6]) / s, 0.25f * s};
	}
	return q;
}
void FromQuat(Quat q, float *m)
{
	const float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
	q = {q.w / n, q.x / n, q.y / n, q.z / n};
	m[0] = 1 - 2 * (q.y * q.y + q.z * q.z), m[1] = 2 * (q.x * q.y + q.w * q.z), m[2] = 2 * (q.x * q.z - q.w * q.y);
	m[4] = 2 * (q.x * q.y - q.w * q.z), m[5] = 1 - 2 * (q.x * q.x + q.z * q.z), m[6] = 2 * (q.y * q.z + q.w * q.x);
	m[8] = 2 * (q.x * q.z + q.w * q.y), m[9] = 2 * (q.y * q.z - q.w * q.x), m[10] = 1 - 2 * (q.x * q.x + q.y * q.y);
}
// From `a` to `b` by t: turned the short way, moved straight.
void Blend(const float *a, const float *b, float t, float *out)
{
	Quat p = ToQuat(a), q = ToQuat(b);
	float c = p.w * q.w + p.x * q.x + p.y * q.y + p.z * q.z;
	if (c < 0.0f) {
		q = {-q.w, -q.x, -q.y, -q.z};
		c = -c;
	}
	float ka = 1.0f - t, kb = t;
	if (c < 0.9995f) {
		const float angle = std::acos(c), s = std::sin(angle);
		ka = std::sin((1.0f - t) * angle) / s;
		kb = std::sin(t * angle) / s;
	}
	FromQuat({p.w * ka + q.w * kb, p.x * ka + q.x * kb, p.y * ka + q.y * kb, p.z * ka + q.z * kb}, out);
	for (int i = 0; i < 3; i++)
		out[12 + i] = a[12 + i] + (b[12 + i] - a[12 + i]) * t;
	out[3] = out[7] = out[11] = out[15] = 0.0f;
}

// --- Where it lies in each car model -------------------------------------------------------

struct Place {
	int model = -1, triangles = 0; // what it was looked for in (a freed geometry's address comes back)
	consolespot::Spot spot{};
};
std::unordered_map<const void *, Place> gPlaces; // by the body's geometry

struct Body {
	void *atomic = nullptr;
};
void *FindBody(void *atomic, void *data)
{
	void *frame = rw.AtomicFrame(atomic);
	const char *name = frame ? rw.FrameName(frame) : nullptr;
	if (name && !strcasecmp(name, "chassis_hi"))
		static_cast<Body *>(data)->atomic = atomic;
	return atomic;
}

const consolespot::Spot *SpotIn(const uint8_t *car)
{
	void *clump = *reinterpret_cast<void *const *>(car + kRwObject);
	Body body;
	if (clump)
		rw.ForAllAtomics(clump, FindBody, &body);
	const void *geometry = body.atomic ? rw.AtomicGeometry(body.atomic) : nullptr;
	const int model = *reinterpret_cast<const int16_t *>(car + kModelIndex);
	const uint8_t *mi = model >= 0 ? api.modelInfoPtrs[model] : nullptr;
	if (!geometry || !mi)
		return nullptr;
	Place &p = gPlaces[geometry];
	const int count = rw.NumTriangles(geometry);
	if (p.model == model && p.triangles == count)
		return &p.spot;
	p.model = model;
	p.triangles = count;
	// The body's triangles in the car's frame (the seat's).
	void *morph = rw.MorphTarget(geometry, 0);
	const V3 *verts = morph ? rw.Vertices(morph) : nullptr;
	const Triangle *tris = rw.Triangles(geometry);
	const float *ltm = rw.FrameLTM(rw.AtomicFrame(body.atomic));
	std::vector<consolespot::Triangle> inCar;
	if (verts && tris) {
		inCar.reserve(size_t(count));
		auto at = [&](uint16_t i) {
			const V3 p = InCar(car, Apply(ltm, verts[i]));
			return Vec3{p.x, p.y, p.z};
		};
		for (int i = 0; i < count; i++)
			inCar.push_back({at(tris[i].v[0]), at(tris[i].v[1]), at(tris[i].v[2])});
	}
	V3 seat = *reinterpret_cast<const V3 *>(mi + kFrontSeat);
	seat.x = -std::fabs(seat.x); // the driver's side
	p.spot = consolespot::Find(inCar, {seat.x, seat.y, seat.z}, {std::sin(kInwards), std::cos(kInwards), 0.0f}, kLength);
	static const char *const kKinds[] = {"on the console", "on his seat (no console)", "on his seat (a guess)"};
	Log("seatgun: %s's gun lies %s, %.2f m over his hips", reinterpret_cast<const char *>(mi + kModelName),
	    kKinds[int(p.spot.kind)], p.spot.at.z - seat.z);
	return &p.spot;
}

// --- The gun out of his hand: our own of the game's model --------------------------------------

struct Gun {
	int model = -1;
	void *atomic = nullptr;
	Vec3 lo{}, hi{}; // its model's box
};
Gun gGun;

void Drop()
{
	if (gGun.atomic) {
		void *frame = rw.AtomicFrame(gGun.atomic);
		rw.AtomicDestroy(gGun.atomic);
		if (frame)
			rw.FrameDestroy(frame);
		if (uint8_t *mi = api.modelInfoPtrs[gGun.model])
			rw.RemoveRef(mi);
	}
	gGun = Gun{};
}

bool Make(int model)
{
	if (gGun.atomic && gGun.model == model)
		return true;
	Drop();
	uint8_t *mi = model > 0 ? api.modelInfoPtrs[model] : nullptr;
	if (!mi)
		return false;
	using Create = void *(*)(void *);
	void *atomic = (*reinterpret_cast<Create *>(*reinterpret_cast<uint8_t **>(mi) + kCreateInstance))(mi);
	const void *geometry = atomic ? rw.AtomicGeometry(atomic) : nullptr;
	void *morph = geometry ? rw.MorphTarget(geometry, 0) : nullptr;
	const V3 *verts = morph ? rw.Vertices(morph) : nullptr;
	if (!verts || !rw.AtomicFrame(atomic)) {
		if (atomic) {
			void *frame = rw.AtomicFrame(atomic);
			rw.AtomicDestroy(atomic);
			if (frame)
				rw.FrameDestroy(frame);
		}
		return false;
	}
	rw.AddRef(mi);
	gGun.model = model;
	gGun.atomic = atomic;
	gGun.lo = {1e9f, 1e9f, 1e9f};
	gGun.hi = {-1e9f, -1e9f, -1e9f};
	for (int i = 0, n = rw.NumVertices(geometry); i < n; i++) {
		const V3 &v = verts[i];
		gGun.lo = {std::min(gGun.lo.x, v.x), std::min(gGun.lo.y, v.y), std::min(gGun.lo.z, v.z)};
		gGun.hi = {std::max(gGun.hi.x, v.x), std::max(gGun.hi.y, v.y), std::max(gGun.hi.z, v.z)};
	}
	return true;
}

// --- Him -------------------------------------------------------------------------------------

enum class Phase { kOff, kDown, kTaking, kHeld, kPutting };
Phase gPhase = Phase::kOff;
float gT;            // s into taking or putting it
const uint8_t *gCar; // the vehicle it's in
bool gStowed;        // a bike's or a boat's: out of his hand, nowhere to be seen
V3 gLastHand{};      // his hand last frame (the car's frame), and where it came from putting it down
V3 gHandFrom{};
bool gHaveLastHand, gHaveHandFrom;

void *InHandAtomic(uint8_t *ped) { return *reinterpret_cast<void **>(ped + kWeaponModel); }
int CurrentModel(uint8_t *ped)
{
	const int32_t type = WeaponType(PedWeapon(ped, PedCurrentWeaponSlot(ped)));
	const uint8_t *info = type > 0 ? api.GetWeaponInfo(type) : nullptr;
	return info ? *reinterpret_cast<const int32_t *>(info + kWeaponModelId) : -1;
}

const float *HandMatrix(uint8_t *ped)
{
	void *clump = *reinterpret_cast<void *const *>(ped + kRwObject);
	void *hierarchy = clump ? rw.Hierarchy(clump) : nullptr;
	const int i = hierarchy ? rw.BoneIndex(hierarchy, kBoneHandR) : -1;
	return i >= 0 ? rw.BoneMatrices(hierarchy) + i * 16 : nullptr;
}

// It out of his hand (the game's put it there).
void TakeFromHand(uint8_t *ped)
{
	if (InHandAtomic(ped)) {
		const int model = PedHandModel(ped);
		api.RemoveWeaponModel(ped, -1);
		if (!gStowed)
			Make(model);
		else
			gGun.model = model;
	}
}

// It back in his hand: the game's own model of what he holds.
void GiveToHand(uint8_t *ped)
{
	const int model = CurrentModel(ped);
	Drop();
	if (!InHandAtomic(ped) && model > 0)
		api.AddWeaponModel(ped, model);
}

void Off(uint8_t *ped)
{
	// Out of it however he went (getting out, dragged out, put out by a script): his gun back in
	// his hand, as the game had it (getting out, the game then swaps it for what he had on foot).
	if (gPhase != Phase::kOff && gPhase != Phase::kHeld && !InHandAtomic(ped) && CurrentModel(ped) == gGun.model)
		GiveToHand(ped);
	Drop();
	gPhase = Phase::kOff;
	gCar = nullptr;
	gHaveLastHand = gHaveHandFrom = false;
}

// The gun's frame on the console (world): the spot in the car, the gun's box resting on it.
bool ConsoleMatrix(const uint8_t *car, float *m)
{
	const consolespot::Spot *spot = SpotIn(car);
	if (!spot || !gGun.atomic)
		return false;
	const consolespot::Pose p = consolespot::Lay(*spot, gGun.lo, gGun.hi);
	const V3 x = DirFromCar(car, {p.x.x, p.x.y, p.x.z}), y = DirFromCar(car, {p.y.x, p.y.y, p.y.z}),
	         z = DirFromCar(car, {p.z.x, p.z.y, p.z.z}), o = FromCar(car, {p.origin.x, p.origin.y, p.origin.z});
	const V3 cols[4] = {x, y, z, o};
	for (int i = 0; i < 4; i++)
		m[i * 4] = cols[i].x, m[i * 4 + 1] = cols[i].y, m[i * 4 + 2] = cols[i].z, m[i * 4 + 3] = 0.0f;
	return true;
}

bool Close(const uint8_t *car)
{
	const CVector cam = shaders::CameraPosition();
	const V3 d = Sub(Axis(car, 0x30), V3{cam.x, cam.y, cam.z});
	return gLod0DistSq && Dot(d, d) < *gLod0DistSq;
}

template <typename T>
bool Bind(T &out, const char *name)
{
	out = reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY, name));
	if (!out)
		Log("seatgun: missing %s", name);
	return out != nullptr;
}

} // namespace

void BeforePose(uint8_t *ped)
{
	if (!gReady)
		return;
	uint8_t *vehicle = PedInVehicle(ped) ? PedVehicle(ped) : nullptr;
	const int32_t type = vehicle ? *reinterpret_cast<const int32_t *>(vehicle + kVehicleType) : -1;
	const bool driving = vehicle && *reinterpret_cast<uint8_t *const *>(vehicle + kDriver) == ped &&
	                     PedState(ped) == kStateDriving && (type == kCar || type == kBike || type == kBoat);
	if (!driving) {
		if (gPhase != Phase::kOff)
			Off(ped);
		gCar = nullptr;
		return;
	}
	if (vehicle != gCar) {
		Off(ped);
		gCar = vehicle;
		gStowed = type != kCar;
	}
	const bool aiming = driveby::Aiming(), inHand = InHandAtomic(ped) != nullptr;
	const float dt = *api.timeStep / 50.0f;
	switch (gPhase) {
	case Phase::kOff: // just in: what he got in with goes down (or away)
		if (inHand && !aiming) {
			TakeFromHand(ped);
			gPhase = gStowed ? Phase::kDown : Phase::kPutting;
			gT = 0.0f;
			gHaveHandFrom = false;
		} else if (inHand) {
			gPhase = Phase::kHeld;
		}
		break;
	case Phase::kDown:
		if (aiming && gStowed) {
			GiveToHand(ped);
			gPhase = Phase::kHeld;
		} else if (aiming) {
			TakeFromHand(ped); // (the drive-by gave it him: it's still on the console)
			gPhase = Phase::kTaking;
			gT = 0.0f;
		} else {
			TakeFromHand(ped); // the game gave it back
		}
		break;
	case Phase::kTaking:
		TakeFromHand(ped);
		if ((gT += dt) >= kReach + kLift) {
			GiveToHand(ped);
			gPhase = Phase::kHeld;
		}
		break;
	case Phase::kHeld:
		if (!inHand) {
			gPhase = Phase::kOff; // the game took it (out of ammo)
		} else if (!aiming) {
			TakeFromHand(ped);
			gPhase = gStowed ? Phase::kDown : Phase::kPutting;
			gT = 0.0f;
			gHandFrom = gLastHand;
			gHaveHandFrom = gHaveLastHand;
		}
		break;
	case Phase::kPutting:
		TakeFromHand(ped);
		if ((gT += dt) >= kLower + kBack)
			gPhase = Phase::kDown;
		break;
	}
}

void Pose(uint8_t *ped)
{
	if (!gReady || !gCar || gStowed || !PedInVehicle(ped) || PedVehicle(ped) != gCar)
		return;
	const float *hand = HandMatrix(ped);
	if (!hand)
		return;
	const V3 now{hand[12], hand[13], hand[14]};
	float console[16];
	if ((gPhase == Phase::kTaking || gPhase == Phase::kPutting) && ConsoleMatrix(gCar, console)) {
		const V3 grip{console[12], console[13], console[14]}; // the gun's own origin is where his hand holds it
		V3 wrist = now;
		if (gPhase == Phase::kTaking)
			wrist = gT < kReach ? Lerp(now, grip, Smooth(gT / kReach)) : Lerp(grip, now, Smooth((gT - kReach) / kLift));
		else if (gT < kLower)
			wrist = Lerp(gHaveHandFrom ? FromCar(gCar, gHandFrom) : now, grip, Smooth(gT / kLower));
		else
			wrist = Lerp(grip, now, Smooth((gT - kLower) / kBack));
		// The elbow down and out to his right, a little back.
		const V3 pole = Sub(Sub(Mul(Axis(gCar, 0), 0.7f), Axis(gCar, 0x20)), Mul(Axis(gCar, 0x10), 0.2f));
		viewmodel::ReachArm(ped, false, {wrist.x, wrist.y, wrist.z}, {pole.x, pole.y, pole.z});
	}
	gLastHand = InCar(gCar, V3{hand[12], hand[13], hand[14]});
	gHaveLastHand = true;
}

void DrawCar(uint8_t *car)
{
	if (!gReady || car != gCar || gStowed || !gGun.atomic || !Close(car))
		return;
	float console[16], m[16];
	if (!ConsoleMatrix(car, console))
		return;
	uint8_t *ped = api.FindPlayerPed();
	const float *hand = ped ? HandMatrix(ped) : nullptr;
	switch (gPhase) {
	case Phase::kDown:
		std::copy(console, console + 16, m);
		break;
	case Phase::kTaking: // on the console till his hand's on it, then up with his hand into its grip
		if (gT < kReach || !hand)
			std::copy(console, console + 16, m);
		else
			Blend(console, hand, Smooth((gT - kReach) / kLift), m);
		break;
	case Phase::kPutting: // down with his hand and out of its grip onto the console
		if (gT >= kLower || !hand)
			std::copy(console, console + 16, m);
		else
			Blend(hand, console, Smooth(gT / kLower), m);
		break;
	default:
		return;
	}
	void *frame = rw.AtomicFrame(gGun.atomic);
	float *matrix = rw.FrameMatrix(frame);
	std::copy(m, m + 16, matrix);
	reinterpret_cast<uint32_t *>(matrix)[3] = kOrthonormal;
	rw.FrameUpdate(frame);
	rw.AtomicRender(gGun.atomic);
}

bool InHand() { return !gReady || gPhase == Phase::kOff || gPhase == Phase::kHeld; }

bool Install()
{
	if (!gConfig.consoleGun || !gConfig.driveByAim)
		return true;
	gReady = Bind(rw.ForAllAtomics, "_Z20RpClumpForAllAtomicsPN2rw5ClumpEPFPNS_6AtomicES3_PvES4_") &&
	         Bind(rw.AtomicFrame, "_Z16RpAtomicGetFramePKN2rw6AtomicE") &&
	         Bind(rw.AtomicGeometry, "_Z19RpAtomicGetGeometryPKN2rw6AtomicE") &&
	         Bind(rw.AtomicRender, "_Z14RpAtomicRenderPN2rw6AtomicE") &&
	         Bind(rw.AtomicDestroy, "_Z15RpAtomicDestroyPN2rw6AtomicE") &&
	         Bind(rw.FrameLTM, "_Z13RwFrameGetLTMPN2rw5FrameE") && Bind(rw.FrameMatrix, "_Z16RwFrameGetMatrixPN2rw5FrameE") &&
	         Bind(rw.FrameUpdate, "_Z20RwFrameUpdateObjectsPN2rw5FrameE") &&
	         Bind(rw.FrameDestroy, "_Z14RwFrameDestroyPN2rw5FrameE") &&
	         Bind(rw.FrameName, "_Z16GetFrameNodeNamePN2rw5FrameE") &&
	         Bind(rw.NumTriangles, "_Z25RpGeometryGetNumTrianglesPKN2rw8GeometryE") &&
	         Bind(rw.NumVertices, "_Z24RpGeometryGetNumVerticesPKN2rw8GeometryE") &&
	         Bind(rw.Triangles, "_Z22RpGeometryGetTrianglesPKN2rw8GeometryE") &&
	         Bind(rw.MorphTarget, "_Z24RpGeometryGetMorphTargetPKN2rw8GeometryEi") &&
	         Bind(rw.Vertices, "_Z24RpMorphTargetGetVerticesPKN2rw11MorphTargetE") &&
	         Bind(rw.Hierarchy, "_Z29GetAnimHierarchyFromSkinClumpPN2rw5ClumpE") &&
	         Bind(rw.BoneIndex, "_Z17RpHAnimIDGetIndexPN2rw14HAnimHierarchyEi") &&
	         Bind(rw.BoneMatrices, "_Z30RpHAnimHierarchyGetMatrixArrayPN2rw14HAnimHierarchyE") &&
	         Bind(rw.AddRef, "_ZN14CBaseModelInfo6AddRefEv") && Bind(rw.RemoveRef, "_ZN14CBaseModelInfo9RemoveRefEv") &&
	         Bind(gLod0DistSq, "_ZN18CVisibilityPlugins18ms_vehicleLod0DistE") && api.AddWeaponModel &&
	         api.RemoveWeaponModel && api.GetWeaponInfo && api.modelInfoPtrs;
	// (Without the drive-by's aim it'd never come back to his hand; without the car's draw it'd be nowhere.)
	if (gReady && (!driveby::Enabled() || !carlights::DrawsCars())) {
		gReady = false;
		Log("seatgun: the drive-by's or the car's hook is missing (re-run install.sh); the gun stays in his hand");
	} else if (!gReady) {
		Log("seatgun: functions missing; the gun stays in his hand as the game has it");
	}
	return true;
}

} // namespace seatgun
