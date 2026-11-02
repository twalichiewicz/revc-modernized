#include "reactself.h"
#include "reactshape.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace reactself {
namespace {
using namespace reactphysics;
Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Mul(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(Vec3 a, Vec3 b)
{
	// Preserve contact decisions under coordinate permutations near a
	// coincident/parallel pair, where float accumulation can choose sides.
	return float(double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z);
}
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
Vec3 Normal(Vec3 a, Vec3 fallback)
{
	const float length = Length(a);
	return length > 1e-6f ? Mul(a, 1 / length) : fallback;
}
struct Capsule {
	int a, b;
	float radius, low = 0, high = 1;
	int endOther = -1;
};
Vec3 At(const Pose &pose, const Capsule &c, float t)
{
	const Vec3 end = c.endOther < 0 ? pose[size_t(c.b)] : Mul(Add(pose[size_t(c.b)], pose[size_t(c.endOther)]), .5f);
	return Add(pose[size_t(c.a)], Mul(Sub(end, pose[size_t(c.a)]), t));
}
void Gradient(std::array<float, Count> &gradient, const Capsule &c, float t, float sign)
{
	gradient[size_t(c.a)] += sign * (1 - t);
	gradient[size_t(c.b)] += sign * t * (c.endOther < 0 ? 1 : .5f);
	if (c.endOther >= 0) gradient[size_t(c.endOther)] += sign * t * .5f;
}
bool HasJoint(const Capsule &c, int joint)
{
	return c.a == joint || (c.endOther < 0 && c.b == joint);
}
struct Closest { float a, b; Vec3 delta; };
Closest Points(const Pose &pose, const Capsule &a, const Capsule &b)
{
	const Vec3 p = At(pose, a, a.low), q = At(pose, b, b.low);
	const Vec3 u = Sub(At(pose, a, a.high), p), v = Sub(At(pose, b, b.high), q), r = Sub(p, q);
	const float uu = Dot(u, u), vv = Dot(v, v), uv = Dot(u, v), ur = Dot(u, r), vr = Dot(v, r);
	float s = 0, t = 0;
	if (uu < 1e-12f) {
		t = vv > 1e-12f ? std::clamp(vr / vv, 0.0f, 1.0f) : 0;
	} else if (vv < 1e-12f) {
		s = std::clamp(-ur / uu, 0.0f, 1.0f);
	} else {
		const float denominator = uu * vv - uv * uv;
		if (denominator > uu * vv * 1e-6f) {
			s = std::clamp((uv * vr - ur * vv) / denominator, 0.0f, 1.0f);
		} else {
			// A midpoint in the overlapping interval avoids choosing only an
			// endpoint when two parallel or coincident limbs overlap fully.
			const float q0 = -ur / uu, q1 = q0 + uv / uu;
			const float low = std::max(0.0f, std::min(q0, q1));
			const float high = std::min(1.0f, std::max(q0, q1));
			s = low <= high ? (low + high) * .5f : std::clamp(q0, 0.0f, 1.0f);
		}
		t = (uv * s + vr) / vv;
		if (t < 0) { t = 0; s = std::clamp(-ur / uu, 0.0f, 1.0f); }
		else if (t > 1) { t = 1; s = std::clamp((uv - ur) / uu, 0.0f, 1.0f); }
	}
	return {a.low + s * (a.high - a.low), b.low + t * (b.high - b.low), Sub(Add(p, Mul(u, s)), Add(q, Mul(v, t)))};
}
// The normal must follow a tumbling body, even when its contact segments
// coincide. Map the measured rest separation into the current torso frame.
struct Frame { Vec3 side, forward, up; };
Frame Axes(const Pose &pose, Frame fallback)
{
	const Vec3 up = Normal(Sub(pose[Chest], pose[Pelvis]), fallback.up);
	const Vec3 across = Sub(pose[ShoulderR], pose[ShoulderL]);
	const Vec3 side = Normal(Sub(across, Mul(up, Dot(across, up))), fallback.side);
	return {side, Normal(Cross(up, side), fallback.forward), up};
}
Vec3 Transport(Vec3 direction, Frame rest, Frame current)
{
	return Add(Add(Mul(current.side, Dot(direction, rest.side)), Mul(current.forward, Dot(direction, rest.forward))),
			   Mul(current.up, Dot(direction, rest.up)));
}
Vec3 Fallback(const Pose &pose, const Pose &rest, const Capsule &a, const Capsule &b, Closest closest,
			  Frame restFrame, Frame currentFrame)
{
	const Vec3 reference = Transport(Sub(At(rest, a, closest.a), At(rest, b, closest.b)), restFrame, currentFrame);
	const Vec3 u = Sub(At(pose, a, a.high), At(pose, a, a.low));
	const Vec3 v = Sub(At(pose, b, b.high), At(pose, b, b.low));
	Vec3 normal = Cross(u, v);
	if (Length(normal) > 1e-6f * Length(u) * Length(v)) {
		normal = Normal(normal, currentFrame.forward);
		if (Dot(normal, reference) < 0 || (std::fabs(Dot(normal, reference)) < 1e-6f && Dot(normal, currentFrame.forward) < 0))
			normal = Mul(normal, -1);
		return normal;
	}
	const Vec3 axis = Normal(Length(u) > Length(v) ? u : v, currentFrame.up);
	for (Vec3 candidate : {reference, currentFrame.forward, currentFrame.side, currentFrame.up}) {
		normal = Sub(candidate, Mul(axis, Dot(candidate, axis)));
		if (Length(normal) > 1e-6f)
			return Normal(normal, currentFrame.forward);
	}
	return currentFrame.forward;
}
void Trim(Capsule &capsule, int joint, float distance, const Pose &rest)
{
	const float length = Length(Sub(At(rest, capsule, 1), At(rest, capsule, 0)));
	const float amount = std::min(.8f, distance / std::max(length, 1e-6f));
	if (capsule.a == joint) capsule.low = std::max(capsule.low, amount);
	if (capsule.b == joint && capsule.endOther < 0) capsule.high = std::min(capsule.high, 1 - amount);
}
float AttachmentDistance(const Capsule &a, const Capsule &b, int joint, float clearance, const Pose &position,
						 const Pose &rest)
{
	const int otherA = a.a == joint ? a.b : a.a, otherB = b.a == joint ? b.b : b.a;
	const Vec3 restU = Normal(Sub(rest[size_t(otherA)], rest[size_t(joint)]), {0, 0, 1});
	const Vec3 restV = Normal(Sub(rest[size_t(otherB)], rest[size_t(joint)]), {0, 0, -1});
	const Vec3 u = Normal(Sub(position[size_t(otherA)], position[size_t(joint)]), restU);
	const Vec3 v = Normal(Sub(position[size_t(otherB)], position[size_t(joint)]), restV);
	// The gap between equally trimmed centerlines is 2*d*sin(angle/2).
	// Follow the current bend, so a normal bent elbow/knee does not
	// acquire an artificial straightening torque. Distal volumes retain
	// their full radii and can still collide when folded past that bend.
	const float angularGap = std::sqrt(std::max(0.0f, 2 * (1 - Dot(u, v))));
	return std::max(clearance * .6f, clearance * 1.01f / std::max(angularGap, .01f));
}
void Attach(Capsule &limb, const Capsule &torso, const Pose &rest)
{
	// Only the proximal attachment is exempt. Find the rest-pose shoulder/
	// hip overlap, keeping at least the distal 40% available for contacts.
	const float clearance = limb.radius + torso.radius;
	for (int i = 0; i <= 12; i++) {
		Capsule point = limb;
		point.low = point.high = i * .05f;
		if (Length(Points(rest, point, torso).delta) >= clearance) {
			limb.low = point.low;
			return;
		}
	}
	limb.low = .6f;
}
bool SweptBounds(const Pose &position, const Pose &previous, const Capsule &a, const Capsule &b)
{
	Vec3 aLow = At(previous, a, a.low), aHigh = aLow, bLow = At(previous, b, b.low), bHigh = bLow;
	auto include = [](Vec3 &low, Vec3 &high, Vec3 p) {
		low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
		high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
	};
	for (const Pose *pose : {&previous, &position}) {
		include(aLow, aHigh, At(*pose, a, a.low)); include(aLow, aHigh, At(*pose, a, a.high));
		include(bLow, bHigh, At(*pose, b, b.low)); include(bLow, bHigh, At(*pose, b, b.high));
	}
	const float radius = a.radius + b.radius;
	return aLow.x <= bHigh.x + radius && bLow.x <= aHigh.x + radius &&
		   aLow.y <= bHigh.y + radius && bLow.y <= aHigh.y + radius &&
		   aLow.z <= bHigh.z + radius && bLow.z <= aHigh.z + radius;
}
Closest Between(const Pose &position, const Pose &previous, const Capsule &a, const Capsule &b, float time)
{
	Pose sample{};
	for (int joint : {a.a, a.b, a.endOther, b.a, b.b, b.endOther})
		if (joint >= 0) sample[size_t(joint)] = Add(previous[size_t(joint)], Mul(Sub(position[size_t(joint)], previous[size_t(joint)]), time));
	return Points(sample, a, b);
}
bool Swept(const Pose &position, const Pose &previous, const Capsule &a, const Capsule &b, float clearance, float scale,
		   Closest &contact, Vec3 &normal, float &penetration)
{
	if (!SweptBounds(position, previous, a, b))
		return false;
	const float tolerance = scale * 1e-4f;
	Closest closest = Points(previous, a, b);
	float distance = Length(closest.delta);
	if (distance <= clearance + tolerance) {
		// A resting or slightly penetrating contact already has an incoming
		// side. Preserve that side for inward motion; outward motion remains
		// free, including escape from an existing penetration.
		if (distance < 1e-8f * scale)
			return false;
		const Vec3 incoming = Mul(closest.delta, 1 / distance);
		const float finalDistance = Dot(Sub(At(position, a, closest.a), At(position, b, closest.b)), incoming);
		if (finalDistance >= distance - tolerance || finalDistance >= clearance - 1e-5f * scale)
			return false;
		contact = closest;
		normal = incoming;
		penetration = clearance - finalDistance;
		return true;
	}
	const float speedA = std::max(Length(Sub(At(position, a, a.low), At(previous, a, a.low))),
								  Length(Sub(At(position, a, a.high), At(previous, a, a.high))));
	const float speedB = std::max(Length(Sub(At(position, b, b.low), At(previous, b, b.low))),
								  Length(Sub(At(position, b, b.high), At(previous, b, b.high))));
	const float speed = speedA + speedB;
	if (speed < 1e-6f * scale)
		return false;
	float time = 0;
	bool hit = false;
	// Segment distance cannot shrink faster than the sum of endpoint
	// motion bounds. Each advance therefore remains on the incoming side
	// of a contact, including crossings with separated final centerlines.
	for (int step = 0; step < 16; step++) {
		if (distance <= clearance + tolerance) { hit = true; break; }
		time += (distance - clearance) / speed;
		if (time > 1)
			return false;
		closest = Between(position, previous, a, b, time);
		distance = Length(closest.delta);
	}
	if (!hit) {
		// Nearly grazing contacts can converge slowly. Search the remaining
		// interval with a fixed budget and refine its first inside sample.
		float before = time;
		for (int step = 1; step <= 16; step++) {
			const float sampleTime = time + (1 - time) * (step / 16.0f);
			Closest sample = Between(position, previous, a, b, sampleTime);
			if (Length(sample.delta) <= clearance + tolerance) {
				float after = sampleTime;
				for (int refine = 0; refine < 10; refine++) {
					const float middle = (before + after) * .5f;
					if (Length(Between(position, previous, a, b, middle).delta) <= clearance + tolerance) after = middle;
					else before = middle;
				}
				closest = Between(position, previous, a, b, before);
				distance = Length(closest.delta);
				hit = true;
				break;
			}
			before = sampleTime;
		}
	}
	if (!hit || distance < 1e-8f * scale)
		return false;
	const Vec3 incoming = Mul(closest.delta, 1 / distance);
	const float wanted = clearance - Dot(Sub(At(position, a, closest.a), At(position, b, closest.b)), incoming);
	if (wanted <= 1e-5f * scale)
		return false;
	normal = incoming;
	penetration = wanted;
	contact = closest;
	return true;
}
} // namespace

void Project(reactphysics::Pose &position, const reactphysics::Pose &rest, const float *inverseMass,
			 const reactphysics::Pose *previous, reactphysics::Sweep sweep, void *context, reactphysics::Contact *upperContact, bool rigidCore, const HeadCenters *head)
{
	using namespace reactphysics;
	const float height = Length(Sub(rest[Chest], rest[Pelvis]));
	const float width = Length(Sub(rest[ShoulderR], rest[ShoulderL]));
	if (!inverseMass || height < 1e-5f || width < 1e-5f)
		return;
	const float scale = height / .4f;
    std::array<Capsule,10> capsules{};
    const auto measured=reactshape::Capsules(rest);
    for(size_t i=0;i<capsules.size();i++) {
        const auto &c=measured[i]; capsules[i]={c.a,c.b,c.radius,0,1,c.endOther};
    }
    Pose collisionRest=rest,collisionPrevious=previous?*previous:position;
    const Vec3 headOffset=head?Sub(head->current,position[Head]):Vec3{};
    if(head) {collisionRest[Head]=head->rest;collisionPrevious[Head]=head->previous;capsules[1].radius=reactshape::kSkullRadius;}
	const Frame restFrame = Axes(rest, {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}});
	const Frame currentFrame = Axes(position, restFrame);
    auto core=[](int joint) {return joint==Pelvis||joint==Chest||joint==ShoulderL||joint==ShoulderR||joint==HipL||joint==HipR;};
    float coreMass=0;
    if(rigidCore) for(int i=0;i<Count;i++) if(core(i)&&inverseMass[i]>0) coreMass+=1/inverseMass[i];
    const float coreInverseMass=coreMass>0?1/coreMass:0;
	std::array<float, Count> budget;
	budget.fill(.06f * scale);
	// Ten anatomical volumes, 45 possible pairs: no broadphase allocation,
	// recursion or work that grows with the number of NPCs or contacts.
	for (size_t i = 0; i < capsules.size(); i++) {
		for (size_t j = i + 1; j < capsules.size(); j++) {
            Pose collisionPosition=position;
            collisionPosition[Head]=Add(collisionPosition[Head],headOffset);
			Capsule a = capsules[i], b = capsules[j];
			float clearance = a.radius + b.radius;
			// The skull attaches at the top of the full ribcage. Its natural
			// neck overlap does not exempt any distal arm or leg contact.
			if (i == 0 && j == 1) clearance = std::min(clearance, Length(Points(collisionRest, a, b).delta));
			for (int joint : {a.a, a.b})
				if (HasJoint(a, joint) && HasJoint(b, joint)) {
					const float attachment = AttachmentDistance(a, b, joint, clearance, collisionPosition, collisionRest);
					Trim(a, joint, attachment, collisionRest);
					Trim(b, joint, attachment, collisionRest);
				}
			if (i == 0 && (j == 2 || j == 4 || j == 6 || j == 8))
				Attach(b, a, collisionRest);
			Closest closest = Points(collisionPosition, a, b);
			const float distance = Length(closest.delta);
			Vec3 normal{};
			float penetration = 0;
			const bool swept = previous && Swept(collisionPosition, collisionPrevious, a, b, clearance, scale, closest, normal, penetration);
			if (!swept) {
				if (distance >= clearance - 1e-5f * scale)
					continue;
				penetration = clearance - distance;
				normal = distance > 1e-6f * scale ? Mul(closest.delta, 1 / distance)
					: Fallback(collisionPosition, collisionRest, a, b, closest, restFrame, currentFrame);
			}
			// Gradients of the closest points distribute correction to both
			// endpoints. Sum repeated indices before computing effective mass.
			std::array<float, Count> gradient{};
			Gradient(gradient, a, closest.a, 1);
			Gradient(gradient, b, closest.b, -1);
            float coreGradient=0,mass=0;
            for(int k=0;k<Count;k++) {
                if(rigidCore&&core(k)) coreGradient+=gradient[size_t(k)];
                else mass+=std::max(0.f,inverseMass[k])*gradient[size_t(k)]*gradient[size_t(k)];
            }
            if(rigidCore) mass+=coreInverseMass*coreGradient*coreGradient;
            if(mass<1e-8f) continue;
            const float requested=swept?penetration:std::min(penetration,.035f*scale);
            float correction=requested/mass;
            auto mobility=[&](int k) {return rigidCore&&core(k)?coreInverseMass*coreGradient:std::max(0.f,inverseMass[k])*gradient[size_t(k)];};
            // The measured torso responds as one body. Independent pushes on
            // its shoulder/hip points would fight shape matching forever and
            // eventually tear or freeze an otherwise feasible falling pose.
            for(int k=0;k<Count;k++) {
                const float move=std::fabs(mobility(k));
                if(move>1e-8f) correction=std::min(correction,budget[size_t(k)]/move);
            }
            for(int k=0;k<Count;k++) {
                const float move=correction*mobility(k);
                position[size_t(k)]=Add(position[size_t(k)],Mul(normal,move));
                budget[size_t(k)]=std::max(0.f,budget[size_t(k)]-std::fabs(move));
            }
		}
	}
	if (sweep) {
		// Pelvis/spine1 already have world spheres. The native shoulders have
		// small arm-joint spheres, so add the missing broad upper ribcage.
		// Sweep this volume as well: its small joint spheres can straddle a
		// narrow obstacle while the broad ribcage passes through its center.
		const Capsule &core = capsules[0];
		const Vec3 center = At(position, core, 1);
		const Vec3 from = previous ? At(*previous, core, 1) : center;
		const Contact contact = sweep(from, center, core.radius, context);
		if (upperContact && contact.hit) *upperContact = contact;
		const Vec3 delta = Sub(contact.position, center);
		const float distance = Length(delta);
		if (distance > 1e-6f) {
			std::array<float, Count> gradient{};
			Gradient(gradient, core, 1, 1);
			float mass = 0;
			for (int k = 0; k < Count; k++)
				mass += std::max(0.0f, inverseMass[k]) * gradient[size_t(k)] * gradient[size_t(k)];
			if (mass > 1e-8f) {
				// Terrain stops motion at the certified hit, just as the existing
				// joint sweeps do. A shallow self-contact push limit would let a
				// fast torso cross before its four terrain checks could undo it.
				const float correction = 1 / mass;
				for (int k = 0; k < Count; k++)
					position[size_t(k)] = Add(position[size_t(k)], Mul(delta, correction * std::max(0.0f, inverseMass[k]) * gradient[size_t(k)]));
			}
		}
	}
}
} // namespace reactself
