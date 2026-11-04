#if __has_include("carfire.h")
#include "carfire.h"
#include "carmechanics.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include <array>
#include <map>
#include <string>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <dlfcn.h>
namespace fixture {void *Lookup(void *,const char *);void *Native(const char *);std::map<std::string,void *> entries;uint64_t gates[3]{};void *imageBase=nullptr;}
namespace game {Api api{};}
Config gConfig;void Log(const char *,...){}
namespace carmechanics {firemodel::Input sample;float exposure=0;firemodel::Input FireInput(const uint8_t *){return sample;}void Burn(uint8_t *,float dt){if(gConfig.carMechanics)exposure+=dt;}}
namespace hooks {
void *Chain(const char *s,void *p){fixture::entries[s]=p;return fixture::Native(s);}
volatile uint64_t *Flag(const char *s){if(!strcmp(s,"skip:car_fire_countdown"))return &fixture::gates[0];if(!strcmp(s,"skip:car_rollover_drain"))return &fixture::gates[1];if(!strcmp(s,"skip:car_ordinary_lethal"))return &fixture::gates[2];return nullptr;}
bool IsCallTo(uint32_t,const char *){return true;}const void *ImageBase(){return fixture::imageBase;}
}
#define dlsym fixture::Lookup
#include "../src/carfire.cpp"
#undef dlsym
namespace fixture {
using Fn=void (*)(uint8_t *);using Inflict=void (*)(uint8_t *,uint8_t *,int,float,CVector);using Damage=void (*)(uint8_t *,float,uint16_t);
alignas(16) uint8_t cars[2][0x6f0]{},player[0x900]{},wanted[0x40]{};
alignas(8) uint8_t pool[24]{},manager[8+40*0x38]{};uint8_t flags[2]{1,2};void *poolPointer=pool;
uint32_t now=1000;float timeStep=5;bool mission=false,cutscene=false,throwControl=false,nest=false,explosionInside=false,repairStart=false,recycleStart=false;
int failures=0,blasts=0,starts=0,processes=0,dispatches=0,duty=0;bool nestedFlags=false;
float &F(uint8_t *v,size_t off){return *reinterpret_cast<float *>(v+off);}
template<class T>T Entry(const char *name){return reinterpret_cast<T>(entries.at(name));}
Fn Control(){return Entry<Fn>("_ZN11CAutomobile14ProcessControlEv");}Fn Fix(){return Entry<Fn>("_ZN11CAutomobile3FixEv");}
Fn Process(){return Entry<Fn>("_ZN5CFire11ProcessFireEv");}Fn Extinguish(){return Entry<Fn>("_ZN5CFire10ExtinguishEv");}
Inflict Hit(){return Entry<Inflict>("_ZN8CVehicle13InflictDamageEP7CEntity11eWeaponTypef7CVector");}
Damage Crash(){return Entry<Damage>("_ZN11CAutomobile13VehicleDamageEft");}
uint8_t *Car(int ref){int i=ref>>8;return i>=0&&i<2&&!(flags[i]&0x80)&&ref==((i<<8)|flags[i])?cars[i]:nullptr;}
bool Mission(){return mission;}
int Engine(void *d){return static_cast<uint8_t *>(d)[1];}void SetEngine(void *d,unsigned n){static_cast<uint8_t *>(d)[1]=uint8_t(n);}
void Check(bool b,const char *s){if(!b){printf("FAIL: %s\n",s);++failures;}}
void Blow(uint8_t *v,uint8_t * =nullptr){++blasts;v[0x60]=(v[0x60]&7)|0x28;F(v,0x2a0)=0;}
void NativeExtinguish(uint8_t *f){if(!f||f<manager+8||f>=manager+sizeof manager||!f[0])return;auto *v=*reinterpret_cast<uint8_t **>(f+0x10);if(v&&*reinterpret_cast<uint8_t **>(v+0x280)==f)*reinterpret_cast<void **>(v+0x280)=nullptr;f[0]=0;*reinterpret_cast<void **>(f+0x10)=nullptr;--*reinterpret_cast<int *>(manager);}
uint8_t *Start(void *,uint8_t *v,uint8_t *by,float size,uint8_t){++starts;if(*reinterpret_cast<void **>(v+0x280)||Engine(v+0x34c)>224)return nullptr;
 for(int i=0;i<40;i++){auto *f=manager+8+i*0x38;if(f[0])continue;memset(f,0,0x38);f[0]=1;*reinterpret_cast<void **>(f+0x10)=v;*reinterpret_cast<void **>(f+0x18)=by;*reinterpret_cast<void **>(v+0x280)=f;*reinterpret_cast<uint32_t *>(f+0x20)=now+4000;F(f,0x2c)=size;F(f,0x30)=1;memcpy(f+4,v+0x38,12);++*reinterpret_cast<int *>(manager);
  if(repairStart){repairStart=false;Fix()(v);}if(recycleStart){recycleStart=false;++flags[v==cars[0]?0:1];NativeExtinguish(f);F(v,0x2a0)=1000;SetEngine(v+0x34c,0);}return f;
 }return nullptr;
}
void NativeHit(uint8_t *v,uint8_t *,int type,float amount,CVector){if(!(v[0x297]&0x40)||(type==31&&(*reinterpret_cast<uint32_t *>(v+0x60)&(1u<<26))))return;const float before=F(v,0x2a0);F(v,0x2a0)=std::max(0.f,before-amount);if(F(v,0x2a0)<250)SetEngine(v+0x34c,225);if(before<=amount&&!gates[2])Blow(v);}
void NativeDamage(uint8_t *v,float amount,uint16_t){if(amount==0&&F(v,0x30)<0&&!gates[1])F(v,0x2a0)=std::max(0.f,F(v,0x2a0)-4*timeStep);else if(amount>0)F(v,0x2a0)=std::max(1.f,F(v,0x2a0)-amount);if(F(v,0x2a0)<250)SetEngine(v+0x34c,225);}
void NativeControl(uint8_t *v){if(throwControl){throwControl=false;throw std::runtime_error("native control");}if(nest&&v==cars[0]){nest=false;Control()(cars[1]);nestedFlags=gates[0]==1;}Crash()(v,0,0);if(explosionInside){explosionInside=false;Hit()(v,nullptr,30,2000,{});}if(F(v,0x2a0)<250&&!gates[0]&&!game::Wrecked(v)){F(v,0x65c)+=timeStep*20;if(F(v,0x65c)>5000)Blow(v);}}
void NativeProcess(uint8_t *f){++processes;if(!f[0])return;auto *v=*reinterpret_cast<uint8_t **>(f+0x10);if(v&&!f[1])Hit()(v,nullptr,31,timeStep*1.2f,{});if(now>*reinterpret_cast<uint32_t *>(f+0x20))Extinguish()(f);}
void NativeFix(uint8_t *v){if(auto *f=*reinterpret_cast<uint8_t **>(v+0x280))NativeExtinguish(f);F(v,0x2a0)=1000;F(v,0x65c)=0;v[0x60]=(v[0x60]&7);SetEngine(v+0x34c,0);}
void NativeDelete(uint8_t *v){*reinterpret_cast<void **>(v+0x58)=reinterpret_cast<void *>(0x9000);}
void NativeRestart(){memset(manager,0,sizeof manager);for(auto &v:cars)*reinterpret_cast<void **>(v+0x280)=nullptr;}
int Total(const void *){return *reinterpret_cast<int *>(manager);}
uint8_t *Nearest(void *,CVector at,float *distance){uint8_t *best=nullptr;for(int i=0;i<40;i++){auto *f=manager+8+i*0x38;if(!f[0]||f[1])continue;const auto p=*reinterpret_cast<CVector *>(f+4);const float d=std::sqrt((p.x-at.x)*(p.x-at.x)+(p.y-at.y)*(p.y-at.y)+(p.z-at.z)*(p.z-at.z));if(d<*distance){*distance=d;best=f;}}return best;}
void NativeDispatch(){++dispatches;}
void *Native(const char *s){
 if(strstr(s,"CAutomobile14ProcessControl"))return reinterpret_cast<void *>(NativeControl);if(strstr(s,"CAutomobile13VehicleDamage"))return reinterpret_cast<void *>(NativeDamage);
 if(strstr(s,"CVehicle13InflictDamage"))return reinterpret_cast<void *>(NativeHit);if(strstr(s,"CFire11ProcessFire"))return reinterpret_cast<void *>(NativeProcess);if(strstr(s,"CFire10Extinguish"))return reinterpret_cast<void *>(NativeExtinguish);
 if(strstr(s,"CAutomobile3Fix"))return reinterpret_cast<void *>(NativeFix);if(strstr(s,"DeleteRwObject"))return reinterpret_cast<void *>(NativeDelete);if(strstr(s,"ClearForRestart"))return reinterpret_cast<void *>(NativeRestart);
 if(strstr(s,"GenerateEmergencyServicesCar"))return reinterpret_cast<void *>(NativeDispatch);if(strstr(s,"GetTotalActiveFires"))return reinterpret_cast<void *>(Total);if(strstr(s,"FindNearestFire"))return reinterpret_cast<void *>(Nearest);return nullptr;
}
void *Lookup(void *,const char *s){
 if(!strcmp(s,"_Z15FindPlayerCoorsv"))return reinterpret_cast<void *>(+[](){return CVector{0,0,0};});
 if(!strcmp(s,"_ZN6CPools15ms_pVehiclePoolE"))return &poolPointer;if(!strcmp(s,"gFireManager"))return manager;
 if(strstr(s,"StartFireEP7CEntity"))return reinterpret_cast<void *>(Start);if(strstr(s,"GetEngineStatus"))return reinterpret_cast<void *>(Engine);if(strstr(s,"SetEngineStatus"))return reinterpret_cast<void *>(SetEngine);
 if(strstr(s,"IsPlayerOnAMission"))return reinterpret_cast<void *>(Mission);if(strstr(s,"CCutsceneMgr10ms_running"))return &cutscene;if(strstr(s,"NumFiretrucksOnDuty"))return &duty;return nullptr;
}
void Reset(){if(!entries.empty())Entry<void (*)()>("_ZN6CWorld15ClearForRestartEv")();memset(cars,0,sizeof cars);memset(player,0,sizeof player);memset(wanted,0,sizeof wanted);mission=cutscene=throwControl=nest=explosionInside=repairStart=recycleStart=false;now+=10000;timeStep=5;blasts=starts=processes=dispatches=duty=0;nestedFlags=false;carmechanics::sample={};carmechanics::exposure=0;
 for(int i=0;i<2;i++){flags[i]=uint8_t((flags[i]+1)&0x7f);auto *v=cars[i];*reinterpret_cast<void **>(v)=reinterpret_cast<void *>(0x1000);*reinterpret_cast<void **>(v+0x58)=reinterpret_cast<void *>(0x2000);*reinterpret_cast<uint32_t *>(v+0x60)=2;*reinterpret_cast<int16_t *>(v+0x6a)=150;v[0x297]=0x40;v[0x294]=1;v[0x295]=0x10;F(v,0x2a0)=1000;F(v,8)=F(v,0x1c)=F(v,0x30)=1;}
 *reinterpret_cast<void **>(player+0x778)=wanted;game::api.timeMs=&now;game::api.timeStep=&timeStep;game::api.PoolsGetVehicle=Car;game::api.FindPlayerPed=+[](){return player;};
}
void Step(unsigned ms=100){now+=ms;timeStep=ms/20.f;Control()(cars[0]);for(int i=0;i<40;i++){auto *f=manager+8+i*0x38;if(f[0])Process()(f);}}
void Light(){F(cars[0],0x2a0)=200;SetEngine(cars[0]+0x34c,225);for(int i=0;i<27;i++)Step();}
}
int main(){using namespace fixture;*reinterpret_cast<void **>(pool)=cars;*reinterpret_cast<void **>(pool+8)=flags;*reinterpret_cast<int *>(pool+16)=2;Reset();gConfig.carFire=false;Check(carfire::Install()&&entries.empty(),"startup off installs no fire hooks");gConfig.carFire=true;Check(carfire::Install(),"native fire hooks install");
 Reset();Light();Check(starts==1&&*reinterpret_cast<int *>(manager)==1&&carfire::Burning(cars[0],false)&&F(cars[0],0x65c)==0&&blasts==0,"low health becomes one real growing fire without native countdown");Check(Engine(cars[0]+0x34c)==225,"native fire-start engine-status loan restores original damage");
 Extinguish()(*reinterpret_cast<uint8_t **>(cars[0]+0x280));for(int i=0;i<50;i++)Step();Check(starts==1&&!carfire::Burning(cars[0],true),"water clears fire awareness and prevents unchanged low-health relight");Hit()(cars[0],nullptr,17,10,{});for(int i=0;i<27;i++)Step();Check(starts==2,"new accepted non-fire injury can ignite extinguished car again");
 Reset();Light();for(int i=0;i<100;i++)Step();Check(blasts==0&&carmechanics::exposure>5,"native fire persists beyond five seconds and injures existing modules");for(int i=0;i<510;i++)Step();Check(game::Wrecked(cars[0])&&blasts==0&&!carfire::Burning(cars[0],true),"fire burns out into disabled wreck without an explosion");
 Reset();F(cars[0],0x30)=-1;for(int i=0;i<100;i++)Step();Check(F(cars[0],0x2a0)==1000&&starts==0,"rolling over alone cannot drain health or ignite");
 for(int mode=0;mode<5;mode++){Reset();if(mode==0)mission=true;if(mode==1)cutscene=true;if(mode==2)cars[0][0x294]=2;if(mode==3)*reinterpret_cast<uint32_t *>(cars[0]+0x60)|=1u<<26;if(mode==4)gConfig.carFire=false;F(cars[0],0x2a0)=200;Step(6000);Check(blasts==1&&starts==0,"mission/cutscene/mission-car/protected/disabled actors retain native behavior");gConfig.carFire=true;}
 Reset();Hit()(cars[0],nullptr,17,2000,{});Check(blasts==0&&F(cars[0],0x2a0)==0,"ordinary lethal bullet retains accepted health loss without automatic blast");Hit()(cars[0],nullptr,30,2000,{});Check(blasts==1,"explosive weapon retains native destruction");
 Reset();F(cars[0],0x2a0)=200;cars[1][0x294]=2;F(cars[1],0x2a0)=200;nest=true;Step();Check(nestedFlags&&F(cars[1],0x65c)>0&&gates[0]==0&&gates[1]==0&&gates[2]==0,"nested unmanaged controller restores parent fire gates and then native defaults");
 Reset();explosionInside=true;Step();Check(blasts==1,"explicit explosion inside managed controller remains authoritative");Reset();throwControl=true;try{Step();}catch(const std::runtime_error &){}Check(!gates[0]&&!gates[1]&&!gates[2],"exception restores all native skip gates");
 Reset();F(cars[0],0x2a0)=200;repairStart=true;for(int i=0;i<28;i++)Step();Check(F(cars[0],0x2a0)==1000&&Engine(cars[0]+0x34c)==0&&!carfire::Burning(cars[0],false),"repair inside fire creation cannot restore stale engine status or fire state");
 Reset();Light();Entry<Fn>("_ZN7CEntity14DeleteRwObjectEv")(cars[0]);Step();Check(starts==1&&carfire::Burning(cars[0],false),"render replacement preserves the existing fire");Fix()(cars[0]);Check(!carfire::Burning(cars[0],false)&&F(cars[0],0x2a0)==1000,"repair extinguishes and clears progression");
 Reset();Light();Entry<void (*)()>("_ZN6CWorld15ClearForRestartEv")();Check(!carfire::Burning(cars[0],false),"world restart retires fire ownership");
 Reset();Light();Check(Entry<int (*)(const void *)>("_ZNK12CFireManager19GetTotalActiveFiresEv")(manager)==1,"ordinary fire count queries report real count");float radius=30;Check(Entry<uint8_t *(*)(void *,CVector,float *)>("_ZN12CFireManager15FindNearestFireE7CVectorPf")(manager,{100,0,0},&radius)==nullptr&&radius==30,"ordinary nearest-fire queries retain original search radius");
 Reset();alignas(8) uint8_t foreignFire[0x38]{};foreignFire[0]=1;foreignFire[1]=1;Process()(foreignFire);Check(processes==1,"unmanaged fire objects retain native processing");
 Reset();for(int i=0;i<40;i++){manager[8+i*0x38]=1;manager[9+i*0x38]=1;}F(cars[0],0x2a0)=200;for(int i=0;i<650;i++){now+=100;timeStep=5;Control()(cars[0]);}Check(!game::Wrecked(cars[0])&&!carfire::Burning(cars[0],false)&&blasts==0,"exhausted native fire pool cannot create an invisible burn or wreck");
 Reset();auto *external=Start(manager,cars[0],nullptr,.8f,0);Process()(external);Check(carfire::Burning(cars[0],false),"real external fire is adopted immediately");
 Reset();for(int i=0;i<39;i++){manager[8+i*0x38]=1;manager[9+i*0x38]=1;}external=Start(manager,cars[0],nullptr,.8f,0);Process()(external);Check(carfire::Burning(cars[0],false),"last native fire slot is eligible for managed external fire");
 Reset();F(cars[0],0x2a0)=200;recycleStart=true;for(int i=0;i<28;i++)Step();Check(F(cars[0],0x2a0)==1000&&Engine(cars[0]+0x34c)==0&&!carfire::Burning(cars[0],false),"recycled actor inside native fire creation cannot inherit fire or status loan");
 printf("carfire: %s\n",failures?"FAILED":"all passed");return failures?1:0;
}
#else
#include <cstdio>
int main(){puts("FAIL: native progressive car fire owner is missing");return 1;}
#endif
