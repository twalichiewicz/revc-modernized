#include "footplant.h"
#include "body.h"
#include "config.h"
#include "footplantmodel.h"
#include "game.h"
#include "reactions.h"

#include <cmath>
#include <limits>

using namespace game;

namespace footplant {
namespace {

footplantmodel::State gState;

// The ped's matrix (CPlaceable): right, forward, up and position.
const CVector &Axis(const uint8_t *ped, int offset) { return *reinterpret_cast<const CVector *>(ped + offset); }
CVector World(const uint8_t *ped, const CVector &p)
{
	const CVector &r = Axis(ped, 0x08), &f = Axis(ped, 0x18), &u = Axis(ped, 0x28), &o = EntityPos(ped);
	return CVector{o.x + r.x * p.x + f.x * p.y + u.x * p.z, o.y + r.y * p.x + f.y * p.y + u.y * p.z,
		       o.z + r.z * p.x + f.z * p.y + u.z * p.z};
}
// The walkable ground (facing up) under `at`, searched from `above` over it to `below` under it.
bool Ground(const CVector &at, float above, float below, float &z)
{
	alignas(16) uint8_t point[0x30]{};
	uint8_t *entity = nullptr;
	if (!api.ProcessVerticalLine ||
	    !api.ProcessVerticalLine(CVector{at.x, at.y, at.z + above}, at.z - below, point, entity, true, true, false, true,
				     false, false, nullptr))
		return false;
	const CVector &position = *reinterpret_cast<const CVector *>(point), &normal = *reinterpret_cast<const CVector *>(point + 0x10);
	z = position.z;
	return normal.z > .5f && std::isfinite(z);
}
footplantmodel::V3 M(const CVector &v) { return {v.x, v.y, v.z}; }
CVector C(const footplantmodel::V3 &v) { return CVector{v.x, v.y, v.z}; }

// Raises one foot `raise` m from where the (lowered) animation has it, bending
// the knee the way the animation bends it; the foot keeps its animated turn.
void Bend(uint8_t *ped, body::Pose &pose, body::Bone thigh, body::Bone calf, body::Bone foot, float raise)
{
	if (!(raise > 1e-4f))
		return;
	const CVector hip = body::JointOver(ped, pose, thigh), knee = body::JointOver(ped, pose, calf),
		      ankle = body::JointOver(ped, pose, foot);
	footplantmodel::V3 k, f;
	if (!footplantmodel::Leg(M(hip), M(knee), M(ankle), M(CVector{ankle.x, ankle.y, ankle.z + raise}), k, f))
		return;
	body::TurnOver(ped, pose, thigh, body::Arc(body::Sub(knee, hip), body::Sub(C(k), hip)));
	const CVector kneeNow = body::JointOver(ped, pose, calf), ankleNow = body::JointOver(ped, pose, foot);
	body::TurnOver(ped, pose, calf, body::Arc(body::Sub(ankleNow, kneeNow), body::Sub(C(f), C(k))));
}

} // namespace

float Pose(uint8_t *ped, body::Pose &pose)
{
	const int32_t state = PedState(ped);
	const bool active = gConfig.footPlant && !PedInVehicle(ped) && (state == kPedIdle || state == kPedAimGun) &&
			    PedMoveState(ped) <= 4 && !reactions::PlayerDown();
	const float dt = api.timeStep ? *api.timeStep / 50.0f : 0;
	// The floor the animation stands him on, and the ground under each animated foot.
	const body::Pose none;
	float floor = 0;
	const bool hasFloor = Ground(EntityPos(ped), .2f, 2.0f, floor);
	footplantmodel::Foot feet[2];
	const body::Bone bones[2] = {body::kFootL, body::kFootR};
	for (int i = 0; i < 2; i++) {
		const CVector at = World(ped, body::JointOver(ped, none, bones[i]));
		float ground = 0;
		feet[i].animated = at.z;
		feet[i].hit = hasFloor && Ground(at, .6f, .9f, ground);
		feet[i].ground = ground;
	}
	const footplantmodel::Output out = footplantmodel::Step(
		gState, feet, hasFloor ? floor : std::numeric_limits<float>::quiet_NaN(), active, dt);
	if (std::fabs(out.drop) < 1e-4f && out.raise[0] < 1e-4f && out.raise[1] < 1e-4f)
		return 0;
	// His root as animated (turn and place), lowered; then each leg to its ground.
	body::TurnOver(ped, pose, body::kRoot, body::Quat{});
	pose.rootMove = body::Add(body::Sub(body::JointOver(ped, none, body::kRoot), body::Joint(body::Pose{}, body::kRoot)),
				  CVector{0, 0, out.drop});
	Bend(ped, pose, body::kThighL, body::kCalfL, body::kFootL, out.raise[0]);
	Bend(ped, pose, body::kThighR, body::kCalfR, body::kFootR, out.raise[1]);
	return 1;
}

} // namespace footplant
