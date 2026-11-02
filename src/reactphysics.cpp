#include "reactphysics.h"
#include "reactself.h"
#include "reactshape.h"
#include <algorithm>
#include <cmath>

namespace reactphysics {
namespace {
constexpr int kParent[Count] = {-1,		Pelvis, Chest, Chest, ShoulderL, ElbowL, Chest, ShoulderR,
								ElbowR, Pelvis, HipL,  KneeL, Pelvis,	 HipR,	 KneeR};
constexpr float kInverseMass[Count] = {.14f, .2f, .5f, 1, 1, 2, 1, 1, 2, .5f, .6f, 1, .5f, .6f, 1};
constexpr float kStep = 1.0f / 60.0f;
Vec3 Add(Vec3 a, Vec3 b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Vec3 Sub(Vec3 a, Vec3 b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Vec3 Mul(Vec3 a, float s)
{
	return {a.x * s, a.y * s, a.z * s};
}
float Dot(Vec3 a, Vec3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
float Length(Vec3 a)
{
	return std::sqrt(Dot(a, a));
}
Vec3 Limited(Vec3 a, float max)
{
	const float len = Length(a);
	return len > max ? Mul(a, max / len) : a;
}
Vec3 Cross(Vec3 a, Vec3 b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 Normal(Vec3 a, Vec3 fallback)
{
	const float length = Length(a);
	return length > 1e-6f ? Mul(a, 1 / length) : fallback;
}
struct Basis { Vec3 side, forward, up; };
Basis Axes(const Pose &pose)
{
	const Vec3 upper = Mul(Add(pose[ShoulderL], pose[ShoulderR]), .5f);
	const Vec3 up = Normal(Sub(upper, pose[Pelvis]), {0, 0, 1});
	const Vec3 across = Sub(pose[ShoulderR], pose[ShoulderL]);
	const Vec3 side = Normal(Sub(across, Mul(up, Dot(up, across))), {1, 0, 0});
	return {side, Cross(up, side), up};
}
Vec3 Carry(Vec3 vector, Basis from, Basis to)
{
	return Add(Add(Mul(to.side, Dot(vector, from.side)), Mul(to.forward, Dot(vector, from.forward))),
			   Mul(to.up, Dot(vector, from.up)));
}
Vec3 Swing(Vec3 from, Vec3 to, Vec3 vector, Vec3 fallback)
{
	from = Normal(from, to);
	to = Normal(to, from);
	const float cosine = std::clamp(Dot(from, to), -1.0f, 1.0f);
	const Vec3 cross = Cross(from, to);
	const float sine = Length(cross);
	if (sine < 1e-6f && cosine > 0)
		return vector;
	const Vec3 axis = sine > 1e-6f ? Mul(cross, 1 / sine) : Normal(Cross(from, fallback), {1, 0, 0});
	return Add(Add(Mul(vector, cosine), Mul(Cross(axis, vector), sine)), Mul(axis, Dot(axis, vector) * (1 - cosine)));
}
void JointCorrection(Pose &pose, int anchor, int end, Vec3 correction, float scale)
{
	correction = Limited(correction, .04f * scale);
	const float a = kInverseMass[anchor], b = kInverseMass[end];
	pose[size_t(end)] = Add(pose[size_t(end)], Mul(correction, b / (a + b)));
	pose[size_t(anchor)] = Sub(pose[size_t(anchor)], Mul(correction, a / (a + b)));
}
void Anatomy(Pose &pose, const Pose &rest)
{
	constexpr float radians = .01745329252f;
	const float scale = Length(Sub(rest[Chest], rest[Pelvis])) / .4f;
	const Basis original = Axes(rest), current = Axes(pose);
	for (auto leg : {std::array<int, 2>{HipL, KneeL}, {HipR, KneeR}}) {
		const float side = leg[0] == HipL ? -1.0f : 1.0f;
		const Vec3 restDirection = Normal(Sub(rest[size_t(leg[1])], rest[size_t(leg[0])]), Mul(original.up, -1));
		const float restFlex = std::atan2(Dot(restDirection, original.forward), -Dot(restDirection, original.up));
		const float restAbduction = std::asin(std::clamp(side * Dot(restDirection, original.side), -1.0f, 1.0f));
		const Vec3 thigh = Sub(pose[size_t(leg[1])], pose[size_t(leg[0])]);
		const float length = Length(thigh);
		if (length < 1e-6f) continue;
		const Vec3 direction = Mul(thigh, 1 / length);
		const float flex = std::atan2(Dot(direction, current.forward), -Dot(direction, current.up));
		const float abduction = std::asin(std::clamp(side * Dot(direction, current.side), -1.0f, 1.0f));
		// Broad hip freedom, with distinct flexion, backward extension and
		// lateral swing. Preserve an already measured extreme starting pose.
		const float limitedFlex = std::clamp(flex, std::min(-30 * radians, restFlex), std::max(125 * radians, restFlex));
		const float limitedAbduction = std::clamp(abduction, std::min(-15 * radians, restAbduction), std::max(55 * radians, restAbduction));
		if (std::fabs(flex - limitedFlex) + std::fabs(abduction - limitedAbduction) < 1e-5f) continue;
		const Vec3 wanted = Add(Mul(current.side, side * std::sin(limitedAbduction)),
			Add(Mul(current.forward, std::cos(limitedAbduction) * std::sin(limitedFlex)),
				Mul(current.up, -std::cos(limitedAbduction) * std::cos(limitedFlex))));
		JointCorrection(pose, leg[0], leg[1], Sub(Mul(wanted, length), thigh), scale);
	}
	for (auto limb : {std::array<int, 3>{ShoulderL, ElbowL, HandL}, {ShoulderR, ElbowR, HandR},
					 {HipL, KneeL, FootL}, {HipR, KneeR, FootR}}) {
		const Vec3 restUpper = Sub(rest[size_t(limb[1])], rest[size_t(limb[0])]);
		const Vec3 restLower = Sub(rest[size_t(limb[2])], rest[size_t(limb[1])]);
		Vec3 restAxis = Cross(restUpper, restLower);
		if (Length(restAxis) < .03f * Length(restUpper) * Length(restLower))
			restAxis = Mul(original.side, limb[0] >= HipL ? -1.0f : 1.0f);
		restAxis = Normal(restAxis, original.side);
		const Vec3 upper = Normal(Sub(pose[size_t(limb[1])], pose[size_t(limb[0])]), Carry(restUpper, original, current));
		const Vec3 lower = Sub(pose[size_t(limb[2])], pose[size_t(limb[1])]);
		const float length = Length(lower);
		if (length < 1e-6f) continue;
		Vec3 axis = Swing(Carry(restUpper, original, current), upper, Carry(restAxis, original, current), current.forward);
		axis = Normal(Sub(axis, Mul(upper, Dot(axis, upper))), current.side);
		const Vec3 lowerDirection = Mul(lower, 1 / length);
		const float bend = std::atan2(Dot(Cross(upper, lowerDirection), axis), Dot(upper, lowerDirection));
		const float limitedBend = std::clamp(bend, 0.0f, 145 * radians);
		const Vec3 wanted = Add(Mul(upper, std::cos(limitedBend)), Mul(Cross(axis, upper), std::sin(limitedBend)));
		// Both endpoints respond by inverse mass. This is a hinge constraint,
		// not a pull towards the current animation or a world-space pose.
		JointCorrection(pose, limb[1], limb[2], Sub(Mul(wanted, length), lower), scale);
	}
}
float Cross2(Vec3 a, Vec3 b, Vec3 c)
{
	return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
float SupportDistance(Vec3 point, std::array<Vec3, Count + 1> points, int count)
{
	if (!count)
		return 1;
	std::sort(points.begin(), points.begin() + count, [](Vec3 a, Vec3 b) {
		return a.x == b.x ? a.y < b.y : a.x < b.x;
	});
	std::array<Vec3, 2 * (Count + 1)> hull{};
	int size = 0;
	for (int i = 0; i < count; i++) {
		while (size >= 2 && Cross2(hull[size - 2], hull[size - 1], points[i]) <= 0)
			--size;
		hull[size++] = points[i];
	}
	const int lower = size;
	for (int i = count - 2; i >= 0; i--) {
		while (size > lower && Cross2(hull[size - 2], hull[size - 1], points[i]) <= 0)
			--size;
		hull[size++] = points[i];
	}
	if (size > 1)
		--size;
	bool inside = size >= 3;
	float distance = 1;
	point.z = 0;
	for (int i = 0; i < size; i++) {
		Vec3 a = hull[i], b = hull[(i + 1) % size];
		a.z = b.z = 0;
		const Vec3 edge = Sub(b, a);
		const float lengthSquared = Dot(edge, edge);
		const float along = lengthSquared > 1e-8f
								? std::clamp(Dot(Sub(point, a), edge) / lengthSquared, 0.0f, 1.0f)
								: 0;
		distance = std::min(distance, Length(Sub(point, Add(a, Mul(edge, along)))));
		inside &= Cross2(a, b, point) >= 0;
	}
	return inside ? 0 : distance;
}
} // namespace

void Body::Reset(const Pose &pose, Vec3 velocity, const Vec3 *neck, const Vec3 *headAxis)
{
    ++revision_;
	position_ = rest_ = frameStart_ = pose;
	const Vec3 attachment = neck ? *neck : Mul(Add(pose[ShoulderL], pose[ShoulderR]), .5f);
	neckOffset_ = Sub(attachment, pose[Chest]);
	headLength_ = Length(Sub(pose[Head], attachment));
	headAxis_ = headAxis ? Normal(*headAxis, Axes(pose).up) : Normal(Sub(pose[Head], attachment), Axes(pose).up);
	contactDirection_ = {};
	targetPelvis_ = pose[Pelvis];
	contactEffort_ = 0;
	for (Vec3 &v : velocity_)
		v = velocity;
	for (int i = 1; i < Count; i++)
		length_[size_t(i)] = Length(Sub(pose[size_t(i)], pose[size_t(kParent[i])]));
	mode_ = Mode::Balanced;
	balance_ = 1;
	sinceHit_ = still_ = accumulator_ = unsupported_ = weight_ = 0;
	dead_ = active_ = false;
	nativeContact_ = false;
	handedBack_ = false;
	nativeLocomotion_ = false;
	nativeVelocity_ = velocity;
	lastSteps_ = 0;
	recoveryAnchor_ = {};
	recoverySupport_ = {};
}

Vec3 Body::Neck() const
{
	return Neck(position_);
}
Vec3 Body::Neck(const Pose &pose) const
{
	return Add(pose[Chest], Carry(neckOffset_, Axes(rest_), Axes(pose)));
}

Vec3 Body::HeadAxis() const
{
	return HeadAxis(position_);
}
Vec3 Body::HeadAxis(const Pose &pose) const
{
	const Basis original = Axes(rest_), current = Axes(pose);
	const Vec3 originalSpan = Sub(rest_[Head], Add(rest_[Chest], neckOffset_));
	Vec3 axis = Normal(Swing(Carry(originalSpan, original, current), Sub(pose[Head], Neck(pose)),
		Carry(headAxis_, original, current), current.forward), current.up);
	constexpr float cosine = .5f;
	if (Dot(axis, current.up) < cosine) {
		const Vec3 side = Normal(Sub(axis, Mul(current.up, Dot(axis, current.up))), current.forward);
		axis = Add(Mul(current.up, cosine), Mul(side, std::sqrt(1-cosine*cosine)));
	}
	return axis;
}

void Body::Hit(Joint joint, float strength, bool fatal)
{
    ++revision_;
	nativeContact_ = false;
	const bool leg = joint >= HipL;
	balance_ = std::max(0.0f, balance_ - strength * (leg ? .22f : .07f));
	dead_ |= fatal;
	active_ = true;
	sinceHit_ = still_ = 0;
	// Handed back (Recovered), a hit jolts the fading body but doesn't bring it
	// back, nor does his balance fell it; a knockdown's force or death does.
	const bool strong = strength > 5;
	handedBack_ &= !(dead_ || strong);
	if (!handedBack_)
		weight_ = 1;
	if (dead_ || strong || (!handedBack_ && balance_ < .25f))
		mode_ = Mode::Falling;
}
void Body::Impulse(Joint joint, Vec3 delta, bool fatal)
{
	Impact(joint, joint, 0, delta, fatal);
}
void Body::Impact(Joint a, Joint b, float along, Vec3 delta, bool fatal)
{
	if (a < 0 || a >= Count || b < 0 || b >= Count || !std::isfinite(along) || !std::isfinite(Length(delta)))
		return;
	const float strength = Length(delta);
	delta = Limited(delta, 15);
	along = a == b ? 0 : std::clamp(along, 0.0f, 1.0f);
	const float x = 1 - along, y = along;
	const float mass = kInverseMass[a] * x * x + kInverseMass[b] * y * y;
	velocity_[a] = Add(velocity_[a], Mul(delta, kInverseMass[a] * x / mass));
	if (a != b) velocity_[b] = Add(velocity_[b], Mul(delta, kInverseMass[b] * y / mass));
	for (Joint core : {Pelvis, Chest})
		if (core != a && core != b)
			velocity_[core] = Add(velocity_[core], Mul(delta, .12f));
	Hit(along < .5f ? a : b, strength, fatal);
}
void Body::ContactResponse(Joint joint, Vec3 delta, Vec3 normal, Vec3 incomingVelocity, ContactControl control)
{
	if (joint < 0 || joint >= Count || dead_ || !std::isfinite(Length(delta)) || !std::isfinite(Length(normal)) || !std::isfinite(Length(incomingVelocity)))
		return;
    ++revision_;
	if (control == ContactControl::Physical) {
		nativeContact_ = false;
		nativeLocomotion_ = false;
		if (mode_ == Mode::Balanced) {
			const Vec3 carry = Sub(Limited(incomingVelocity, 15), velocity_[Pelvis]);
			for (Vec3 &v : velocity_) v = Add(v, carry);
		}
	} else {
		nativeLocomotion_ = true;
		if (!active_)
			nativeContact_ = true;
	}
	delta = Limited(delta, 8);
	if (control == ContactControl::Physical) {
		velocity_[joint] = Add(velocity_[joint], delta);
		for (Joint core : {Pelvis, Chest})
			if (core != joint) velocity_[core] = Add(velocity_[core], Mul(delta, .12f));
	}
	contactDirection_ = Normal({delta.x, delta.y, 0}, Normal({normal.x, normal.y, 0}, Axes(position_).forward));
	contactEffort_ = std::min(2.0f, contactEffort_ + Length(delta) * .75f);
	contactJoint_ = joint;
	active_ = true;
	sinceHit_ = still_ = 0;
	if (!handedBack_) // fading out, the game's: a hit or contact jolts it, but doesn't bring it back
		weight_ = 1;
}

float Body::Radius(Joint j)
{
	return j == Pelvis ? .16f : j == Chest ? .18f : j == Head ? .12f : (j == FootL || j == FootR) ? .085f : .065f;
}

void Body::Momentum(Vec3 delta, bool fatal)
{
	if (!std::isfinite(Length(delta)))
		return;
    ++revision_;
	nativeContact_ = false;
	delta = Limited(delta, 15);
	for (Vec3 &v : velocity_)
		v = Add(v, delta);
	dead_ |= fatal;
	balance_ = std::max(0.0f, balance_ - Length(delta) * .2f);
	active_ = true;
	sinceHit_ = still_ = 0;
	const bool strong = Length(velocity_[Pelvis]) > 3.5f; // as Impact: a knockdown's force
	handedBack_ &= !(dead_ || strong);
	if (!handedBack_)
		weight_ = 1;
	if (dead_ || strong || (!handedBack_ && balance_ < .25f))
		mode_ = Mode::Falling;
}
void Body::Knockdown(Vec3 delta)
{
	if (!std::isfinite(Length(delta)))
		return;
	handedBack_ = false; // the game's own knockdown
	Momentum(delta);
	balance_ = 0;
	mode_ = Mode::Falling;
}

Contact Body::Constrain(const Pose *previous, Sweep, void *, bool rigidAttachments)
{
    auto core=[&](int joint) {
        return rigidAttachments&&(joint==Pelvis||joint==Chest||joint==ShoulderL||joint==ShoulderR||joint==HipL||joint==HipR);
    };
    constexpr float coreInverse=1/(1/kInverseMass[Pelvis]+1/kInverseMass[Chest]+1/kInverseMass[ShoulderL]+1/kInverseMass[ShoulderR]+1/kInverseMass[HipL]+1/kInverseMass[HipR]);
    auto span = [&](int a, int b, float low, float high) {
        if(core(a)&&core(b)) return;
        const Vec3 d=Sub(position_[size_t(b)],position_[size_t(a)]);
        const float length=Length(d);
        if(length<1e-6f) return;
        const float wanted=std::clamp(length,low,high),wa=core(a)?coreInverse:kInverseMass[a],wb=core(b)?coreInverse:kInverseMass[b];
        const Vec3 correction=Mul(d,(length-wanted)/length/(wa+wb));
        if(core(a)) {
            for(Joint joint:{Pelvis,Chest,ShoulderL,ShoulderR,HipL,HipR})
                position_[joint]=Add(position_[joint],Mul(correction,wa));
        } else position_[size_t(a)]=Add(position_[size_t(a)],Mul(correction,wa));
        if(core(b)) {
            for(Joint joint:{Pelvis,Chest,ShoulderL,ShoulderR,HipL,HipR})
                position_[joint]=Sub(position_[joint],Mul(correction,wb));
        } else position_[size_t(b)]=Sub(position_[size_t(b)],Mul(correction,wb));
    };
    Anatomy(position_,rest_);
    for(int i=1;i<Count;i++) if(i!=Head) span(kParent[i],i,length_[size_t(i)],length_[size_t(i)]);
    for(auto link:{std::array<int,2>{ShoulderL,ShoulderR},{HipL,HipR},{Pelvis,ShoulderL},{Pelvis,ShoulderR},{Chest,HipL},{Chest,HipR}}) {
        const float rest=Length(Sub(rest_[size_t(link[0])],rest_[size_t(link[1])]));
        span(link[0],link[1],rest*.97f,rest*1.03f);
    }
    for(auto limb:{std::array<int,3>{ShoulderL,ElbowL,HandL},{ShoulderR,ElbowR,HandR},{HipL,KneeL,FootL},{HipR,KneeR,FootR}}) {
        const float reach=length_[size_t(limb[1])]+length_[size_t(limb[2])];
        span(limb[0],limb[2],reach*.25f,reach*.995f);
    }
    // Shape-match the ribcage/pelvis as a measured rigid core. Pairwise
    // distance links alone admit a folded or reflected tetrahedron, which
    // moves the apparent neck even while each individual link is short.
    const Basis coreOriginal=Axes(rest_),coreCurrent=Axes(position_);
    Vec3 center{},referenceCenter{}; float mass=0;
    for(Joint core:{Pelvis,Chest,ShoulderL,ShoulderR,HipL,HipR}) {
        const float weight=1/kInverseMass[core];
        center=Add(center,Mul(position_[core],weight));
        referenceCenter=Add(referenceCenter,Mul(Sub(rest_[core],rest_[Pelvis]),weight)); mass+=weight;
    }
    center=Mul(center,1/mass); referenceCenter=Mul(referenceCenter,1/mass);
    for(Joint core:{Pelvis,Chest,ShoulderL,ShoulderR,HipL,HipR})
        position_[core]=Add(center,Carry(Sub(Sub(rest_[core],rest_[Pelvis]),referenceCenter),coreOriginal,coreCurrent));
    const reactself::HeadCenters headCenters{
        reactshape::Skull(position_,HeadAxis()),
        reactshape::Skull(previous?*previous:position_,HeadAxis(previous?*previous:position_)),
        reactshape::Skull(rest_,HeadAxis(rest_))};
    reactself::Project(position_,rest_,kInverseMass,previous,nullptr,nullptr,nullptr,true,&headCenters);
    const Vec3 neck=Neck();
    const Basis original=Axes(rest_),current=Axes(position_);
    const Vec3 restDirection=Normal(Sub(rest_[Head],Add(rest_[Chest],neckOffset_)),original.up);
    const Vec3 reference=Carry(restDirection,original,current);
    Vec3 direction=Normal(Sub(position_[Head],neck),reference);
    constexpr float cosine=.70710678f;
    if(Dot(direction,reference)<cosine) {
        const Vec3 side=Normal(Sub(direction,Mul(reference,Dot(direction,reference))),current.forward);
        direction=Add(Mul(reference,cosine),Mul(side,cosine));
    }
    const Vec3 correction=Sub(Add(neck,Mul(direction,headLength_)),position_[Head]);
    // A blocked skull pushes its attached torso through the same constraint.
    // Moving the whole core preserves its frame and avoids snapping the head
    // back through a world plane after solving that plane.
    constexpr float coreInverseMass=1/(1/kInverseMass[Pelvis]+1/kInverseMass[Chest]+1/kInverseMass[ShoulderL]+1/kInverseMass[ShoulderR]+1/kInverseMass[HipL]+1/kInverseMass[HipR]);
    constexpr float headShare=kInverseMass[Head]/(kInverseMass[Head]+coreInverseMass);
    position_[Head]=Add(position_[Head],Mul(correction,headShare));
    for(Joint core:{Pelvis,Chest,ShoulderL,ShoulderR,HipL,HipR})
        position_[core]=Sub(position_[core],Mul(correction,1-headShare));
    return {};
}

Contact Body::SolveWorld(const Pose &previous,Sweep sweep,void *context,std::array<Contact,Count> &contacts)
{
    struct Plane {
        reactshape::Sphere sphere;
        Vec3 normal{};
        float offset=0;
        bool skull=false;
    };
    std::array<Plane,256> planes{};
    int planeCount=0,sweepCount=0;
    Contact upper{};
    auto error=[&] {
        float worst=0;
        for(int i=1;i<Count;i++) if(i!=Head)
            worst=std::max(worst,std::fabs(Length(Sub(position_[size_t(i)],position_[size_t(kParent[i])]))-length_[size_t(i)]));
        return std::max(worst,std::fabs(Length(Sub(position_[Head],Neck()))-headLength_));
    };
    auto skull=[&](const Pose &pose) {return Add(pose[Head],Mul(HeadAxis(pose),.105f));};
    int volumeCount=1;
    reactshape::Cover(rest_,[&](const reactshape::Sphere &){++volumeCount;});
    auto gather=[&](bool retain,bool stationary,bool fallback=false) {
        bool clear=true;
        auto query=[&](const reactshape::Sphere &sphere,bool isSkull) {
            const Vec3 goal=isSkull?skull(position_):sphere.Position(position_);
            const Vec3 from=stationary?goal:(isSkull?skull(previous):sphere.Position(previous));
            // Reserve a complete current-time fallback check. Exhausting a
            // candidate's work must not also make the previous pose unknown.
            const int limit=fallback?kMaxWorldSweeps:kMaxWorldSweeps-volumeCount;
            if(sweepCount>=limit) {clear=false;return;}
            ++sweepCount;
            const Contact hit=sweep(from,goal,sphere.radius,context);
            const bool valid=hit.resolved&&std::isfinite(Length(hit.position))&&std::isfinite(Length(hit.normal));
            const float distance=valid?Length(Sub(hit.position,goal)):1;
            if(!valid||distance>.0005f) clear=false;
            if(!retain||!hit.hit||Length(hit.normal)<.5f||!valid) return;
            const Vec3 normal=Normal(hit.normal,{0,0,1});
            Plane plane{sphere,normal,Dot(hit.position,normal),isSkull};
            // Keep local separating planes, not independent point positions.
            // Anatomical constraints remain free to move tangentially.
            bool found=false;
            for(int i=0;i<planeCount;i++) {
                Plane &old=planes[size_t(i)];
                if(old.skull==isSkull&&old.sphere.capsule.a==sphere.capsule.a&&old.sphere.capsule.b==sphere.capsule.b&&
                   old.sphere.capsule.endOther==sphere.capsule.endOther&&old.sphere.along==sphere.along&&Dot(old.normal,normal)>.995f) {
                    old.offset=std::max(old.offset,plane.offset); found=true; break;
                }
            }
            if(!found&&planeCount<int(planes.size())) planes[size_t(planeCount++)]=plane;
            const auto gradient=reactshape::Weights(sphere.capsule,sphere.along);
            for(int i=0;i<Count;i++) if(gradient[size_t(i)]>0) contacts[size_t(i)]=hit;
            if(isSkull||(sphere.capsule.a==Pelvis&&sphere.capsule.endOther==ShoulderR)) upper=hit;
        };
        reactshape::Cover(rest_,[&](const reactshape::Sphere &sphere){query(sphere,false);},&previous,&position_);
        query({{Head,Head,.105f},0,.105f},true);
        return clear;
    };
    for(int i=0;i<(sweep?8:20);i++) Constrain(&previous,nullptr,nullptr);
    if(!sweep) return upper;
    auto projectPlanes=[&] {
        for(int p=0;p<planeCount;p++) {
            const Plane &plane=planes[size_t(p)];
            const Vec3 point=plane.skull?skull(position_):plane.sphere.Position(position_);
            const float penetration=plane.offset-Dot(point,plane.normal);
            if(penetration<=0) continue;
            const auto gradient=reactshape::Weights(plane.sphere.capsule,plane.sphere.along);
            float inverseMass=0;
            for(int i=0;i<Count;i++) inverseMass+=kInverseMass[i]*gradient[size_t(i)]*gradient[size_t(i)];
            if(inverseMass<1e-8f) continue;
            for(int i=0;i<Count;i++) position_[size_t(i)]=Add(position_[size_t(i)],Mul(plane.normal,penetration*kInverseMass[i]*gradient[size_t(i)]/inverseMass));
        }
    };
    bool solved=false;
    for(int round=0;round<6;round++) {
        const bool clear=gather(true,true);
        if(clear&&error()<.0075f&&gather(true,false)) {solved=true;break;}
        for(int iteration=0;iteration<20;iteration++) {
            Constrain(&previous,nullptr,nullptr);
            projectPlanes();
        }
    }
    if(!solved&&mode_==Mode::Recovering&&error()>=.01f&&sweepCount+3*volumeCount<=kMaxWorldSweeps) {
        // A trapped limb can pull its shoulder/hip out of the core on each
        // pass, only for shape matching to undo that correction. Repeating
        // that rejected candidate rolls back to the same motionless pose.
        // Retry its attachment constraints against the measured whole core,
        // as self/head constraints already do. Ordinary successful falling
        // and get-up dynamics retain their existing angular response.
        for(int iteration=0;iteration<20;iteration++) {
            Constrain(&previous,nullptr,nullptr,true);
            projectPlanes();
        }
    }
    if(!solved) solved=error()<.01f&&gather(false,true)&&gather(false,false);
    if(!solved) {
        // Roll back a complete coherent pose, never fifteen independently
        // clipped points. Historical clearance is rechecked against the
        // current world: moving geometry can invalidate the old pose too.
        position_=previous;
        if(!gather(false,true,true)) {
            // No certified pose remains. Release ownership so native physics
            // can separate the actor, instead of suppressing it indefinitely.
            active_=false; weight_=0;
        }
    }
    return upper;
}

bool Body::SupportedRest(Sweep sweep,void *context) const
{
    if(!sweep) return false;
    bool clear=true,supported=false;
    auto query=[&](Vec3 point,float radius) {
        if(!clear) return;
        const Contact contact=sweep(point,point,radius,context);
        clear=contact.resolved&&std::isfinite(Length(contact.position))&&Length(Sub(contact.position,point))<=.0005f;
        if(clear&&!supported) {
            const Contact floor=sweep(point,Add(point,{0,0,-.04f}),radius,context);
            supported=floor.resolved&&floor.hit&&std::isfinite(Length(floor.normal))&&floor.normal.z>.4f;
        }
    };
    reactshape::Cover(rest_,[&](const reactshape::Sphere &sphere){query(sphere.Position(position_),sphere.radius);},&position_);
    query(reactshape::Skull(position_,HeadAxis()),reactshape::kSkullRadius);
    return clear&&supported;
}

void Body::Step(const Pose &target, Sweep sweep, void *context, Vec3 targetVelocity)
{
    if(dead_&&mode_==Mode::Settled) {
        if(SupportedRest(sweep,context)) {
            velocity_.fill({});
            return;
        }
        mode_=Mode::Falling;
        still_=0;
    }
	sinceHit_ += kStep;
	contactEffort_ *= std::exp(-3 * kStep);
	const bool upright = mode_ == Mode::Balanced || mode_ == Mode::Recovering;
	Pose goals = target;
	if (mode_ == Mode::Recovering) {
		// Native GetUp starts prone. Anchor its lowest joint to supporting
		// ground and let the animation raise the pelvis progressively; lifting
		// a prone pelvis straight to standing height would also lift both feet.
		Vec3 offset = Sub(recoveryAnchor_, target[Pelvis]);
		if (recoverySupport_.hit) {
			offset.z = 0;
			const Vec3 n = recoverySupport_.normal;
			float lowest = Dot(Add(target[0], offset), n) - Radius(Pelvis);
			for (int i = 1; i < Count; i++)
				lowest = std::min(lowest, Dot(Add(target[size_t(i)], offset), n) - Radius(Joint(i)));
			offset.z = (Dot(recoverySupport_.position, n) - Radius(FootL) - lowest) / n.z;
		}
		for (Vec3 &goal : goals)
			goal = Add(goal, offset);
	}
	if (mode_ == Mode::Balanced && !dead_ && contactEffort_ > .01f) {
		const Joint movingFoot = contactJoint_ == HipR || contactJoint_ == KneeR || contactJoint_ == FootR ? FootL : FootR;
		const Vec3 shift = Mul(contactDirection_, .16f * contactEffort_);
		Vec3 footGoal = Add(goals[movingFoot], shift);
		bool support = !sweep;
		if (sweep) {
			const Contact clear = sweep(goals[movingFoot], footGoal, Radius(movingFoot), context);
			footGoal = clear.position;
			const Contact floor = sweep(Add(footGoal,{0,0,.08f}), Add(footGoal,{0,0,-.24f}), Radius(movingFoot), context);
			support = floor.hit && floor.normal.z > .55f;
			if (support) footGoal.z = std::max(goals[movingFoot].z, floor.position.z);
		}
		if (support) {
			// A bounded lift followed by placement recruits a new support
			// region. Unsupported destinations cannot become phantom steps.
			footGoal.z += .045f * contactEffort_ * std::max(0.0f, std::sin(std::min(sinceHit_/.3f,1.0f)*3.14159265f));
			goals[movingFoot] = footGoal;
		}
		for (Joint hand : {HandL, HandR}) {
			goals[hand] = Add(goals[hand], Mul(contactDirection_, -.24f*contactEffort_));
			goals[hand].z += .18f*contactEffort_;
		}
	}
	Pose old = position_;
    double priorKinetic=0;
    if(dead_) for(int i=0;i<Count;i++)
        priorKinetic+=.5*double(Dot(velocity_[size_t(i)],velocity_[size_t(i)]))/kInverseMass[i];
	Vec3 step{};
	if (mode_ == Mode::Balanced && !dead_) {
		const Vec3 feet = Mul(Add(position_[FootL], position_[FootR]), .5f);
		step = Limited({position_[Pelvis].x - feet.x, position_[Pelvis].y - feet.y, 0}, .3f);
	}
	for (int i = 0; i < Count; i++) {
		Vec3 acceleration{0, 0, upright ? 0.0f : -20.0f};
		if (upright && !dead_) {
			Vec3 goal = goals[size_t(i)];
			if (i == FootL || i == FootR)
				goal = Add(goal, step);
			const float spring = i == Pelvis || i == Chest ? 85.0f : i == FootL || i == FootR ? 110.0f : 40.0f;
			acceleration = Add(acceleration, Sub(Mul(Sub(goal, position_[size_t(i)]), spring),
												 Mul(Sub(velocity_[size_t(i)], mode_ == Mode::Balanced ? targetVelocity : Vec3{}), 2 * std::sqrt(spring))));
		} else {
			velocity_[size_t(i)] = Mul(velocity_[size_t(i)], std::exp(-.55f * kStep));
			if (!dead_ && mode_ == Mode::Falling && (i == HandL || i == HandR)) {
				Vec3 forward = velocity_[Pelvis];
				forward.z = 0;
				if (Length(forward) < .1f)
					forward = Sub(position_[Chest], position_[Pelvis]);
				forward.z = 0;
				forward = Length(forward) > .001f ? Mul(forward, 1 / Length(forward)) : Vec3{0, 1, 0};
				const Vec3 side = {forward.y, -forward.x, 0};
				Vec3 brace = Add(position_[Pelvis], Add(Mul(forward, .6f), Mul(side, i == HandL ? -.22f : .22f)));
				brace.z -= .5f;
				if (sweep)
					brace = sweep(position_[size_t(i)], brace, Radius(Joint(i)), context).position;
				acceleration =
					Add(acceleration,
						Limited(Sub(Mul(Sub(brace, position_[size_t(i)]), 35), Mul(velocity_[size_t(i)], 5)), 60));
			}
		}
		velocity_[size_t(i)] = Add(velocity_[size_t(i)], Mul(acceleration, kStep));
		position_[size_t(i)] = Add(position_[size_t(i)], Mul(velocity_[size_t(i)], kStep));
    }
    std::array<Contact,Count> jointContacts{};
    Contact upperContact=SolveWorld(old,sweep,context,jointContacts);
	float motion = 0;
	bool supported = false;
	std::array<Vec3, Count + 1> supports{};
	int supportCount = 0;
	if (upperContact.hit && upperContact.normal.z > .4f) {
		supported = true;
		supports[size_t(supportCount++)] = upperContact.position;
	}
	for (int i = 0; i < Count; i++) {
		const Contact &contact = jointContacts[size_t(i)];
		// The final coupled repair is depenetration, not added momentum.
		Vec3 v = Mul(Sub(position_[size_t(i)], old[size_t(i)]), 1/kStep);
		const bool upper = (i == Pelvis || i == Chest || i == Head || i == ShoulderL || i == ShoulderR || i == HipL || i == HipR) && upperContact.hit;
		auto terrainVelocity = [&](const Contact &hit) {
			const float normalVelocity = Dot(v, hit.normal);
			const float outgoing = std::max(0.0f, Dot(velocity_[size_t(i)], hit.normal));
			// Projection is a position repair, not restitution. Preserve actual
			// outgoing motion, but do not turn depenetration into an upward kick.
			v = Sub(v, Mul(hit.normal, normalVelocity - std::clamp(normalVelocity, 0.0f, outgoing)));
		};
		if (contact.hit) terrainVelocity(contact);
		if (upper) terrainVelocity(upperContact);
		if (contact.hit || upper) {
			v = Mul(v, .75f);
		}
		velocity_[size_t(i)] = Limited(v, 25);
		motion += Dot(velocity_[size_t(i)],velocity_[size_t(i)]);
		if ((mode_ == Mode::Recovering || i == FootL || i == FootR) && sweep) {
			Vec3 down = position_[size_t(i)];
			down.z -= .12f;
			const Contact support = sweep(position_[size_t(i)], down, Radius(Joint(i)), context);
			if (support.hit && support.normal.z > .4f) {
				supported = true;
				supports[size_t(supportCount++)] = position_[size_t(i)];
			}
		}
	}
    if(dead_&&upperContact.hit&&upperContact.normal.z>.4f) {
        double kinetic=0,available=priorKinetic;
        for(int i=0;i<Count;i++) {
            kinetic+=.5*double(Dot(velocity_[size_t(i)],velocity_[size_t(i)]))/kInverseMass[i];
            available+=20*(double(old[size_t(i)].z)-position_[size_t(i)].z)/kInverseMass[i];
        }
        // After upper-body landing, passive contact repair is not a motor.
        // Bound derived kinetic energy by incoming energy plus gravity work;
        // leave free falling motion and living controllers untouched. Native
        // impulses have already entered the incoming kinetic budget.
        available=std::max(0.0,available);
        if(kinetic>available&&kinetic>1e-10) {
            const float scale=float(std::sqrt(available/kinetic));
            motion=0;
            for(Vec3 &velocity:velocity_) {velocity=Mul(velocity,scale);motion+=Dot(velocity,velocity);}
        }
    }
	if (mode_ == Mode::Balanced && !dead_ && !nativeContact_) {
		const Vec3 com = Add(Mul(position_[Pelvis], .65f), Mul(position_[Chest], .35f));
		const Vec3 relative = Sub(Add(Mul(velocity_[Pelvis],.65f),Mul(velocity_[Chest],.35f)), targetVelocity);
		const float feetHeight = std::min(position_[FootL].z, position_[FootR].z);
		const float fallTime = std::sqrt(std::max(.1f, com.z-feetHeight)/20.0f);
		const Vec3 capture = Add(com, Mul(relative, fallTime));
		const float outside = sweep && contactEffort_ > .01f ? SupportDistance(capture,supports,supportCount) : Length(step);
		balance_ = std::clamp(balance_ + kStep * (outside <= .18f ? .45f : -.8f),0.0f,1.0f);
		// A corrective foot can recruit at most 28 cm of fresh support.
		// A moving COM whose capture point lies beyond that reach has
		// actual irrecoverable momentum, even when a stopped foot touches.
		if (!handedBack_ && sweep && contactEffort_ > .01f && supportCount && outside > .28f && Length({relative.x,relative.y,0}) > 1.5f)
			mode_ = Mode::Falling;
	}
	if (mode_ == Mode::Recovering && !dead_) {
		// A prone GetUp bears weight through torso, hands, and knees before
		// transferring onto its feet. Balance against the actual support region.
		const bool balanced = !sweep || SupportDistance(position_[Pelvis], supports, supportCount) <= .18f;
		balance_ = std::clamp(balance_ + kStep * (balanced ? .45f : -.8f), 0.0f, 1.0f);
	}
	unsupported_ = supported ? 0 : unsupported_ + kStep;
	if (!dead_ && !nativeContact_ && !handedBack_ && upright && (balance_ < .25f || (sweep && unsupported_ > .2f)))
		mode_ = Mode::Falling;
	still_ = motion / Count < .06f ? still_ + kStep : 0;
	if (mode_ == Mode::Falling && still_ > .55f) {
        if (dead_) {
            if(SupportedRest(sweep,context)) {
                mode_=Mode::Settled;
                velocity_.fill({});
            }
        } else if (sinceHit_ > 1.2f) {
			recoveryAnchor_ = position_[Pelvis];
			recoverySupport_ = {};
			bool ground = true;
			if (sweep) {
				Vec3 above = position_[Pelvis], below = position_[Pelvis];
				above.z += 1.25f;
				below.z -= 2.5f;
				const Contact floor = sweep(above, below, Radius(FootL), context);
				ground = floor.hit && floor.normal.z > .4f;
				if (ground) {
					recoverySupport_ = floor;
					recoveryAnchor_.z = floor.position.z + rest_[Pelvis].z - std::min(rest_[FootL].z, rest_[FootR].z);
				}
			}
			if (ground) {
				mode_ = Mode::Recovering;
				balance_ = .5f;
			}
		}
	}
	if (mode_ == Mode::Recovering && sinceHit_ > 2.5f && balance_ > .9f) {
		const bool highEnough = position_[Pelvis].z > recoveryAnchor_.z - .1f;
		const bool uprightPose = goals[Head].z - goals[Pelvis].z > (rest_[Head].z - rest_[Pelvis].z) * .6f;
		if (highEnough && uprightPose && Length(Sub(position_[Pelvis], recoveryAnchor_)) < .15f)
			mode_ = Mode::Balanced;
	}
	if (mode_ == Mode::Balanced && (sinceHit_ > .8f || handedBack_)) {
		weight_ = std::max(0.0f, weight_ - kStep * 1.5f);
		if (weight_ <= 0)
			active_ = handedBack_ = false;
	}
}

void Body::Recovered()
{
	if (!active_ || dead_ || mode_ != Mode::Recovering)
		return;
	mode_ = Mode::Balanced;
	balance_ = 1;
	handedBack_ = true;
}

float Body::Translate(Vec3 delta,Sweep sweep,void *context)
{
    const float distance=Length(delta);
    if(!active_||!std::isfinite(distance)||distance>4) return 0;
    if(distance<1e-7f) return 1;
    float fraction=1;
    auto query=[&](Vec3 from,float radius,bool collect) {
        if(collect&&fraction<=1e-5f) return false;
        const Vec3 goal=Add(from,Mul(delta,fraction));
        if(!sweep) return true;
        Contact hit=sweep(from,goal,radius,context);
        const Contact overlap=sweep(goal,goal,radius,context);
        if(!hit.resolved||!overlap.resolved||!std::isfinite(Length(hit.position))||!std::isfinite(Length(hit.normal))||
           !std::isfinite(Length(overlap.position))||!std::isfinite(Length(overlap.normal))) return false;
        if(Length(Sub(overlap.position,goal))>Length(Sub(hit.position,goal))) hit=overlap;
        if(Length(Sub(hit.position,goal))<=.0005f) return true;
        if(collect) {
            const float into=Dot(delta,hit.normal);
            const float allowed=into<-.000001f?Dot(Sub(hit.position,from),hit.normal)/into:0;
            fraction=std::clamp(std::min(fraction,allowed),0.f,1.f);
        }
        return false;
    };
    reactshape::Cover(rest_,[&](const reactshape::Sphere &sphere){query(sphere.Position(position_),sphere.radius,true);},&position_);
    query(Add(position_[Head],Mul(HeadAxis(),.105f)),.105f,true);
    for(int retry=0;retry<8&&fraction>1e-5f;retry++) {
        bool clear=true;
        reactshape::Cover(rest_,[&](const reactshape::Sphere &sphere){clear=clear&&query(sphere.Position(position_),sphere.radius,false);},&position_);
        clear=clear&&query(Add(position_[Head],Mul(HeadAxis(),.105f)),.105f,false);
        if(clear) {
            ++revision_;
            for(Vec3 &position:position_) position=Add(position,Mul(delta,fraction));
            return fraction;
        }
        fraction*=.5f;
    }
    return 0;
}

Vec3 Body::CenterVelocity() const
{
    Vec3 center{};float mass=0;
    for(int i=0;i<Count;i++) {
        const float weight=1/kInverseMass[i];
        center=Add(center,Mul(velocity_[size_t(i)],weight));mass+=weight;
    }
    return Mul(center,1/mass);
}

void Body::ResolveVelocity(Vec3 normal,float speed)
{
    const float length=Length(normal);
    if(!active_||!std::isfinite(length)||length<.5f||!std::isfinite(speed)) return;
    normal=Mul(normal,1/length);
    const float closing=Dot(CenterVelocity(),normal)-speed;
    if(closing>=0) return;
    // Crowd position repair has whole-body translation degrees of freedom.
    // Apply its normal impulse to that same mass-weighted velocity, preserving
    // internal articulation instead of independently accelerating light joints.
    for(Vec3 &velocity:velocity_) velocity=Add(velocity,Mul(normal,-closing));
    ++revision_;
    if(mode_==Mode::Settled) {mode_=Mode::Falling;still_=0;}
}

void Body::Advance(float dt, const Pose &target, Sweep sweep, void *context)
{
	lastSteps_ = 0;
	frameStart_ = position_;
	if (!active_ || !std::isfinite(dt) || dt <= 0)
		return;
    ++revision_;
	Vec3 targetVelocity = Limited(Mul(Sub(target[Pelvis], targetPelvis_), 1/std::max(dt,kStep)), 15);
	targetVelocity.z = 0;
	if (nativeLocomotion_ && mode_ == Mode::Balanced) {
		// Carry changes in the accepted native trajectory once, preserving
		// any independently applied physical impulse relative to that motion.
		// A contact-only pose has no physical COM impulse to retain.
		const Vec3 carry = Sub(targetVelocity, nativeContact_ ? CenterVelocity() : nativeVelocity_);
		for (Vec3 &velocity : velocity_)
			velocity = Add(velocity, carry);
	}
	nativeVelocity_ = targetVelocity;
	targetPelvis_ = target[Pelvis];
	accumulator_ = std::min(accumulator_ + dt, 4 * kStep);
	while (accumulator_ >= kStep - 1e-6f && lastSteps_ < 4) {
		Step(target, sweep, context, targetVelocity);
		accumulator_ -= kStep;
		lastSteps_++;
	}
}
} // namespace reactphysics
