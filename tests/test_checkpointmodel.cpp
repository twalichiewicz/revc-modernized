#include <algorithm>
#include "checkpointmodel.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
int main()
{
	using namespace checkpointmodel;
	Blip blips[3] = {{true, 4, {10, 20, 3}}, {true, 7, {40, 60, 3}}, {true, 8, {0, 20, 3}}};
	Input in{true, (4 << 16) | 0, (7 << 16) | 1, {10, 20, 3}, {40, 60, 3}, 4};
	auto a = Resolve(in, blips, 3);
	assert(a.valid);
	assert(std::fabs(a.direction.x - 0.6f) < 0.00001f && std::fabs(a.direction.y - 0.8f) < 0.00001f);
	assert(a.vertices[0].x > in.current.x && a.vertices[0].y > in.current.y);
	const auto p = a.vertices[0], q = a.vertices[1], r = a.vertices[2];
	assert((q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x) > 0); // visible from above with normal vehicle culling

	in.currentHandle = (7 << 16) | 1;
	in.current = in.next;
	in.nextHandle = (8 << 16) | 2;
	in.next = {0, 20, 3};
	assert(Resolve(in, blips, 3).direction.x < 0);
	in.nextHandle = -1;
	assert(!Resolve(in, blips, 3).valid); // finish checkpoint, no following one
	in.nextHandle = (7 << 16) | 2;
	assert(!Resolve(in, blips, 3).valid); // recycled pool slot
	in.nextHandle = (8 << 16) | 2;
	in.racing = false;
	assert(!Resolve(in, blips, 3).valid);
	in.racing = true;
	in.next.x = 99;
	assert(!Resolve(in, blips, 3).valid); // another script's blip
	in.next = blips[2].pos;
	blips[2].inUse = false;
	assert(!Resolve(in, blips, 3).valid);
	blips[2].inUse = true;
	in.next.z = std::numeric_limits<float>::quiet_NaN();
	assert(!Resolve(in, blips, 3).valid);
	in.next = blips[2].pos;
	blips[2].generation = 0xfffe;
	in.nextHandle = int32_t((0xfffeu << 16) | 2);
	assert(Resolve(in, blips, 3).valid);
	in.next.z = 23;
	blips[2].pos.z = 23;
	assert(Resolve(in, blips, 3).vertices[0].z > in.current.z + 0.35f); // flight checkpoints can lead uphill
	in.next.z = in.current.z;
	blips[2].pos.z = in.current.z;
	// #28: a solid arrow floating in the checkpoint, not painted on the ground:
	// a top and an underside a thickness apart, closed by its sides.
	const auto solid = Resolve(in, blips, 3);
	assert(solid.valid && kArrowVertices == 14);
	float lowest = 1e9f;
	for (int i = 0; i < kArrowVertices; i++)
		lowest = std::min(lowest, solid.vertices[i].z);
	assert(lowest > in.current.z + 1.0f); // up in the sphere, clear of the road
	for (int i = 0; i < 7; i++) {
		const auto top = solid.vertices[i], under = solid.vertices[i + 7];
		assert(top.x == under.x && top.y == under.y && top.z - under.z > 0.2f);
	}
	// Every face, both windings (the cull mode is the engine's): top 5, underside 5, sides 7 x 2.
	assert(kArrowIndexCount == (5 + 5 + 14) * 3 * 2);
	int edges[14][14] = {};
	for (int t = 0; t < kArrowIndexCount; t += 3)
		for (int k = 0; k < 3; k++) {
			const int a = kArrowIndices[t + k], b = kArrowIndices[t + (k + 1) % 3];
			assert(a >= 0 && a < kArrowVertices && b >= 0 && b < kArrowVertices && a != b);
			edges[a][b]++;
		}
	for (int i = 0; i < 7; i++) { // each outline edge on its face, and walled
		const int j = (i + 1) % 7;
		assert(edges[i][j] + edges[j][i] >= 2 && edges[i + 7][j + 7] + edges[j + 7][i + 7] >= 2);
		assert(edges[i][i + 7] + edges[i + 7][i] >= 2);
	}
	puts("all checkpoint model tests passed");
}
