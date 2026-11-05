#define main ExistingGlassChecks
#include "test_carglass.cpp"
#undef main
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif
#include "panelphysics.h"
static void *PanelSymbol(void *,const char *);
#define dlsym PanelSymbol
#include "../src/panelphysics.cpp"
#undef dlsym
namespace paneltest {
alignas(16) uint8_t object[0x1e8]{},objectPool[24]{};uint8_t objectFlag=1;void *objectPoolPointer=objectPool;
bool leaseValid=true,noObject=false;int moves=0,turns=0;constexpr auto key="_ZN11CAutomobile20SpawnFlyingComponentEij";
uint8_t *GetObject(int ref){return ref==objectFlag&&!(objectFlag&128)?object:nullptr;}
uint8_t *Spawn(uint8_t *,int,unsigned){if(noObject)return nullptr;std::memset(object,0,sizeof object);fixture::Identity(reinterpret_cast<float *>(object+8));objectFlag=1;*reinterpret_cast<unsigned *>(object+0x60)=4|0x400;*reinterpret_cast<void **>(object+0x58)=&fixture::atoms[0][0];*reinterpret_cast<float *>(object+0xcc)=1000;*reinterpret_cast<float *>(object+0xd0)=1000;return object;}
int Type(const void *p){for(auto &c:fixture::clumps)if(p==&c)return 2;return 1;}
void Move(uint8_t *v,float x,float y,float z){++moves;const float mass=*reinterpret_cast<float *>(v+0xcc);auto &speed=*reinterpret_cast<Vec3 *>(v+0x84);speed={speed.x+x/mass,speed.y+y/mass,speed.z+z/mass};}
void Turn(uint8_t *,float,float,float,float,float,float){++turns;}
void Fresh(){fixture::Reset();leaseValid=true;noObject=false;moves=turns=0;objectFlag=1;*reinterpret_cast<float *>(fixture::cars.data()+0xcc)=1000;*reinterpret_cast<float *>(fixture::cars.data()+0xd0)=1000;*reinterpret_cast<void **>(fixture::cars.data()+0x440+15*8)=&fixture::frames[0][0];auto door=fixture::cars.data()+0x368+2*0x24;*reinterpret_cast<float *>(door) = 0;*reinterpret_cast<float *>(door+4)=-1.2f;door[9]=2;fixture::cars[0x34c+9+2]=2;}
panelphysics::Load Load(){return {fixture::cars.data(),{1},15,2,{-.8f,.5f,-.1f},{1,0,0},12};}
uint8_t *Detached(){return reinterpret_cast<uint8_t *(*)(uint8_t *,int,unsigned)>(fixture::entries[key])(fixture::cars.data(),15,2);}
}
namespace carparts {bool CurrentBullet(uint8_t *,const BulletToken &t){return paneltest::leaseValid&&t.revision==1;}}
static void *PanelSymbol(void *h,const char *s){using namespace paneltest;if(std::strstr(s,"ms_pObjectPool"))return &objectPoolPointer;if(std::strstr(s,"CPools9GetObject"))return reinterpret_cast<void *>(GetObject);if(std::strstr(s,"RwObjectGetType"))return reinterpret_cast<void *>(Type);if(std::strstr(s,"ApplyMoveForce"))return reinterpret_cast<void *>(Move);if(std::strstr(s,"ApplyTurnForce"))return reinterpret_cast<void *>(Turn);return Lookup(h,s);}
int PanelPhysicsChecks(){using namespace fixture;using namespace paneltest;if(ExistingGlassChecks())return 1;
 *reinterpret_cast<void **>(objectPool)=object;*reinterpret_cast<void **>(objectPool+8)=&objectFlag;*reinterpret_cast<int *>(objectPool+16)=1;gConfig.carParts=true;entries[key]=reinterpret_cast<void *>(Spawn);Check(panelphysics::Install(),"panel physics native bindings install");
 Fresh();panelphysics::Apply(Load(),[](){});auto hinge=cars.data()+0x368+2*0x24;Check(std::fabs(*reinterpret_cast<float *>(hinge+0x14))>0,"local absorbed shot work produces native hinge angular momentum");
 Fresh();cars[0x34c+9+2]=1;panelphysics::Apply(Load(),[](){});Check(moves==1&&turns==1,"a rigidly attached panel transfers its local impulse to the native vehicle body");
 Fresh();*reinterpret_cast<int *>(base->prefix+0x20)=0;cars[0x34c+9+2]=1;panelphysics::Apply(Load(),[](){});Check(moves==0&&turns==0,"missing morph target refuses material measurement before applying forces");
 Fresh();panelphysics::Apply(Load(),[](){cars[0x34c+9+2]=3;Detached();});Check(moves==1&&turns==1&&*reinterpret_cast<float *>(object+0x84)>0,"detached native CObject receives the point impulse exactly once");
 Check((*reinterpret_cast<unsigned *>(object+0x60)&0x100)&&!(*reinterpret_cast<unsigned *>(object+0x60)&0x400),"detached part enables native world collision and dynamic physics");
 Check(*reinterpret_cast<float *>(object+0xcc)>1&&*reinterpret_cast<float *>(object+0xcc)<100&&*reinterpret_cast<float *>(object+0xd0)>0,"native mass and inertia come from actual remaining panel material");
 Fresh();panelphysics::Apply(Load(),[](){cars[0x34c+9+2]=3;Detached();objectFlag|=128;});Check(moves==0&&turns==0,"deleted or recycled debris cannot receive a late impulse");
 Fresh();panelphysics::Apply(Load(),[](){cars[0x34c+9+2]=3;Detached();*reinterpret_cast<int16_t *>(object+0x6a)=140;});Check(moves==0&&turns==0,"repurposed debris cannot receive the old part's impulse");
 Fresh();panelphysics::Apply(Load(),[](){leaseValid=false;});Check(moves==0&&turns==0,"repair invalidates a pending panel impulse");
 Fresh();noObject=true;panelphysics::Apply(Load(),[](){cars[0x34c+9+2]=3;Detached();});Check(moves==0&&turns==0,"temporary object exhaustion does not invent debris or kick the parent");
 Fresh();*reinterpret_cast<int *>(cars.data()+0x348)=4;
#if __has_feature(address_sanitizer)
 __asan_poison_memory_region(cars.data()+0x388,0x6f0-0x388);
#endif
 panelphysics::Apply(Load(),[](){});Check(moves==0&&turns==0,"CPlane never uses native automobile hinge members");
#if __has_feature(address_sanitizer)
 __asan_unpoison_memory_region(cars.data()+0x388,0x6f0-0x388);
#endif
 Fresh();gConfig.carParts=false;panelphysics::Apply(Load(),[](){Detached();});Check(moves==0&&turns==0&&*reinterpret_cast<float *>(object+0xcc)==1000,"runtime off preserves native part physics");gConfig.carParts=true;
 Reset();std::printf("panel physics: %d failures\n",failed);return failed?1:0;
}

#ifndef PANELPHYSICS_NO_MAIN
int main(){return PanelPhysicsChecks();}
#endif
