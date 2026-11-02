#pragma once
// A skinned Vice City DFF (a ped) read into the viewmodel's SkinnedMesh, for
// the offline check (the mod itself reads the loaded clump through
// RenderWare). From the viewmodel spike's vcdff.py: frames carry the HAnim
// bone tags, the skin gives each vertex up to four (bone, weight) pairs, a
// bone being an index into the HAnim hierarchy's node list. RenderWare 3.3
// geometry carries 12 bytes of lighting after its header, and each skin
// inverse-bind matrix is preceded by 0xDEADDEAD.
#include "vmarms.h"

#include <cstdint>
#include <string>
#include <vector>

bool ReadSkinnedDff(const std::vector<uint8_t> &dff, vm::SkinnedMesh &out, std::string &why);

// A plain (unskinned) DFF's first geometry, as a weapon's: in its own model
// space (a weapon's is his hand bone's), each material's texture by name.
struct StaticMesh {
	std::vector<vm::SkinVertex> vertices; // bones unused
	std::vector<vm::Triangle> triangles;
	std::vector<std::string> textures;    // by material
};
bool ReadStaticDff(const std::vector<uint8_t> &dff, StaticMesh &out, std::string &why);
