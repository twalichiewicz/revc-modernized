#pragma once
#include "jetwheels.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

// Shared native geometry-to-body-COL boundary. Both the actual prepared-model
// regression and the runtime Shape wrapper use these exact operations.
namespace airportjetcollision {
struct Triangle {uint16_t v[3],material;};
struct Input {
 void *(*each)(void *,void *(*)(void *,void *),void *),*(*geometry)(const void *),*(*morph)(const void *,int);
 int (*verticesCount)(const void *),(*triangleCount)(const void *);
 const CVector *(*vertices)(const void *);const Triangle *(*triangles)(const void *);
 void *(*atomicFrame)(const void *);const float *(*ltm)(void *);const char *(*name)(void *);
};
inline CVector Add(CVector a,CVector b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline CVector Sub(CVector a,CVector b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline CVector Mul(CVector a,float s){return {a.x*s,a.y*s,a.z*s};}
inline float Dot(CVector a,CVector b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline float Length(CVector a){return std::sqrt(Dot(a,a));}
inline bool Finite(CVector v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline CVector &Vector(uint8_t *p,size_t offset){return *reinterpret_cast<CVector *>(p+offset);}
inline const CVector &Vector(const uint8_t *p,size_t offset){return *reinterpret_cast<const CVector *>(p+offset);}
struct Context {const Input &n;std::vector<CVector> &points;const float *bodyMatrix=nullptr;};
inline void *Collect(void *atomic,void *data){
 auto &context=*static_cast<Context *>(data);const Input &n=context.n;auto &points=context.points;void *geometry=n.geometry(atomic);if(!geometry)return atomic;
 void *frame=n.atomicFrame(atomic);const char *name=frame?n.name(frame):nullptr;const float *matrix=frame?n.ltm(frame):nullptr;
 if(!matrix||!name||!std::strncmp(name,"jet_tire_",9)||!std::strncmp(name,"jet_strut_",10)||!std::strncmp(name,"jet_fairing_",12)||!std::strncmp(name,"jet_link_",9)||!std::strncmp(name,"jet_cabin",9)||!std::strcmp(name,"jet_dashboard")||!std::strcmp(name,"jet_window_trim")||!std::strcmp(name,"jet_baywell")||!std::strncmp(name,"jet_lining",10)||!std::strncmp(name,"jet_steps",9)||!std::strncmp(name,"jet_step_",9)||!std::strcmp(name,"jet_door")||!std::strncmp(name,"jet_lenses",10))return atomic;
 // Bay door vertices retain their closed, original body coordinates. The
 // fixed structural envelope uses that reference through the hull transform,
 // including when the visual leaves are open on a cloned actor.
 if(!std::strcmp(name,"jet_hull"))context.bodyMatrix=matrix;
 if(!std::strncmp(name,"jet_baydoor_",12)&&context.bodyMatrix)matrix=context.bodyMatrix;
 auto model=[&](CVector v){return CVector{matrix[0]*v.x+matrix[4]*v.y+matrix[8]*v.z+matrix[12],matrix[1]*v.x+matrix[5]*v.y+matrix[9]*v.z+matrix[13],matrix[2]*v.x+matrix[6]*v.y+matrix[10]*v.z+matrix[14]};};
 auto append=[&](CVector point){if(!Finite(point)||Length(point)>=100)return;point.z=std::max(point.z,jetwheels::kTireBottom+.225f);points.push_back(point);};
 const int count=n.verticesCount(geometry);if(count<=0||count>20000||points.size()+size_t(count)>120000)return atomic;
 const CVector *vertices=n.vertices(n.morph(geometry,0));if(!vertices)return atomic;
 for(int i=0;i<count;++i)append(model(vertices[i]));
 const int triangleCount=n.triangleCount(geometry);const Triangle *triangles=n.triangles(geometry);
 if(!triangles||triangleCount<=0||triangleCount>20000)return atomic;
 // Long low-poly wing faces contain metres of surface between vertices.
 // Sample a barycentric grid with edge<=.7m; a .43m fitting allowance below
 // covers the gap from any point of a grid triangle to its closest vertex.
 for(int i=0;i<triangleCount;++i){const Triangle &t=triangles[i];if(t.v[0]>=count||t.v[1]>=count||t.v[2]>=count)continue;
  const CVector a=model(vertices[t.v[0]]),b=model(vertices[t.v[1]]),c=model(vertices[t.v[2]]);if(!Finite(a)||!Finite(b)||!Finite(c))continue;
  const float edge=std::max({Length(Sub(a,b)),Length(Sub(a,c)),Length(Sub(b,c))});if(edge>40)continue;
  const int divisions=std::max(1,int(std::ceil(edge/.7f)));
  if(points.size()+size_t(divisions+1)*size_t(divisions+2)/2>120000){points.clear();return nullptr;}
  for(int u=0;u<=divisions;++u)for(int v=0;v<=divisions-u;++v)append(Add(a,Add(Mul(Sub(b,a),float(u)/divisions),Mul(Sub(c,a),float(v)/divisions))));
 }
 return atomic;
}
inline bool Samples(void *clump,const Input &n,std::vector<CVector> &points){
 points.clear();Context context{n,points};n.each(clump,Collect,&context);return points.size()>=20;
}
inline bool Build(const std::vector<CVector> &points,const uint8_t *col,uint8_t *collision,uint8_t *spheres,uint8_t *boxes){
 constexpr int kMaxSpheres=32;
 if(!col||!collision||!spheres||!boxes||points.size()<20||*reinterpret_cast<const int16_t *>(col+0x28)!=2||*reinterpret_cast<const int16_t *>(col+0x2a)!=2||*reinterpret_cast<const int16_t *>(col+0x2c))return false;
 const uint8_t *originalBoxes=*reinterpret_cast<uint8_t *const *>(col+0x48);if(!originalBoxes)return false;
 const float bottom=jetwheels::kTireBottom+.20f;
 // Actual wheel contacts belong to jetwheels. Neither old static gear spheres
 // nor structural spheres may support the body at the tire contact plane.
 constexpr int count=kMaxSpheres;std::array<CVector,count> centres{};centres[0]=points[0];
 for(int i=1;i<count;++i){float far=-1;for(CVector p:points){float nearest=1e9f;for(int j=0;j<i;++j)nearest=std::min(nearest,Dot(Sub(p,centres[j]),Sub(p,centres[j])));if(nearest>far){far=nearest;centres[i]=p;}}}
 std::vector<int> groups(points.size());
 for(int iteration=0;iteration<8;++iteration){std::array<CVector,count> sums{};std::array<int,count> counts{};
  for(size_t i=0;i<points.size();++i){float nearest=1e9f;int group=0;for(int j=0;j<count;++j){const CVector d=Sub(points[i],centres[j]);const float distance=Dot(d,d);if(distance<nearest){nearest=distance;group=j;}}groups[i]=group;sums[group]=Add(sums[group],points[i]);++counts[group];}
  for(int i=0;i<count;++i)if(counts[i])centres[i]=Mul(sums[i],1.0f/counts[i]);
 }
 std::memcpy(collision,col,0x68);std::memset(spheres,0,kMaxSpheres*20);std::memcpy(boxes,originalBoxes,2*28);
 CVector lo=Vector(col,0x10),hi=Vector(col,0x1c);lo.z=jetwheels::kTireBottom;
 for(int j=0;j<count;++j){float radius=.15f;for(size_t i=0;i<points.size();++i)if(groups[i]==j)radius=std::max(radius,Length(Sub(points[i],centres[j]))+.43f);
  if(centres[j].z-radius<bottom){
   // A few small static gear-strut faces sit close to the support plane.
   // Centre their tangent sphere on that lowest footprint; averaging the
   // entire adjacent belly would need a huge sphere to cover the strut.
   CVector low{1e9f,1e9f,1e9f},high{-1e9f,-1e9f,-1e9f};bool footprint=false;
   for(size_t i=0;i<points.size();++i)if(groups[i]==j&&points[i].z<bottom+.05f){footprint=true;low.x=std::min(low.x,points[i].x);low.y=std::min(low.y,points[i].y);high.x=std::max(high.x,points[i].x);high.y=std::max(high.y,points[i].y);}
   if(footprint){centres[j].x=(low.x+high.x)*.5f;centres[j].y=(low.y+high.y)*.5f;}
   // Tangent sphere above the body-clearance plane, leaving tire support to
   // the wheel solver. Only tiny legacy strut tips need a <=.04m projection.
   radius=.15f;for(size_t i=0;i<points.size();++i)if(groups[i]==j){const float z=points[i].z-bottom;if(z<=.02f)return false;const float margin=std::min(.43f,z*.4f);const CVector d=Sub(points[i],centres[j]);radius=std::max(radius,(d.x*d.x+d.y*d.y+z*z-margin*margin)/(2*(z-margin))+.01f);}
   centres[j].z=bottom+radius;
  }
  if(!std::isfinite(radius)||radius>4)return false;
  uint8_t *sphere=spheres+j*20;std::memset(sphere,0,20);Vector(sphere,0)=centres[j];*reinterpret_cast<float *>(sphere+12)=radius;
  lo.x=std::min(lo.x,centres[j].x-radius);lo.y=std::min(lo.y,centres[j].y-radius);hi.x=std::max(hi.x,centres[j].x+radius);hi.y=std::max(hi.y,centres[j].y+radius);hi.z=std::max(hi.z,centres[j].z+radius);
 }
 for(int i=0;i<2;++i)Vector(boxes+i*28,0).z=std::max(Vector(boxes+i*28,0).z,bottom);
 Vector(collision,0x10)=lo;Vector(collision,0x1c)=hi;Vector(collision,0)=Mul(Add(lo,hi),.5f);*reinterpret_cast<float *>(collision+12)=Length(Sub(hi,lo))*.5f;
 *reinterpret_cast<int16_t *>(collision+0x28)=kMaxSpheres;collision[0x30]=0;
 *reinterpret_cast<uint8_t **>(collision+0x38)=spheres;*reinterpret_cast<uint8_t **>(collision+0x48)=boxes;
 return true;
}
}
