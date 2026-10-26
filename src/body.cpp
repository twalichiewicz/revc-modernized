#include "body.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <dlfcn.h>
#include <string>

using namespace game;

namespace body {
namespace {

// Bone tags (BONE_*, as the animations name them), in the order the hierarchy keeps them.
constexpr int kTags[kBones] = {0, 1, 2, 3, 4, 5, 31, 32, 33, 34, 35, 21, 22, 23, 24, 25, 41, 42, 43, 51, 52, 53};
const char *const kNames[kBones] = {
    "root",       "pelvis",      "spine",     "spine1",      "neck",   "head",       "l_clavicle", "l_upperarm",
    "l_forearm",  "l_hand",      "l_finger",  "r_clavicle",  "r_upperarm", "r_forearm", "r_hand",  "r_finger",
    "l_thigh",    "l_calf",      "l_foot",    "r_thigh",     "r_calf", "r_foot",
};
// What each bone points at.
constexpr Bone kChild[kBones] = {kPelvis, kSpine,  kSpine1, kNeck,  kHead,  kBones, kUpperArmL, kForearmL,
                                 kHandL,  kFingerL, kBones, kUpperArmR, kForearmR, kHandR, kFingerR, kBones,
                                 kCalfL,  kFootL,  kBones,  kCalfR, kFootR, kBones};
// idle_stance's first frame in ped.ifp, as the file has it (x, y, z, w): the
// game inverts each rotation as it loads it (CAnimManager::LoadAnimFile).
constexpr float kStanding[kBones][4] = {
    {-0.020701f, 0.018880f, -0.539805f, 0.841324f}, {0.520138f, 0.520139f, 0.479015f, 0.479016f},
    {-0.097780f, -0.045822f, -0.039680f, 0.993360f}, {0.027070f, 0.011621f, -0.044212f, 0.998588f},
    {0.000000f, 0.000000f, -0.139173f, 0.990268f},   {-0.115337f, -0.006481f, 0.176265f, 0.977541f},
    {0.631680f, -0.084903f, 0.762545f, -0.110895f},  {-0.080750f, -0.550825f, 0.076512f, 0.827174f},
    {0.000000f, 0.000000f, 0.240157f, 0.970734f},    {0.395619f, 0.085835f, -0.086953f, 0.910251f},
    {0.000306f, -0.000255f, -0.639439f, 0.768842f},  {-0.618083f, 0.072273f, 0.773405f, -0.120807f},
    {0.331589f, 0.470654f, 0.192392f, 0.794682f},    {0.000000f, 0.000000f, 0.177440f, 0.984132f},
    {-0.771250f, 0.032420f, -0.059852f, 0.632882f},  {-0.000306f, 0.000255f, -0.639439f, 0.768842f},
    {0.047437f, 0.979124f, 0.189016f, 0.057789f},    {0.000000f, 0.000000f, 0.218032f, 0.975942f},
    {-0.052757f, 0.037752f, -0.164436f, 0.984252f},  {0.033067f, 0.985611f, -0.162011f, -0.035061f},
    {0.000000f, 0.000000f, 0.084656f, 0.996410f},    {-0.075446f, -0.006096f, -0.050523f, 0.995850f},
};
constexpr CVector kStandingRoot{-0.002666f, -0.000728f, -0.022486f};

// A rotation as its matrix's columns (where x, y and z go), m[row][column].
struct Mat3 {
	float m[3][3];
};

Mat3 Transpose(const Mat3 &a)
{
	Mat3 r;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			r.m[i][j] = a.m[j][i];
	return r;
}

Mat3 Mul(const Mat3 &a, const Mat3 &b)
{
	Mat3 r;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
	return r;
}

CVector Mul(const Mat3 &a, const CVector &v)
{
	return {a.m[0][0] * v.x + a.m[0][1] * v.y + a.m[0][2] * v.z, a.m[1][0] * v.x + a.m[1][1] * v.y + a.m[1][2] * v.z,
	        a.m[2][0] * v.x + a.m[2][1] * v.y + a.m[2][2] * v.z};
}

// An RwMatrix: right, up, at and pos rows of 4 floats, each axis a column here.
Mat3 FromRw(const float *rw)
{
	Mat3 r;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			r.m[i][j] = rw[j * 4 + i];
	return r;
}

Mat3 ToMat(const Quat &q)
{
	const float x = q.x, y = q.y, z = q.z, w = q.w;
	return {{{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
	         {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
	         {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}}};
}

Quat Normalise(Quat q)
{
	const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	return l > 1e-8f ? Quat{q.x / l, q.y / l, q.z / l, q.w / l} : Quat{};
}

Quat ToQuat(const Mat3 &a)
{
	const auto &m = a.m;
	const float trace = m[0][0] + m[1][1] + m[2][2];
	Quat q;
	if (trace > 0) {
		const float s = std::sqrt(trace + 1.0f) * 2;
		q = {(m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, s / 4};
	} else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
		const float s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2;
		q = {s / 4, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s};
	} else if (m[1][1] > m[2][2]) {
		const float s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2;
		q = {(m[0][1] + m[1][0]) / s, s / 4, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s};
	} else {
		const float s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2;
		q = {(m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, s / 4, (m[1][0] - m[0][1]) / s};
	}
	return Normalise(q);
}

float Alike(const Quat &a, const Quat &b) { return std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w); }

// AnimBlendFrameData: its keyframe at +0x10; the keyframe's rotation (x, y, z,
// w) at +0x10 and translation at +0x20 (FrameUpdateCallBackSkinned).
uint8_t *Key(uint8_t *frame) { return *reinterpret_cast<uint8_t **>(frame + 0x10); }
Quat &KeyRotation(uint8_t *key) { return *reinterpret_cast<Quat *>(key + 0x10); }
CVector &KeyTranslation(uint8_t *key) { return *reinterpret_cast<CVector *>(key + 0x20); }

struct Skeleton {
	uint8_t *clump = nullptr;
	bool ok = false;
	uint8_t *frame[kBones] = {};
	int parent[kBones] = {};
	Quat rest[kBones];       // standing orientation, ped frame
	CVector restPos[kBones]; // joint, ped frame
	bool inverted = false;   // the keyframes hold the inverse of the local rotation
} gSkel;

uint8_t *(*gFindBone)(uint8_t *clump, uint32_t tag); // RpAnimBlendClumpFindBone
int gFailures;

bool Fail(const char *why)
{
	if (gFailures++ < 3)
		Log("body: %s; Tommy keeps the game's animations", why);
	return false;
}

bool Measure(uint8_t *ped)
{
	uint8_t *clump = EntityClump(ped);
	if (clump == gSkel.clump)
		return gSkel.ok;
	gSkel = Skeleton{};
	gSkel.clump = clump;
	if (!gFindBone)
		gFindBone = reinterpret_cast<uint8_t *(*)(uint8_t *, uint32_t)>(
		    dlsym(RTLD_MAIN_ONLY, "_Z24RpAnimBlendClumpFindBonePN2rw5ClumpEj"));
	uint8_t *hier = clump ? api.GetAnimHierarchyFromSkinClump(clump) : nullptr;
	if (!hier || !gFindBone)
		return Fail("no skeleton to pose");
	int index[kBones];
	for (int b = 0; b < kBones; b++) {
		gSkel.frame[b] = gFindBone(clump, kTags[b]);
		index[b] = api.HAnimIDGetIndex(hier, kTags[b]);
		if (!gSkel.frame[b] || !Key(gSkel.frame[b]) || index[b] < 0)
			return Fail("a bone is missing from his skeleton");
	}

	// Stand him up, read the bones back, and put his animation back.
	Quat saved[kBones], written[kBones];
	for (int b = 0; b < kBones; b++) {
		Quat &q = KeyRotation(Key(gSkel.frame[b]));
		saved[b] = q;
		written[b] = q = Normalise({-kStanding[b][0], -kStanding[b][1], -kStanding[b][2], kStanding[b][3]});
	}
	CVector &rootT = KeyTranslation(Key(gSkel.frame[kRoot]));
	const CVector savedRoot = rootT;
	rootT = kStandingRoot;
	api.UpdateRpHAnim(ped);
	const float *mats = api.HAnimMatrixArray(hier);
	Mat3 world[kBones];
	CVector pos[kBones];
	for (int b = 0; b < kBones; b++) {
		const float *m = mats + index[b] * 16;
		world[b] = FromRw(m);
		pos[b] = {m[12], m[13], m[14]};
	}
	for (int b = 0; b < kBones; b++)
		KeyRotation(Key(gSkel.frame[b])) = saved[b];
	rootT = savedRoot;
	api.UpdateRpHAnim(ped);

	// Each bone's parent: the one its joint sits on at its own offset.
	for (int b = 1; b < kBones; b++) {
		const CVector offset = KeyTranslation(Key(gSkel.frame[b]));
		float best = 1e9f;
		for (int c = 0; c < kBones; c++) {
			if (c == b)
				continue;
			const CVector d = Sub(Sub(pos[b], pos[c]), Mul(world[c], offset));
			const float e = std::sqrt(Dot(d, d));
			if (e < best) {
				best = e;
				gSkel.parent[b] = c;
			}
		}
		if (best > 0.01f || gSkel.parent[b] >= b)
			return Fail("his skeleton isn't the one expected");
	}
	// Which way round the keyframes hold rotations.
	float straight = 0, inverse = 0;
	for (int b = 1; b < kBones; b++) {
		const Quat local = ToQuat(Mul(Transpose(world[gSkel.parent[b]]), world[b]));
		straight += 1 - Alike(local, written[b]);
		inverse += 1 - Alike(local, Conjugate(written[b]));
	}
	gSkel.inverted = inverse < straight;
	if (std::min(straight, inverse) > 0.002f * kBones)
		return Fail("his bones don't follow their keyframes as expected");
	// The ped's frame, the root's parent: world(root) = frame * local(root).
	const Quat rootLocal = gSkel.inverted ? Conjugate(written[kRoot]) : written[kRoot];
	const Mat3 frame = Mul(world[kRoot], Transpose(ToMat(rootLocal)));
	const CVector origin = Sub(pos[kRoot], Mul(frame, kStandingRoot));
	const Mat3 toPed = Transpose(frame);
	for (int b = 0; b < kBones; b++) {
		gSkel.rest[b] = ToQuat(Mul(toPed, world[b]));
		gSkel.restPos[b] = Mul(toPed, Sub(pos[b], origin));
	}
	std::string parents;
	for (int b = 1; b < kBones; b++)
		parents += std::string(b > 1 ? ", " : "") + kNames[b] + "<" + kNames[gSkel.parent[b]];
	Log("body: skeleton measured (head %.2f m above the root, keyframes %s): %s", gSkel.restPos[kHead].z,
	    gSkel.inverted ? "inverted" : "as is", parents.c_str());
	gSkel.ok = true;
	return true;
}

void Resolve(const Pose &pose, Quat (&s)[kBones])
{
	for (int b = 0; b < kBones; b++)
		s[b] = pose.set[b] ? pose.turn[b] : b == kRoot ? Quat{} : s[gSkel.parent[b]];
}

} // namespace

Quat operator*(const Quat &a, const Quat &b)
{
	return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
	        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Quat Conjugate(const Quat &q) { return {-q.x, -q.y, -q.z, q.w}; }

Quat AxisAngle(CVector axis, float radians)
{
	axis = Normalised(axis);
	const float s = std::sin(radians / 2);
	return {axis.x * s, axis.y * s, axis.z * s, std::cos(radians / 2)};
}

CVector Rotate(const Quat &q, const CVector &v)
{
	const Quat r = q * Quat{v.x, v.y, v.z, 0} * Conjugate(q);
	return {r.x, r.y, r.z};
}

Quat Slerp(Quat a, const Quat &b, float t)
{
	float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
	if (d < 0) {
		a = {-a.x, -a.y, -a.z, -a.w};
		d = -d;
	}
	float ka = 1 - t, kb = t;
	if (d < 0.9995f) {
		const float angle = std::acos(d), s = std::sin(angle);
		ka = std::sin((1 - t) * angle) / s;
		kb = std::sin(t * angle) / s;
	}
	return Normalise({a.x * ka + b.x * kb, a.y * ka + b.y * kb, a.z * ka + b.z * kb, a.w * ka + b.w * kb});
}

Quat Arc(CVector from, CVector to)
{
	from = Normalised(from);
	to = Normalised(to);
	const float c = Dot(from, to);
	if (c > 0.99999f)
		return {};
	if (c < -0.99999f) { // half a turn about anything square to it
		const CVector axis = Normalised(std::fabs(from.x) < 0.9f ? Cross(from, {1, 0, 0}) : Cross(from, {0, 1, 0}));
		return {axis.x, axis.y, axis.z, 0};
	}
	const CVector axis = Cross(from, to);
	return Normalise({axis.x, axis.y, axis.z, 1 + c});
}

Quat X(float radians) { return AxisAngle({1, 0, 0}, radians); }
Quat Y(float radians) { return AxisAngle({0, 1, 0}, radians); }
Quat Z(float radians) { return AxisAngle({0, 0, 1}, radians); }

CVector Add(const CVector &a, const CVector &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
CVector Sub(const CVector &a, const CVector &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
CVector Scale(const CVector &a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(const CVector &a, const CVector &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
CVector Cross(const CVector &a, const CVector &b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
CVector Normalised(const CVector &v)
{
	const float l = std::sqrt(Dot(v, v));
	return l > 1e-6f ? Scale(v, 1 / l) : v;
}
CVector Lerp(const CVector &a, const CVector &b, float t) { return Add(a, Scale(Sub(b, a), t)); }
float Smooth(float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	return t * t * (3 - 2 * t);
}

bool Prepare(uint8_t *ped) { return ped && Measure(ped); }

CVector Joint(const Pose &pose, Bone b)
{
	if (!gSkel.ok)
		return {};
	Quat s[kBones];
	Resolve(pose, s);
	CVector p[kBones];
	p[kRoot] = Add(gSkel.restPos[kRoot], pose.rootMove);
	for (int c = 1; c <= b; c++) {
		const int up = gSkel.parent[c];
		p[c] = Add(p[up], Rotate(pose.whole * s[up], Sub(gSkel.restPos[c], gSkel.restPos[up])));
	}
	return p[b];
}

CVector RestDirection(Bone b)
{
	return kChild[b] < kBones ? Normalised(Sub(gSkel.restPos[kChild[b]], gSkel.restPos[b])) : CVector{0, 0, -1};
}

float Length(Bone b)
{
	if (kChild[b] >= kBones)
		return 0;
	const CVector d = Sub(gSkel.restPos[kChild[b]], gSkel.restPos[b]);
	return std::sqrt(Dot(d, d));
}

void Point(Pose &pose, Bone b, const CVector &direction) { pose.Turn(b, Arc(RestDirection(b), direction)); }

Pose Mix(const Pose &a, const Pose &b, float t)
{
	Pose r;
	Quat sa[kBones], sb[kBones];
	Resolve(a, sa);
	Resolve(b, sb);
	for (int i = 0; i < kBones; i++)
		r.Turn(Bone(i), Slerp(sa[i], sb[i], t));
	r.whole = Slerp(a.whole, b.whole, t);
	r.rootMove = Lerp(a.rootMove, b.rootMove, t);
	return r;
}

void Apply(uint8_t *ped, const Pose &pose, float weight)
{
	if (!gSkel.ok || EntityClump(ped) != gSkel.clump || weight <= 0.0f)
		return;
	weight = std::min(weight, 1.0f);
	Quat s[kBones], d[kBones];
	Resolve(pose, s);
	for (int b = 0; b < kBones; b++)
		d[b] = pose.whole * s[b] * gSkel.rest[b];
	for (int b = 0; b < kBones; b++) {
		const Quat local = b == kRoot ? d[b] : Conjugate(d[gSkel.parent[b]]) * d[b];
		Quat &key = KeyRotation(Key(gSkel.frame[b]));
		key = Slerp(key, gSkel.inverted ? Conjugate(local) : local, weight);
	}
	CVector &rootT = KeyTranslation(Key(gSkel.frame[kRoot]));
	rootT = Lerp(rootT, Add(gSkel.restPos[kRoot], pose.rootMove), weight);
}

namespace {
// Each bone's orientation (ped frame) for ApplyOver: the pose's where it's
// set, else its animation's on top of its parent's.
void Over(const Pose &pose, Quat (&d)[kBones])
{
	for (int b = 0; b < kBones; b++) {
		Quat keyed = KeyRotation(Key(gSkel.frame[b]));
		if (gSkel.inverted)
			keyed = Conjugate(keyed);
		const Quat parent = b == kRoot ? Quat{} : d[gSkel.parent[b]];
		d[b] = pose.set[b] ? pose.whole * pose.turn[b] * gSkel.rest[b] : parent * keyed;
	}
}
} // namespace

CVector JointOver(uint8_t *ped, const Pose &pose, Bone b)
{
	if (!gSkel.ok || EntityClump(ped) != gSkel.clump)
		return {};
	Quat d[kBones];
	Over(pose, d);
	CVector p[kBones];
	p[kRoot] = pose.set[kRoot] ? Add(gSkel.restPos[kRoot], pose.rootMove) : KeyTranslation(Key(gSkel.frame[kRoot]));
	for (int c = 1; c <= b; c++) {
		const int up = gSkel.parent[c];
		p[c] = Add(p[up], Rotate(d[up] * Conjugate(gSkel.rest[up]), Sub(gSkel.restPos[c], gSkel.restPos[up])));
	}
	return p[b];
}

void TurnOver(uint8_t *ped, Pose &pose, Bone b, const Quat &by)
{
	if (!gSkel.ok || EntityClump(ped) != gSkel.clump)
		return;
	Quat d[kBones];
	Over(pose, d);
	pose.Turn(b, Conjugate(pose.whole) * by * d[b] * Conjugate(gSkel.rest[b]));
}

void ApplyOver(uint8_t *ped, const Pose &pose, float weight)
{
	if (!gSkel.ok || EntityClump(ped) != gSkel.clump || weight <= 0.0f)
		return;
	weight = std::min(weight, 1.0f);
	Quat d[kBones];
	Over(pose, d);
	for (int b = 0; b < kBones; b++) {
		if (!pose.set[b])
			continue;
		const Quat local = b == kRoot ? d[b] : Conjugate(d[gSkel.parent[b]]) * d[b];
		Quat &key = KeyRotation(Key(gSkel.frame[b]));
		key = Slerp(key, gSkel.inverted ? Conjugate(local) : local, weight);
	}
	if (pose.set[kRoot]) {
		CVector &rootT = KeyTranslation(Key(gSkel.frame[kRoot]));
		rootT = Lerp(rootT, Add(gSkel.restPos[kRoot], pose.rootMove), weight);
	}
}

namespace {

// The bones held, relative to `frame` (a turn in the ped's frame).
bool HoldAbout(uint8_t *ped, Held &held, const Quat *frame, Bone base, const Bone *bones, int count)
{
	if (!gSkel.ok || EntityClump(ped) != gSkel.clump)
		return false;
	Quat d[kBones];
	Over(Pose{}, d);
	held = Held{};
	held.base = base;
	const Quat about = frame ? *frame : d[base];
	for (int i = 0; i < count; i++) {
		held.rel[bones[i]] = Conjugate(about) * d[bones[i]];
		held.set[bones[i]] = true;
	}
	return true;
}

void RestoreAbout(uint8_t *ped, const Held &held, const Quat *frame, float weight)
{
	if (!gSkel.ok || EntityClump(ped) != gSkel.clump || weight <= 0.0f)
		return;
	weight = std::min(weight, 1.0f);
	Quat d[kBones]; // each as it ends up, for its children
	for (int b = 0; b < kBones; b++) {
		const Quat parent = b == kRoot ? Quat{} : d[gSkel.parent[b]];
		Quat &key = KeyRotation(Key(gSkel.frame[b]));
		if (held.set[b]) {
			const Quat local = Conjugate(parent) * (frame ? *frame : d[held.base]) * held.rel[b];
			key = Slerp(key, gSkel.inverted ? Conjugate(local) : local, weight);
		}
		d[b] = parent * (gSkel.inverted ? Conjugate(key) : key);
	}
}

} // namespace

bool Hold(uint8_t *ped, Held &held, Bone base, const Bone *bones, int count)
{
	return HoldAbout(ped, held, nullptr, base, bones, count);
}
void Restore(uint8_t *ped, const Held &held, float weight) { RestoreAbout(ped, held, nullptr, weight); }
bool HoldIn(uint8_t *ped, Held &held, const Quat &frame, const Bone *bones, int count)
{
	return HoldAbout(ped, held, &frame, kRoot, bones, count);
}
void RestoreIn(uint8_t *ped, const Held &held, const Quat &frame, float weight)
{
	RestoreAbout(ped, held, &frame, weight);
}

// Two-bone reach: the elbow, bent towards `pole`, for the hand at `hand` (it
// stops short where the arm can't reach).
void Reach(Pose &p, Bone upper, Bone fore, CVector hand, const CVector &pole, float weight)
{
	if (weight <= 0.0f)
		return;
	const CVector shoulder = Joint(p, upper);
	const float a = Length(upper), b = Length(fore);
	CVector d = Sub(hand, shoulder);
	const float len = std::clamp(std::sqrt(Dot(d, d)), std::fabs(a - b) + 0.01f, a + b - 0.002f);
	const CVector u = Normalised(d);
	hand = Add(shoulder, Scale(u, len));
	const float x = (a * a - b * b + len * len) / (2 * len), y = std::sqrt(std::max(0.0f, a * a - x * x));
	const CVector v = Normalised(Sub(pole, Scale(u, Dot(pole, u))));
	const CVector elbow = Add(shoulder, Add(Scale(u, x), Scale(v, y)));
	Pose reached = p;
	Point(reached, upper, Sub(elbow, shoulder));
	Point(reached, fore, Sub(hand, elbow));
	const Pose mixed = Mix(p, reached, weight);
	p.Turn(upper, mixed.turn[upper]);
	p.Turn(fore, mixed.turn[fore]);
}

} // namespace body
