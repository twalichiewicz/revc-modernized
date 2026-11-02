#pragma once
#include "reactphysics.h"
#include <algorithm>
#include <array>
#include <cmath>

// One measured anatomical description for terrain and inter-body contacts.
namespace reactshape {
using namespace reactphysics;
constexpr float kSkullRadius=.105f,kSkullOffset=.105f;
inline Vec3 Add(Vec3 a,Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec3 Sub(Vec3 a,Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec3 Mul(Vec3 a,float s) { return {a.x*s,a.y*s,a.z*s}; }
inline float Dot(Vec3 a,Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline float Length(Vec3 a) { return std::sqrt(Dot(a,a)); }
inline Vec3 Skull(const Pose &pose,Vec3 axis) {return Add(pose[Head],Mul(axis,kSkullOffset));}
struct Capsule {
	int a,b; float radius; int endOther=-1;
};
inline Vec3 At(const Pose &pose,const Capsule &c,float t)
{
	const Vec3 end=c.endOther<0?pose[size_t(c.b)]:Mul(Add(pose[size_t(c.b)],pose[size_t(c.endOther)]),.5f);
	return Add(Mul(pose[size_t(c.a)],1-t),Mul(end,t));
}
inline std::array<float,Count> Weights(const Capsule &c,float t)
{
	std::array<float,Count> g{};
	g[size_t(c.a)]+=1-t;
	g[size_t(c.b)]+=t*(c.endOther<0?1:.5f);
	if(c.endOther>=0) g[size_t(c.endOther)]+=t*.5f;
	return g;
}
inline std::array<Capsule,10> Capsules(const Pose &rest)
{
	const float height=Length(Sub(rest[Chest],rest[Pelvis]));
	const float width=Length(Sub(rest[ShoulderR],rest[ShoulderL]));
	auto radius=[&](int a,int b,float proportion) {return std::min(Length(Sub(rest[size_t(a)],rest[size_t(b)]))*proportion,width*.2f);};
	return {{{Pelvis,ShoulderL,std::min(width*.4f,height*.55f),ShoulderR},
		{Head,Head,std::min(width*.25f,height*.32f)},
		{ShoulderL,ElbowL,radius(ShoulderL,ElbowL,.2f)}, {ElbowL,HandL,radius(ElbowL,HandL,.2f)},
		{ShoulderR,ElbowR,radius(ShoulderR,ElbowR,.2f)}, {ElbowR,HandR,radius(ElbowR,HandR,.2f)},
		{HipL,KneeL,radius(HipL,KneeL,.22f)}, {KneeL,FootL,radius(KneeL,FootL,.18f)},
		{HipR,KneeR,radius(HipR,KneeR,.22f)}, {KneeR,FootR,radius(KneeR,FootR,.18f)}}};
}
struct Sphere {
	Capsule capsule; float along=0,radius=0;
	Vec3 Position(const Pose &pose) const {return At(pose,capsule,along);}
};
template<class Visit> void Cover(const Pose &rest,Visit visit,const Pose *from=nullptr,const Pose *to=nullptr)
{
	for(int joint=0;joint<Count;joint++)
		visit(Sphere{{joint,joint,Body::Radius(Joint(joint))},0,Body::Radius(Joint(joint))});
	for(const Capsule &capsule:Capsules(rest)) {
		if(capsule.a==capsule.b||capsule.radius<.01f) continue;
		const float length=Length(Sub(At(rest,capsule,1),At(rest,capsule,0)));
		const int divisions=std::clamp(int(std::ceil(length/(2*capsule.radius))),1,8);
		float span=length;
		if(from) span=std::max(span,Length(Sub(At(*from,capsule,1),At(*from,capsule,0))));
		if(to) span=std::max(span,Length(Sub(At(*to,capsule,1),At(*to,capsule,0))));
		const float halfSpacing=span/(2*divisions);
		// A circumscribed sphere at each grid point covers the entire capsule
		// cross-section halfway to its neighbor, including endpoints.
		const float radius=std::sqrt(capsule.radius*capsule.radius+halfSpacing*halfSpacing);
		for(int i=0;i<=divisions;i++) visit(Sphere{capsule,float(i)/divisions,radius});
	}
}
} // namespace reactshape
