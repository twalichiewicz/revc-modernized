#include "reacthit.h"
#include "game.h"
#include "hooks.h"
#include "reactions.h"
#include "reactrig.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <dlfcn.h>

namespace reacthit {
namespace {
struct Line { CVector a; uint32_t padA; CVector b; uint32_t padB; };
struct Point { CVector p; uint32_t padA; CVector normal; uint32_t padB; uint8_t surfaceA,pieceA,surfaceB,pieceB; float depth; };
struct Sphere { CVector center; float radius; uint8_t surface,piece; uint16_t pad; };
struct Model {
 CVector center; float radius;
 CVector minimum,maximum;
 int16_t spheres,boxes,triangles;
 uint8_t lines,owns;
 uint8_t pad[8];
 Sphere *sphere;
 void *line,*box,*vertex,*triangle,*plane;
};
static_assert(sizeof(Line)==0x20 && sizeof(Point)==0x28 && sizeof(Sphere)==0x14 && sizeof(Model)==0x68);
static_assert(offsetof(Model,sphere)==0x38 && offsetof(Model,minimum)==0x10);
using Narrow=bool (*)(const Line &,const void *,void *,Point &,float &,bool,bool);
Narrow gNarrow;
void **gPedPool,**gModels;
bool gReady;
constexpr size_t kPedStride=0x8d0;
constexpr int kSpheres=10;
// macVC5217's CPedModelInfo::AnimatePedColModelSkinned uses these native
// bones and MODEL-LOCAL x/z offsets after converting each world bone origin.
// Keep native radius, material, and piece bytes from the incoming hit COL.
constexpr int kBone[kSpheres]={5,3,3,3,7,12,17,20,18,21};
constexpr float kOffsetX[kSpheres]={0,0,0,0,.03f,-.03f,0,0,0,0};
constexpr float kOffsetZ[kSpheres]={.05f,.15f,-.05f,-.25f,-.05f,-.05f,.15f,.15f,.15f,.15f};
bool Finite(CVector p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
CVector Sub(CVector a,CVector b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
float Dot(CVector a,CVector b){return a.x*b.x+a.y*b.y+a.z*b.z;}
struct Basis {
 CVector right,forward,up,position;
 bool Set(const uint8_t *ped){
  std::memcpy(&right,ped+8,sizeof right);std::memcpy(&forward,ped+0x18,sizeof forward);
  std::memcpy(&up,ped+0x28,sizeof up);std::memcpy(&position,ped+0x38,sizeof position);
  // The native CMatrix LOS inverse transposes its orthonormal basis. Refuse
  // an unsupported scaled/sheared root instead of projecting different rays.
  return Finite(right)&&Finite(forward)&&Finite(up)&&Finite(position)&&
   std::fabs(Dot(right,right)-1)<.01f&&std::fabs(Dot(forward,forward)-1)<.01f&&std::fabs(Dot(up,up)-1)<.01f&&
   std::fabs(Dot(right,forward))<.01f&&std::fabs(Dot(right,up))<.01f&&std::fabs(Dot(forward,up))<.01f;
 }
 CVector Local(CVector p)const{p=Sub(p,position);return {Dot(right,p),Dot(forward,p),Dot(up,p)};}
 CVector World(CVector p)const{return {position.x+right.x*p.x+forward.x*p.y+up.x*p.z,position.y+right.y*p.x+forward.y*p.y+up.y*p.z,position.z+right.z*p.x+forward.z*p.y+up.z*p.z};}
};
// A narrow-phase matrix can belong to a vehicle or temporary stack object.
// Certify pool allocation/stride/slot/generation BEFORE any CPed field read.
uint8_t *Ped(const void *matrix)
{
 if(!matrix||!gPedPool||!*gPedPool||!game::api.PoolsGetPed)return nullptr;
 const auto *pool=static_cast<const uint8_t *>(*gPedPool);
 const auto *entries=*reinterpret_cast<uint8_t *const *>(pool);
 const auto *flags=*reinterpret_cast<uint8_t *const *>(pool+8);
 const int count=*reinterpret_cast<const int *>(pool+16);
 const uintptr_t address=reinterpret_cast<uintptr_t>(matrix),base=reinterpret_cast<uintptr_t>(entries);
 if(!entries||!flags||count<=0||count>4096||address<base||address-base<8)return nullptr;
 const uintptr_t offset=address-base-8;
 if(offset%kPedStride||offset/kPedStride>=size_t(count))return nullptr;
 const size_t slot=offset/kPedStride;
 if(flags[slot]&0x80)return nullptr;
 const int ref=int(slot*256)|flags[slot];
 auto *ped=const_cast<uint8_t *>(entries)+offset;
 return game::api.PoolsGetPed(ref)==ped&&(game::EntityFlags(ped)&7)==3?ped:nullptr;
}
bool Source(uint8_t *ped,void *model,Model &copy)
{
 if(!ped||!model||!gModels)return false;
 const int id=game::EntityModel(ped);
 if(id<0||id>=6500||!gModels[id]||*reinterpret_cast<void **>(static_cast<uint8_t *>(gModels[id])+0x50)!=model)return false;
 std::memcpy(&copy,model,sizeof copy);
 return copy.spheres==kSpheres&&!copy.boxes&&!copy.triangles&&!copy.lines&&copy.sphere;
}
struct Projection { Model model{};std::array<Sphere,kSpheres> spheres{};Basis basis; };
bool Project(uint8_t *ped,void *source,Projection &out)
{
 reactrig::Matrices pose;
 if(!Source(ped,source,out.model)||!out.basis.Set(ped)||!reactions::CopyHitPose(ped,pose))return false;
 std::memcpy(out.spheres.data(),out.model.sphere,sizeof out.spheres);
 CVector minimum{INFINITY,INFINITY,INFINITY},maximum{-INFINITY,-INFINITY,-INFINITY};
 for(int i=0;i<kSpheres;i++){
  Sphere &s=out.spheres[size_t(i)];const auto &m=pose[size_t(kBone[i])];
  const CVector world{m[12],m[13],m[14]};
  if(!Finite(world)||!std::isfinite(s.radius)||s.radius<=0||s.radius>2)return false;
  s.center=out.basis.Local(world);s.center.x+=kOffsetX[i];s.center.z+=kOffsetZ[i];
  if(!Finite(s.center))return false;
  minimum={std::min(minimum.x,s.center.x-s.radius),std::min(minimum.y,s.center.y-s.radius),std::min(minimum.z,s.center.z-s.radius)};
  maximum={std::max(maximum.x,s.center.x+s.radius),std::max(maximum.y,s.center.y+s.radius),std::max(maximum.z,s.center.z+s.radius)};
 }
 out.model.minimum=minimum;out.model.maximum=maximum;
 out.model.center={(minimum.x+maximum.x)*.5f,(minimum.y+maximum.y)*.5f,(minimum.z+maximum.z)*.5f};
 out.model.radius=0;
 for(const Sphere &s:out.spheres){const CVector d=Sub(s.center,out.model.center);out.model.radius=std::max(out.model.radius,std::sqrt(Dot(d,d))+s.radius);}
 out.model.sphere=out.spheres.data();
 return std::isfinite(out.model.radius);
}
bool Hook_Narrow(const Line &line,const void *matrix,void *model,Point &point,float &closest,bool see,bool shoot)
{
 if(gReady&&Finite(line.a)&&Finite(line.b)&&std::isfinite(closest)&&closest>0){
  Projection posed;
  if(Project(Ped(matrix),model,posed))
   return gNarrow(line,matrix,&posed.model,point,closest,see,shoot);
 }
 return gNarrow(line,matrix,model,point,closest,see,shoot);
}
} // namespace
void ExpandBounds(uint8_t *entity,float &left,float &top,float &right,float &bottom)
{
 if(!gReady||!entity||!std::isfinite(left)||!std::isfinite(top)||!std::isfinite(right)||!std::isfinite(bottom)||left>right||bottom>top)return;
 uint8_t *ped=Ped(reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(entity)+8));
 if(!ped||!gModels)return;
 const int id=game::EntityModel(ped);
 if(id<0||id>=6500||!gModels[id])return;
 void *source=*reinterpret_cast<void **>(static_cast<uint8_t *>(gModels[id])+0x50);
 Projection posed;if(!Project(ped,source,posed))return;
 for(const Sphere &s:posed.spheres){
  const CVector p=posed.basis.World(s.center);
  left=std::min(left,p.x-s.radius);right=std::max(right,p.x+s.radius);
  bottom=std::min(bottom,p.y-s.radius);top=std::max(top,p.y+s.radius);
 }
}
bool Install()
{
 if(gReady)return true;
 gPedPool=reinterpret_cast<void **>(dlsym(RTLD_MAIN_ONLY,"_ZN6CPools11ms_pPedPoolE"));
 gModels=reinterpret_cast<void **>(dlsym(RTLD_MAIN_ONLY,"_ZN10CModelInfo16ms_modelInfoPtrsE"));
 if(!gPedPool||!gModels||!game::api.PoolsGetPed)return false;
 gNarrow=reinterpret_cast<Narrow>(hooks::Chain("_ZN10CCollision18ProcessLineOfSightERK8CColLineRK7CMatrixR9CColModelR9CColPointRfbb",reinterpret_cast<void *>(Hook_Narrow)));
 gReady=gNarrow!=nullptr;return gReady;
}
} // namespace reacthit
