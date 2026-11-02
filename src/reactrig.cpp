#include "reactrig.h"
#include "vmmath.h"
#include <algorithm>
#include <cmath>
namespace reactrig {
namespace rp = reactphysics;
namespace {
constexpr int kPhysical[rp::Count] = {1, 3, 5, 7, 8, 9, 12, 13, 14, 16, 17, 18, 19, 20, 21};
constexpr int kChild[Bones] = {1, 2, 3, 4, 5, -1, 7, 8, 9, 10, -1, 12, 13, 14, 15, -1, 17, 18, -1, 20, 21, -1};
vm::Vec3 V(Vec3 p)
{
	return {p.x, p.y, p.z};
}
vm::Vec3 Position(const Matrix &m)
{
	return {m[12], m[13], m[14]};
}
vm::Frame Turn(vm::Vec3 from, vm::Vec3 to, float weight)
{
	if (vm::Length(from) < 1e-6f || vm::Length(to) < 1e-6f)
		return vm::Identity();
	const vm::Vec3 a = vm::Normal(from), b = vm::Normal(to), cross = vm::Cross(a, b);
	const float sine = vm::Length(cross), cosine = std::clamp(vm::Dot(a, b), -1.0f, 1.0f);
	if (sine < 1e-6f && cosine > 0)
		return vm::Identity();
	const vm::Vec3 axis = sine > 1e-6f
							  ? vm::Mul(cross, 1 / sine)
							  : vm::Normal(vm::Cross(a, std::fabs(a.x) < .9f ? vm::Vec3{1, 0, 0} : vm::Vec3{0, 1, 0}));
	return vm::TurnAbout(axis, std::atan2(sine, cosine) * weight, {});
}
vm::Frame Torso(const rp::Pose &pose)
{
	const vm::Vec3 shoulders = vm::Mul(vm::Add(V(pose[rp::ShoulderL]), V(pose[rp::ShoulderR])), .5f);
	const vm::Vec3 up = vm::Normal(vm::Sub(shoulders, V(pose[rp::Pelvis])));
	const vm::Vec3 across = vm::Sub(V(pose[rp::ShoulderR]), V(pose[rp::ShoulderL]));
	vm::Vec3 side = vm::Normal(vm::Sub(across, vm::Mul(up, vm::Dot(across, up))));
	if (vm::Length(side) < 1e-6f)
		side = vm::Normal(vm::Cross(up, std::fabs(up.x) < .9f ? vm::Vec3{1, 0, 0} : vm::Vec3{0, 1, 0}));
	return {side, vm::Cross(up, side), up, {}};
}
vm::Frame Weighted(vm::Frame rotation, float weight)
{
	if (weight >= 1)
		return rotation;
	const float angle = std::acos(std::clamp((rotation.x.x + rotation.y.y + rotation.z.z - 1) * .5f, -1.0f, 1.0f));
	if (angle < 1e-6f)
		return vm::Identity();
	vm::Vec3 axis = {rotation.y.z - rotation.z.y, rotation.z.x - rotation.x.z, rotation.x.y - rotation.y.x};
	if (vm::Length(axis) < 1e-5f) {
		axis = vm::Add(rotation.x, {1, 0, 0});
		for (vm::Vec3 candidate : {vm::Add(rotation.y, {0, 1, 0}), vm::Add(rotation.z, {0, 0, 1})})
			if (vm::Length(candidate) > vm::Length(axis)) axis = candidate;
	}
	return vm::TurnAbout(vm::Normal(axis), angle * weight, {});
}
} // namespace
rp::Pose Capture(const Matrices &animated)
{
	rp::Pose result;
	for (int j = 0; j < rp::Count; j++) {
		const vm::Vec3 p = Position(animated[size_t(kPhysical[j])]);
		result[size_t(j)] = {p.x, p.y, p.z};
	}
	return result;
}
Matrices Apply(const Matrices &animated, const rp::Pose &physical, float weight, const Vec3 *neck, const Vec3 *headAxis)
{
	if (weight <= 0)
		return animated;
	weight = std::min(weight, 1.0f);
	const rp::Pose source = Capture(animated);
	const vm::Frame torso = vm::Compose(Torso(physical), vm::Inverse(Torso(source)));
	auto segment = [&](rp::Joint a, rp::Joint b) {
		const vm::Vec3 oldAxis = vm::Sub(V(source[b]), V(source[a]));
		const vm::Vec3 newAxis = vm::Sub(V(physical[b]), V(physical[a]));
		return vm::Compose(Turn(vm::Rotate(torso, oldAxis), newAxis, 1), torso);
	};
	std::array<vm::Vec3, Bones> positions{};
	for (int j = 0; j < rp::Count; j++)
		positions[size_t(kPhysical[j])] = V(physical[size_t(j)]);
	auto follow = [&](int bone, rp::Joint a, const vm::Frame &rotation) {
		positions[size_t(bone)] =
			vm::Add(V(physical[size_t(a)]),
					vm::Rotate(rotation, vm::Sub(Position(animated[size_t(bone)]), V(source[size_t(a)]))));
	};
	follow(0, rp::Pelvis, torso);
	follow(2, rp::Pelvis, torso);
	// The neck attaches to the ribcage. Head motion must not move the neck
	// root or rotate spine1 and collapse the shirt into the chest.
	follow(4, rp::Chest, torso);
	if (neck) positions[4] = V(*neck);
	follow(6, rp::Chest, torso);
	follow(11, rp::Chest, torso);
	follow(10, rp::ElbowL, segment(rp::ElbowL, rp::HandL));
	follow(15, rp::ElbowR, segment(rp::ElbowR, rp::HandR));
	Matrices result = animated;
	for (int b = 0; b < Bones; b++) {
		const int reference = kChild[b] >= 0 ? kChild[b] : b == 5 ? 4 : b == 10 ? 9 : b == 15 ? 14 : b == 18 ? 17 : 20;
		const vm::Vec3 oldAxis = vm::Sub(Position(animated[size_t(reference)]), Position(animated[size_t(b)]));
		const vm::Vec3 newAxis = vm::Sub(positions[size_t(reference)], positions[size_t(b)]);
		// A single shortest-arc swing loses rotation around a bone's axis.
		// Start from the complete torso frame, then apply the limb's swing.
		const bool core = b == 0 || b == 1 || b == 2 || b == 3 || b == 6 || b == 11;
		vm::Frame full = core ? torso : vm::Compose(Turn(vm::Rotate(torso, oldAxis), newAxis, 1), torso);
		if (headAxis && b == 5) {
			const vm::Vec3 axis = vm::Rotate(full, {animated[b][0], animated[b][1], animated[b][2]});
			full = vm::Compose(Turn(axis, V(*headAxis), 1), full);
		}

		const vm::Frame rotation = Weighted(full, weight);
		for (int row = 0; row < 3; row++) {
			const Matrix &m = animated[size_t(b)];
			const vm::Vec3 axis =
				vm::Rotate(rotation, {m[size_t(row * 4)], m[size_t(row * 4 + 1)], m[size_t(row * 4 + 2)]});
			result[size_t(b)][size_t(row * 4)] = axis.x;
			result[size_t(b)][size_t(row * 4 + 1)] = axis.y;
			result[size_t(b)][size_t(row * 4 + 2)] = axis.z;
		}
		const vm::Vec3 at = vm::Lerp(Position(animated[size_t(b)]), positions[size_t(b)], weight);
		result[size_t(b)][12] = at.x;
		result[size_t(b)][13] = at.y;
		result[size_t(b)][14] = at.z;
	}
	if (neck || headAxis) {
		// Constrain the published matrix, including the intermediate fade.
		// Clamping only the full physical rotation lets the remaining native
		// look point the skull into the chest at partial weights.
		const vm::Frame frame = Torso(Capture(result));
		for (int bone : {4, 5}) {
			Matrix &m = result[bone];
			const vm::Vec3 primary{m[0], m[1], m[2]};
			if (vm::Length(primary) < 1e-6f) continue; // preserve native head removal
			const vm::Vec3 axis = vm::Normal(primary);
			constexpr float cosine = .5f;
			if (vm::Dot(axis, frame.z) >= cosine) continue;
			vm::Vec3 side = vm::Normal(vm::Sub(axis, vm::Mul(frame.z, vm::Dot(axis, frame.z))));
			if (vm::Length(side) < 1e-6f) side = frame.y;
			const vm::Vec3 wanted = vm::Add(vm::Mul(frame.z, cosine), vm::Mul(side, std::sqrt(1-cosine*cosine)));
			const vm::Frame correction = Turn(axis, wanted, 1);
			for (int row=0; row<3; row++) {
				const vm::Vec3 corrected = vm::Rotate(correction,{m[row*4],m[row*4+1],m[row*4+2]});
				m[row*4] = corrected.x;
				m[row*4+1] = corrected.y;
				m[row*4+2] = corrected.z;
			}
		}
	}
	return result;
}
} // namespace reactrig
