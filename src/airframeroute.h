#pragma once
#include "airframemodel.h"
#include "skinmodel.h"
#include <array>
#include <cstring>
namespace airframeroute {
struct State {std::array<float,16> native{},applied{};Vec3 offset{},angles{},ground{};float descent=0,travel=1;bool seen=false,grounded=false;};
inline Vec3 Row(const std::array<float,16> &m,int i){return {m[i],m[i+1],m[i+2]};}
inline bool Finite(const std::array<float,16> &m){for(int i:{0,1,2,4,5,6,8,9,10,12,13,14})if(!std::isfinite(m[i]))return false;return true;}
inline void Before(State &s,float *pose){if(s.seen&&!std::memcmp(pose,s.applied.data(),sizeof s.applied))std::memcpy(pose,s.native.data(),sizeof s.native);else if(s.seen)s={};}
inline float Angle(float x){return std::remainder(x,6.283185307f);}
inline Vec3 Rotate(Vec3 p,Vec3 axis,float a){const float c=std::cos(a),s=std::sin(a),d=skinmodel::Dot(axis,p);const auto cross=skinmodel::Cross(axis,p);return {p.x*c+cross.x*s+axis.x*d*(1-c),p.y*c+cross.y*s+axis.y*d*(1-c),p.z*c+cross.z*s+axis.z*d*(1-c)};}
// Native route controllers write poses directly, bypassing FlyingControl.
// Keep their route/script state, then integrate persistent losses of travel,
// altitude and attitude. Before removes only our exact prior pose, so the
// native controller never feeds the damage displacement back into its route.
inline bool After(State &s,const std::array<float,16> &input,std::array<float,16> &pose,const airframemodel::Effects &e,bool heli,float dt)
{
 if(!Finite(input)||!Finite(pose)||!std::isfinite(dt)||dt<=0||dt>.1f)return false;
 auto delta=skinmodel::Sub(Row(pose,12),Row(input,12));if(skinmodel::Dot(delta,delta)>10000){s={};return false;}
 const auto native=pose;const auto previous=s.seen?s.native:input;
 s.travel+=(std::max(.15f,e.power)-s.travel)*(1-std::exp(-dt/6));
 s.offset.x-=delta.x*(1-s.travel);s.offset.y-=delta.y*(1-s.travel);
 const float lift=heli?e.power*e.rotor:(e.leftLift+e.rightLift)*.5f*s.travel*s.travel;
 s.descent=std::min(80.f,s.descent+20*(1-lift)*dt);s.offset.z-=s.descent*dt;
 const auto f=Row(pose,4),oldF=Row(previous,4),r=Row(pose,0),oldR=Row(previous,0);
 s.angles.x=Angle(s.angles.x-(std::asin(std::clamp(f.z,-1.f,1.f))-std::asin(std::clamp(oldF.z,-1.f,1.f)))*(1-e.pitch));
 s.angles.y=Angle(s.angles.y+(std::asin(std::clamp(r.z,-1.f,1.f))-std::asin(std::clamp(oldR.z,-1.f,1.f)))*(1-e.roll)+(e.leftLift-e.rightLift)*dt*.6f);
 const float yaw=Angle(std::atan2(-f.x,f.y)-std::atan2(-oldF.x,oldF.y));s.angles.z=Angle(s.angles.z-yaw*(1-e.yaw)+(heli?(1-e.antiTorque)*dt*1.3f:0));
 Vec3 axes[]{Row(pose,0),Row(pose,4),Row(pose,8)};
 for(int axis=0;axis<3;axis++){const auto around=axes[axis];const float angle=axis==0?s.angles.x:axis==1?s.angles.y:s.angles.z;for(auto &v:axes)v=Rotate(v,around,angle);}
 for(int i=0;i<3;i++){pose[i*4]=axes[i].x;pose[i*4+1]=axes[i].y;pose[i*4+2]=axes[i].z;}
 pose[12]+=s.offset.x;pose[13]+=s.offset.y;pose[14]+=s.offset.z;
 if(s.grounded){pose[12]=s.ground.x;pose[13]=s.ground.y;pose[14]=s.ground.z;}
 if(!Finite(pose)){s={};return false;}s.native=native;s.applied=pose;s.seen=true;return true;
}
}
