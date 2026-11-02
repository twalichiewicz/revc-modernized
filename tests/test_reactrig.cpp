#include "reactrig.h"
#include "vmmath.h"
#include <cmath>
#include <cstdio>
using namespace reactrig;
int main()
{
	Matrices original{};
	for (int i = 0; i < Bones; i++) {
		original[size_t(i)] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, float(i) * .03f, float(i % 3) * .1f, float(i) * .07f,
							   1};
	}
	const auto captured = Capture(original);
	int fail = 0;
	if (captured[reactphysics::Head].z != original[5][14]) {
		puts("FAIL: physical joints don't match skeleton tags");
		fail++;
	}
	for (float weight : {0.0f, 1.0f}) {
		const auto unchanged = Apply(original, captured, weight);
		for (int i = 0; i < Bones; i++)
			for (int k = 0; k < 16; k++)
				if (std::fabs(unchanged[size_t(i)][size_t(k)] - original[size_t(i)][size_t(k)]) > 1e-5f) {
					puts("FAIL: applying unchanged physical pose deforms the NPC");
					fail++;
					goto checked;
				}
	checked:;
	}
	auto moved = captured;
	for (auto &p : moved) {
		p.x += 3;
		p.y += 1;
		p.z += 2;
	}
	const auto translated = Apply(original, moved, 1);
	for (int i = 0; i < Bones; i++)
		if (std::fabs(translated[size_t(i)][12] - original[size_t(i)][12] - 3) > 1e-5f ||
			std::fabs(translated[size_t(i)][13] - original[size_t(i)][13] - 1) > 1e-5f ||
			std::fabs(translated[size_t(i)][14] - original[size_t(i)][14] - 2) > 1e-5f) {
			puts("FAIL: unsimulated neck/finger/root offsets did not follow the body");
			fail++;
			break;
		}
	const vm::Frame rigid = vm::Compose(vm::TurnAbout({0, 0, 1}, .9f, {}), vm::TurnAbout({0, 1, 0}, .7f, {}));
	auto rotatedPose = captured;
	for (auto &p : rotatedPose) {
		const vm::Vec3 turned = vm::Rotate(rigid, {p.x, p.y, p.z});
		p = {turned.x, turned.y, turned.z};
	}
	const auto rotated = Apply(original, rotatedPose, 1);
	for (int b = 0; b < Bones; b++) {
		for (int axis = 0; axis < 4; axis++) {
			const int offset = axis * 4;
			const vm::Vec3 expected = vm::Rotate(rigid, {original[b][offset], original[b][offset + 1], original[b][offset + 2]});
			const vm::Vec3 actual = {rotated[b][offset], rotated[b][offset + 1], rotated[b][offset + 2]};
			if (vm::Length(vm::Sub(expected, actual)) > 1e-4f) {
				puts("FAIL: rotating the physical body must rotate every skin matrix rigidly without losing axial twist");
				fail++;
				break;
			}
		}
	}
	auto headOnly = captured;
	headOnly[reactphysics::Head].x += .15f;
	const auto neckBend = Apply(original, headOnly, 1);
	for (int axis = 0; axis < 3; axis++)
		for (int row = 0; row < 3; row++)
			if (std::fabs(neckBend[3][axis * 4 + row] - original[3][axis * 4 + row]) > 1e-5f) {
				puts("FAIL: a head bend must not rotate the skinned ribcage independently of its shoulders");
				fail++;
				goto checkedNeck;
			}
	checkedNeck:;
	if (fail) {
		printf("reactrig: %d failures\n", fail);
		return 1;
	}
	puts("reactrig: all passed");
}
