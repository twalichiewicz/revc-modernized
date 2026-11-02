#include "fpgun.h"
#include "playerreact.h"
#include "fpshoot.h"
#include "camera.h"
#include "climb.h"
#include "config.h"
#include "draw.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "shaders.h"
#include "swim.h"
#include "shootdodge.h"
#include "vmanim.h"
#include "vmarms.h"
#include "vmfit.h"
#include "vmgun.h"
#include "vmhands.h"
#include "vmpose.h"
#include "vmskin.h"
#include "vmuse.h"
#include "vmview.h"
#include "steering.h"
#include "wheelgrip.h"

#include <algorithm>
#include <cmath>
#include <dlfcn.h>
#include <map>
#include <string>
#include <vector>

using namespace game;

namespace fpgun {
namespace {

constexpr size_t kRwObject = 0x58;                                 // CEntity::m_rwObject (the clump)
constexpr int kArmTags[2][4] = {{22, 23, 24, 25}, {32, 33, 34, 35}}; // upper arm, forearm, hand, fingers
constexpr int kMaxTag = 64;
constexpr int kTexCoords0 = 1;                                     // rwTEXTURECOORDINATEINDEX0
constexpr uint32_t kIm3DVertexUV = 1, kIm3DLighting = 0x20;        // lit by the lights set for him
constexpr int kStateCull = 20, kCullNone = 1, kAddressWrap = 1;
constexpr unsigned kGLDepthBufferBit = 0x100;
constexpr int32_t kMoveSprint = 5;
constexpr float kFlashSize = 0.07f; // m: half the muzzle flash's width
// The shot's light on his hands and the gun: warm (GoldSrc's and Source's
// muzzle lights are 255/192/64), from the muzzle, gone this far from it (m).
constexpr float kLightReach = 0.55f;
constexpr uint8_t kLightColour[3] = {255, 192, 64};
constexpr size_t kWeaponModel = 0x290;                             // CPed::m_pWeaponModel: what's in his hand (an atomic)
constexpr size_t kAssocBlend = 0x28, kAssocTime = 0x30;            // CAnimBlendAssociation's blend, its time (s)
constexpr uint32_t kMeleeAnims[3] = {205, 206, 209};               // a melee weapon's swings: first, second, third
constexpr size_t kFireTime = 0x40, kFireTime2 = 0x4c;             // CWeaponInfo: when its first and second swings hit (s)
constexpr int kKnuckles = 1;
// The fists' fight moves (tFightMoves, 28 bytes each: its animation, then
// when it starts to hit, s) the knuckles' jab, hook and uppercut show:
// punches as the jab, the hook and the right elbow as the hook; kicks, knees
// and the headbutt leave his fist up.
constexpr int kFightMoves = 17;
constexpr int kFistSwing[kFightMoves + 1] = {0, 1, 0, 0, 0, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0};
const char *const kRightAnchors[vm::kDigits] = {"r_index", "r_middle", "r_ring", "r_pinky", "r_thumb"};
const char *const kLeftAnchors[vm::kDigits] = {"l_index", "l_middle", "l_ring", "l_pinky", "l_thumb"};

struct V3 {
	float x, y, z;
};
struct RwTriangle {
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
	void *(*AtomicGeometry)(const void *atomic);
	int (*NumTriangles)(const void *geometry);
	int (*NumVertices)(const void *geometry);
	const RwTriangle *(*Triangles)(const void *geometry);
	void *(*MorphTarget)(const void *geometry, int index);
	const V3 *(*Vertices)(const void *morphTarget);
	const V3 *(*Normals)(const void *morphTarget);
	const float *(*TexCoords)(const void *geometry, int index);
	void *(*Material)(const void *geometry, int index);
	void *(*MaterialTexture)(const void *material);
	void *(*TextureRaster)(const void *texture);
	void *(*GeometrySkin)(void *geometry);
	int (*NumBones)(void *skin);
	const uint32_t *(*BoneIndices)(void *skin);
	const float *(*BoneWeights)(void *skin);
	const float *(*SkinToBone)(void *skin);
	void *(*Hierarchy)(void *clump);
	int (*BoneIndex)(void *hierarchy, int tag);
	float *(*BoneMatrices)(void *hierarchy);
	void *(*Im3DTransform)(Im3DVertex *vertices, uint32_t count, float *world, uint32_t flags);
	int (*Im3DRenderIndexed)(int primitive, uint16_t *indices, int count);
	int (*Im3DEnd)();
	int (*StateGet)(int state, void *value);
} rw;
bool (*gSetupLighting)(uint8_t *ped);
void (*gRemoveLighting)(uint8_t *ped, bool reset);
void (**gClear)(unsigned mask);       // glad_glClear
void (**gDepthMask)(unsigned char on); // glad_glDepthMask
void **gFlashRaster;                    // gpGunFlashRaster: the game's muzzle flash
const uint8_t *gFightMoves;             // tFightMoves
const uint32_t *gFrameCounter;          // CTimer::m_FrameCounter
void (*orig_RenderFirstPersonVehicle)();
bool gReady;
bool gSteeringDraw;

// His arms, as built from his model.
struct Built {
	vm::ModelKey key;
	bool ok = false;
	vm::Arms arms;
	std::vector<std::vector<uint16_t>> indices, indicesLeft; // each arm's triangles, by material
};
Built gBuilt;

// A gun's viewmodel: its model, its atlas, and his hand fitted to it (for the outfit it was fitted for).
struct GunModel {
	bool ok = false;
	vm::Gun gun;
	RwRaster *atlas = nullptr;
	vm::ModelKey fittedFor;
	vm::Fit fit, fitLeft;
	int magazine = -1;
	std::vector<uint16_t> withoutMagazine;
};
std::map<int, GunModel> gGuns; // by weapon type

// A melee weapon: the game's own model in his hand (its space is his hand
// bone's), read from the atomic the game gives him.
struct HeldModel {
	const void *geometry = nullptr;
	int vertexCount = 0, triangleCount = 0;     // with it, what it was read from (another may load where it was)
	std::vector<vm::GunVertex> vertices;
	std::vector<std::vector<uint16_t>> indices; // by material
	std::vector<void *> rasters;                // by material, fetched each frame
};
HeldModel gHeld;
bool gHeldShown; // this frame's viewmodel is a melee weapon
bool gGuardOnly; // unarmed contact: both palms reach into view

vm::Anim gAnim;
vm::ShotSeen gShots;
vm::ReloadClock gReloadClock;
vm::Heartbeat gBeat;
std::vector<void *> gRasters; // his model's textures, by material, fetched each frame (a clothes change frees them)
// This frame's, worked out before he's drawn and drawn after the world.
bool gDraw;
GunModel *gGun;
vm::AnimOutput gOut;
vm::Frame gGunView;                    // the gun's frame in the view's
bool gLeftShown;                       // his left arm drawn: a two-handed gun, or reloading a magazine
std::vector<vm::Frame> gParts;         // each part where the animation has it, in the gun's frame
vm::Frame gMatrices[vm::kArmBones], gMatricesLeft[vm::kArmBones]; // his arms' skinning, into the view's frame
std::vector<Im3DVertex> gVertices;
bool gActive;     // the new viewmodel, last frame
CVector gMuzzle;  // its muzzle then, in the world
std::vector<uint16_t> gFlashIndices = {0, 1, 2, 0, 2, 3};

template <typename T>
bool Bind(T &out, const char *name)
{
	out = reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY, name));
	if (!out)
		Log("fpgun: missing %s", name);
	return out != nullptr;
}

void *FirstAtomic(void *clump)
{
	void *found = nullptr;
	rw.ForAllAtomics(
	    clump,
	    [](void *atomic, void *data) -> void * {
		    *static_cast<void **>(data) = atomic;
		    return nullptr; // the first will do: a ped is one atomic
	    },
	    &found);
	return found;
}

// His arms from his model, once for each model he has on.
bool Build(uint8_t *ped)
{
	void *clump = *reinterpret_cast<void *const *>(ped + kRwObject);
	void *atomic = clump ? FirstAtomic(clump) : nullptr;
	void *geometry = atomic ? rw.AtomicGeometry(atomic) : nullptr;
	void *hierarchy = clump ? rw.Hierarchy(clump) : nullptr;
	if (!geometry || !hierarchy)
		return false;
	void *material0 = rw.Material(geometry, 0);
	const vm::ModelKey key{geometry, material0 ? rw.MaterialTexture(material0) : nullptr, EntityModel(ped),
	                       rw.NumVertices(geometry), rw.NumTriangles(geometry)};
	if (vm::SameModel(gBuilt.key, key))
		return gBuilt.ok;
	gBuilt = Built{};
	gBuilt.key = key;
	for (auto &g : gGuns)
		g.second.fittedFor = vm::ModelKey{}; // his hand is fitted to each gun again
	void *skin = rw.GeometrySkin(geometry);
	const int vertices = rw.NumVertices(geometry), triangles = rw.NumTriangles(geometry);
	void *target = rw.MorphTarget(geometry, 0);
	const V3 *pos = target ? rw.Vertices(target) : nullptr, *normal = target ? rw.Normals(target) : nullptr;
	const float *uv = rw.TexCoords(geometry, kTexCoords0);
	const RwTriangle *tris = rw.Triangles(geometry);
	const int bones = skin ? rw.NumBones(skin) : 0;
	if (!skin || vertices <= 0 || triangles <= 0 || !pos || !normal || !uv || !tris || bones <= 0) {
		Log("fpgun: model %d has no skinned mesh to read (skin %p, %d vertices, normals %p, UVs %p); first person keeps "
		    "the old viewmodel",
		    key.model, skin, vertices, static_cast<const void *>(normal), static_cast<const void *>(uv));
		return false;
	}
	const uint8_t *index = reinterpret_cast<const uint8_t *>(rw.BoneIndices(skin));
	const float *weight = rw.BoneWeights(skin), *toBone = rw.SkinToBone(skin);
	vm::SkinnedMesh mesh;
	mesh.tags.assign(size_t(bones), -1);
	for (int tag = 0; tag < kMaxTag; tag++) {
		const int b = rw.BoneIndex(hierarchy, tag);
		if (b >= 0 && b < bones)
			mesh.tags[size_t(b)] = tag;
	}
	for (int b = 0; b < bones; b++)
		mesh.bind.push_back(vm::Inverse(vm::FromRw(toBone + 16 * b)));
	mesh.vertices.resize(size_t(vertices));
	for (int i = 0; i < vertices; i++) {
		vm::SkinVertex &v = mesh.vertices[size_t(i)];
		v.pos = {pos[i].x, pos[i].y, pos[i].z};
		v.normal = {normal[i].x, normal[i].y, normal[i].z};
		v.u = uv[i * 2];
		v.v = uv[i * 2 + 1];
		for (int k = 0; k < 4; k++) {
			v.bone[k] = index[i * 4 + k] < bones ? index[i * 4 + k] : 0;
			v.weight[k] = weight[i * 4 + k];
		}
	}
	int materials = 1;
	for (int i = 0; i < triangles; i++) {
		mesh.triangles.push_back({{tris[i].v[0], tris[i].v[1], tris[i].v[2]}, tris[i].material});
		materials = std::max(materials, tris[i].material + 1);
	}
	gBuilt.arms = vm::Articulate(mesh);
	if (!gBuilt.arms.ok) {
		Log("fpgun: model %d's %s; first person keeps the old viewmodel", key.model, gBuilt.arms.why.c_str());
		return false;
	}
	gBuilt.indices.assign(size_t(materials), {});
	gBuilt.indicesLeft.assign(size_t(materials), {});
	for (const vm::Triangle &t : gBuilt.arms.right.triangles)
		for (uint16_t v : t.v)
			gBuilt.indices[std::min<size_t>(t.material, size_t(materials) - 1)].push_back(v);
	for (const vm::Triangle &t : gBuilt.arms.left.triangles)
		for (uint16_t v : t.v)
			gBuilt.indicesLeft[std::min<size_t>(t.material, size_t(materials) - 1)].push_back(v);
	gBuilt.ok = true;
	Log("fpgun: his arms from model %d: %zu vertices in the right, its fingers and thumb jointed", key.model,
	    gBuilt.arms.right.vertices.size());
	return true;
}

// A gun's viewmodel, loaded the first time it's wanted and fitted to his hand.
GunModel *GunFor(int weapon)
{
	auto it = gGuns.find(weapon);
	if (it == gGuns.end()) {
		GunModel g;
		const vm::Embedded *e = vm::FindEmbedded(weapon);
		std::string why;
		if (e && vm::ParseGun(e->begin, size_t(e->end - e->begin), g.gun, why)) {
			g.atlas = CreateRaster(g.gun.atlasWidth, g.gun.atlasHeight, g.gun.atlasRgba);
			g.magazine = vm::FindPart(g.gun, "magazine");
			for (size_t i = 0; i + 2 < g.gun.indices.size(); i += 3) {
				const uint16_t *t = &g.gun.indices[i];
				if (g.magazine < 0 || int(g.gun.vertices[t[0]].part) != g.magazine)
					g.withoutMagazine.insert(g.withoutMagazine.end(), t, t + 3);
			}
			g.ok = g.atlas != nullptr;
		}
		Log(g.ok ? "fpgun: %s loaded" : "fpgun: %s didn't load (%s)", e ? e->name : "?", why.c_str());
		it = gGuns.emplace(weapon, std::move(g)).first;
	}
	GunModel &g = it->second;
	if (!g.ok)
		return nullptr;
	if (!vm::SameModel(g.fittedFor, gBuilt.key)) {
		g.fit = vm::FitHand(gBuilt.arms.right, g.gun, kRightAnchors, false);
		if (g.gun.twoHanded)
			g.fitLeft = vm::FitHand(gBuilt.arms.left, g.gun, kLeftAnchors, true);
		g.fittedFor = gBuilt.key;
		Log("fpgun: his hand fitted to weapon %d: %d %d %d %d %d of each finger in it", weapon, g.fit.inside[0],
		    g.fit.inside[1], g.fit.inside[2], g.fit.inside[3], g.fit.inside[4]);
	}
	return &g;
}

// The melee weapon in his hand, read from the game's model of it (again when it changes).
bool LoadHeld(uint8_t *ped)
{
	void *atomic = *reinterpret_cast<void *const *>(ped + kWeaponModel);
	void *geometry = atomic ? rw.AtomicGeometry(atomic) : nullptr;
	if (!geometry)
		return false;
	const int vertices = rw.NumVertices(geometry), triangles = rw.NumTriangles(geometry);
	if (geometry != gHeld.geometry || vertices != gHeld.vertexCount || triangles != gHeld.triangleCount) {
		gHeld = HeldModel{};
		void *target = rw.MorphTarget(geometry, 0);
		const V3 *pos = target ? rw.Vertices(target) : nullptr, *normal = target ? rw.Normals(target) : nullptr;
		const float *uv = rw.TexCoords(geometry, kTexCoords0);
		const RwTriangle *tris = rw.Triangles(geometry);
		if (!pos || !uv || !tris || vertices <= 0 || vertices > 65535 || triangles <= 0)
			return false;
		gHeld.vertices.resize(size_t(vertices));
		for (int i = 0; i < vertices; i++)
			gHeld.vertices[size_t(i)] = {{pos[i].x, pos[i].y, pos[i].z}, {0, 0, 0}, uv[i * 2], uv[i * 2 + 1], 0};
		for (int i = 0; i < triangles; i++) {
			const RwTriangle &t = tris[i];
			if (t.v[0] >= vertices || t.v[1] >= vertices || t.v[2] >= vertices)
				continue;
			if (gHeld.indices.size() <= t.material)
				gHeld.indices.resize(size_t(t.material) + 1);
			gHeld.indices[t.material].insert(gHeld.indices[t.material].end(), {t.v[0], t.v[1], t.v[2]});
			if (!normal) { // none in the model: each face's, shared out
				vm::GunVertex *v[3] = {&gHeld.vertices[t.v[0]], &gHeld.vertices[t.v[1]], &gHeld.vertices[t.v[2]]};
				const vm::Vec3 n = vm::Cross(vm::Sub(v[1]->pos, v[0]->pos), vm::Sub(v[2]->pos, v[0]->pos));
				for (vm::GunVertex *x : v)
					x->normal = vm::Add(x->normal, n);
			}
		}
		for (int i = 0; i < vertices; i++)
			gHeld.vertices[size_t(i)].normal =
			    normal ? vm::Vec3{normal[i].x, normal[i].y, normal[i].z} : vm::Normal(gHeld.vertices[size_t(i)].normal);
		gHeld.geometry = geometry;
		gHeld.vertexCount = vertices;
		gHeld.triangleCount = triangles;
		Log("fpgun: a melee weapon's model in his hand: %d vertices, %zu materials", vertices, gHeld.indices.size());
	}
	gHeld.rasters.assign(gHeld.indices.size(), nullptr);
	for (size_t m = 0; m < gHeld.rasters.size(); m++) {
		void *material = rw.Material(geometry, int(m)), *texture = material ? rw.MaterialTexture(material) : nullptr;
		gHeld.rasters[m] = texture ? rw.TextureRaster(texture) : nullptr;
	}
	return true;
}

// His melee weapon's swing as the game has it: its attack animation (the
// most blended in), how far in it is, and when it hits.
vm::Attack MeleeAttack(uint8_t *ped, int type)
{
	vm::Attack attack;
	uint8_t *clump = EntityClump(ped);
	const uint8_t *info = api.GetWeaponInfo ? api.GetWeaponInfo(type) : nullptr;
	if (!clump || !info || !api.GetAssociation)
		return attack;
	float most = 0.0f;
	auto consider = [&](uint32_t anim, int which, float strike) {
		const uint8_t *assoc = which > 0 ? api.GetAssociation(clump, anim) : nullptr;
		const float blend = assoc ? *reinterpret_cast<const float *>(assoc + kAssocBlend) : 0.0f;
		if (blend > most) {
			most = blend;
			attack.which = which;
			attack.time = *reinterpret_cast<const float *>(assoc + kAssocTime);
			attack.strike = strike > 2.0f ? strike / 30.0f : strike; // in frames after all
		}
	};
	if (type == kKnuckles) { // a fist's fight moves
		for (int move = 1; gFightMoves && move <= kFightMoves; move++) {
			const uint8_t *m = gFightMoves + size_t(move) * 28;
			consider(uint32_t(*reinterpret_cast<const int32_t *>(m)), kFistSwing[move], *reinterpret_cast<const float *>(m + 4));
		}
		return attack;
	}
	const float first = *reinterpret_cast<const float *>(info + kFireTime), second = *reinterpret_cast<const float *>(info + kFireTime2);
	for (int i = 0; i < 3; i++)
		consider(kMeleeAnims[i], i + 1, i == 0 ? first : second);
	return attack;
}

// His real arms shrunk to their shoulders (the gun in his hand with them).
void HideRealArms(uint8_t *ped)
{
	void *clump = *reinterpret_cast<void *const *>(ped + kRwObject);
	void *hierarchy = clump ? rw.Hierarchy(clump) : nullptr;
	if (!hierarchy)
		return;
	float *matrices = rw.BoneMatrices(hierarchy);
	for (const auto &arm : kArmTags) {
		const int shoulder = rw.BoneIndex(hierarchy, arm[0]);
		if (shoulder < 0)
			continue;
		const float at[3] = {matrices[shoulder * 16 + 12], matrices[shoulder * 16 + 13], matrices[shoulder * 16 + 14]};
		for (int tag : arm) {
			const int b = rw.BoneIndex(hierarchy, tag);
			if (b < 0)
				continue;
			float *m = matrices + b * 16;
			for (int row = 0; row < 3; row++)
				for (int col = 0; col < 3; col++)
					m[row * 4 + col] *= 0.001f;
			for (int col = 0; col < 3; col++)
				m[12 + col] = at[col];
		}
	}
}

void Vertex(Im3DVertex &out, vm::Vec3 p, vm::Vec3 n, float u, float v, uint8_t alpha = 255)
{
	out = {p.x, p.y, p.z, n.x, n.y, n.z, 255, 255, 255, alpha, u, v};
}

void Render(uint16_t *indices, size_t count, uint32_t flags)
{
	if (!count)
		return;
	float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	rw.Im3DTransform(gVertices.data(), uint32_t(gVertices.size()), identity, flags);
	rw.Im3DRenderIndexed(kPrimTriList, indices, int(count));
	rw.Im3DEnd();
}

// The eye's frame (x right, y ahead, z up) from librw's view matrix (its rows
// are the screen's right, up and ahead), and the projection's tan(fov/2) up and down.
bool Eye(vm::Frame &eye, float &tanHalf)
{
	float proj[16], view[16];
	if (!shaders::CameraMatrices(proj, view) || proj[5] == 0.0f)
		return false;
	const vm::Vec3 right{view[0], view[4], view[8]}, up{view[1], view[5], view[9]};
	vm::Vec3 ahead{view[2], view[6], view[10]};
	const vm::Vec3 t{view[12], view[13], view[14]};
	const vm::Vec3 camera = *reinterpret_cast<const vm::Vec3 *>(api.theCamera + 0x10); // where the game's camera looks
	if (vm::Dot(ahead, camera) < 0.0f) {
		static bool told;
		if (!told)
			Log("fpgun: librw's view looks along -z here; turned round");
		told = true;
		ahead = vm::Mul(ahead, -1.0f);
	}
	eye = {right, ahead, up, vm::Mul(vm::Add(vm::Add(vm::Mul(right, t.x), vm::Mul(up, t.y)), vm::Mul({view[2], view[6], view[10]}, t.z)), -1.0f)};
	tanHalf = 1.0f / std::fabs(proj[5]);
	return true;
}

// How much the shot's light (`light` 0..1, from `from`) lights a point facing `normal`, all in the view.
float Lit(vm::Vec3 p, vm::Vec3 normal, vm::Vec3 from, float light)
{
	const vm::Vec3 to = vm::Sub(from, p);
	const float d = vm::Length(to);
	const float fall = std::max(0.0f, 1.0f - d / kLightReach);
	const float facing = d > 1e-4f ? std::max(0.0f, vm::Dot(normal, vm::Mul(to, 1.0f / d))) : 1.0f;
	return light * fall * fall * (0.35f + 0.65f * facing);
}

void Glow(Im3DVertex &v, float lit)
{
	const float k = std::min(1.0f, lit);
	v.r = uint8_t(kLightColour[0] * k);
	v.g = uint8_t(kLightColour[1] * k);
	v.b = uint8_t(kLightColour[2] * k);
}

// `light` > 0: the shot's light only, added over what's drawn (unlit, its colour the light's).
void DrawArm(const vm::Arm &arm, const vm::Frame *matrices, std::vector<std::vector<uint16_t>> &indices, const vm::Frame &eye,
             float squash, float light = 0.0f, vm::Vec3 from = {})
{
	std::vector<vm::Vec3> pos, normal;
	vm::Skin(arm.vertices, matrices, pos, normal);
	if (!gSteeringDraw && &arm == &gBuilt.arms.right && gGun && vm::FindPart(gGun->gun, "pump") >= 0)
		vm::ClearArmNearPlane(arm, pos, 0.15f / vm::kPushOut, std::tan(110.0f * 3.14159265f / 360.0f));
	gVertices.resize(pos.size());
	for (size_t i = 0; i < pos.size(); i++) {
		Vertex(gVertices[i], gSteeringDraw ? pos[i] : vm::ToWorld(eye, pos[i], squash),
		       gSteeringDraw ? normal[i] : vm::ToWorldDirection(eye, normal[i]), arm.vertices[i].u, arm.vertices[i].v);
		if (light > 0.0f)
			Glow(gVertices[i], Lit(pos[i], normal[i], from, light));
	}
	for (size_t m = 0; m < indices.size(); m++) {
		api.RwRenderStateSet(kStateTextureRaster, m < gRasters.size() ? gRasters[m] : nullptr);
		Render(indices[m].data(), indices[m].size(), light > 0.0f ? kIm3DVertexUV : kIm3DVertexUV | kIm3DLighting);
	}
}

// The gun, its parts where the animation has them (`light` as DrawArm's).
void DrawGun(const vm::Frame &eye, float squash, float light = 0.0f, vm::Vec3 from = {})
{
	const vm::Gun &gun = gGun->gun;
	gVertices.resize(gun.vertices.size());
	for (size_t i = 0; i < gun.vertices.size(); i++) {
		const vm::GunVertex &v = gun.vertices[i];
		const vm::Frame &part = gParts[v.part];
		const vm::Vec3 p = vm::Apply(gGunView, vm::Apply(part, v.pos));
		const vm::Vec3 n = vm::Rotate(gGunView, vm::Rotate(part, v.normal));
		Vertex(gVertices[i], vm::ToWorld(eye, p, squash), vm::ToWorldDirection(eye, n), v.u, v.v);
		if (light > 0.0f)
			Glow(gVertices[i], Lit(p, n, from, light));
	}
	api.RwRenderStateSet(kStateTextureRaster, gGun->atlas);
	std::vector<uint16_t> &indices = gOut.magazineShown ? gGun->gun.indices : gGun->withoutMagazine;
	Render(indices.data(), indices.size(), light > 0.0f ? kIm3DVertexUV : kIm3DVertexUV | kIm3DLighting);
}

// A melee weapon: the game's own model, in his hand.
void DrawHeld(const vm::Frame &eye, float squash)
{
	gVertices.resize(gHeld.vertices.size());
	for (size_t i = 0; i < gHeld.vertices.size(); i++) {
		const vm::GunVertex &v = gHeld.vertices[i];
		Vertex(gVertices[i], vm::ToWorld(eye, vm::Apply(gGunView, v.pos), squash),
		       vm::ToWorldDirection(eye, vm::Rotate(gGunView, v.normal)), v.u, v.v);
	}
	for (size_t m = 0; m < gHeld.indices.size(); m++) {
		api.RwRenderStateSet(kStateTextureRaster, gHeld.rasters[m]);
		Render(gHeld.indices[m].data(), gHeld.indices[m].size(), kIm3DVertexUV | kIm3DLighting);
	}
}

void Draw()
{
	if (gSteeringDraw) {
		uint8_t *ped = api.FindPlayerPed();
		if (!ped || !camera::InSeat()) return;
		shaders::SavedStates saved;
		intptr_t cull = 0; rw.StateGet(kStateCull, &cull);
		const bool reset = gSetupLighting(ped);
		api.RwRenderStateSet(kStateZTest, reinterpret_cast<void *>(1));
		api.RwRenderStateSet(kStateZWrite, reinterpret_cast<void *>(1));
		api.RwRenderStateSet(kStateVertexAlpha, nullptr);
		api.RwRenderStateSet(kStateCull, reinterpret_cast<void *>(kCullNone));
		// Cabin hands share the world's depth and FOV, so fingers wrap the
		// wheel and remain occluded by it. No viewmodel depth-clear/push-out.
		DrawArm(gBuilt.arms.right, gMatrices, gBuilt.indices, vm::Identity(), 1.0f);
		DrawArm(gBuilt.arms.left, gMatricesLeft, gBuilt.indicesLeft, vm::Identity(), 1.0f);
		gRemoveLighting(ped, reset);
		api.RwRenderStateSet(kStateCull, reinterpret_cast<void *>(cull));
		return;
	}
	vm::Frame eye;
	float tanHalf;
	if ((!gGun && !gHeldShown && !gGuardOnly) || !Eye(eye, tanHalf))
		return;
	const float squash = vm::FovSquash(tanHalf, gConfig.viewmodelFov);
	uint8_t *ped = api.FindPlayerPed();
	shaders::SavedStates saved;
	intptr_t cull = 0;
	rw.StateGet(kStateCull, &cull);
	const bool reset = ped && gSetupLighting(ped);
	api.RwRenderStateSet(kStateZTest, reinterpret_cast<void *>(1));
	api.RwRenderStateSet(kStateZWrite, reinterpret_cast<void *>(1));
	api.RwRenderStateSet(kStateFog, nullptr);
	api.RwRenderStateSet(kStateVertexAlpha, nullptr);
	api.RwRenderStateSet(kStateSrcBlend, reinterpret_cast<void *>(kBlendSrcAlpha)); // as an atomic draws, whatever the last pass left
	api.RwRenderStateSet(kStateDestBlend, reinterpret_cast<void *>(kBlendInvSrcAlpha));
	api.RwRenderStateSet(kStateCull, reinterpret_cast<void *>(kCullNone));
	api.RwRenderStateSet(kStateTextureFilter, reinterpret_cast<void *>(kFilterLinear));
	api.RwRenderStateSet(kStateTextureAddress, reinterpret_cast<void *>(kAddressWrap));
	(*gDepthMask)(1);
	(*gClear)(kGLDepthBufferBit); // nothing of the world in front of it

	// His arms (the left for a two-handed gun, or reloading).
	DrawArm(gBuilt.arms.right, gMatrices, gBuilt.indices, eye, squash);
	if (gLeftShown)
		DrawArm(gBuilt.arms.left, gMatricesLeft, gBuilt.indicesLeft, eye, squash);
	if (gHeldShown)
		DrawHeld(eye, squash);
	else if (!gGuardOnly)
		DrawGun(eye, squash);
	if (ped)
		gRemoveLighting(ped, reset);
	if (gHeldShown || gGuardOnly) {
		api.RwRenderStateSet(kStateCull, reinterpret_cast<void *>(cull));
		return;
	}

	const vm::Gun &gun = gGun->gun;
	const vm::Vec3 muzzle = vm::Apply(gGunView, gun.muzzle);
	// The shot lights his hands and the gun: the same again, added over, in its light.
	if (gOut.light > 0.0f) {
		api.RwRenderStateSet(kStateZWrite, nullptr);
		api.RwRenderStateSet(kStateVertexAlpha, reinterpret_cast<void *>(1));
		api.RwRenderStateSet(kStateSrcBlend, reinterpret_cast<void *>(kBlendSrcAlpha));
		api.RwRenderStateSet(kStateDestBlend, reinterpret_cast<void *>(kBlendOne));
		DrawArm(gBuilt.arms.right, gMatrices, gBuilt.indices, eye, squash, gOut.light, muzzle);
		if (gLeftShown)
			DrawArm(gBuilt.arms.left, gMatricesLeft, gBuilt.indicesLeft, eye, squash, gOut.light, muzzle);
		DrawGun(eye, squash, gOut.light, muzzle);
	}
	// The flash at the muzzle: the game's own, facing the eye, turned and sized for this shot.
	if (gOut.flash > 0.0f && gOut.flashSize > 0.0f && gFlashRaster && *gFlashRaster) {
		const float turn = gOut.flashTurn, size = kFlashSize * gOut.flashSize;
		const vm::Vec3 a{std::cos(turn) * size, 0.0f, std::sin(turn) * size}, b{-a.z, 0.0f, a.x};
		const vm::Vec3 corners[4] = {vm::Sub(vm::Sub(muzzle, a), b), vm::Sub(vm::Add(muzzle, a), b),
		                             vm::Add(vm::Add(muzzle, a), b), vm::Add(vm::Sub(muzzle, a), b)};
		const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
		gVertices.resize(4);
		for (int i = 0; i < 4; i++)
			Vertex(gVertices[size_t(i)], vm::ToWorld(eye, corners[i], squash), vm::Mul(eye.y, -1.0f), uv[i][0], uv[i][1],
			       uint8_t(255.0f * gOut.flash));
		api.RwRenderStateSet(kStateZWrite, nullptr);
		api.RwRenderStateSet(kStateVertexAlpha, reinterpret_cast<void *>(1));
		api.RwRenderStateSet(kStateSrcBlend, reinterpret_cast<void *>(kBlendSrcAlpha));
		api.RwRenderStateSet(kStateDestBlend, reinterpret_cast<void *>(kBlendOne));
		api.RwRenderStateSet(kStateTextureRaster, *gFlashRaster);
		Render(gFlashIndices.data(), gFlashIndices.size(), kIm3DVertexUV);
	}
	api.RwRenderStateSet(kStateCull, reinterpret_cast<void *>(cull));
}

void Hook_RenderFirstPersonVehicle()
{
	orig_RenderFirstPersonVehicle();
	if (gDraw)
		Draw();
	gDraw = false;
}

} // namespace

bool Install()
{
	gReady = Bind(rw.ForAllAtomics, "_Z20RpClumpForAllAtomicsPN2rw5ClumpEPFPNS_6AtomicES3_PvES4_") &&
	         Bind(rw.AtomicGeometry, "_Z19RpAtomicGetGeometryPKN2rw6AtomicE") &&
	         Bind(rw.NumTriangles, "_Z25RpGeometryGetNumTrianglesPKN2rw8GeometryE") &&
	         Bind(rw.NumVertices, "_Z24RpGeometryGetNumVerticesPKN2rw8GeometryE") &&
	         Bind(rw.Triangles, "_Z22RpGeometryGetTrianglesPKN2rw8GeometryE") &&
	         Bind(rw.MorphTarget, "_Z24RpGeometryGetMorphTargetPKN2rw8GeometryEi") &&
	         Bind(rw.Vertices, "_Z24RpMorphTargetGetVerticesPKN2rw11MorphTargetE") &&
	         Bind(rw.Normals, "_Z29RpMorphTargetGetVertexNormalsPKN2rw11MorphTargetE") &&
	         Bind(rw.TexCoords, "_Z28RpGeometryGetVertexTexCoordsPKN2rw8GeometryE24RwTextureCoordinateIndex") &&
	         Bind(rw.Material, "_Z21RpGeometryGetMaterialPKN2rw8GeometryEi") &&
	         Bind(rw.MaterialTexture, "_Z20RpMaterialGetTexturePKN2rw8MaterialE") &&
	         Bind(rw.TextureRaster, "_Z18RwTextureGetRasterPKN2rw7TextureE") &&
	         Bind(rw.GeometrySkin, "_Z21RpSkinGeometryGetSkinPN2rw8GeometryE") &&
	         Bind(rw.NumBones, "_Z17RpSkinGetNumBonesPN2rw4SkinE") &&
	         Bind(rw.BoneIndices, "_Z26RpSkinGetVertexBoneIndicesPN2rw4SkinE") &&
	         Bind(rw.BoneWeights, "_Z26RpSkinGetVertexBoneWeightsPN2rw4SkinE") &&
	         Bind(rw.SkinToBone, "_Z27RpSkinGetSkinToBoneMatricesPN2rw4SkinE") &&
	         Bind(rw.Hierarchy, "_Z29GetAnimHierarchyFromSkinClumpPN2rw5ClumpE") &&
	         Bind(rw.BoneIndex, "_Z17RpHAnimIDGetIndexPN2rw14HAnimHierarchyEi") &&
	         Bind(rw.BoneMatrices, "_Z30RpHAnimHierarchyGetMatrixArrayPN2rw14HAnimHierarchyE") &&
	         Bind(rw.Im3DTransform, "_Z15RwIm3DTransformPN2rw3gl310Im3DVertexEjPNS_6MatrixEj") &&
	         Bind(rw.Im3DRenderIndexed, "_Z28RwIm3DRenderIndexedPrimitive15RwPrimitiveTypePti") &&
	         Bind(rw.Im3DEnd, "_Z9RwIm3DEndv") && Bind(rw.StateGet, "_Z16RwRenderStateGet13RwRenderStatePv") &&
	         Bind(gSetupLighting, "_ZN4CPed13SetupLightingEv") && Bind(gRemoveLighting, "_ZN4CPed14RemoveLightingEb") &&
	         Bind(gClear, "glad_glClear") && Bind(gDepthMask, "glad_glDepthMask") &&
	         Bind(gFlashRaster, "gpGunFlashRaster") && Bind(gFrameCounter, "_ZN6CTimer14m_FrameCounterE") &&
	         shaders::Ready();
	if (!gReady) {
		Log("fpgun: functions missing; first person keeps the old viewmodel");
		return false;
	}
	gFightMoves = static_cast<const uint8_t *>(dlsym(RTLD_MAIN_ONLY, "tFightMoves"));
	if (!gFightMoves)
		Log("fpgun: no tFightMoves; brass knuckles stay up through his punches");
	if (!hooks::Install("_ZN9CRenderer24RenderFirstPersonVehicleEv", Hook_RenderFirstPersonVehicle,
	                    orig_RenderFirstPersonVehicle)) {
		Log("fpgun: no RenderFirstPersonVehicle hook (re-run install.sh); first person keeps the old viewmodel");
		gReady = false;
		return false;
	}
	return true;
}

// One of his hands where vmhands has it, its fingers on their anchors as
// they're fitted (moving with their parts, and with the hand as it leaves its
// hold); none reaching while it holds the magazine.
void PoseHand(const vm::HandPlace &place, const vm::Fit &fit, bool left, vm::Frame *out)
{
	vm::Vec3 anchors[vm::kDigits], bends[vm::kDigits];
	bool reach[vm::kDigits];
	for (int i = 0; i < vm::kDigits; i++) {
		reach[i] = place.reaching && fit.reach[i];
		const vm::Frame &part = fit.part[i] < gParts.size() ? gParts[fit.part[i]] : gParts[0];
		anchors[i] = vm::Apply(place.anchors, vm::Apply(gGunView, vm::Apply(part, fit.anchor[i])));
		bends[i] = vm::Rotate(place.anchors, vm::Rotate(gGunView, fit.bend[i]));
	}
	vm::Pose(left ? gBuilt.arms.left : gBuilt.arms.right, place.hand, anchors, reach, place.angles, left, out, bends);
}

namespace {
struct ArmEnvelope {
	vm::Vec3 shoulder{}, elbow{};
	float upper=0, fore=0, minimum=0, maximum=0;
	bool shown=false;
};
ArmEnvelope Envelope(const vm::Arm &arm,const vm::HandPlace &native,bool left)
{
	ArmEnvelope result;
	result.shown=native.shown;
	if(!result.shown)return result;
	result.upper=vm::Length(vm::Sub(arm.bind[vm::kUpper].pos,arm.bind[vm::kFore].pos));
	result.fore=vm::Length(vm::Sub(arm.bind[vm::kFore].pos,arm.bind[vm::kHand].pos));
	vm::Vec3 fore,upper;
	vm::ArmDirections(native.hand,native.angles,left,fore,upper);
	result.elbow=vm::Add(native.hand.pos,vm::Mul(fore,result.fore));
	result.shoulder=vm::Add(result.elbow,vm::Mul(upper,result.upper));
	const float nativeReach=vm::Length(vm::Sub(native.hand.pos,result.shoulder));
	// An existing animation may already use almost all its reach. Preserve that
	// pose, but never let a contact move its shoulder to manufacture extra reach.
	result.minimum=std::min(nativeReach,std::fabs(result.upper-result.fore)+.01f);
	result.maximum=std::max(nativeReach,result.upper+result.fore-.002f);
	return result;
}
bool Reaches(const ArmEnvelope &arm,vm::Vec3 hand)
{
	if(!arm.shown)return true;
	const float distance=vm::Length(vm::Sub(hand,arm.shoulder));
	return std::isfinite(distance)&&distance>=arm.minimum&&distance<=arm.maximum;
}
vm::ArmAngles AnchoredAngles(const ArmEnvelope &arm,vm::Vec3 hand,const vm::ArmAngles &native)
{
	const vm::Vec3 delta=vm::Sub(hand,arm.shoulder);
	const float distance=vm::Length(delta);
	if(distance<1e-6f||arm.upper<1e-6f||arm.fore<1e-6f)return native;
	const vm::Vec3 along=vm::Mul(delta,1/distance);
	const vm::Vec3 previous=vm::Sub(arm.elbow,arm.shoulder);
	vm::Vec3 out=vm::Normal(vm::Sub(previous,vm::Mul(along,vm::Dot(previous,along))));
	if(vm::Length(out)<.5f)out=vm::Normal(vm::Cross(along,{0,0,1}));
	if(vm::Length(out)<.5f)out=vm::Normal(vm::Cross(along,{1,0,0}));
	const float cosine=std::clamp((arm.upper*arm.upper+distance*distance-arm.fore*arm.fore)/(2*arm.upper*distance),-1.0f,1.0f);
	const auto elbow=vm::Add(arm.shoulder,vm::Add(vm::Mul(along,arm.upper*cosine),
	    vm::Mul(out,arm.upper*std::sqrt(std::max(0.0f,1-cosine*cosine)))));
	auto result=native;
	result.forearm=vm::Normal(vm::Sub(elbow,hand));
	result.upper=vm::Normal(vm::Sub(arm.shoulder,elbow));
	return result;
}
vm::Frame ContactGun(const vm::Gun &gun,const vm::Frame &native,const vm::Frame &desired,
                     const vm::Frame &kicked,const vm::ReloadHand &reload,const ArmEnvelope &right,
                     const ArmEnvelope &left)
{
	auto feasible=[&](const vm::Frame &frame){
		return Reaches(right,vm::HandFor(gun,frame).pos)&&
		       Reaches(left,vm::LeftPlace(gun,kicked,frame,gParts,gGun->fitLeft.part[vm::kDigitIndex],reload).hand.pos);
	};
	if(feasible(desired))return desired;
	// Limit the shared weapon frame, rather than pulling either hand off its
	// grip or stretching an arm. Every accepted lower bound satisfies BOTH arms.
	float low=0,high=1;
	vm::Frame accepted=native;
	for(int i=0;i<12;++i){
		const float middle=(low+high)*.5f;
		const auto candidate=vm::Blend(native,desired,middle);
		if(feasible(candidate)){low=middle;accepted=candidate;}else high=middle;
	}
	return accepted;
}
} // namespace

void CancelFrame() { gDraw = gActive = gSteeringDraw = gGuardOnly = false; }

bool PrepareSteering(uint8_t *ped)
{
	gSteeringDraw = gDraw = gActive = false;
	wheelgrip::Grip grips[2]; // right, left
	if (!gReady || !camera::InSeat() || !steering::Grips(ped, grips[1], grips[0]) || !Build(ped)) return false;
	void *clump = EntityClump(ped), *hierarchy = clump ? rw.Hierarchy(clump) : nullptr;
	float *matrices = hierarchy ? rw.BoneMatrices(hierarchy) : nullptr;
	if (!matrices) return false;
	for (int side = 0; side < 2; ++side) {
		vm::Frame posed[3];
		for (int bone = 0; bone < 3; ++bone) {
			const int index = rw.BoneIndex(hierarchy, kArmTags[side][bone]);
			if (index < 0) return false;
			posed[bone] = vm::FromRw(matrices + index*16);
		}
		const vm::Arm &arm = side ? gBuilt.arms.left : gBuilt.arms.right;
		const auto &grip = grips[side];
		const bool contact = vm::Length(vm::Sub(posed[2].pos, grip.hand.pos)) < 0.04f;
		vm::Frame hand = contact ? grip.hand : posed[2];
		hand.pos = posed[2].pos;
		vm::Vec3 anchors[vm::kDigits], bends[vm::kDigits];
		wheelgrip::Contacts(arm, grip, anchors, bends);
		bool reach[vm::kDigits]; for (auto &digit : reach) digit = contact;
		vm::ArmAngles angles;
		angles.forearm = vm::Normal(vm::Sub(posed[1].pos, posed[2].pos));
		angles.upper = vm::Normal(vm::Sub(posed[0].pos, posed[1].pos));
		vm::Pose(arm, hand, anchors, reach, angles, side != 0, side ? gMatricesLeft : gMatrices, bends);
	}
	void *atomic = FirstAtomic(clump), *geometry = atomic ? rw.AtomicGeometry(atomic) : nullptr;
	gRasters.assign(gBuilt.indices.size(), nullptr);
	for (size_t m = 0; geometry && m < gRasters.size(); ++m) {
		void *material = rw.Material(geometry, int(m)), *texture = material ? rw.MaterialTexture(material) : nullptr;
		gRasters[m] = texture ? rw.TextureRaster(texture) : nullptr;
	}
	HideRealArms(ped);
	gSteeringDraw = gDraw = true;
	gActive = false;
	return true;
}

bool Active() { return gActive; }
bool Ready() { return gActive && !gGuardOnly && gOut.ready; }
CVector Muzzle() { return gMuzzle; }

bool Prepare(uint8_t *ped)
{
	gSteeringDraw = false;
	// The drawing pass takes each frame's viewmodel; if it stops (its hook not
	// reached), his arms mustn't stay hidden with nothing drawn.
	static uint32_t frame = ~0u;
	if (gReady && *gFrameCounter == frame) { // again this frame (another PreRender): as the first time
		if (gActive)
			HideRealArms(ped);
		return gActive;
	}
	frame = gReady ? *gFrameCounter : frame;
	const bool missed = gDraw;
	gDraw = false;
	gActive = false;
	if (gReady && !gBeat.Beat(!missed)) {
		Log("fpgun: the viewmodel wasn't drawn three frames running; first person keeps the old viewmodel");
		gReady = false;
	}
	if (!gReady || !gConfig.firstPersonGuns)
		return false;
	uint8_t *weapon = PedWeapon(ped, PedCurrentWeaponSlot(ped));
	const int type = WeaponType(weapon);
	vm::Moment moment;
	moment.firstPersonOnFoot = camera::FirstPersonOnFoot();
	moment.enabled = true;
	const bool melee = vm::IsMelee(type) && *reinterpret_cast<void *const *>(ped + kWeaponModel);
	moment.covered = vm::FindEmbedded(type) != nullptr || melee;
	moment.pedState = PedState(ped);
	moment.swimming = swim::Swimming();
	moment.climbing = climb::Active();
	moment.dodging = shootdodge::Posing(); // its landing's get-up too
	moment.armsOk = moment.covered && Build(ped);
	const vm::Use use = vm::Choose(moment);

	// The animation runs on game time (bullet time slows it).
	const int32_t clip = *reinterpret_cast<const int32_t *>(weapon + 0x8);
	vm::AnimInput in{};
	in.weapon = type;
	in.covered = use == vm::Use::kNew;
	in.clip = clip;
	in.reloading = *reinterpret_cast<const int32_t *>(weapon + 4) == kWeaponReloading;
	in.reloadProgress = gReloadClock.Progress(in.reloading, *api.timeMs, *reinterpret_cast<const uint32_t *>(weapon + 0x10));
	in.sprinting = PedMoveState(ped) == kMoveSprint && !fpshoot::Shooting(); // up to fire from a sprint
	in.triggerHeld = fpshoot::TriggerHeld();
	in.interval = fpshoot::Interval(type);
	in.shot = gShots.Update(type, clip, *gFrameCounter); // frame to frame: not on coming back to first person
	in.dt = *api.timeStep / 50.0f;
	in.stepPhase = camera::StepPhase(); // his steps: the gun bobs with them, and swings with them sprinting
	in.stepSpeed = camera::StepSpeed();
	in.aimed = camera::AimBlend();
	in.attack = melee ? MeleeAttack(ped, type) : vm::Attack{};
	gOut = gAnim.Update(in);
	gHeldShown = gGuardOnly = false;
	gGun = nullptr;
	// Fists have no weapon model: show his own articulated palms for a real contact.
	const auto guard = playerreact::Current(ped);
	if (type == 0 && guard.weight > 0 && moment.firstPersonOnFoot && !moment.swimming &&
	    !moment.climbing && !moment.dodging && Build(ped)) {
		float proj[16], view[16];
		if (!shaders::CameraMatrices(proj, view)) return false;
		const vm::Vec3 anchors[vm::kDigits] = {};
		const bool reach[vm::kDigits] = {};
		for (bool left : {false, true}) {
			const vm::Arm &arm = left ? gBuilt.arms.left : gBuilt.arms.right;
			auto hand = playerreact::GuardHand(ped, left);
			vm::ArmAngles angles;
			const vm::Vec3 shoulder{left?-.19f:.19f,-.06f,-.24f};
			const auto reached = playerreact::ProtectiveReach(shoulder,
			    vm::Length(vm::Sub(arm.bind[vm::kFore].pos, arm.bind[vm::kUpper].pos)),
			    vm::Length(vm::Sub(arm.bind[vm::kHand].pos, arm.bind[vm::kFore].pos)), hand.pos,
			    {0,1,0}, {left?-.6f:.6f,.35f,-1});
			if (!reached.valid) return false;
			hand.pos = reached.wrist;
			angles.forearm = vm::Normal(vm::Sub(reached.elbow, reached.wrist));
			angles.upper = vm::Normal(vm::Sub(shoulder, reached.elbow));
			vm::Pose(arm, hand, anchors, reach, angles, left, left ? gMatricesLeft : gMatrices);
		}
		void *clump = EntityClump(ped), *atomic = clump ? FirstAtomic(clump) : nullptr;
		void *geometry = atomic ? rw.AtomicGeometry(atomic) : nullptr;
		gRasters.assign(gBuilt.indices.size(), nullptr);
		for (size_t m = 0; geometry && m < gRasters.size(); ++m) {
			void *material = rw.Material(geometry, int(m)), *texture = material ? rw.MaterialTexture(material) : nullptr;
			gRasters[m] = texture ? rw.TextureRaster(texture) : nullptr;
		}
		HideRealArms(ped);
		gLeftShown = gGuardOnly = gDraw = gActive = true;
		return true;
	}
	if (use != vm::Use::kNew || !gOut.visible)
		return false;
	if (vm::IsMelee(gOut.weapon)) { // the game's own model, held up ready or swinging
		if (gOut.weapon != type || !LoadHeld(ped))
			return false;
		const vm::Frame ready = vm::GripFrame(gOut.grip);
		const vm::Frame carried = vm::MovedAbout(ready, ready.pos, gOut.carry);
		gGunView = vm::Compose(playerreact::ViewTransform(ped),
		                       vm::MovedAbout(carried, carried.pos, vm::Move{gOut.offset, gOut.pitch, gOut.roll, gOut.yaw}));
		const vm::Arm &arm = gBuilt.arms.right;
		vm::ArmAngles angles;
		vm::ArmTo(gGunView.pos, vm::Length(vm::Sub(arm.bind[vm::kHand].pos, arm.bind[vm::kFore].pos)),
		          vm::Length(vm::Sub(arm.bind[vm::kFore].pos, arm.bind[vm::kUpper].pos)), angles.forearm, angles.upper);
		const vm::Vec3 anchors[vm::kDigits] = {};
		const bool reach[vm::kDigits] = {}; // his fingers as his model has them round a handle
		vm::Pose(arm, gGunView, anchors, reach, angles, false, gMatrices);
		gLeftShown = false;
		float proj[16], view[16];
		if (!shaders::CameraMatrices(proj, view))
			return false;
		void *clump = *reinterpret_cast<void *const *>(ped + kRwObject);
		void *atomic = clump ? FirstAtomic(clump) : nullptr;
		void *geometry = atomic ? rw.AtomicGeometry(atomic) : nullptr;
		gRasters.assign(gBuilt.indices.size(), nullptr);
		for (size_t m = 0; geometry && m < gRasters.size(); m++) {
			void *material = rw.Material(geometry, int(m)), *texture = material ? rw.MaterialTexture(material) : nullptr;
			gRasters[m] = texture ? rw.TextureRaster(texture) : nullptr;
		}
		HideRealArms(ped);
		gHeldShown = gDraw = gActive = true;
		return true;
	}
	gGun = GunFor(gOut.weapon);
	if (!gGun)
		return false;

	// The gun in the view, moved as the animation has it (turned about his grip).
	const vm::Gun &gun = gGun->gun;
	const vm::Frame framed = vm::GunInView(gun, gun.framing, camera::AimBlend());
	const vm::Frame carried = vm::GunCarried(gun, framed, gOut.carry);
	const vm::Frame kicked = vm::GunKicked(gun, carried, gOut.kick);  // firing; his forearm goes with it
	const vm::Frame native = vm::GunMoved(gun, kicked, gOut);
	gParts.assign(std::max<size_t>(1, gun.parts.size()), vm::Identity()); // at least the body
	for (size_t i = 0; i < gun.parts.size(); i++)
		gParts[i] = vm::PartMove(gun.parts[i], gOut, gun.scale);
	// His hands on it, his fingers on their anchors (moving with their parts:
	// the trigger, a pump); his left at the magazine through a reload.
	const vm::ReloadHand reload = vm::HandForReload(in.reloading && gOut.weapon == type, in.reloadProgress, gun.twoHanded);
	vm::HandPlace rightHand = vm::RightPlace(gBuilt.arms.right, gun, carried, native);
	vm::HandPlace left = vm::LeftPlace(gun, kicked, native, gParts, gGun->fitLeft.part[vm::kDigitIndex], reload);
	if (left.shown) { // the kick moves his left hand; its shoulder stays, its elbow takes it
		const vm::HandPlace rest = vm::LeftPlace(gun, carried, vm::GunMoved(gun, carried, gOut), gParts,
		                                         gGun->fitLeft.part[vm::kDigitIndex], reload);
		left.angles = vm::KeepShoulder(gBuilt.arms.left, rest.hand, rest.angles, left.hand.pos, true);
	}
	gGunView = native;
	if (guard.weight > 0) {
		const auto rightLimit = Envelope(gBuilt.arms.right,rightHand,false);
		const auto leftLimit = Envelope(gBuilt.arms.left,left,true);
		gGunView = ContactGun(gun,native,vm::Compose(playerreact::ViewTransform(ped),native),kicked,reload,rightLimit,leftLimit);
		rightHand.hand = vm::HandFor(gun,gGunView);
		rightHand.angles = AnchoredAngles(rightLimit,rightHand.hand.pos,rightHand.angles);
		const auto nativeAngles = left.angles;
		left = vm::LeftPlace(gun,kicked,gGunView,gParts,gGun->fitLeft.part[vm::kDigitIndex],reload);
		if (left.shown) left.angles = AnchoredAngles(leftLimit,left.hand.pos,nativeAngles);
	}
	PoseHand(rightHand,gGun->fit,false,gMatrices);
	gLeftShown = left.shown;
	if (left.shown)
		PoseHand(left, gGun->fitLeft, true, gMatricesLeft);
	// It can only be drawn seeing the camera as librw has it; his arms stay if not.
	float proj[16], view[16];
	if (!shaders::CameraMatrices(proj, view))
		return false;
	// His textures, this frame's (a clothes change frees the last ones).
	void *clump = *reinterpret_cast<void *const *>(ped + kRwObject);
	void *atomic = clump ? FirstAtomic(clump) : nullptr;
	void *geometry = atomic ? rw.AtomicGeometry(atomic) : nullptr;
	gRasters.assign(gBuilt.indices.size(), nullptr);
	for (size_t m = 0; geometry && m < gRasters.size(); m++) {
		void *material = rw.Material(geometry, int(m)), *texture = material ? rw.MaterialTexture(material) : nullptr;
		gRasters[m] = texture ? rw.TextureRaster(texture) : nullptr;
	}
	HideRealArms(ped);
	// Where its muzzle is in the world (the camera as it is now; shots leave from there, the game's flash with them).
	const vm::Vec3 m = vm::Apply(gGunView, gun.muzzle);
	const vm::Vec3 ahead = *reinterpret_cast<const vm::Vec3 *>(api.theCamera + 0x10), up = *reinterpret_cast<const vm::Vec3 *>(api.theCamera + 0x20),
	               eye = *reinterpret_cast<const vm::Vec3 *>(api.theCamera + 0x30);
	const vm::Vec3 right = vm::Normal(vm::Cross(ahead, up));
	const vm::Vec3 w = vm::Add(eye, vm::Add(vm::Add(vm::Mul(right, m.x), vm::Mul(ahead, m.y)), vm::Mul(up, m.z)));
	gMuzzle = {w.x, w.y, w.z};
	gDraw = gActive = true;
	return true;
}

} // namespace fpgun
