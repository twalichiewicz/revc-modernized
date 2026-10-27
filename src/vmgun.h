#pragma once
// The viewmodel's guns: each a low-poly model in its moving parts, wearing a
// 256 px photo atlas, with where it sits in his hand and where his fingers
// go on it. Built in Blender by tools/guns/ (developers only) into .vmg files
// that are built into the mod (gunassets.S). Unit-tested (tests/test_vmgun.cpp).
//
// The format, little-endian:
//   char  magic[4] = "VMG2"
//   u32   vertexCount, triangleCount, partCount, anchorCount
//   f32   scale                     the game's model of the gun over the real one
//   f32   hold[12]                  the gun frame in his hand bone's: x, y, z axes, origin
//   f32   muzzle[3], sightRear[3], sightFront[3]
//   u32   twoHanded
//   f32   holdLeft[12]              the gun frame in his left hand bone's (two-handed guns)
//   f32   leftForearm[3]            the way his left forearm runs back from the wrist, gun frame
//   f32   hipEye[3], hipYaw, hipPitch, aimBack, aimAbove   its framing (Framing)
//   parts[partCount]                char name[16]; f32 pivot[3]; f32 axis[3]
//   anchors[anchorCount]            char name[16]; f32 pos[3]; u32 part (0xffffffff: none); f32 bend[3], slack[3]
//   vertices[vertexCount]           f32 pos[3], normal[3], uv[2]; u32 part
//   u16   indices[3 * triangleCount], then padding to 4 bytes
//   u32   atlasWidth, atlasHeight
//   u8    palette[256 * 4]          RGBA
//   u8    pixels[atlasWidth * atlasHeight], top row first
// The gun frame: x right, y forward along the barrel, z up, in metres scaled
// to the game's own model (so `hold` is rigid).
#include "vmmath.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vm {

struct GunVertex {
	Vec3 pos, normal;
	float u, v;
	uint32_t part;
};
// A moving part (part 0 is the body): it slides along `axis`, or turns about it through `pivot`.
struct Part {
	char name[16];
	Vec3 pivot, axis;
};
// Where it's seen from. The hip: the eye in the gun's frame (real metres;
// scaled with the gun), looking `hipYaw` degrees right of the barrel and
// `hipPitch` up. Aiming: the eye `aimBack` behind the rear sight and
// `aimAbove` over the sight line, looking along it.
struct Framing {
	Vec3 hipEye;
	float hipYaw, hipPitch;
	float aimBack, aimAbove;
};

// Where a fingertip goes ("r_index", "l_thumb", ...): moves with `part` if it
// has one. `bend` (zero for none): which way the finger's middle joint goes
// out reaching it (a trigger finger along the frame, over the guard, say).
// `slack` (zero for none): how far, and which way, it may move for a finger
// that can't reach it without going into the gun (along the grip towards the
// front strap, say: a longer-fingered outfit's hand).
constexpr uint32_t kNoPart = 0xffffffffu;
struct Anchor {
	char name[16];
	Vec3 pos;
	uint32_t part;
	Vec3 bend, slack;
};

struct Gun {
	float scale = 1.0f;
	Frame hold{};
	Vec3 muzzle{}, sightRear{}, sightFront{};
	bool twoHanded = false;
	Frame holdLeft{};
	Vec3 leftForearm{};
	Framing framing{};
	std::vector<Part> parts;
	std::vector<Anchor> anchors;
	std::vector<GunVertex> vertices;
	std::vector<uint16_t> indices;
	int atlasWidth = 0, atlasHeight = 0;
	std::vector<uint8_t> atlasRgba;
};

bool ParseGun(const uint8_t *data, size_t size, Gun &out, std::string &why);
const Anchor *FindAnchor(const Gun &gun, const char *name);
int FindPart(const Gun &gun, const char *name); // -1 if none
// His left hand holding the gun's magazine by its base (the gun frame, the
// magazine where it sits): palm up the magazine, under the middle of its
// base, fingers forward. False for a gun without a part named "magazine".
bool MagazineGrip(const Gun &gun, Frame &hand);

// The guns built into the mod, by the game's weapon type (eWeaponType).
struct Embedded {
	int weapon;
	const char *name;
	const uint8_t *begin, *end;
};
const Embedded *FindEmbedded(int weapon);
// The game's weapon type (eWeaponType) of a gun by its asset's name ("m4"); -1 if none.
int WeaponNamed(const char *name);

} // namespace vm
