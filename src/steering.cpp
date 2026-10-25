#include "steering.h"
#include "carlights.h"
#include "config.h"
#include "driveby.h"
#include "game.h"
#include "log.h"
#include "shaders.h"
#include "viewmodel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <strings.h>
#include <unordered_map>
#include <vector>

using namespace game;

namespace steering {
namespace {

// CAutomobile, CPed (macVC 1.0.5217).
constexpr size_t kRwObject = 0x58, kModelIndex = 0x6a, kDriver = 0x218, kVehicleType = 0x348;
constexpr size_t kSteerAngle = 0x288; // CVehicle::m_fSteerAngle: rad at the front wheels, left > 0
constexpr size_t kFrontSeat = 0x78;   // CVehicleModelInfo::m_positions[CAR_POS_FRONTSEAT] (the driver's mirrors it)
constexpr size_t kModelName = 0x8;    // CBaseModelInfo::m_name
constexpr int32_t kStateDriving = 0x32;
constexpr PadButton kPadHorn = PadButton(0x24); // CControllerState::LeftShock: the horn

// Where a wheel is looked for (m): from the driver's seat across, ahead and
// up; his eyes from it. A wheel: a piece of so many triangles at least, so
// big (its radius), this flat (its thinnest spread over its widest), facing
// his eyes this much; its other pieces have their middles this close to it.
constexpr float kLookAcross = 0.4f, kLookFrom = 0.15f, kLookTo = 0.9f, kLookUp = 0.65f;
constexpr float kEyeBack = 0.15f, kEyeUp = 0.7f;
constexpr int kWheelTriangles = 6;
constexpr float kRadiusMin = 0.1f, kRadiusMax = 0.3f, kFlatMost = 0.35f, kFacingLeast = 0.5f, kSameMiddle = 0.05f;

// The round wheel, in its radius (the old one's to its corners): the rim
// a ring so far in, its tube so thick; a hub; three spokes (left, right and
// down), so wide and thick. The wheel turns so much more than the front
// wheels do (lock to lock about 200 degrees).
constexpr float kRim = 0.92f, kTube = 0.085f, kHub = 0.24f, kHubDepth = 0.16f, kSpokeWidth = 0.17f, kSpokeThick = 0.07f;
constexpr int kRimSides = 32, kTubeSides = 8, kHubSides = 16;
constexpr float kSteerRatio = 3.0f;
// The blinker stalk, in the wheel's radius: from the column behind the
// wheel (so far back, out to the left, down), out to the left (and a little
// towards him); so long and thick; it throws so far (rad).
constexpr float kStalkBack = 0.42f, kStalkOut = 0.16f, kStalkDown = 0.16f, kStalkLength = 0.62f, kStalkThick = 0.06f;
constexpr float kStalkThrow = 0.32f, kStalkBackRate = 8.0f; // back to the middle, per second, when the signal ends

// His hands: at ten to two (rad from the top), a little in front of the rim;
// the horn pressed a little in front of the hub. The flick of the stalk:
// reaching it, pushing it, back to the wheel (s).
constexpr float kGripAngle = 1.05f, kGripFront = 0.02f, kHornFront = 0.035f, kHornRate = 14.0f;
constexpr float kFlickReach = 0.26f, kFlickPush = 0.16f, kFlickBack = 0.26f;

// RenderWare.
constexpr uint32_t kAtomicRender = 0x04; // rpATOMICRENDER
constexpr int kLockVertices = 2;         // rpGEOMETRYLOCKVERTICES
constexpr int kTexCoords0 = 1;           // rwTEXTURECOORDINATEINDEX0
constexpr uint32_t kIm3DVertexUV = 1, kIm3DLighting = 0x20; // lit by the scene's lights, as the car is
constexpr int kPrimTriList = 3;
constexpr int kStateCull = 20, kCullNone = 1, kFilterLinear = 2, kAddressWrap = 1;

struct V3 {
	float x, y, z;
};
struct Triangle {
	uint16_t v[3], material;
};
struct Im3DVertex { // rw::gl3::Im3DVertex
	float x, y, z, nx, ny, nz;
	uint8_t r, g, b, a;
	float u, v;
};
static_assert(sizeof(Im3DVertex) == 0x24, "librw's Im3D vertex");

struct RW {
	void *(*ForAllAtomics)(void *clump, void *(*cb)(void *atomic, void *data), void *data);
	uint32_t (*AtomicFlags)(const void *atomic);
	void *(*AtomicFrame)(const void *atomic);
	void *(*AtomicGeometry)(const void *atomic);
	float *(*FrameLTM)(void *frame);
	const char *(*FrameName)(void *frame);
	int (*NumTriangles)(const void *geometry);
	int (*NumVertices)(const void *geometry);
	Triangle *(*Triangles)(const void *geometry);
	void *(*MorphTarget)(const void *geometry, int index);
	const V3 *(*Vertices)(const void *morphTarget);
	const float *(*TexCoords)(const void *geometry, int index);
	void *(*Material)(const void *geometry, int index);
	void *(*MaterialTexture)(const void *material);
	void *(*TextureRaster)(const void *texture);
	void *(*Lock)(void *geometry, int flags);
	void *(*Unlock)(void *geometry);
	void *(*Im3DTransform)(Im3DVertex *vertices, uint32_t count, float *world, uint32_t flags);
	int (*Im3DRenderIndexed)(int primitive, uint16_t *indices, int count);
	int (*Im3DEnd)();
	int (*StateGet)(int state, void *value);
} rw;
const float *gLod0DistSq; // CVisibilityPlugins::ms_vehicleLod0Dist

struct Mesh {
	std::vector<Im3DVertex> vertices;
	std::vector<uint16_t> indices;
};
// A model's wheel, in its body's frame (chassis_hi's).
struct Wheel {
	int model = -1, triangles = 0; // what it was looked for in (a freed geometry's address comes back)
	bool found = false;
	V3 centre{}, axis{}, up{}, left{}; // its middle; towards the driver; its own up and left
	float radius = 0;
	uint16_t material = 0;        // the old wheel's (its texture colours the new one)
	std::vector<uint32_t> old;    // its triangles
	std::vector<uint16_t> corners; // their corners (theirs alone), moved onto its middle
	V3 stalkPivot{};
	Mesh wheel, stalk;
};
std::unordered_map<const void *, Wheel> gWheels; // by the body's geometry
bool gReady;

// The player's car: its stalk, his flick of it, the horn.
const uint8_t *gStalkCar;
float gStalk;            // its turn now (rad)
int gSignalSeen;         // the signal his hand has answered
float gFlick = -1.0f;    // s into the flick (< 0: none)
int gFlickTo;            // which way
float gHorn;             // his right hand on the horn, 0..1

V3 Add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 Sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 Mul(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 Normal(V3 a)
{
	const float l = std::sqrt(Dot(a, a));
	return l > 1e-9f ? Mul(a, 1.0f / l) : a;
}
V3 Turn(V3 v, V3 axis, float angle)
{
	const float c = std::cos(angle), s = std::sin(angle);
	return Add(Add(Mul(v, c), Mul(Cross(axis, v), s)), Mul(axis, Dot(axis, v) * (1.0f - c)));
}
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
V3 Rotate(const float *m, V3 d) { return {m[0] * d.x + m[4] * d.y + m[8] * d.z, m[1] * d.x + m[5] * d.y + m[9] * d.z, m[2] * d.x + m[6] * d.y + m[10] * d.z}; }
// The car's CMatrix: right, forward, up and position (each 16 bytes from +8).
const V3 &Axis(const uint8_t *car, size_t row) { return *reinterpret_cast<const V3 *>(car + 8 + row); }
V3 InCar(const uint8_t *car, V3 world)
{
	const V3 d = Sub(world, Axis(car, 0x30));
	return {Dot(d, Axis(car, 0)), Dot(d, Axis(car, 0x10)), Dot(d, Axis(car, 0x20))};
}

// The `body`-frame matrix turning `angle` about `axis` through `pivot`, then into the world by `ltm`.
void Turned(const float *ltm, V3 pivot, V3 axis, float angle, float out[16])
{
	const V3 units[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
	for (int i = 0; i < 3; i++) {
		const V3 e = Rotate(ltm, Turn(units[i], axis, angle));
		out[i * 4] = e.x, out[i * 4 + 1] = e.y, out[i * 4 + 2] = e.z, out[i * 4 + 3] = 0.0f;
	}
	const V3 o = Apply(ltm, Sub(pivot, Turn(pivot, axis, angle)));
	out[12] = o.x, out[13] = o.y, out[14] = o.z, out[15] = 0.0f;
}

// Eigenvalues (smallest first) and their unit vectors of a symmetric 3x3 (Jacobi).
void Eigen(double a[3][3], double value[3], V3 vector[3])
{
	double v[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
	for (int sweep = 0; sweep < 32; sweep++) {
		const double off = std::fabs(a[0][1]) + std::fabs(a[0][2]) + std::fabs(a[1][2]);
		if (off < 1e-12)
			break;
		for (int p = 0; p < 2; p++)
			for (int q = p + 1; q < 3; q++) {
				if (std::fabs(a[p][q]) < 1e-15)
					continue;
				const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
				const double t = (theta >= 0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
				const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
				for (int k = 0; k < 3; k++) { // A = J^T A J
					const double akp = a[k][p], akq = a[k][q];
					a[k][p] = c * akp - s * akq;
					a[k][q] = s * akp + c * akq;
				}
				for (int k = 0; k < 3; k++) {
					const double apk = a[p][k], aqk = a[q][k];
					a[p][k] = c * apk - s * aqk;
					a[q][k] = s * apk + c * aqk;
				}
				for (int k = 0; k < 3; k++) {
					const double vkp = v[k][p], vkq = v[k][q];
					v[k][p] = c * vkp - s * vkq;
					v[k][q] = s * vkp + c * vkq;
				}
			}
	}
	int order[3] = {0, 1, 2};
	std::sort(order, order + 3, [&](int i, int j) { return a[i][i] < a[j][j]; });
	for (int i = 0; i < 3; i++) {
		value[i] = a[order[i]][order[i]];
		vector[i] = Normal(V3{float(v[0][order[i]]), float(v[1][order[i]]), float(v[2][order[i]])});
	}
}

// --- the meshes ---------------------------------------------------------------

void Vertex(Mesh &m, V3 p, V3 n, float u, float v)
{
	Im3DVertex out{};
	out.x = p.x, out.y = p.y, out.z = p.z, out.nx = n.x, out.ny = n.y, out.nz = n.z;
	out.a = 255; // black: lit, as the car is, not glowing
	out.u = u, out.v = v;
	m.vertices.push_back(out);
}
void Quad(Mesh &m, V3 a, V3 b, V3 c, V3 d, V3 n, float u, float v)
{
	const uint16_t i = uint16_t(m.vertices.size());
	for (V3 p : {a, b, c, d})
		Vertex(m, p, n, u, v);
	for (uint16_t k : {0, 1, 2, 0, 2, 3})
		m.indices.push_back(uint16_t(i + k));
}
// A box about `c`, its half extents along x, y and z (each a vector).
void Box(Mesh &m, V3 c, V3 x, V3 y, V3 z, float u, float v)
{
	const V3 axes[3] = {x, y, z};
	for (int i = 0; i < 3; i++)
		for (float s : {-1.0f, 1.0f}) {
			const V3 n = Mul(axes[i], s), a = axes[(i + 1) % 3], b = axes[(i + 2) % 3], f = Add(c, n);
			Quad(m, Sub(Sub(f, a), b), Sub(Add(f, a), b), Add(Add(f, a), b), Add(Sub(f, a), b), Normal(n), u, v);
		}
}

void BuildMeshes(Wheel &w, float u, float v)
{
	const V3 c = w.centre, A = w.axis, U = w.up, L = w.left;
	const float r = w.radius * kRim, tube = w.radius * kTube;
	Mesh &m = w.wheel;
	// The rim: a ring of tube.
	for (int i = 0; i < kRimSides; i++) {
		const float a = 6.2831853f * float(i) / kRimSides;
		const V3 out = Add(Mul(U, std::cos(a)), Mul(L, std::sin(a))), ring = Add(c, Mul(out, r));
		for (int j = 0; j < kTubeSides; j++) {
			const float b = 6.2831853f * float(j) / kTubeSides;
			const V3 n = Add(Mul(out, std::cos(b)), Mul(A, std::sin(b)));
			Vertex(m, Add(ring, Mul(n, tube)), n, u, v);
		}
	}
	for (int i = 0; i < kRimSides; i++)
		for (int j = 0; j < kTubeSides; j++) {
			const auto at = [](int ii, int jj) { return uint16_t((ii % kRimSides) * kTubeSides + jj % kTubeSides); };
			for (uint16_t k : {at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j), at(i + 1, j + 1), at(i, j + 1)})
				m.indices.push_back(k);
		}
	// The hub: a short drum, its face towards him.
	const float hub = w.radius * kHub, depth = w.radius * kHubDepth;
	const uint16_t face = uint16_t(m.vertices.size());
	Vertex(m, Add(c, Mul(A, depth * 0.5f)), A, u, v);
	for (int i = 0; i < kHubSides; i++) {
		const float a = 6.2831853f * float(i) / kHubSides, a2 = 6.2831853f * float(i + 1) / kHubSides;
		const V3 o1 = Add(Mul(U, std::cos(a)), Mul(L, std::sin(a))), o2 = Add(Mul(U, std::cos(a2)), Mul(L, std::sin(a2)));
		const V3 f1 = Add(Add(c, Mul(o1, hub)), Mul(A, depth * 0.5f)), f2 = Add(Add(c, Mul(o2, hub)), Mul(A, depth * 0.5f));
		Quad(m, f1, f2, Sub(f2, Mul(A, depth)), Sub(f1, Mul(A, depth)), Normal(Add(o1, o2)), u, v);
		const uint16_t i1 = uint16_t(m.vertices.size());
		Vertex(m, f1, A, u, v);
		Vertex(m, f2, A, u, v);
		for (uint16_t k : {face, i1, uint16_t(i1 + 1)})
			m.indices.push_back(k);
	}
	// The spokes: left, right and down, hub to rim.
	const float half = (r - hub * 0.8f) * 0.5f, mid = hub * 0.8f + half;
	for (V3 dir : {L, Mul(L, -1.0f), Mul(U, -1.0f)}) {
		const V3 across = Normal(Cross(A, dir));
		Box(m, Add(c, Mul(dir, mid)), Mul(dir, half), Mul(across, w.radius * kSpokeWidth * 0.5f),
		    Mul(A, w.radius * kSpokeThick * 0.5f), u, v);
	}
	// The stalk: out from the column to the left, a little towards him, and a knob.
	w.stalkPivot = Add(Add(Sub(c, Mul(A, w.radius * kStalkBack)), Mul(L, w.radius * kStalkOut)), Mul(U, -w.radius * kStalkDown));
	const V3 along = Normal(Add(L, Mul(A, 0.25f))), across = Normal(Cross(A, along)), thick = Mul(A, w.radius * kStalkThick * 0.5f);
	const float length = w.radius * kStalkLength;
	Box(w.stalk, Add(w.stalkPivot, Mul(along, length * 0.5f)), Mul(along, length * 0.5f), Mul(across, w.radius * kStalkThick * 0.5f),
	    thick, u, v);
	Box(w.stalk, Add(w.stalkPivot, Mul(along, length)), Mul(along, w.radius * 0.06f), Mul(across, w.radius * 0.07f),
	    Mul(A, w.radius * 0.07f), u, v);
}

// --- finding a model's wheel --------------------------------------------------

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

int ModelIndex(const uint8_t *car) { return *reinterpret_cast<const int16_t *>(car + kModelIndex); }
const uint8_t *ModelInfo(const uint8_t *car)
{
	const int model = ModelIndex(car);
	return model >= 0 ? api.modelInfoPtrs[model] : nullptr;
}

bool Collapsed(const void *geometry, const Wheel &w)
{
	const V3 d = Sub(rw.Vertices(rw.MorphTarget(geometry, 0))[w.corners.front()], w.centre);
	return Dot(d, d) < 1e-8f;
}

// The old wheel out of the body: its corners all moved onto its middle (inside
// the new hub), so its triangles have no size. Only the vertices change, and
// the model goes to the GPU again (its triangles are left alone: building a
// car's triangle strips again from them can fail, and ends the game).
void Collapse(const void *geometry, const Wheel &w)
{
	void *g = const_cast<void *>(geometry);
	rw.Lock(g, kLockVertices);
	V3 *verts = const_cast<V3 *>(rw.Vertices(rw.MorphTarget(g, 0)));
	for (uint16_t i : w.corners)
		verts[i] = w.centre;
	rw.Unlock(g);
}

void Find(const uint8_t *car, void *atomic, const void *geometry, Wheel &w)
{
	const uint8_t *mi = ModelInfo(car);
	void *morph = rw.MorphTarget(geometry, 0);
	const V3 *verts = morph ? rw.Vertices(morph) : nullptr;
	const float *uv = rw.TexCoords(geometry, kTexCoords0);
	const Triangle *tris = rw.Triangles(geometry);
	const int nverts = rw.NumVertices(geometry), ntris = rw.NumTriangles(geometry);
	if (!mi || !verts || !uv || !tris || !strncasecmp(reinterpret_cast<const char *>(mi + kModelName), "rc", 2))
		return; // the RC toys have no wheel to turn
	const float *ltm = rw.FrameLTM(rw.AtomicFrame(atomic));
	// The body's points in the car's frame (the seat's), and back.
	std::vector<V3> P(static_cast<size_t>(nverts));
	for (int i = 0; i < nverts; i++)
		P[size_t(i)] = InCar(car, Apply(ltm, verts[i]));
	const V3 origin = InCar(car, Apply(ltm, {0, 0, 0}));
	V3 bodyAxes[3];
	for (int i = 0; i < 3; i++)
		bodyAxes[i] = Sub(InCar(car, Apply(ltm, V3{i == 0 ? 1.0f : 0.0f, i == 1 ? 1.0f : 0.0f, i == 2 ? 1.0f : 0.0f})), origin);
	auto toBodyDir = [&](V3 d) { return V3{Dot(d, bodyAxes[0]), Dot(d, bodyAxes[1]), Dot(d, bodyAxes[2])}; };
	auto toBody = [&](V3 p) { return toBodyDir(Sub(p, origin)); };

	V3 seat = *reinterpret_cast<const V3 *>(mi + kFrontSeat);
	seat.x = -std::fabs(seat.x); // the driver's side
	const V3 eye{seat.x, seat.y - kEyeBack, seat.z + kEyeUp};
	auto inBox = [&](V3 p) {
		return std::fabs(p.x - seat.x) < kLookAcross && p.y > seat.y + kLookFrom && p.y < seat.y + kLookTo && p.z > seat.z &&
		       p.z < seat.z + kLookUp;
	};
	std::vector<int> region;
	for (int i = 0; i < ntris; i++)
		if (inBox(P[tris[i].v[0]]) && inBox(P[tris[i].v[1]]) && inBox(P[tris[i].v[2]]))
			region.push_back(i);
	// Its pieces: triangles meeting at their corners (to the millimetre).
	std::vector<int> parent(region.size());
	for (size_t i = 0; i < region.size(); i++)
		parent[i] = int(i);
	auto root = [&](int a) {
		while (parent[size_t(a)] != a)
			a = parent[size_t(a)] = parent[size_t(parent[size_t(a)])];
		return a;
	};
	std::unordered_map<int64_t, int> corner;
	for (size_t i = 0; i < region.size(); i++)
		for (int k = 0; k < 3; k++) {
			const V3 &p = P[tris[region[i]].v[k]];
			auto mm = [](float m) { return int64_t(std::lround(m * 1000.0f) + (1 << 20)) & ((1 << 21) - 1); };
			const int64_t key = (mm(p.x) << 42) | (mm(p.y) << 21) | mm(p.z);
			const auto [it, fresh] = corner.emplace(key, int(i));
			if (!fresh)
				parent[size_t(root(int(i)))] = root(it->second);
		}
	std::unordered_map<int, std::vector<int>> pieces;
	for (size_t i = 0; i < region.size(); i++)
		pieces[root(int(i))].push_back(region[i]);

	struct Piece {
		std::vector<int> tris;
		V3 middle, normal;
		float radius, flat, facing;
	};
	std::vector<Piece> found;
	for (auto &[key, list] : pieces) {
		Piece p{list, {0, 0, 0}, {0, 0, 0}, 0, 1, 0};
		for (int t : list)
			for (int k = 0; k < 3; k++)
				p.middle = Add(p.middle, P[tris[t].v[k]]);
		p.middle = Mul(p.middle, 1.0f / float(list.size() * 3));
		double cov[3][3] = {};
		for (int t : list)
			for (int k = 0; k < 3; k++) {
				const V3 d = Sub(P[tris[t].v[k]], p.middle);
				const double e[3] = {d.x, d.y, d.z};
				for (int i = 0; i < 3; i++)
					for (int j = 0; j < 3; j++)
						cov[i][j] += e[i] * e[j];
			}
		double value[3];
		V3 vector[3];
		Eigen(cov, value, vector);
		p.normal = vector[0];
		if (Dot(p.normal, Sub(eye, p.middle)) < 0)
			p.normal = Mul(p.normal, -1.0f);
		p.flat = float(std::sqrt(std::max(value[0], 0.0) / std::max(value[2], 1e-12)));
		p.facing = Dot(p.normal, Normal(Sub(eye, p.middle)));
		for (int t : list)
			for (int k = 0; k < 3; k++) {
				const V3 d = Sub(P[tris[t].v[k]], p.middle);
				p.radius = std::max(p.radius, std::sqrt(std::max(Dot(d, d) - Dot(d, p.normal) * Dot(d, p.normal), 0.0f)));
			}
		found.push_back(std::move(p));
	}
	const Piece *best = nullptr;
	float bestScore = 0;
	for (const Piece &p : found) {
		if (int(p.tris.size()) < kWheelTriangles || p.radius < kRadiusMin || p.radius > kRadiusMax || p.flat > kFlatMost ||
		    p.facing < kFacingLeast)
			continue;
		const float score = p.facing + (1.0f - p.flat) - 2.0f * std::fabs(p.middle.x - seat.x);
		if (score > bestScore)
			bestScore = score, best = &p;
	}
	if (!best)
		return;
	// The whole wheel: every piece about its middle.
	int biggest = -1;
	float biggestArea = 0;
	for (const Piece &p : found) {
		const V3 d = Sub(p.middle, best->middle);
		if (std::sqrt(Dot(d, d)) > kSameMiddle)
			continue;
		for (int t : p.tris) {
			w.old.push_back(uint32_t(t));
			const V3 &a = P[tris[t].v[0]], &b = P[tris[t].v[1]], &c = P[tris[t].v[2]];
			const V3 n = Cross(Sub(b, a), Sub(c, a));
			if (Dot(n, n) > biggestArea)
				biggestArea = Dot(n, n), biggest = t;
		}
	}
	// Its corners, if nothing else in the body uses them.
	std::vector<bool> wheel(static_cast<size_t>(ntris), false), used(static_cast<size_t>(nverts), false);
	for (uint32_t t : w.old)
		wheel[t] = true;
	for (uint32_t t : w.old)
		for (int k = 0; k < 3; k++)
			used[tris[t].v[k]] = true;
	for (int t = 0; t < ntris; t++)
		for (int k = 0; k < 3 && !wheel[size_t(t)]; k++)
			if (used[tris[t].v[k]]) {
				Log("steering: %s's wheel shares corners with the rest of the body; left as it is",
				    reinterpret_cast<const char *>(mi + kModelName));
				w.old.clear();
				return;
			}
	for (int i = 0; i < nverts; i++)
		if (used[size_t(i)])
			w.corners.push_back(uint16_t(i));
	const V3 axis = best->normal, up = Normal(Sub(V3{0, 0, 1}, Mul(axis, axis.z)));
	w.centre = toBody(best->middle);
	w.axis = Normal(toBodyDir(axis));
	w.up = Normal(toBodyDir(up));
	w.left = Normal(toBodyDir(Cross(axis, up)));
	w.radius = best->radius;
	w.material = tris[biggest].material;
	// Its colour: a texel in the middle of its biggest triangle.
	float u = 0, v = 0;
	for (int k = 0; k < 3; k++)
		u += uv[2 * tris[biggest].v[k]] / 3.0f, v += uv[2 * tris[biggest].v[k] + 1] / 3.0f;
	BuildMeshes(w, u, v);
	w.found = true;
	Log("steering: %s's wheel found, %zu triangles, %.2f m across", reinterpret_cast<const char *>(mi + kModelName),
	    w.old.size(), w.radius * 2.0f);
}

// A car's body (chassis_hi) and its model's wheel, found the first time.
const Wheel *WheelOf(const uint8_t *car, void *&atomic)
{
	void *clump = *reinterpret_cast<void *const *>(car + kRwObject);
	Body body;
	if (clump)
		rw.ForAllAtomics(clump, FindBody, &body);
	atomic = body.atomic;
	const void *geometry = atomic ? rw.AtomicGeometry(atomic) : nullptr;
	if (!geometry)
		return nullptr;
	Wheel &w = gWheels[geometry];
	const int model = ModelIndex(car), count = rw.NumTriangles(geometry);
	if (w.model != model || w.triangles != count) {
		w = Wheel{};
		w.model = model;
		w.triangles = count;
		Find(car, atomic, geometry, w);
	}
	if (w.found && !Collapsed(geometry, w)) // found now, or the model loaded again where it was
		Collapse(geometry, w);
	return &w;
}

bool IsCar(const uint8_t *vehicle) { return *reinterpret_cast<const int32_t *>(vehicle + kVehicleType) == 0; }
float WheelTurn(const uint8_t *car) { return *reinterpret_cast<const float *>(car + kSteerAngle) * kSteerRatio; }

bool Close(const uint8_t *car)
{
	const CVector cam = shaders::CameraPosition();
	const V3 d = Sub(Axis(car, 0x30), V3{cam.x, cam.y, cam.z});
	return gLod0DistSq && Dot(d, d) < *gLod0DistSq;
}

void Draw(Mesh &m, float *world)
{
	rw.Im3DTransform(m.vertices.data(), uint32_t(m.vertices.size()), world, kIm3DVertexUV | kIm3DLighting);
	rw.Im3DRenderIndexed(kPrimTriList, m.indices.data(), int(m.indices.size()));
	rw.Im3DEnd();
}

template <typename T>
bool Bind(T &out, const char *name)
{
	out = reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY, name));
	if (!out)
		Log("steering: missing %s", name);
	return out != nullptr;
}

} // namespace

void DrawCar(uint8_t *car)
{
	if (!gReady || !IsCar(car) || !Close(car))
		return;
	void *atomic;
	const Wheel *found = WheelOf(car, atomic);
	if (!found || !found->found || !(rw.AtomicFlags(atomic) & kAtomicRender))
		return;
	Wheel &w = const_cast<Wheel &>(*found);
	void *texture = rw.MaterialTexture(rw.Material(rw.AtomicGeometry(atomic), w.material));
	const float *ltm = rw.FrameLTM(rw.AtomicFrame(atomic));
	const shaders::SavedStates saved;
	void *cull = nullptr;
	rw.StateGet(kStateCull, &cull);
	api.RwRenderStateSet(kStateCull, reinterpret_cast<void *>(kCullNone));
	api.RwRenderStateSet(kStateTextureRaster, texture ? rw.TextureRaster(texture) : nullptr);
	api.RwRenderStateSet(kStateZTest, reinterpret_cast<void *>(1));
	api.RwRenderStateSet(kStateZWrite, reinterpret_cast<void *>(1));
	api.RwRenderStateSet(kStateVertexAlpha, nullptr);
	api.RwRenderStateSet(kStateTextureFilter, reinterpret_cast<void *>(kFilterLinear));
	api.RwRenderStateSet(kStateTextureAddress, reinterpret_cast<void *>(kAddressWrap));
	float world[16];
	Turned(ltm, w.centre, w.axis, WheelTurn(car), world);
	Draw(w.wheel, world);
	Turned(ltm, w.stalkPivot, w.axis, car == gStalkCar ? gStalk : 0.0f, world);
	Draw(w.stalk, world);
	api.RwRenderStateSet(kStateCull, cull);
}

void PoseDriver(uint8_t *ped)
{
	uint8_t *car = gReady && PedInVehicle(ped) ? PedVehicle(ped) : nullptr;
	const bool driving = car && IsCar(car) && PedState(ped) == kStateDriving &&
	                     *reinterpret_cast<uint8_t *const *>(car + kDriver) == ped && !driveby::Aiming();
	if (car != gStalkCar) { // a new car: its stalk in the middle, no flick yet
		gStalkCar = car;
		gStalk = 0.0f;
		gFlick = -1.0f;
		gSignalSeen = car ? carlights::Signalling(car) : 0;
	}
	if (!driving)
		return;
	void *atomic;
	const Wheel *w = WheelOf(car, atomic);
	if (!w || !w->found)
		return;
	const float dt = *api.timeStep / 50.0f;
	const float *ltm = rw.FrameLTM(rw.AtomicFrame(atomic));
	const V3 c = Apply(ltm, w->centre), A = Normal(Rotate(ltm, w->axis)), U = Normal(Rotate(ltm, w->up)),
	         L = Normal(Rotate(ltm, w->left));
	const float turn = WheelTurn(car), rim = w->radius * kRim;
	auto onRim = [&](float angle) {
		return Add(Add(c, Mul(Add(Mul(U, std::cos(angle)), Mul(L, std::sin(angle))), rim)), Mul(A, kGripFront));
	};
	V3 leftHand = onRim(kGripAngle + turn), rightHand = onRim(-kGripAngle + turn);

	// The horn: his right hand in the middle while it sounds.
	uint8_t *pad = api.GetPad(0);
	const bool horn = pad && PadNew(pad, kPadHorn) != 0 && !MenuActive(api.frontEndMenuManager);
	gHorn += ((horn ? 1.0f : 0.0f) - gHorn) * std::min(1.0f, dt * kHornRate);
	rightHand = Lerp(rightHand, Add(c, Mul(A, kHornFront)), Smooth(gHorn));

	// The blinkers: a flick of the stalk with his left hand as they come on;
	// the stalk goes back by itself when they end.
	const int signal = carlights::Signalling(car);
	if (signal != gSignalSeen) {
		if (signal) {
			gFlick = 0.0f;
			gFlickTo = signal;
		}
		gSignalSeen = signal;
	}
	const float thrown = gFlickTo < 0 ? kStalkThrow : -kStalkThrow; // a left turn is down (anticlockwise, to him)
	const V3 pivot = Apply(ltm, w->stalkPivot), along = Normal(Add(L, Mul(A, 0.25f)));
	auto tip = [&](float angle) { return Add(Add(pivot, Mul(Turn(along, A, angle), w->radius * kStalkLength)), Mul(A, 0.01f)); };
	if (gFlick >= 0.0f) {
		gFlick += dt;
		const float t = gFlick;
		if (t < kFlickReach) {
			gStalk = 0.0f;
			leftHand = Lerp(leftHand, tip(0.0f), Smooth(t / kFlickReach));
		} else if (t < kFlickReach + kFlickPush) {
			gStalk = thrown * Smooth((t - kFlickReach) / kFlickPush);
			leftHand = tip(gStalk);
		} else if (t < kFlickReach + kFlickPush + kFlickBack) {
			gStalk = thrown;
			leftHand = Lerp(tip(gStalk), leftHand, Smooth((t - kFlickReach - kFlickPush) / kFlickBack));
		} else {
			gFlick = -1.0f;
		}
	} else if (!signal) {
		gStalk -= gStalk * std::min(1.0f, dt * kStalkBackRate);
	}

	// Elbows down and out.
	viewmodel::ReachArm(ped, true, {leftHand.x, leftHand.y, leftHand.z}, {-U.x + L.x * 0.7f, -U.y + L.y * 0.7f, -U.z + L.z * 0.7f});
	viewmodel::ReachArm(ped, false, {rightHand.x, rightHand.y, rightHand.z},
	                    {-U.x - L.x * 0.7f, -U.y - L.y * 0.7f, -U.z - L.z * 0.7f});
}

bool Install()
{
	if (!gConfig.steeringWheel)
		return true;
	gReady = Bind(rw.ForAllAtomics, "_Z20RpClumpForAllAtomicsPN2rw5ClumpEPFPNS_6AtomicES3_PvES4_") &&
	         Bind(rw.AtomicFlags, "_Z16RpAtomicGetFlagsPKN2rw6AtomicE") &&
	         Bind(rw.AtomicFrame, "_Z16RpAtomicGetFramePKN2rw6AtomicE") &&
	         Bind(rw.AtomicGeometry, "_Z19RpAtomicGetGeometryPKN2rw6AtomicE") &&
	         Bind(rw.FrameLTM, "_Z13RwFrameGetLTMPN2rw5FrameE") && Bind(rw.FrameName, "_Z16GetFrameNodeNamePN2rw5FrameE") &&
	         Bind(rw.NumTriangles, "_Z25RpGeometryGetNumTrianglesPKN2rw8GeometryE") &&
	         Bind(rw.NumVertices, "_Z24RpGeometryGetNumVerticesPKN2rw8GeometryE") &&
	         Bind(rw.Triangles, "_Z22RpGeometryGetTrianglesPKN2rw8GeometryE") &&
	         Bind(rw.MorphTarget, "_Z24RpGeometryGetMorphTargetPKN2rw8GeometryEi") &&
	         Bind(rw.Vertices, "_Z24RpMorphTargetGetVerticesPKN2rw11MorphTargetE") &&
	         Bind(rw.TexCoords, "_Z28RpGeometryGetVertexTexCoordsPKN2rw8GeometryE24RwTextureCoordinateIndex") &&
	         Bind(rw.Material, "_Z21RpGeometryGetMaterialPKN2rw8GeometryEi") &&
	         Bind(rw.MaterialTexture, "_Z20RpMaterialGetTexturePKN2rw8MaterialE") &&
	         Bind(rw.TextureRaster, "_Z18RwTextureGetRasterPKN2rw7TextureE") &&
	         Bind(rw.Lock, "_Z14RpGeometryLockPN2rw8GeometryEi") && Bind(rw.Unlock, "_Z16RpGeometryUnlockPN2rw8GeometryE") &&
	         Bind(rw.Im3DTransform, "_Z15RwIm3DTransformPN2rw3gl310Im3DVertexEjPNS_6MatrixEj") &&
	         Bind(rw.Im3DRenderIndexed, "_Z28RwIm3DRenderIndexedPrimitive15RwPrimitiveTypePti") &&
	         Bind(rw.Im3DEnd, "_Z9RwIm3DEndv") && Bind(rw.StateGet, "_Z16RwRenderStateGet13RwRenderStatePv") &&
	         Bind(gLod0DistSq, "_ZN18CVisibilityPlugins18ms_vehicleLod0DistE");
	if (!gReady)
		Log("steering: functions missing; the wheels are the game's own");
	return gReady;
}

} // namespace steering
