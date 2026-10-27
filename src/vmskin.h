#pragma once
// Skinning on the CPU, for the viewmodel's arms (a few hundred vertices a
// frame): each vertex moved by up to four bones' matrices, blended by its
// weights. Unit-tested (tests/test_vmskin.cpp).
#include "vmmath.h"

#include <cstdint>
#include <vector>

namespace vm {

struct SkinVertex {
	Vec3 pos, normal;
	float u, v;
	uint8_t bone[4];
	float weight[4];
};

struct Triangle {
	uint16_t v[3];
	uint16_t material;
};

// `matrices[b]` takes bone b's bind frame where it's posed (W_b · Bind_b⁻¹).
void Skin(const std::vector<SkinVertex> &in, const Frame *matrices, std::vector<Vec3> &pos, std::vector<Vec3> &normal);

} // namespace vm
