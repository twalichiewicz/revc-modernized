#include "game.h"
#include "config.h"
#include "hooks.h"
#include "carglass.h"
#include <map>
#include <string>
#include <cstring>
#include <cstdio>
#include <cmath>
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif
static void *Lookup(void *,const char *);
#define dlsym Lookup
#include "../src/airframedamage.cpp"
#undef dlsym
namespace game {Api api{};}
Config gConfig;void Log(const char *,...){}
namespace carglass {int restored=0;void Restore(uint8_t *){++restored;}}
namespace fixture {
alignas(16) uint8_t car[0x6f0]{},pool[24]{},info[0x80]{},col[0x40]{};uint8_t flags[1]{1};uint8_t *models[6500]{};void *poolPointer=pool;int fireRepairs=0;bool teleport=false;float step=1;uint32_t now=1000;std::map<std::string,void *> entries;int bad=0;
void Check(bool b,const char *s){if(!b){printf("FAIL: %s\n",s);++bad;}}
uint8_t *Get(int ref){return ref==flags[0]?car:nullptr;}void Native(uint8_t *){}void Restart(){}
void NativeFlying(uint8_t *v,int){auto &speed=*reinterpret_cast<Vec3 *>(v+0x84);speed.y+=.02f;speed.z+=.03f;if(teleport)*reinterpret_cast<float *>(v+0x38)+=100;}
void Move(uint8_t *v,float x,float y,float z){auto &speed=*reinterpret_cast<Vec3 *>(v+0x84);speed.x+=x/1000;speed.y+=y/1000;speed.z+=z/1000;}
void Turn(uint8_t *,float,float,float,float,float,float){}
void Setup(){std::memset(car,0,sizeof car);*reinterpret_cast<void **>(car)=reinterpret_cast<void *>(1);*reinterpret_cast<void **>(car+0x58)=reinterpret_cast<void *>(2);*reinterpret_cast<unsigned *>(car+0x60)=2;*reinterpret_cast<int16_t *>(car+0x6a)=180;*reinterpret_cast<int *>(car+0x348)=4;*reinterpret_cast<float *>(car+0x2a0)=1000;*reinterpret_cast<float *>(car+0xcc)=1000;*reinterpret_cast<float *>(car+0xd0)=1000;car[0x297]=0x40;car[0x295]=0x10;car[0x294]=2;auto m=reinterpret_cast<float *>(car+8);m[0]=m[5]=m[10]=m[15]=1;}
}
namespace hooks {void *Chain(const char *s,void *p){auto old=std::strstr(s,"FlyingControl")?reinterpret_cast<void *>(fixture::NativeFlying):std::strstr(s,"Restart")?reinterpret_cast<void *>(fixture::Restart):reinterpret_cast<void *>(fixture::Native);fixture::entries[s]=p;return old;}}
static void *Lookup(void *,const char *s){if(std::strstr(s,"ApplyMoveForce"))return reinterpret_cast<void *>(fixture::Move);if(std::strstr(s,"ApplyTurnForce"))return reinterpret_cast<void *>(fixture::Turn);return std::strstr(s,"VehiclePool")?&fixture::poolPointer:nullptr;}
int main(){using namespace fixture;Setup();game::api.PoolsGetVehicle=Get;game::api.timeMs=&now;game::api.timeStep=&step;game::api.modelInfoPtrs=models;models[180]=info;info[0x1d]=6;*reinterpret_cast<void **>(info+0x20)=col;*reinterpret_cast<void **>(info+0x30)=reinterpret_cast<void *>(3);*reinterpret_cast<Vec3 *>(col+0x10)={-10,-10,-2};*reinterpret_cast<Vec3 *>(col+0x1c)={10,10,2};*reinterpret_cast<void **>(pool)=car;*reinterpret_cast<void **>(pool+8)=flags;*reinterpret_cast<int *>(pool+16)=1;gConfig.planes=gConfig.heliDamage=true;game::api.RepairVehicleFire=+[](uint8_t *){++fireRepairs;};
 Check(airframedamage::Install(),"airframe owner installs native lifecycle bindings");
#if __has_feature(address_sanitizer)
 __asan_poison_memory_region(car+0x388,sizeof car-0x388);
#endif
 auto t=airframedamage::BeginBullet(car);Check(t.revision!=0,"mission-created exact CPlane gets independent systems lease");
 projectilemodel::Layer skin{0,1,projectilemodel::Material::Skin};projectilemodel::Ray ray{{-12,0,0},{0,0,0},0,1,100,&skin,1};for(int i=0;i<20;i++)airframedamage::Projectile(car,t,ray);
 float internal=1;Vec3 normal;Check(airframedamage::ComponentHit(car,{-12,0,0},{0,0,0},.17f,internal,normal)&&internal<1&&internal>=.17f,"existing opening traces a bounded real internal component volume");
 auto e=game::api.AirframeEffects?game::api.AirframeEffects(car):airframemodel::Effects{};Check(e.managed&&e.leftLift<.5f&&e.rightLift==1,"actual owner applies localized systems in missions");
 if(entries.count("_ZN8CVehicle13FlyingControlE12eFlightModel"))reinterpret_cast<void (*)(uint8_t *,int)>(entries["_ZN8CVehicle13FlyingControlE12eFlightModel"])(car,5);
 Check(entries.count("_ZN8CVehicle13FlyingControlE12eFlightModel")&&reinterpret_cast<Vec3 *>(car+0x84)->z>0&&reinterpret_cast<Vec3 *>(car+0x84)->z<.03f,"native AI/mission flight consumes damaged wing lift without replacing its flight controller");
 *reinterpret_cast<Vec3 *>(car+0x84)={};teleport=true;if(entries.count("_ZN8CVehicle13FlyingControlE12eFlightModel"))reinterpret_cast<void (*)(uint8_t *,int)>(entries["_ZN8CVehicle13FlyingControlE12eFlightModel"])(car,5);teleport=false;Check(std::fabs(reinterpret_cast<Vec3 *>(car+0x84)->z-.03f)<1e-6f,"native mission teleport retires the old force receipt");
 if(entries.count("_ZN7CEntity14DeleteRwObjectEv"))reinterpret_cast<void (*)(uint8_t *)>(entries["_ZN7CEntity14DeleteRwObjectEv"])(car);
 *reinterpret_cast<void **>(car+0x58)=reinterpret_cast<void *>(4);auto streamed=airframedamage::BeginBullet(car);auto afterStream=game::api.AirframeEffects?game::api.AirframeEffects(car):airframemodel::Effects{};Check(streamed.revision!=t.revision&&afterStream.leftLift<.5f,"render streaming retires contacts while preserving damaged internals");
 if(entries.count("_ZN11CAutomobile3FixEv"))reinterpret_cast<void (*)(uint8_t *)>(entries["_ZN11CAutomobile3FixEv"])(car);
 Check(!airframedamage::Projectile(car,t,ray)&&carglass::restored>0&&fireRepairs>0,"repair retires pending airframe bullets and restores skin");

#if __has_feature(address_sanitizer)
 __asan_unpoison_memory_region(car+0x388,sizeof car-0x388);
#endif
 for(size_t i=0x388;i<sizeof car;i++)Check(car[i]==0,"CPlane owner never touches automobile-only members");
 printf("airframedamage: %s\n",bad?"FAILED":"all passed");return bad?1:0;
}
