#include "playerreact.h"
#include <algorithm>
#include <cmath>

namespace playerreact {
namespace {
vm::Vec3 V(Vec3 p) { return {p.x,p.y,p.z}; }
bool Finite(vm::Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
vm::Vec3 At(const Matrix &m) { return {m[12],m[13],m[14]}; }
void Transform(Matrix &m, const vm::Frame &by)
{
	// Keep the native flags/padding and scale; change axes by a rigid rotation only.
	const vm::Frame f=vm::Compose(by,vm::FromRw(m.data()));
	const vm::Vec3 columns[4]={f.x,f.y,f.z,f.pos};
	for(int i=0;i<4;++i) {m[size_t(i*4)]=columns[i].x;m[size_t(i*4+1)]=columns[i].y;m[size_t(i*4+2)]=columns[i].z;}
}
void Reach(Matrices &m,int upper,vm::Vec3 target,vm::Vec3 forward,vm::Vec3 pole)
{
	const vm::Vec3 shoulder=At(m[size_t(upper)]), elbow0=At(m[size_t(upper+1)]), wrist0=At(m[size_t(upper+2)]);
	const float a=vm::Length(vm::Sub(elbow0,shoulder)), b=vm::Length(vm::Sub(wrist0,elbow0));
	const ArmReach reached=ProtectiveReach(shoulder,a,b,target,forward,pole);
	if(!reached.valid)return;
	const vm::Vec3 elbow=reached.elbow;
	target=reached.wrist;
	const vm::Frame upperTurn=vm::Swing(vm::Sub(elbow0,shoulder),vm::Sub(elbow,shoulder),shoulder);
	for(int i=upper;i<=upper+3;++i) Transform(m[size_t(i)],upperTurn);
	const vm::Frame foreTurn=vm::Swing(vm::Sub(At(m[size_t(upper+2)]),elbow),vm::Sub(target,elbow),elbow);
	for(int i=upper+1;i<=upper+3;++i) Transform(m[size_t(i)],foreTurn);
}
}
ArmReach ProtectiveReach(vm::Vec3 shoulder,float a,float b,vm::Vec3 target,vm::Vec3 forward,vm::Vec3 pole)
{
	if(!Finite(shoulder)||!Finite(target)||!Finite(forward)||!Finite(pole)||
	   !std::isfinite(a)||!std::isfinite(b)||a<.01f||b<.01f)return {};
	forward=vm::Normal(forward);
	if(vm::Length(forward)<.5f)return {};
	const float span=a+b;
	vm::Vec3 delta=vm::Sub(target,shoulder);
	// Clamp in the current torso frame, including after Tommy turns away from
	// the old world-space contact. A rear hit protects his chest, not his back.
	delta=vm::Add(delta,vm::Mul(forward,std::max(0.0f,span*.10f-vm::Dot(delta,forward))));
	const vm::Vec3 direction=vm::Normal(delta);
	const float distance=std::clamp(vm::Length(delta),std::max(std::fabs(a-b)+.005f,span*.20f),span*.94f);
	target=vm::Add(shoulder,vm::Mul(direction,distance));
	const float along=(a*a-b*b+distance*distance)/(2*distance);
	const float height=std::sqrt(std::max(0.0f,a*a-along*along));
	vm::Vec3 out=vm::Normal(vm::Sub(pole,vm::Mul(direction,vm::Dot(direction,pole))));
	const vm::Vec3 projected=vm::Sub(forward,vm::Mul(direction,vm::Dot(direction,forward)));
	const float projectedLength=vm::Length(projected);
	if(vm::Length(out)<.5f)out=projectedLength>1e-5f?vm::Mul(projected,1/projectedLength):vm::Normal(vm::Cross(direction,{0,0,1}));
	// The wrist being in front is insufficient: the elbow's bend circle can
	// still fold the upper arm backwards. Restrict that circle without changing
	// either measured bone length or the reachable hand target.
	if(projectedLength>1e-5f&&height>1e-5f&&
	   along*vm::Dot(direction,forward)+height*vm::Dot(out,forward)<0) {
		const vm::Vec3 toward=vm::Mul(projected,1/projectedLength);
		vm::Vec3 across=vm::Normal(vm::Sub(out,vm::Mul(toward,vm::Dot(out,toward))));
		if(vm::Length(across)<.5f)across=vm::Normal(vm::Cross(direction,toward));
		const float coefficient=std::clamp(-along*vm::Dot(direction,forward)/(height*projectedLength),-1.0f,1.0f);
		out=vm::Add(vm::Mul(toward,coefficient),vm::Mul(across,std::sqrt(std::max(0.0f,1-coefficient*coefficient))));
	}
	return {vm::Add(shoulder,vm::Add(vm::Mul(direction,along),vm::Mul(out,height))),target,true};
}
bool Guard::Contact(Vec3 point,Vec3 normal,Vec3 relativeVelocity)
{
	const vm::Vec3 p=V(point), n=V(normal), velocity=V(relativeVelocity);
	const float length=vm::Length(n);
	if(!Finite(p)||!Finite(n)||!Finite(velocity)||length<.5f||length>1.5f) return false;
	const vm::Vec3 outward=vm::Mul(n,1/length);
	const float closing=-vm::Dot(velocity,outward);
	if(outward.z>.55f || closing<.35f) return false;
	const float strength=std::clamp(.32f+closing*.16f,.36f,.95f);
	response_={std::max(response_.weight,strength),point,{outward.x,outward.y,outward.z}};
	remaining_=.42f;
	return true;
}
void Guard::Advance(float dt)
{
	if(!std::isfinite(dt)||dt<0) {Reset();return;}
	remaining_=std::max(0.0f,remaining_-std::min(dt,.5f));
	if(remaining_==0) {Reset();return;}
	response_.weight=std::min(response_.weight,remaining_*4.5f);
}
void Guard::Reset() {response_={};remaining_=0;}
Response Guard::Current() const {return response_;}
Matrices Pose(const Matrices &animated,Response response,Vec3 forward,bool aiming)
{
	Matrices posed=animated;
	if(!std::isfinite(response.weight)||response.weight<=0||!Finite(V(response.normal))||!Finite(V(response.point))||
	   !Finite(V(forward))||std::hypot(forward.x,forward.y)<.1f) return posed;
	const float weight=std::clamp(response.weight,0.0f,1.0f);
	vm::Vec3 up=vm::Normal(vm::Sub(At(animated[4]),At(animated[3])));
	if(vm::Length(up)<.5f)return posed;
	vm::Vec3 right=vm::Sub(At(animated[12]),At(animated[7]));
	right=vm::Normal(vm::Sub(right,vm::Mul(up,vm::Dot(up,right))));
	vm::Vec3 front=vm::Normal(vm::Cross(up,right));
	if(vm::Length(front)<.5f)return posed;
	if(vm::Dot(front,V(forward))<0){front=vm::Mul(front,-1);right=vm::Mul(right,-1);}
	const vm::Vec3 outward=vm::Normal(vm::Sub(V(response.normal),vm::Mul(up,vm::Dot(V(response.normal),up))));
	if(vm::Length(outward)<.5f) return posed;
	if(!aiming) {
		const vm::Frame torso=vm::TurnAbout(vm::Normal(vm::Cross(up,outward)),weight*.20f,At(animated[2]));
		for(int i=2;i<=15;++i) Transform(posed[size_t(i)],torso);
		front=vm::Rotate(torso,front);right=vm::Rotate(torso,right);up=vm::Rotate(torso,up);
	}
	// Chin tucks toward the chest, within a small angle, rather than folding the neck.
	const vm::Frame tuck=vm::TurnAbout(right,-weight*.09f,At(posed[4]));
	for(int i=4;i<=5;++i) Transform(posed[size_t(i)],tuck);
	if(aiming) return posed; // native aimed hands stay on their weapon
	const float side=vm::Dot(vm::Sub(V(response.point),At(posed[3])),right);
	const float shoulderWidth=vm::Length(vm::Sub(At(posed[12]),At(posed[7])));
	const bool behind=vm::Dot(vm::Sub(V(response.point),At(posed[3])),front)<-.04f||vm::Dot(outward,front)>.4f;
	for(bool left:{true,false}) {
		const int upper=left?7:12;
		const auto shoulder=At(posed[size_t(upper)]);
		const float span=vm::Length(vm::Sub(At(posed[size_t(upper+1)]),shoulder))+
		                 vm::Length(vm::Sub(At(posed[size_t(upper+2)]),At(posed[size_t(upper+1)])));
		const bool farSide=(side>shoulderWidth*.6f&&left)||(side<-shoulderWidth*.6f&&!left);
		vm::Vec3 target=vm::Add(V(response.point),vm::Add(vm::Mul(V(response.normal),.09f),vm::Mul(right,left?-.10f:.10f)));
		if(behind||farSide)
			target=vm::Add(shoulder,vm::Add(vm::Mul(front,span*.45f),vm::Add(vm::Mul(up,-span*.12f),vm::Mul(right,span*(left?.10f:-.10f)))));
		const vm::Vec3 relative=vm::Sub(target,shoulder);
		const float lateral=std::clamp(vm::Dot(relative,right),-span*.65f,span*.65f);
		const float vertical=std::clamp(vm::Dot(relative,up),-span*.40f,span*.40f);
		target=vm::Add(shoulder,vm::Add(vm::Mul(front,vm::Dot(relative,front)),vm::Add(vm::Mul(right,lateral),vm::Mul(up,vertical))));
		target=vm::Lerp(At(posed[size_t(upper+2)]),target,weight);
		Reach(posed,upper,target,front,vm::Add(vm::Mul(right,left?-.75f:.75f),vm::Add(vm::Mul(front,.35f),vm::Mul(up,-.9f))));
	}
	return posed;
}
vm::Frame ViewTransform(Response response,Vec3 right,Vec3 forward,bool aiming)
{
	if(response.weight<=0 || !std::isfinite(response.weight)) return vm::Identity();
	const float w=std::clamp(response.weight,0.0f,1.0f);
	const float side=vm::Dot(V(response.normal),V(right)), ahead=vm::Dot(V(response.normal),V(forward));
	if(aiming) {auto f=vm::Identity();f.pos={side*w*.004f,ahead*w*.004f,-w*.004f};return f;}
	auto f=vm::Compose(vm::TurnAbout({0,1,0},-side*w*.10f,{}),vm::TurnAbout({1,0,0},-ahead*w*.14f,{}));
	f.pos={side*w*.055f,ahead*w*.075f,-w*.045f};
	return f;
}
vm::Frame GuardHand(Response response,Vec3 right,Vec3 forward,bool left)
{
	const float side=vm::Dot(V(response.normal),V(right)), ahead=vm::Dot(V(response.normal),V(forward));
	const float w=std::clamp(response.weight,0.0f,1.0f);
	// Fingers up, palms forward: the model's +x goes to the knuckles, +y out of the palm.
	vm::Frame hand{{0,0,1},{0,1,0},{-1,0,0},{left?-.17f:.17f,.30f,-.23f}};
	hand.pos.x-=side*w*.07f;
	hand.pos.y+=std::max(.0f,-ahead)*w*.12f;
	hand.pos.z+=w*.12f;
	if(left) {hand.y={0,-1,0};hand.z={1,0,0};}
	return hand;
}
} // namespace playerreact
