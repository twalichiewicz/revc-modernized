#include "game.h"
#include "config.h"
#include "hooks.h"
#include <array>
#include <map>
#include <cstring>
#include <cstdio>
static void *Symbol(void *,const char *);
#if __has_include("../src/crashdamage.cpp")
#define dlsym Symbol
#include "../src/crashdamage.cpp"
#undef dlsym
#else
namespace crashdamage {bool Install(){return true;}void Contact(uint8_t *,float){}}
#endif
namespace game {Api api{};}
Config gConfig;void Log(const char *,...){}
namespace fixture {
alignas(16) uint8_t car[0x6f0]{},peds[2][0x8d0]{},vehiclePool[24]{},pedPool[24]{};uint8_t vehicleFlag=1,pedFlags[2]{1,1};void *vp=vehiclePool,*pp=pedPool;
std::map<std::string,void *> entries;uint32_t now=1000;int bad=0,hits=0;bool recycle=false,proof=false;
void Check(bool b,const char *m){if(!b){printf("FAIL: %s\n",m);++bad;}}
uint8_t *Vehicle(int r){return r==vehicleFlag?car:nullptr;}
uint8_t *Ped(int r){int i=r>>8;return i<2&&!(pedFlags[i]&128)&&r==((i<<8)|pedFlags[i])?peds[i]:nullptr;}
void NativeWorld(){}
bool Hurt(uint8_t *p,uint8_t *by,int weapon,float damage,int,uint8_t){Check(by==car&&weapon==44,"collision injury reaches native ped damage with vehicle source");++hits;if(!proof)*reinterpret_cast<float *>(p+0x44c)-=damage;if(recycle)++vehicleFlag;return false;}
void Flush(){if(entries.count("_ZN6CWorld7ProcessEv"))reinterpret_cast<void (*)()>(entries["_ZN6CWorld7ProcessEv"])();}
void Setup(){now+=1000;std::memset(car,0,sizeof car);std::memset(peds,0,sizeof peds);++vehicleFlag;for(auto &f:pedFlags)++f;hits=0;recycle=proof=false;
 *reinterpret_cast<void **>(car)=reinterpret_cast<void *>(1);*reinterpret_cast<unsigned *>(car+0x60)=2|0x100;car[0x297]=64;*reinterpret_cast<int16_t *>(car+0x6a)=227;*reinterpret_cast<void **>(car+0x58)=reinterpret_cast<void *>(2);auto m=reinterpret_cast<float *>(car+8);m[0]=m[5]=m[10]=1;
 for(int i=0;i<2;i++){*reinterpret_cast<unsigned *>(peds[i]+0x60)=3;*reinterpret_cast<float *>(peds[i]+0x44c)=100;peds[i][0x4c0]=1;*reinterpret_cast<void **>(peds[i]+0x4b8)=car;*reinterpret_cast<void **>(car+0x218+i*8)=peds[i];}
}
}
namespace hooks {void *Chain(const char *s,void *p){fixture::entries[s]=p;return reinterpret_cast<void *>(fixture::NativeWorld);}}
static void *Symbol(void *,const char *s){using namespace fixture;if(std::strstr(s,"ms_pVehiclePool"))return &vp;if(std::strstr(s,"ms_pPedPool"))return &pp;if(std::strstr(s,"InflictDamage"))return reinterpret_cast<void *>(Hurt);return nullptr;}
int main(){using namespace fixture;*reinterpret_cast<void **>(vehiclePool)=car;*reinterpret_cast<void **>(vehiclePool+8)=&vehicleFlag;*reinterpret_cast<int *>(vehiclePool+16)=1;*reinterpret_cast<void **>(pedPool)=peds;*reinterpret_cast<void **>(pedPool+8)=pedFlags;*reinterpret_cast<int *>(pedPool+16)=2;game::api.PoolsGetVehicle=Vehicle;game::api.PoolsGetPed=Ped;game::api.timeMs=&now;gConfig.carMechanics=gConfig.heliDamage=gConfig.planes=true;
 Check(crashdamage::Install(),"native collision injury bindings install");Setup();crashdamage::Contact(car,29.3f);Check(hits==0,"injury waits until native collision/world traversal completes");Flush();Check(hits==2&&game::Health(peds[0])<=0&&game::Health(peds[1])<=0,"hard helicopter deceleration injures native driver and passenger");
 Setup();for(int i=0;i<10;i++)crashdamage::Contact(car,29.3f);Flush();Check(hits==2,"one contact manifold cannot repeatedly injure occupants");
 Setup();crashdamage::Contact(car,8);Flush();Check(hits==0,"ordinary landing or kerb gives no crash injury");
 Setup();crashdamage::Contact(car,29.3f);peds[0][0x4c0]=0;Flush();Check(hits==1,"a ped who exits before delivery is not injured by the old seat receipt");
 Setup();crashdamage::Contact(car,29.3f);++vehicleFlag;Flush();Check(hits==0,"recycled source vehicle invalidates queued occupant loads");
 Setup();crashdamage::Contact(car,29.3f);++pedFlags[0];Flush();Check(hits==1,"recycled driver cannot inherit the old injury");
 Setup();recycle=true;crashdamage::Contact(car,29.3f);Flush();Check(hits==1,"native injury callback changing source generation retires remaining passengers");
 Setup();proof=true;crashdamage::Contact(car,29.3f);Flush();Check(hits==2&&game::Health(peds[0])==100,"native ped veto remains authoritative");
 Setup();*reinterpret_cast<unsigned *>(car+0x60)|=game::kEntityCollisionProof;crashdamage::Contact(car,29.3f);Flush();Check(hits==0,"collision proof source rejects occupant shock");
 printf("crash damage: %d failures\n",bad);return bad?1:0;
}
