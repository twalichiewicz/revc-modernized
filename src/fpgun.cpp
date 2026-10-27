#include "fpgun.h"
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

#include <algorithm>
#include <cmath>
#include <cstdlib>
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
const uint32_t *gFrameCounter;          // CTimer::m_FrameCounter
void (*orig_RenderFirstPersonVehicle)();
bool gReady;

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

void DrawArm(const vm::Arm &arm, const vm::Frame *matrices, std::vector<std::vector<uint16_t>> &indices, const vm::Frame &eye,
             float squash)
{
	std::vector<vm::Vec3> pos, normal;
	vm::Skin(arm.vertices, matrices, pos, normal);
	gVertices.resize(pos.size());
	for (size_t i = 0; i < pos.size(); i++)
		Vertex(gVertices[i], vm::ToWorld(eye, pos[i], squash), vm::ToWorldDirection(eye, normal[i]), arm.vertices[i].u,
		       arm.vertices[i].v);
	for (size_t m = 0; m < indices.size(); m++) {
		api.RwRenderStateSet(kStateTextureRaster, m < gRasters.size() ? gRasters[m] : nullptr);
		Render(indices[m].data(), indices[m].size(), kIm3DVertexUV | kIm3DLighting);
	}
}

void Draw()
{
	vm::Frame eye;
	float tanHalf;
	if (!gGun || !Eye(eye, tanHalf))
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
	// The gun, its parts where the animation has them.
	const vm::Gun &gun = gGun->gun;
	gVertices.resize(gun.vertices.size());
	for (size_t i = 0; i < gun.vertices.size(); i++) {
		const vm::GunVertex &v = gun.vertices[i];
		const vm::Frame &part = gParts[v.part];
		const vm::Vec3 p = vm::Apply(gGunView, vm::Apply(part, v.pos));
		const vm::Vec3 n = vm::Rotate(gGunView, vm::Rotate(part, v.normal));
		Vertex(gVertices[i], vm::ToWorld(eye, p, squash), vm::ToWorldDirection(eye, n), v.u, v.v);
	}
	api.RwRenderStateSet(kStateTextureRaster, gGun->atlas);
	std::vector<uint16_t> &indices = gOut.magazineShown ? gGun->gun.indices : gGun->withoutMagazine;
	Render(indices.data(), indices.size(), kIm3DVertexUV | kIm3DLighting);
	if (ped)
		gRemoveLighting(ped, reset);

	// The flash at the muzzle: the game's own, facing the eye, turned at random.
	if (gOut.flash > 0.0f && gFlashRaster && *gFlashRaster) {
		const vm::Vec3 muzzle = vm::Apply(gGunView, gun.muzzle);
		const float turn = float(rand() % 360) * 3.14159265f / 180.0f, size = kFlashSize * (0.8f + 0.4f * float(rand() % 100) / 100.0f);
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

bool Active() { return gActive; }
bool Ready() { return gActive && gOut.ready; }
CVector Muzzle() { return gMuzzle; }

bool Prepare(uint8_t *ped)
{
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
	moment.covered = vm::FindEmbedded(type) != nullptr;
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
	in.shot = gShots.Update(type, clip, *gFrameCounter); // frame to frame: not on coming back to first person
	in.dt = *api.timeStep / 50.0f;
	in.stepPhase = camera::StepPhase(); // his steps: the gun bobs with them, and swings with them sprinting
	in.stepSpeed = camera::StepSpeed();
	in.aimed = camera::AimBlend();
	gOut = gAnim.Update(in);
	if (use != vm::Use::kNew || !gOut.visible)
		return false;
	gGun = GunFor(gOut.weapon);
	if (!gGun)
		return false;

	// The gun in the view, moved as the animation has it (turned about his grip).
	const vm::Gun &gun = gGun->gun;
	const vm::Frame framed = vm::GunInView(gun, gun.framing, camera::AimBlend());
	const vm::Frame carried = vm::GunCarried(gun, framed, gOut.carry); // sprinting; his arms keep to it
	const vm::Frame moved = vm::GunMoved(gun, carried, gOut);
	gGunView = moved;
	gParts.assign(std::max<size_t>(1, gun.parts.size()), vm::Identity()); // at least the body
	for (size_t i = 0; i < gun.parts.size(); i++)
		gParts[i] = vm::PartMove(gun.parts[i], gOut, gun.scale);
	// His hands on it, his fingers on their anchors (moving with their parts:
	// the trigger, a pump); his left at the magazine through a reload.
	PoseHand(vm::RightPlace(gun, carried, moved), gGun->fit, false, gMatrices);
	const vm::ReloadHand reload = vm::HandForReload(in.reloading && gOut.weapon == type, in.reloadProgress, gun.twoHanded);
	const vm::HandPlace left = vm::LeftPlace(gun, carried, moved, gParts, gGun->fitLeft.part[vm::kDigitIndex], reload);
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
