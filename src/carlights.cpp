#include "carlights.h"
#include "camera.h"
#include "config.h"
#include "draw.h"
#include "game.h"
#include "gps.h"
#include "hooks.h"
#include "log.h"
#include "shaders.h"
#include "steering.h"
#include "turns.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace game;

namespace carlights {
namespace {

// CBrightLights' types (BRIGHTLIGHT_*): traffic lights 1-3, then a vehicle's
// front lights 4-7 and rear lights 8-11 (long, small, big, tall).
constexpr uint8_t kFirstVehicle = 4, kLastVehicle = 11;

// CAutomobile (macVC 1.0.5217).
constexpr size_t kRwObject = 0x58;   // CEntity::m_rwObject (the clump)
constexpr size_t kStatus = 0x60;     // CEntity: m_status in bits 3-7
constexpr size_t kModelIndex = 0x6a; // CEntity::m_modelIndex (int16)
constexpr size_t kGasPedal = 0x28c, kBrakePedal = 0x290;
constexpr size_t kVehicleFlags = 0x295; // bEngineOn 0x10, bLightsOn 0x40
constexpr size_t kDamage = 0x34c;       // CDamageManager
constexpr size_t kVehicleType = 0x348;  // m_vehType: a car is 0
constexpr size_t kDriver = 0x218;       // CVehicle::pDriver
constexpr uint8_t kEngineOn = 0x10, kLightsOn = 0x40;
constexpr uint32_t kStatusAbandoned = 4, kStatusWrecked = 5;
// CVehicleModelInfo::m_positions: the right headlight, then the right tail
// light (the left ones mirror them).
constexpr size_t kHeadlights = 0x60, kTaillights = 0x6c;
// eLights: front left, front right, rear left, rear right (0: working).
constexpr int kFrontLeft = 0, kRearLeft = 2;
// CAutomobile::PreRender's tail light coronas: the car's address + 14 (left), + 15.
constexpr uint32_t kTailCorona = 14;

// A light's triangles: textured and unpainted (white), facing out of the
// car's end (its normal along the car at least this much) and this close to
// the light's point (m): across, up or down, along. The glow sits a few mm
// off the surface so it never fights it for depth.
constexpr float kFacing = 0.3f, kAcross = 0.32f, kUpDown = 0.14f, kAlong = 0.35f, kLift = 0.004f;
// How brightly: braking, the tail lights on, reversing, the headlights on.
constexpr float kBraking = 1.0f, kTailOn = 0.4f, kReversing = 1.0f, kHeadOn = 0.85f;
// Stopped with someone at the wheel and off the accelerator, they're holding
// the brake (m/s, pedal).
constexpr float kStoppedSpeed = 0.5f, kPedalOff = 0.05f;
// The blinkers: the player's car signals the GPS route's next turn (a bend of
// 40 degrees or more, judged over 12 m either side) once it's within 3.5 s at
// its speed (30 to 90 m), and for a moment after it last saw it. A rear
// blinker is the red lamp, as on American cars, and any amber texels flash
// too, front and back.
constexpr float kTurnAngle = 0.70f, kTurnWindow = 12.0f, kSignalSeconds = 3.5f, kSignalNear = 30.0f,
                kSignalFar = 90.0f, kSignalKeep = 1.3f; // reach while already signalling
constexpr float kRouteAlong = 0.3f; // the route must lead the way the car faces (cosine)
constexpr float kRouteFrom = 2.0f;  // m ahead of the car the route is followed from
constexpr size_t kRouteSkipMost = 3; // route points passed over to get there (more: it leads behind)
constexpr uint32_t kSignalHoldMs = 1200, kBlinkMs = 700, kBlinkOnMs = 380;
// The dash's blinker in the seat view: a green arrow either side of the
// bottom middle (clear of the radio station's logo), in screen heights.
constexpr float kDashArrow = 0.02f, kDashApart = 0.085f, kDashHeight = 0.87f;
constexpr CRGBA kDashGreen{70, 235, 110, 255}, kDashShadow{0, 0, 0, 150};

// RenderWare.
constexpr uint32_t kAtomicRender = 0x04; // rpATOMICRENDER
constexpr int kTexCoords0 = 1;           // rwTEXTURECOORDINATEINDEX0
constexpr uint32_t kIm3DVertexUV = 1;    // rwIM3D_VERTEXUV (without it Im3D drops the texture)
constexpr int kPrimTriList = 3;          // rwPRIMTYPETRILIST
constexpr int kFilterLinear = 2, kAddressWrap = 1;
constexpr int kMaxVertices = 9999; // librw's Im3D buffer holds 10000

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

// The glow's shaders: the game's Im3D vertex shader, and a fragment shader
// that lights the light-coloured texels only. The vertex colour says how
// much: red for the red lenses, green for reversing and blue for the
// headlights (both on the white lenses), alpha for the amber ones.
const char *const kVertex = R"(
VSIN(ATTRIB_POS) vec3 in_pos;
VSOUT vec4 v_color;
VSOUT vec2 v_tex0;
void main(void)
{
	vec4 Vertex = u_world * vec4(in_pos, 1.0);
	gl_Position = u_proj * u_view * Vertex;
	v_color = in_color;
	v_tex0 = in_tex0;
}
)";
const char *const kFragment = R"(
uniform sampler2D tex0;
FSIN vec4 v_color;
FSIN vec2 v_tex0;
void main(void)
{
	vec4 t = texture(tex0, vec2(v_tex0.x, 1.0 - v_tex0.y));
	float hi = max(t.r, max(t.g, t.b)), lo = min(t.r, min(t.g, t.b));
	// Red lenses (dull red included): red well over green and blue.
	float red = smoothstep(1.25, 1.6, t.r / max(max(t.g, t.b), 0.04)) * smoothstep(0.12, 0.3, t.r);
	// White ones: bright and pale.
	float white = smoothstep(0.45, 0.75, lo) * (1.0 - smoothstep(0.12, 0.3, hi - lo));
	// Amber ones: green about half the red, little blue.
	float q = t.g / max(t.r, 0.04), qb = t.b / max(t.r, 0.04);
	float amber = smoothstep(0.3, 0.42, q) * (1.0 - smoothstep(0.78, 0.9, q)) * (1.0 - smoothstep(0.3, 0.45, qb)) *
	              smoothstep(0.35, 0.55, t.r);
	vec3 glow = vec3(1.0, 0.16, 0.08) * red * v_color.r * 1.5 + vec3(0.25, 0.08, 0.05) * red * red * v_color.r +
	            vec3(1.0, 0.96, 0.88) * white * max(v_color.g, v_color.b) + vec3(1.0, 0.5, 0.06) * amber * v_color.a * 1.3;
	FRAGCOLOR(vec4(glow * t.a, 1.0));
}
)";

struct RW {
	void *(*ForAllAtomics)(void *clump, void *(*cb)(void *atomic, void *data), void *data);
	uint32_t (*AtomicFlags)(const void *atomic);
	void *(*AtomicFrame)(const void *atomic);
	void *(*AtomicGeometry)(const void *atomic);
	float *(*FrameLTM)(void *frame);
	const char *(*FrameName)(void *frame);
	int (*NumTriangles)(const void *geometry);
	const Triangle *(*Triangles)(const void *geometry);
	void *(*MorphTarget)(const void *geometry, int index);
	const V3 *(*Vertices)(const void *morphTarget);
	const float *(*TexCoords)(const void *geometry, int index);
	void *(*Material)(const void *geometry, int index);
	void *(*MaterialTexture)(const void *material);
	const uint8_t *(*MaterialColour)(const void *material);
	void *(*TextureRaster)(const void *texture);
	void *(*Im3DTransform)(Im3DVertex *vertices, uint32_t count, float *world, uint32_t flags);
	int (*Im3DRenderIndexed)(int primitive, uint16_t *indices, int count);
	int (*Im3DEnd)();
	void (*ShaderUse)(void *shader);
	uint32_t (*LightStatus)(const void *damage, int light);
} rw;
const float *gLod0DistSq;     // CVisibilityPlugins::ms_vehicleLod0Dist: closer, a car's detailed model draws
const uint32_t *gFrameCounter; // CTimer::m_FrameCounter

void (*orig_RegisterOne)(CVector, CVector, CVector, CVector, uint8_t, uint8_t, uint8_t, uint8_t);
void (*orig_Render)(uint8_t *car);
void (*orig_SpecialFXRender)();
void (*orig_PreRender)(uint8_t *car);
void (*orig_RegisterCorona)(uint32_t, uint8_t, uint8_t, uint8_t, uint8_t, const CVector &, float, float, uint8_t, int8_t,
                            uint8_t, uint8_t, uint8_t, float, bool, float);

// A light's triangle, as its atomic has it (lifted off the surface).
struct Tri {
	V3 p[3];
	float u[3], v[3];
	uint16_t material;
	bool tail, left;
};
struct Lights {
	int model = -1, triangles = 0; // what it was found for (a freed geometry's address comes back)
	std::vector<Tri> tris;
};
std::unordered_map<const void *, Lights> gLights; // by geometry
std::unordered_set<int> gModelsWithTail;          // models whose tail lights glow
bool gTried;
void *gShader;
const uint8_t *gPreRendering; // the car CAutomobile::PreRender is on
// The cars drawn this frame with a light on, glowing once everything is in
// the depth buffer: a car's body (with its windows) is drawn last, among the
// see-through things, so drawn straight after CAutomobile::Render the glow
// would be painted over.
constexpr size_t kMaxQueued = 64;
std::vector<uint8_t *> gQueued;
uint32_t gQueuedFrame;
uint16_t gIndices[kMaxVertices];
Im3DVertex gVertices[kMaxVertices];

V3 Sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Normal(V3 a)
{
	const float l = std::sqrt(Dot(a, a));
	return l > 1e-9f ? V3{a.x / l, a.y / l, a.z / l} : V3{0, 0, 0};
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
bool Near(V3 c, const CVector &light)
{
	return std::fabs(std::fabs(c.x) - std::fabs(light.x)) < kAcross && std::fabs(c.z - light.z) < kUpDown &&
	       std::fabs(c.y - light.y) < kAlong;
}

const uint8_t *ModelInfo(const uint8_t *car)
{
	const int model = *reinterpret_cast<const int16_t *>(car + kModelIndex);
	return model >= 0 ? api.modelInfoPtrs[model] : nullptr;
}

// The light triangles of an atomic's geometry, found with the car as it is.
const Lights &Find(void *atomic, const void *geometry, const uint8_t *car, int model)
{
	Lights &found = gLights[geometry];
	const int count = rw.NumTriangles(geometry);
	if (found.model == model && found.triangles == count)
		return found;
	found = Lights{model, count, {}};
	void *morph = rw.MorphTarget(geometry, 0);
	const V3 *verts = morph ? rw.Vertices(morph) : nullptr;
	const float *uv = rw.TexCoords(geometry, kTexCoords0);
	const Triangle *tris = rw.Triangles(geometry);
	const uint8_t *mi = ModelInfo(car);
	if (!verts || !uv || !tris || !mi)
		return found;
	const CVector &head = *reinterpret_cast<const CVector *>(mi + kHeadlights);
	const CVector &tail = *reinterpret_cast<const CVector *>(mi + kTaillights);
	const float *ltm = rw.FrameLTM(rw.AtomicFrame(atomic));
	for (int i = 0; i < count; i++) {
		const Triangle &t = tris[i];
		void *material = rw.Material(geometry, t.material);
		const uint8_t *colour = material ? rw.MaterialColour(material) : nullptr;
		if (!material || !rw.MaterialTexture(material) || !colour || colour[0] < 250 || colour[1] < 250 || colour[2] < 250)
			continue;
		const V3 a = verts[t.v[0]], b = verts[t.v[1]], c = verts[t.v[2]];
		const V3 A = InCar(car, Apply(ltm, a)), B = InCar(car, Apply(ltm, b)), C = InCar(car, Apply(ltm, c));
		const V3 n = Normal(Cross(Sub(B, A), Sub(C, A)));
		const V3 centre{(A.x + B.x + C.x) / 3, (A.y + B.y + C.y) / 3, (A.z + B.z + C.z) / 3};
		const bool isTail = n.y < -kFacing && Near(centre, tail), isHead = n.y > kFacing && Near(centre, head);
		if (!isTail && !isHead)
			continue;
		const V3 lift = Normal(Cross(Sub(b, a), Sub(c, a)));
		Tri out{};
		const V3 p[3] = {a, b, c};
		for (int k = 0; k < 3; k++) {
			out.p[k] = {p[k].x + lift.x * kLift, p[k].y + lift.y * kLift, p[k].z + lift.z * kLift};
			out.u[k] = uv[2 * t.v[k]];
			out.v[k] = uv[2 * t.v[k] + 1];
		}
		out.material = t.material;
		out.tail = isTail;
		out.left = centre.x < 0;
		found.tris.push_back(out);
	}
	bool anyTail = false;
	for (const Tri &t : found.tris)
		anyTail |= t.tail;
	if (anyTail)
		gModelsWithTail.insert(model);
	return found;
}

bool Close(const uint8_t *car)
{
	const CVector cam = shaders::CameraPosition();
	const V3 d = Sub(Axis(car, 0x30), V3{cam.x, cam.y, cam.z});
	return gLod0DistSq && Dot(d, d) < *gLod0DistSq;
}

bool Running(const uint8_t *car)
{
	const uint32_t status = (car[kStatus] >> 3) & 0x1f;
	return *reinterpret_cast<const int32_t *>(car + kVehicleType) == 0 && status != kStatusAbandoned &&
	       status != kStatusWrecked;
}

bool Prepare()
{
	if (!gTried) {
		gTried = true;
		gShader = shaders::Create("car lights", kVertex, kFragment);
		for (int i = 0; i < kMaxVertices; i++)
			gIndices[i] = uint16_t(i);
	}
	return gShader != nullptr;
}

// What a car's lights are doing, each 0 to 1, left and right.
struct State {
	float tail[2] = {}, reverse[2] = {}, head[2] = {}, amberRear[2] = {}, amberFront[2] = {};
	bool Any() const
	{
		for (int s = 0; s < 2; s++)
			if (tail[s] > 0 || reverse[s] > 0 || head[s] > 0 || amberRear[s] > 0 || amberFront[s] > 0)
				return true;
		return false;
	}
};

const uint8_t *gSignalCar; // the player's car, which signals
int gSignal;               // its side (turns::kLeft, kRight; 0: none)
uint32_t gSignalUntil;     // game time it keeps on after it last saw the turn

bool BlinkOn() { return *api.timeMs % kBlinkMs < kBlinkOnMs; }

bool PlayersCar(const uint8_t *car)
{
	uint8_t *ped = api.FindPlayerPed();
	return ped && PedInVehicle(ped) && PedVehicle(ped) == car;
}

// The side the player's car signals: the GPS route's next turn, from its
// first point ahead of the car, the way in being the car's own heading. (The
// route starts at the road node nearest the car, which nearing a junction is
// the junction itself, the route already off down the new road; and it runs
// down the road's middle, on a divided road metres to the side of the car, so
// the way from the car to it isn't the way in.)
void UpdateSignal(const uint8_t *car)
{
	gSignalCar = car;
	const uint32_t now = *api.timeMs;
	const Route *route = gps::Current();
	const V3 &pos = Axis(car, 0x30), &ahead = Axis(car, 0x10);
	const float flat = std::hypot(ahead.x, ahead.y);
	turns::Turn turn;
	if (route && flat > 0.3f) {
		const Vec2 at{pos.x, pos.y}, f{ahead.x / flat, ahead.y / flat};
		auto forward = [&](Vec2 p) { return (p.x - at.x) * f.x + (p.y - at.y) * f.y; };
		size_t k = 0;
		while (k < route->points.size() && forward(route->points[k]) <= kRouteFrom)
			k++;
		if (k <= kRouteSkipMost && k < route->points.size()) {
			const Vec2 first = route->points[k];
			const float toFirst = forward(first);
			const CVector &v = EntitySpeed(const_cast<uint8_t *>(car));
			float reach = std::clamp(std::hypot(v.x, v.y) * 50.0f * kSignalSeconds, kSignalNear, kSignalFar);
			if (gSignal)
				reach *= kSignalKeep;
			if (toFirst / std::hypot(first.x - at.x, first.y - at.y) > kRouteAlong && toFirst < reach) {
				std::vector<Vec2> path{{first.x - f.x * kTurnWindow, first.y - f.y * kTurnWindow}};
				path.insert(path.end(), route->points.begin() + long(k), route->points.end());
				turn = turns::Next(path, kTurnAngle, kTurnWindow, reach - toFirst + kTurnWindow);
				turn.distance += toFirst - kTurnWindow; // from the car
			}
		}
	}
	const int was = gSignal;
	if (turn.side) {
		gSignal = turn.side;
		gSignalUntil = now + kSignalHoldMs;
	} else if (int32_t(now - gSignalUntil) > 0) {
		gSignal = 0;
	}
	if (gSignal != was && gSignal)
		Log("carlights: signalling %s for a turn %.0f m ahead", gSignal == turns::kLeft ? "left" : "right",
		    std::max(turn.distance, 0.0f));
	else if (gSignal != was)
		Log("carlights: signal off");
}

int Signal(const uint8_t *car) { return car == gSignalCar ? gSignal : 0; }

State LightsOf(uint8_t *car)
{
	const uint8_t flags = car[kVehicleFlags];
	const bool engine = flags & kEngineOn, on = engine && (flags & kLightsOn);
	const float brake = *reinterpret_cast<const float *>(car + kBrakePedal);
	const float gas = *reinterpret_cast<const float *>(car + kGasPedal);
	const CVector &v = EntitySpeed(car);
	const bool held = *reinterpret_cast<void *const *>(car + kDriver) && engine &&
	                  std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z) * 50.0f < kStoppedSpeed && std::fabs(gas) < kPedalOff;
	const float tail = brake > 0.0f || held ? kBraking : on ? kTailOn : 0.0f;
	const float reverse = gas < 0.0f ? kReversing : 0.0f, head = on ? kHeadOn : 0.0f;
	const int signal = Signal(car);
	const bool blink = BlinkOn();
	const uint8_t *damage = car + kDamage;
	State s;
	for (int side = 0; side < 2; side++) {
		const bool rearOk = rw.LightStatus(damage, kRearLeft + side) == 0;
		const bool frontOk = rw.LightStatus(damage, kFrontLeft + side) == 0;
		const bool signals = signal == (side == 0 ? turns::kLeft : turns::kRight);
		s.tail[side] = rearOk ? (signals ? (blink ? kBraking : on ? kTailOn : 0.0f) : tail) : 0.0f;
		s.reverse[side] = rearOk ? reverse : 0.0f;
		s.head[side] = frontOk ? head : 0.0f;
		s.amberRear[side] = rearOk && signals && blink ? 1.0f : 0.0f;
		s.amberFront[side] = frontOk && signals && blink ? 1.0f : 0.0f;
	}
	return s;
}

struct Glow {
	const uint8_t *car;
	int model;
	V3 eye;
	State lights;
};

void Draw(void *atomic, const void *geometry, const std::vector<Tri> &tris, const Glow &glow)
{
	float *ltm = rw.FrameLTM(rw.AtomicFrame(atomic));
	// One batch per texture, of the triangles facing the camera.
	std::vector<uint16_t> materials;
	for (const Tri &t : tris)
		if (std::find(materials.begin(), materials.end(), t.material) == materials.end())
			materials.push_back(t.material);
	for (uint16_t material : materials) {
		int n = 0;
		for (const Tri &t : tris) {
			if (t.material != material || n + 3 > kMaxVertices)
				continue;
			const int side = t.left ? 0 : 1;
			const State &s = glow.lights;
			const float red = t.tail ? s.tail[side] : 0.0f, green = t.tail ? s.reverse[side] : 0.0f,
			            blue = t.tail ? 0.0f : s.head[side], amber = t.tail ? s.amberRear[side] : s.amberFront[side];
			if (red <= 0.0f && green <= 0.0f && blue <= 0.0f && amber <= 0.0f)
				continue;
			const V3 a = Apply(ltm, t.p[0]), b = Apply(ltm, t.p[1]), c = Apply(ltm, t.p[2]);
			if (Dot(Cross(Sub(b, a), Sub(c, a)), Sub(glow.eye, a)) <= 0.0f)
				continue; // facing away
			for (int k = 0; k < 3; k++) {
				Im3DVertex &v = gVertices[n++];
				v = {};
				v.x = t.p[k].x, v.y = t.p[k].y, v.z = t.p[k].z;
				v.r = uint8_t(red * 255.0f), v.g = uint8_t(green * 255.0f), v.b = uint8_t(blue * 255.0f);
				v.a = uint8_t(amber * 255.0f);
				v.u = t.u[k], v.v = t.v[k];
			}
		}
		void *texture = n ? rw.MaterialTexture(rw.Material(geometry, material)) : nullptr;
		if (!texture)
			continue;
		api.RwRenderStateSet(kStateTextureRaster, rw.TextureRaster(texture));
		rw.Im3DTransform(gVertices, uint32_t(n), ltm, kIm3DVertexUV);
		rw.ShaderUse(gShader); // after Im3DTransform, which takes the game's
		rw.Im3DRenderIndexed(kPrimTriList, gIndices, n);
		rw.Im3DEnd();
	}
}

void *DrawAtomic(void *atomic, void *data)
{
	const Glow &glow = *static_cast<const Glow *>(data);
	if (!(rw.AtomicFlags(atomic) & kAtomicRender))
		return atomic;
	void *frame = rw.AtomicFrame(atomic);
	const char *name = frame ? rw.FrameName(frame) : nullptr;
	if (name && std::strstr(name, "_vlo"))
		return atomic;
	const void *geometry = rw.AtomicGeometry(atomic);
	if (!geometry)
		return atomic;
	const Lights &lights = Find(atomic, geometry, glow.car, glow.model);
	if (!lights.tris.empty())
		Draw(atomic, geometry, lights.tris, glow);
	return atomic;
}

void DrawCar(uint8_t *car)
{
	const State lights = LightsOf(car);
	void *clump = *reinterpret_cast<void *const *>(car + kRwObject);
	if (!lights.Any() || !clump)
		return;
	const CVector cam = shaders::CameraPosition();
	Glow glow{car, *reinterpret_cast<const int16_t *>(car + kModelIndex), {cam.x, cam.y, cam.z}, lights};
	const shaders::SavedStates saved;
	api.RwRenderStateSet(kStateZTest, reinterpret_cast<void *>(1));
	api.RwRenderStateSet(kStateZWrite, nullptr);
	api.RwRenderStateSet(kStateVertexAlpha, reinterpret_cast<void *>(1));
	api.RwRenderStateSet(kStateSrcBlend, reinterpret_cast<void *>(kBlendOne));
	api.RwRenderStateSet(kStateDestBlend, reinterpret_cast<void *>(kBlendOne));
	api.RwRenderStateSet(kStateFog, nullptr);
	api.RwRenderStateSet(kStateTextureFilter, reinterpret_cast<void *>(kFilterLinear));
	api.RwRenderStateSet(kStateTextureAddress, reinterpret_cast<void *>(kAddressWrap));
	rw.ForAllAtomics(clump, DrawAtomic, &glow);
}

void Hook_Render(uint8_t *car)
{
	orig_Render(car);
	steering::DrawCar(car); // its steering wheel, with the car's own opaque parts
	if (!gConfig.lightGlow)
		return;
	if (gQueuedFrame != *gFrameCounter) {
		gQueued.clear();
		gQueuedFrame = *gFrameCounter;
	}
	if (PlayersCar(car)) {
		UpdateSignal(car);
	} else if (car == gSignalCar) { // he's out of it
		gSignalCar = nullptr;
		gSignal = 0;
	}
	if (Running(car) && Close(car) && gQueued.size() < kMaxQueued && LightsOf(car).Any() &&
	    std::find(gQueued.begin(), gQueued.end(), car) == gQueued.end())
		gQueued.push_back(car);
}

void Hook_SpecialFXRender()
{
	if (gQueuedFrame == *gFrameCounter && !gQueued.empty() && Prepare())
		for (uint8_t *car : gQueued)
			DrawCar(car);
	gQueued.clear();
	orig_SpecialFXRender();
}

void Hook_PreRender(uint8_t *car)
{
	gPreRendering = car;
	orig_PreRender(car);
	gPreRendering = nullptr;
}

void Hook_RegisterCorona(uint32_t id, uint8_t r, uint8_t g, uint8_t b, uint8_t a, const CVector &at, float size,
                         float drawDist, uint8_t type, int8_t flare, uint8_t reflection, uint8_t los, uint8_t streak,
                         float angle, bool useNear, float nearDist)
{
	const uint8_t *car = gPreRendering;
	if (car && id - uint32_t(uintptr_t(car)) - kTailCorona < 2 && gShader && Close(car) &&
	    gModelsWithTail.count(*reinterpret_cast<const int16_t *>(car + kModelIndex)))
		return; // its tail lights glow on the model instead
	orig_RegisterCorona(id, r, g, b, a, at, size, drawDist, type, flare, reflection, los, streak, angle, useNear, nearDist);
}

void Hook_RegisterOne(CVector pos, CVector up, CVector side, CVector front, uint8_t type, uint8_t r, uint8_t g,
                      uint8_t b)
{
	if (type >= kFirstVehicle && type <= kLastVehicle)
		return;
	orig_RegisterOne(pos, up, side, front, type, r, g, b);
}

template <typename T>
bool Bind(T &out, const char *name)
{
	out = reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY, name));
	if (!out)
		Log("carlights: missing %s", name);
	return out != nullptr;
}

Im2DCanvas gDash;

// An arrow pointing `side` (turns::kLeft, kRight), `s` its half length.
void Arrow(float cx, float cy, float s, int side, CRGBA col)
{
	const float d = float(side), neck = cx + d * s * 0.1f;
	gDash.Tri({cx + d * s, cy, 0, 0}, {neck, cy - s * 0.8f, 0, 0}, {neck, cy + s * 0.8f, 0, 0}, col);
	gDash.Quad({cx - d * s, cy - s * 0.32f}, {neck, cy - s * 0.32f}, {neck, cy + s * 0.32f}, {cx - d * s, cy + s * 0.32f}, col);
}

} // namespace

int Signalling(const uint8_t *car) { return Signal(car); }

void DrawDash()
{
	if (!gSignal || !gSignalCar || !camera::InSeat() || !PlayersCar(gSignalCar) || !BlinkOn())
		return;
	const float W = float(ScreenWidth()), H = float(ScreenHeight());
	const float s = kDashArrow * H, cx = W / 2 + float(gSignal) * kDashApart * H, cy = kDashHeight * H;
	SetHudRenderStates();
	gDash.SetTexture(nullptr);
	Arrow(cx, cy + s * 0.12f, s * 1.15f, gSignal, kDashShadow);
	Arrow(cx, cy, s, gSignal, kDashGreen);
	gDash.Flush();
}

bool Install()
{
	if (!hooks::Install("_ZN13CBrightLights11RegisterOneE7CVectorS0_S0_S0_hhhh", Hook_RegisterOne, orig_RegisterOne))
		return false;
	if (!gConfig.lightGlow) { // the steering wheels still want the car's draw
		if (gConfig.steeringWheel && !hooks::Install("_ZN11CAutomobile6RenderEv", Hook_Render, orig_Render))
			Log("carlights: hook missing (re-run install.sh); the steering wheels are the game's own");
		return true;
	}
	const bool bound = Bind(rw.ForAllAtomics, "_Z20RpClumpForAllAtomicsPN2rw5ClumpEPFPNS_6AtomicES3_PvES4_") &&
	                   Bind(rw.AtomicFlags, "_Z16RpAtomicGetFlagsPKN2rw6AtomicE") &&
	                   Bind(rw.AtomicFrame, "_Z16RpAtomicGetFramePKN2rw6AtomicE") &&
	                   Bind(rw.AtomicGeometry, "_Z19RpAtomicGetGeometryPKN2rw6AtomicE") &&
	                   Bind(rw.FrameLTM, "_Z13RwFrameGetLTMPN2rw5FrameE") &&
	                   Bind(rw.FrameName, "_Z16GetFrameNodeNamePN2rw5FrameE") &&
	                   Bind(rw.NumTriangles, "_Z25RpGeometryGetNumTrianglesPKN2rw8GeometryE") &&
	                   Bind(rw.Triangles, "_Z22RpGeometryGetTrianglesPKN2rw8GeometryE") &&
	                   Bind(rw.MorphTarget, "_Z24RpGeometryGetMorphTargetPKN2rw8GeometryEi") &&
	                   Bind(rw.Vertices, "_Z24RpMorphTargetGetVerticesPKN2rw11MorphTargetE") &&
	                   Bind(rw.TexCoords, "_Z28RpGeometryGetVertexTexCoordsPKN2rw8GeometryE24RwTextureCoordinateIndex") &&
	                   Bind(rw.Material, "_Z21RpGeometryGetMaterialPKN2rw8GeometryEi") &&
	                   Bind(rw.MaterialTexture, "_Z20RpMaterialGetTexturePKN2rw8MaterialE") &&
	                   Bind(rw.MaterialColour, "_Z18RpMaterialGetColorPKN2rw8MaterialE") &&
	                   Bind(rw.TextureRaster, "_Z18RwTextureGetRasterPKN2rw7TextureE") &&
	                   Bind(rw.Im3DTransform, "_Z15RwIm3DTransformPN2rw3gl310Im3DVertexEjPNS_6MatrixEj") &&
	                   Bind(rw.Im3DRenderIndexed, "_Z28RwIm3DRenderIndexedPrimitive15RwPrimitiveTypePti") &&
	                   Bind(rw.Im3DEnd, "_Z9RwIm3DEndv") && Bind(rw.ShaderUse, "_ZN2rw3gl36Shader3useEv") &&
	                   Bind(rw.LightStatus, "_ZN14CDamageManager14GetLightStatusE7eLights") &&
	                   Bind(gLod0DistSq, "_ZN18CVisibilityPlugins18ms_vehicleLod0DistE") &&
	                   Bind(gFrameCounter, "_ZN6CTimer14m_FrameCounterE");
	if (!bound || !shaders::Ready()) {
		Log("carlights: the glow's functions are missing; cars' lights are the game's own");
		return true;
	}
	if (!hooks::Install("_ZN10CSpecialFX6RenderEv", Hook_SpecialFXRender, orig_SpecialFXRender) ||
	    !hooks::Install("_ZN11CAutomobile6RenderEv", Hook_Render, orig_Render) ||
	    !hooks::Install("_ZN11CAutomobile9PreRenderEv", Hook_PreRender, orig_PreRender) ||
	    !hooks::Install("_ZN8CCoronas14RegisterCoronaEjhhhhRK7CVectorffhahhhfbf", Hook_RegisterCorona,
	                    orig_RegisterCorona)) {
		Log("carlights: hooks missing (re-run install.sh); cars' lights are the game's own");
		return true;
	}
	return true;
}

} // namespace carlights
