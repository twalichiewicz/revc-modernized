#pragma once
#include "types.h"
#include <algorithm>
#include <cmath>
#include <vector>
namespace skinmodel {
struct Hole {Vec3 centre{},normal{};float radius=0;};
struct Sample {Vec3 position{},weights{};};
inline Vec3 Sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline Vec3 Mix(Vec3 a,Vec3 b,float t){return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};}
// Subtract a convex octagonal opening from one convex skin polygon. Samples
// carry original-triangle barycentrics, preserving all interpolated attributes.
// The slab prevents a bullet's entrance hole from cutting the opposite shell.
inline bool Subtract(const std::vector<Sample> &polygon,const Hole &hole,std::vector<std::vector<Sample>> &out)
{
 if(polygon.size()<3||polygon.size()>32||!std::isfinite(hole.radius)||hole.radius<=0)return false;
 const float length=std::sqrt(Dot(hole.normal,hole.normal));if(!std::isfinite(length)||length<1e-6f)return false;
 const Vec3 n{hole.normal.x/length,hole.normal.y/length,hole.normal.z/length};
 bool near=false;float lo=1e9f,hi=-1e9f;
 for(auto p:polygon){const float d=Dot(Sub(p.position,hole.centre),n);lo=std::min(lo,d);hi=std::max(hi,d);near|=std::fabs(d)<.06f;}
 if(!near&&(lo>.06f||hi<-.06f)){out.push_back(polygon);return true;}
 const Vec3 axis=std::fabs(n.z)<.9f?Vec3{0,0,1}:Vec3{0,1,0};Vec3 u=Cross(n,axis);const float ul=std::sqrt(Dot(u,u));u={u.x/ul,u.y/ul,u.z/ul};const Vec3 v=Cross(n,u);
 std::vector<Sample> inside=polygon;
 for(int edge=0;edge<8&&!inside.empty();edge++){
  const float angle=float(edge)*.7853981634f;const Vec3 e{u.x*std::cos(angle)+v.x*std::sin(angle),u.y*std::cos(angle)+v.y*std::sin(angle),u.z*std::cos(angle)+v.z*std::sin(angle)};
  std::vector<Sample> next,outside;next.reserve(16);outside.reserve(16);
  for(size_t i=0;i<inside.size();i++){
   const auto a=inside[i],b=inside[(i+1)%inside.size()];const float da=Dot(Sub(a.position,hole.centre),e)-hole.radius*.9238795325f,db=Dot(Sub(b.position,hole.centre),e)-hole.radius*.9238795325f;
   if(da<=0)next.push_back(a);if(da>=0)outside.push_back(a);
   if((da<0&&db>0)||(da>0&&db<0)){const float t=da/(da-db);Sample cut{Mix(a.position,b.position,t),Mix(a.weights,b.weights,t)};next.push_back(cut);outside.push_back(cut);}
  }
  if(outside.size()>=3)out.push_back(std::move(outside));inside=std::move(next);
  if(out.size()>1024||inside.size()>32)return false;
 }
 return true;
}
}
