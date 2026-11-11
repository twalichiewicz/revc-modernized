#include "panelcollision.h"
#include "panelphysics.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "aircraftbodyclient.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
namespace panelcollision {namespace {
using Pair=int (*)(uint8_t *,uint8_t *,uint8_t *);
using Models=int (*)(const uint8_t *,uint8_t *,const uint8_t *,uint8_t *,uint8_t *,uint8_t *,float *);
Pair physical,automobile,ped;Models models;void (*removePlanes)(uint8_t *);void **vehiclePool,**pedPool;bool ready;
int Ref(uint8_t *v,void **binding,size_t stride){if(!v||!binding||!*binding)return -1;auto p=static_cast<uint8_t *>(*binding);auto base=*reinterpret_cast<uint8_t **>(p),flags=*reinterpret_cast<uint8_t **>(p+8);int count=*reinterpret_cast<int *>(p+16);auto a=reinterpret_cast<uintptr_t>(v),b=reinterpret_cast<uintptr_t>(base);if(!base||!flags||count<1||count>100000||a<b||(a-b)%stride)return -1;auto i=(a-b)/stride;return i<size_t(count)&&!(flags[i]&128)?int(i*256)|flags[i]:-1;}
struct Actor {uint8_t *v=nullptr,*col=nullptr;int ref=-1,type=0,model=-1,kind=-1;void *clump=nullptr,*table=nullptr;std::array<float,16> pose{};std::array<float,6> hinges{};std::array<uint8_t,24> damage{};};
Actor Read(uint8_t *v){
 Actor a;if(!v||!game::api.modelInfoPtrs)return a;int type=2,ref=Ref(v,vehiclePool,0x6f0);
 if(ref<0||!game::api.PoolsGetVehicle||game::api.PoolsGetVehicle(ref)!=v){type=3;ref=Ref(v,pedPool,0x8d0);if(ref<0||!game::api.PoolsGetPed||game::api.PoolsGetPed(ref)!=v)return a;}
 if((game::EntityFlags(v)&7)!=unsigned(type))return a;
 const int model=game::EntityModel(v);if(model<0||model>=6500||!game::api.modelInfoPtrs[model])return a;
 a.v=v;a.ref=ref;a.type=type;a.model=model;a.kind=type==2?*reinterpret_cast<int *>(v+0x348):-1;a.col=*reinterpret_cast<uint8_t **>(game::api.modelInfoPtrs[model]+0x20);a.clump=game::EntityClump(v);a.table=*reinterpret_cast<void **>(v);std::memcpy(a.pose.data(),v+8,sizeof a.pose);if(type==2&&a.kind==0&&!game::IsAircraft(v)){std::memcpy(a.damage.data(),v+0x34c,a.damage.size());for(int i=0;i<6;i++)a.hinges[i]=*reinterpret_cast<float *>(v+0x368+i*0x24+0xc);}return a;
}
bool Current(const Actor &a,bool pose){if(!a.v||Ref(a.v,a.type==2?vehiclePool:pedPool,a.type==2?0x6f0:0x8d0)!=a.ref)return false;const auto b=Read(a.v);return a.v&&b.v==a.v&&a.ref==b.ref&&a.type==b.type&&a.model==b.model&&a.kind==b.kind&&a.col==b.col&&a.clump==b.clump&&a.table==b.table&&(!pose||(!std::memcmp(a.pose.data(),b.pose.data(),sizeof a.pose)&&a.hinges==b.hinges&&a.damage==b.damage));}
struct Scope {Scope *previous;Actor a,b;uint8_t *points;};thread_local Scope *active;
int Collision(uint8_t *a,uint8_t *b,uint8_t *out,Pair native){Scope scope{active,Read(a),Read(b),out};active=&scope;struct Restore {Scope *p;~Restore(){active=p;}} restore{scope.previous};return native(a,b,out);}
int Physical(uint8_t *a,uint8_t *b,uint8_t *out){return Collision(a,b,out,physical);}int Automobile(uint8_t *a,uint8_t *b,uint8_t *out){return Collision(a,b,out,automobile);}int Ped(uint8_t *a,uint8_t *b,uint8_t *out){return Collision(a,b,out,ped);}
struct Column {alignas(16) std::array<uint8_t,0x68> bytes{};~Column(){if(*reinterpret_cast<void **>(bytes.data()+0x60))removePlanes(bytes.data());}};
bool Matrix(const uint8_t *m,const Actor &source){if(!m)return false;for(int row:{0,4,8,12})for(int j=0;j<3;j++){const float f=reinterpret_cast<const float *>(m)[row+j];if(!std::isfinite(f)||std::fabs(f-source.pose[row+j])>1e-5f)return false;}return true;}
bool Source(const Actor &a){return a.type==2&&a.kind==0&&!aircraftbodyclient::Owned(a.v)&&!game::IsAircraft(a.v)&&(game::EntityFlags(a.v)&game::kEntityUsesCollision);}
int Supplement(const Scope &scope,const Actor &source,const Actor &receiver,const uint8_t *panelMatrix,const uint8_t *receiverMatrix,uint8_t *receiverCol,uint8_t *out,int count,bool flip){
 if(!Current(source,true)||!Current(receiver,false)||!Source(source)||!Matrix(panelMatrix,source))return count;
 const int spheres=*reinterpret_cast<int16_t *>(receiverCol+0x28);if(spheres<1||spheres>64||!*reinterpret_cast<void **>(receiverCol+0x38))return count;
 // Only body spheres participate. Native wheel-line results stay exclusively
 // owned by the original call; copied COLs never acquire triangle-plane caches.
 alignas(16) uint8_t body[0x68]{};std::memcpy(body,receiverCol,0x28);*reinterpret_cast<int16_t *>(body+0x28)=int16_t(spheres);*reinterpret_cast<void **>(body+0x38)=*reinterpret_cast<void **>(receiverCol+0x38);
 panelphysics::Surface surface;
 for(int door=0;door<6&&count<64;door++){
  const int firstContact=count;for(size_t first=0;first<131072&&count<64;first+=surface.count){
  if(!panelphysics::CollisionSurface(source.v,door,surface,first))break;Column panel;auto col=panel.bytes.data();Vec3 lo=surface.vertices[0],hi=lo;
  for(size_t i=0;i<surface.count*3;i++){auto p=surface.vertices[i];lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
  const Vec3 centre{(lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f,(lo.z+hi.z)*.5f};*reinterpret_cast<Vec3 *>(col)=centre;*reinterpret_cast<float *>(col+12)=std::sqrt((hi.x-lo.x)*(hi.x-lo.x)+(hi.y-lo.y)*(hi.y-lo.y)+(hi.z-lo.z)*(hi.z-lo.z))*.5f+.01f;
  *reinterpret_cast<Vec3 *>(col+0x10)={lo.x-.01f,lo.y-.01f,lo.z-.01f};*reinterpret_cast<Vec3 *>(col+0x1c)={hi.x+.01f,hi.y+.01f,hi.z+.01f};*reinterpret_cast<int16_t *>(col+0x2c)=int16_t(surface.count);*reinterpret_cast<void **>(col+0x50)=surface.vertices.data();*reinterpret_cast<void **>(col+0x58)=surface.faces.data();
  const uint8_t piece[]={1,2,3,4,5,6};for(size_t i=0;i<surface.count;i++)surface.faces[i].piece=piece[door];
  alignas(16) std::array<uint8_t,64*40> points{};const int extra=models(receiverMatrix,body,panelMatrix,col,points.data(),nullptr,nullptr);
  if(!Current(scope.a,false)||!Current(scope.b,false)||!Current(source,true))return count;
  if(extra<0||extra>spheres)return count;
  for(int i=0;i<extra&&count<64;i++){auto p=points.data()+i*40;if(flip){for(int j=0;j<3;j++)reinterpret_cast<float *>(p+0x10)[j]*=-1;std::swap(p[0x20],p[0x22]);std::swap(p[0x21],p[0x23]);}
   bool duplicate=false;for(int j=0;j<count;j++){auto old=out+j*40;float distance=0,normal=0;for(int k=0;k<3;k++){float d=reinterpret_cast<float *>(old)[k]-reinterpret_cast<float *>(p)[k];distance+=d*d;normal+=reinterpret_cast<float *>(old+0x10)[k]*reinterpret_cast<float *>(p+0x10)[k];}
    if(distance<1e-6f||(j>=firstContact&&old[0x21]==p[0x21]&&distance<.0625f&&normal>.99f)){duplicate=true;if(j>=firstContact&&*reinterpret_cast<float *>(p+0x24)>*reinterpret_cast<float *>(old+0x24))std::memcpy(old,p,40);break;}}
   if(!duplicate){std::memcpy(out+count*40,p,40);++count;}

  }
  if(!surface.more)break;
  }
 }return count;
}
int ColModels(const uint8_t *ma,uint8_t *a,const uint8_t *mb,uint8_t *b,uint8_t *out,uint8_t *lines,float *fractions){
 const int original=models(ma,a,mb,b,out,lines,fractions);const auto scope=active;
 if(!ready||!gConfig.carParts||!scope||!out||scope->points!=out||original<0||original>=64||a!=scope->a.col||b!=scope->b.col||!Current(scope->a,false)||!Current(scope->b,false))return original;
 int count=Supplement(*scope,scope->b,scope->a,mb,ma,a,out,original,false);
 return Supplement(*scope,scope->a,scope->b,ma,mb,b,out,count,true);
}
}
bool Install(){if(ready)return true;
 vehiclePool=reinterpret_cast<void **>(dlsym(RTLD_MAIN_ONLY,"_ZN6CPools15ms_pVehiclePoolE"));pedPool=reinterpret_cast<void **>(dlsym(RTLD_MAIN_ONLY,"_ZN6CPools11ms_pPedPoolE"));removePlanes=reinterpret_cast<decltype(removePlanes)>(dlsym(RTLD_MAIN_ONLY,"_ZN10CCollision20RemoveTrianglePlanesEP9CColModel"));if(!vehiclePool||!pedPool||!removePlanes)return false;
#define C(v,s,f) if(!v)v=reinterpret_cast<decltype(v)>(hooks::Chain(s,reinterpret_cast<void *>(f)))
 C(models,"_ZN10CCollision16ProcessColModelsERK7CMatrixR9CColModelS2_S4_P9CColPointS6_Pf",ColModels);C(physical,"_ZN9CPhysical22ProcessEntityCollisionEP7CEntityP9CColPoint",Physical);C(automobile,"_ZN11CAutomobile22ProcessEntityCollisionEP7CEntityP9CColPoint",Automobile);C(ped,"_ZN4CPed22ProcessEntityCollisionEP7CEntityP9CColPoint",Ped);
#undef C
 return ready=models&&physical&&automobile&&ped;
}
}
