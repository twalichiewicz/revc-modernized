#pragma once
#include "vec3.h"
#include <cstdint>
// Value-only source-host seam. Mutations are queued for the next safe world
// boundary; queries consume the last committed body revision.
struct AircraftBodyWheelEffect {float grip=1,drive=1,brake=1,toe=0,steering=1;};
struct AircraftBodySnapshot {
 float power=1,leftLift=1,rightLift=1,pitch=1,roll=1,yaw=1,rotor=1,antiTorque=1,drag=0;
 float heat=.2f,fuel=1,engine=0,electrical=0,breach=0;
 float lighting=1;
 bool wheelPresent[4]{true,true,true,true}; // actual retained wheel material/mount
 AircraftBodyWheelEffect wheels[4]{}; // native LF, LR, RF, RR order
 float tipLeft=0,tipRight=0;
 uint32_t wheelTriangles[3]{};
 uint32_t detachedBodies=0;
 uint64_t revision=0;
 // Lifetime/repair lease; unlike a geometry revision this never repeats.
 uint64_t generation=0;
};
extern "C" {
bool VCSourceAircraftBodyOwned(const void *);
bool VCSourceAircraftBodyRead(const void *,AircraftBodySnapshot *);
bool VCSourceAircraftBodyBullet(void *,Vec3 from,Vec3 to,float budget,float limit);
bool VCSourceAircraftBodyContact(void *,Vec3 point,Vec3 inward,float deltaV,uint32_t when);
bool VCSourceAircraftBodyTrace(const void *,Vec3 from,Vec3 to,float entry,float *fraction,Vec3 *normal);
void VCSourceAircraftBodyBurn(void *,float dt);
void VCSourceAircraftBodyRepair(void *);
void VCSourceAircraftBodyControls(void *,float left,float right,float elevator,float rudder);
// Farthest attached main-rotor blade material from the drawn hub, metres; 0
// when every blade has separated. False when no material main rotor exists.
bool VCSourceAircraftBodyBladeRadius(const void *,float *radius);
// Accepted native blade overlap: queue work on the actual struck blade. Spin is
// the native rotor speed before the strike, radians per 1/50 s step.
bool VCSourceAircraftBodyBladeStrike(void *,float spin,uint32_t when);
}

// Native rotor presentation. Stock blade/blur crossfade values come in; the
// blur disc fades with surviving rotor material, and missing material exposes
// the actual remaining blades rather than drawing a complete disc.
inline void AircraftBodyRotorAlpha(const AircraftBodySnapshot &material,int &blade,int &blur,int &rearBlade,int &rearBlur){
 const int stockBlade=blade,stockBlur=blur;
 auto fade=[&](float survive,int &solid,int &disc){survive=survive>0?(survive<1?survive:1):0;const int exposed=int((1-survive)*255+.5f);solid=stockBlade>exposed?stockBlade:exposed;disc=int(stockBlur*survive+.5f);};
 fade(material.rotor,blade,blur);fade(material.antiTorque,rearBlade,rearBlur);
}

#include <algorithm>
#include <cmath>
#include <vector>
// Rotor strike planning, shared by the source host and its actual-asset tests.
// Reads a committed material body through the kernel's public API, never
// mutates it; the caller queues the resulting contact as ordinary material work.
struct AircraftBodyBladeSample {Vec3 point{},radial{};float radius=0,angle=0,area=0;};
struct AircraftBodyBladePlan {bool valid=false,root=false;Vec3 point{},inward{};float work=0,radius=0,released=0;};
namespace aircraftbodyblade {
inline Vec3 Add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}inline Vec3 Sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}inline Vec3 Mul(Vec3 a,float s){return {a.x*s,a.y*s,a.z*s};}
inline float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}inline Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}inline float Length(Vec3 a){return std::sqrt(Dot(a,a));}
inline float Wrap(float a){while(a>3.14159265f)a-=6.28318531f;while(a<-3.14159265f)a+=6.28318531f;return a;}
// Posed attached blade material about the drawn hub; e1/e2 span the rotor plane.
// Inertia is the sheet material's own polar moment, kg m^2.
template<class Body,class Rotor> std::vector<AircraftBodyBladeSample> Samples(const Body &body,Vec3 hub,Vec3 axis,Vec3 e1,Vec3 e2,Rotor rotor,float &outer,float &inertia,float &axial){
 std::vector<AircraftBodyBladeSample> out;outer=inertia=axial=0;const float density=body.SurfaceDensity();const auto &surfaces=body.Surfaces();
 for(const auto &f:body.Fragments()){if(!f.attached||f.polygon.size()<3||f.source>=surfaces.size()||!rotor(surfaces[f.source]))continue;
  std::vector<Vec3> p;p.reserve(f.polygon.size());Vec3 c{};for(const auto &v:f.polygon){p.push_back(body.Deform(v.position,f.group));c=Add(c,p.back());}c=Mul(c,1.f/p.size());
  float area=0;for(size_t i=1;i+1<p.size();++i)area+=.5f*Length(Cross(Sub(p[i],p[0]),Sub(p[i+1],p[0])));
  for(auto q:p){const auto r=Sub(q,hub);const float t=Length(Sub(r,Mul(axis,Dot(r,axis))));if(t>outer)outer=t;}
  const auto r=Sub(c,hub);AircraftBodyBladeSample s;s.point=c;s.radial=Sub(r,Mul(axis,Dot(r,axis)));s.radius=Length(s.radial);s.angle=std::atan2(Dot(s.radial,e2),Dot(s.radial,e1));s.area=area;
  inertia+=area*density*s.radius*s.radius;out.push_back(s);
 }
 float weight=0;for(const auto &s:out)if(s.radius>.8f*outer){axial+=Dot(Sub(s.point,hub),axis)*s.area;weight+=s.area;}if(weight>0)axial/=weight;
 return out;
}
// The obstacle bearing selects the blade sweeping into it. The rigid rotor's
// effective mass at that station is I/r^2, so the strike spends the blade-
// relative kinetic energy there. The obstacle is solid outboard of its inner
// edge: if that load also fails the blade inboard (proved by a dry run on a
// copy), the struck blade shears at its root station and the outboard blade
// separates; otherwise the work stays local to the struck station.
template<class Body,class Rotor> AircraftBodyBladePlan Plan(const Body &body,const std::vector<AircraftBodyBladeSample> &samples,Vec3 axis,float outer,float inertia,float hitAngle,float hitRadius,float spin,Vec3 airframe,uint32_t now,float minimum,Rotor rotor){
 AircraftBodyBladePlan plan;if(samples.empty()||!(outer>0)||!(inertia>0)||!std::isfinite(hitAngle)||!std::isfinite(hitRadius)||!std::isfinite(spin)||!std::isfinite(airframe.x+airframe.y+airframe.z))return plan;
 const AircraftBodyBladeSample *bearing=nullptr;for(const auto &s:samples)if(s.radius>=.5f*outer&&(!bearing||std::fabs(Wrap(s.angle-hitAngle))<std::fabs(Wrap(bearing->angle-hitAngle))))bearing=&s;if(!bearing)return plan;
 const float station=std::min(std::max(hitRadius,0.f),outer);const AircraftBodyBladeSample *tip=nullptr,*root=nullptr;
 for(const auto &s:samples){if(std::fabs(Wrap(s.angle-bearing->angle))>.35f||s.radius<.1f*outer)continue;
  if(!tip||std::fabs(s.radius-station)<std::fabs(tip->radius-station))tip=&s;if(!root||std::fabs(s.radius-.35f*outer)<std::fabs(root->radius-.35f*outer))root=&s;}
 if(!tip)return plan;
 const auto velocity=Add(Mul(Cross(axis,tip->radial),spin),airframe);const float speed=Length(velocity),r=std::max(tip->radius,.5f);
 plan.work=std::min(100000.f,.5f*inertia/(r*r)*speed*speed);if(!(plan.work>=2)||plan.work<=minimum)return plan;
 plan.inward=speed>1e-4f?Mul(velocity,-1/speed):Vec3{0,0,-1};plan.point=tip->point;plan.radius=tip->radius;plan.valid=true;
 if(root&&root->radius<tip->radius-.3f){
  Body trial=body;const auto result=trial.ContactEnergy(root->point,plan.inward,plan.work,now,1.f/60);std::vector<typename std::decay<decltype(body.Fragments()[0])>::type> released;
  if(result.valid&&trial.TransferDetached(released))for(const auto &f:released)if(f.source<body.Surfaces().size()&&rotor(body.Surfaces()[f.source]))for(size_t i=1;i+1<f.polygon.size();++i)plan.released+=.5f*Length(Cross(Sub(f.polygon[i].position,f.polygon[0].position),Sub(f.polygon[i+1].position,f.polygon[0].position)));
  if(plan.released>=.02f){plan.root=true;plan.point=root->point;plan.radius=root->radius;}else plan.released=0;
 }
 return plan;
}
}
