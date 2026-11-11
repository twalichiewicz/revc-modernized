#pragma once
#include "vec3.h"
#include <algorithm>
#include <cmath>
#include <vector>
namespace skinmodel {
struct Hole {Vec3 centre{},normal{};float radius=0;};
struct Sample {Vec3 position{},weights{};};
struct Cut {Vec3 normal{};float offset=0;};

inline Vec3 Sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline Vec3 Mix(Vec3 a,Vec3 b,float t){return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};}
inline std::vector<Sample> Slice(const std::vector<Sample> &polygon,const Cut &cut)
{
 std::vector<Sample> out;if(polygon.size()<3||polygon.size()>64)return out;out.reserve(polygon.size()+1);
 for(size_t i=0;i<polygon.size();i++){
  const auto a=polygon[i],b=polygon[(i+1)%polygon.size()];const float da=a.position.x*cut.normal.x+a.position.y*cut.normal.y+a.position.z*cut.normal.z-cut.offset,db=b.position.x*cut.normal.x+b.position.y*cut.normal.y+b.position.z*cut.normal.z-cut.offset;
  if(da<=0)out.push_back(a);if((da<0&&db>0)||(da>0&&db<0)){const float t=da/(da-db);out.push_back({Mix(a.position,b.position,t),Mix(a.weights,b.weights,t)});}
 }
 return out;
}
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
 // Reject disjoint convex polygons before splitting. Without this, a hole
 // outside a triangle still extends its clip lines across the whole sheet,
 // generating unrelated slivers and exhausting the topology budget.
 for(int edge=0;edge<8;edge++){
  const float angle=float(edge)*.7853981634f;const Vec3 e{u.x*std::cos(angle)+v.x*std::sin(angle),u.y*std::cos(angle)+v.y*std::sin(angle),u.z*std::cos(angle)+v.z*std::sin(angle)};
  bool outside=true;for(const auto &p:polygon)outside&=Dot(Sub(p.position,hole.centre),e)>=hole.radius*.9238795325f;
  if(outside){out.push_back(polygon);return true;}
 }
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
  if(out.size()>65535/3||inside.size()>32)return false;
 }
 return true;
}
}
