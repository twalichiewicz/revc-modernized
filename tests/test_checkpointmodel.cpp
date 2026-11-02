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
	puts("all checkpoint model tests passed");
}
