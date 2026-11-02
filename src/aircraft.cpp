#include "aircraft.h"
#include <algorithm>
#include <cmath>

namespace aircraft {
namespace {
Vec3 Add(Vec3 a, Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vec3 Mul(Vec3 a, float k) { return {a.x*k,a.y*k,a.z*k}; }
float Dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
float Length(Vec3 a) { return std::sqrt(Dot(a,a)); }
Vec3 Unit(Vec3 a) { const float l=Length(a); return l>1e-5f?Mul(a,1/l):Vec3{1,0,0}; }
Vec3 World(const Pose &p, Vec3 local) { return Add(p.position,Add(Mul(p.right,local.x),Add(Mul(p.forward,local.y),Mul(p.up,local.z)))); }
struct CrashContact { Sweep sweep; void *context; };
Contact PhysicalContact(Vec3 from,Vec3 to,float navigationRadius,void *context)
{
	const auto &c=*static_cast<const CrashContact *>(context);
	// Flying maintains a 0.5 m safety margin. A crash must reach the actual hull.
	return c.sweep?c.sweep(from,to,std::max(0.1f,navigationRadius-0.5f),c.context):Contact{};
}
Contact Body(const Pose &pose, Vec3 delta, Sweep sweep, void *context)
{
	Contact best;
	if (!sweep) return best;
	struct Sphere { Vec3 local; float radius; };
	const Sphere body[] = {{{0,0.3f,0.3f},1.65f},{{0,2.4f,0.3f},1.2f},{{0,-3.4f,1},0.8f},{{0,-6.6f,1.2f},1.1f}};
	for (const Sphere &s:body) {
		const Vec3 from=World(pose,s.local);
		const Contact c=sweep(from,Add(from,delta),s.radius,context);
		if (c.hit && c.fraction<best.fraction) best=c;
	}
	for (int i=0;i<8;i++) {
		const float a=float(i)*0.78539816f;
		const Vec3 from=World(pose,{5.6f*std::cos(a),5.6f*std::sin(a),2.0f});
		const Contact c=sweep(from,Add(from,delta),0.6f,context);
		if (c.hit && c.fraction<best.fraction) best=c;
	}
	return best;
}
}

Motion Move(const Pose &pose, Vec3 delta, Vec3 velocity, Sweep sweep, void *context)
{
	Motion result{pose.position,velocity,{},false};
	Pose at=pose;
	for (int i=0;i<3 && Length(delta)>1e-5f;i++) {
		const Contact c=Body(at,delta,sweep,context);
		if (!c.hit) { result.position=Add(at.position,delta); break; }
		result.contact=true; result.normal=Unit(c.normal);
		const float t=std::clamp(c.fraction-0.001f/std::max(Length(delta),0.001f),0.0f,1.0f);
		result.position=Add(at.position,Mul(delta,t));
		delta=Mul(delta,1-t);
		const float into=Dot(delta,result.normal);
		if (into<0) delta=Sub(delta,Mul(result.normal,into));
		const float speed=Dot(result.velocity,result.normal);
		if (speed<0) result.velocity=Sub(result.velocity,Mul(result.normal,speed));
		at.position=result.position;
	}
	return result;
}
Motion Separate(Vec3 from, Vec3 to, Vec3 velocity, Vec3 other, Vec3 otherVelocity, float dt)
{
	// Flattened rotor/body envelope: 13.2 m lateral, 4.4 m vertical pair clearance.
	constexpr float radius=13.2f;
	auto scaled=[](Vec3 a) { return Vec3{a.x,a.y,a.z*3}; };
	const Vec3 a=scaled(Sub(from,other)), end=scaled(Sub(to,Add(other,Mul(otherVelocity,std::max(0.0f,dt)))));
	const Vec3 d=Sub(end,a);
	const float aa=Dot(d,d), b=Dot(a,d), c=Dot(a,a)-radius*radius;
	float t=0;
	if (c>0) {
		const float h=b*b-aa*c;
		if (aa<1e-6f || h<0 || b>=0) return {to,velocity,{},false};
		t=(-b-std::sqrt(h))/aa;
		if (t<0 || t>1) return {to,velocity,{},false};
	}
	const Vec3 n=Unit(Add(a,Mul(d,t)));
	Vec3 position=Add(from,Mul(Sub(to,from),std::max(0.0f,t-0.001f)));
	if (c<=0) position=Add(other,{n.x*(radius+0.01f),n.y*(radius+0.01f),n.z*(radius+0.01f)/3});
	const Vec3 normal=Unit({n.x,n.y,n.z*3});
	const float closing=Dot(Sub(velocity,otherVelocity),normal);
	if (closing<0) velocity=Sub(velocity,Mul(normal,closing));
	return {position,velocity,normal,true};
}
Vec3 Escape(const Pose &pose, Vec3 desired, int side, Sweep sweep, void *context, float reach)
{
	const Vec3 d=Unit(desired), lateral=Unit({-d.y*float(side),d.x*float(side),0});
	const Vec3 choices[]={d,Unit(Add(d,{0,0,1})),lateral,Mul(lateral,-1),{0,0,1},Mul(d,-1),{0,0,-1}};
	Vec3 best{}; float score=-1;
	for (Vec3 direction:choices) {
		const Contact c=Body(pose,Mul(direction,std::max(6.0f,reach)),sweep,context);
		const float clear=c.hit?c.fraction:1.0f;
		const float preference=clear + 0.04f*Dot(d,direction);
		if (clear>0.05f && preference>score) { best=direction; score=preference; }
	}
	return best;
}
float Spotlight(float daylight, float lamp)
{
	const float x=std::clamp(1.0f-daylight,0.0f,1.0f);
	return std::clamp(lamp,0.0f,1.0f)*x*x*(3-2*x);
}
void Crash::Disable(Failure failure, Vec3 momentum)
{
	if (phase!=Phase::Flying) return;
	cause=failure; velocity=momentum; phase=Phase::Descending;
}
Motion Crash::Step(const Pose &pose, float dt, Sweep sweep, void *context)
{
	Motion result{pose.position,velocity,{},false};
	if (phase!=Phase::Descending || !std::isfinite(dt) || dt<=0) return result;
	float remaining=std::min(dt,8.0f/60.0f);
	Pose at=pose;
	CrashContact physical{sweep,context};
	for (int i=0;i<8 && remaining>1e-6f;i++) {
		const float step=std::min(remaining,1.0f/60.0f); remaining-=step;
		elapsed+=step;
		const float drag=std::exp(-0.18f*step);
		velocity.x*=drag; velocity.y*=drag;
		velocity.z=std::max(-45.0f,velocity.z-6.5f*step);
		const Vec3 impactVelocity=velocity;
		result=Move(at,Mul(velocity,step),velocity,PhysicalContact,&physical);
		fallen+=std::max(0.0f,at.position.z-result.position.z);
		velocity=result.velocity; at.position=result.position;
		if (result.contact && (result.normal.z>0.4f || -Dot(impactVelocity,result.normal)>6.0f)) {
			phase=Phase::Impacted; velocity={}; result.velocity={}; break;
		}
	}
	return result;
}
} // namespace aircraft
