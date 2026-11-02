// Six-star native creation/crew/weapon boundary. No running game required.
#include <dlfcn.h>
static void *HunterSymbol(void *,const char *);
#define dlsym HunterSymbol
#include "../src/hunter.cpp"
#undef dlsym
#include <cstdio>
#include <cstring>

namespace game { Api api{}; float CameraHeading(){return 0;} CVector HeadingForward(float a){return {std::sin(a),std::cos(a),0};} }
Config gConfig;
void Log(const char *,...){}
static bool gKnown=true,gClear=true;
namespace wanted { bool Active(){return true;} bool OnHim(){return gKnown;} CVector Target(){return {0,0,0};} }
namespace {
alignas(16) uint8_t player[0x900]{},soldier[0x900]{},civilian[0x900]{},vehicle[0x700]{},wantedData[0x80]{};
uint8_t stream[1000*0x20]{},*ignore=nullptr;int area=0,failures=0,army=0,anims=0,refs=0,positioned=0,anonymous=0,cannon=0,rockets=0,releases=0;
alignas(8) uint8_t pedPool[0x20]{},pedFlags[16]{};uint8_t *pedPoolPtr=pedPool;
uint32_t now=10000;
void Check(bool ok,const char *s){if(!ok){printf("FAIL: %s\n",s);++failures;}}
uint8_t *Player(){return player;}
uint8_t *Vehicle(int handle){return handle==17?vehicle:nullptr;}
uint8_t *Ped(int handle){return handle==25?soldier:nullptr;}
void Request(int,int){}
void *New(size_t n){Check(n==0x6f0,"verified vehicle pool allocation size");return vehicle;}
void Construct(void *v,int model,uint8_t created){std::memset(v,0,0x6f0);auto *p=static_cast<uint8_t *>(v);*reinterpret_cast<int16_t *>(p+0x6a)=model;p[0x294]=created;p[0x60]=2|(3<<3);}
void Add(uint8_t *){}
void NativeDriver(uint8_t *v){*reinterpret_cast<uint8_t **>(v+0x218)=civilian;}
uint8_t *AddPed(int type,unsigned cop,const CVector &,int){Check(type==6 && cop==4,"explicit COP_ARMY constructor");++army;*reinterpret_cast<float *>(soldier+0x44c)=100;*reinterpret_cast<float *>(soldier+0x354)=0;*reinterpret_cast<int16_t *>(soldier+0x6a)=4;*reinterpret_cast<uint32_t *>(soldier+0x60)=3|0x100;return soldier;}
void SetDriver(uint8_t *v,uint8_t *ped){*reinterpret_cast<uint8_t **>(v+0x218)=ped;}
void Reference(uint8_t *,uint8_t **){++refs;}
void Animate(uint8_t *ped,uint8_t *v,bool driver){Check(ped==soldier && v==vehicle && driver,"native driver animation uses actual Hunter seat");++anims;}
void Seat(uint8_t *ped){++positioned;*reinterpret_cast<CVector *>(ped+0x38)=hunter::At(vehicle,{0,3.1462586f,-.1063286f});}
void HideWeapon(uint8_t *){}
void GoTo(uint8_t *v,float x,float y,float z,uint8_t){*reinterpret_cast<CVector *>(v+0x1b4)={x,y,z};v[0x19e]=20;}
void Release(uint8_t *){++releases;}
int VehicleRef(uint8_t *){return 17;}int PedRef(uint8_t *){return 25;}
bool Clear(const CVector &,const CVector &,bool,bool,bool,bool,bool,bool,bool){return gClear;}
void Anonymous(CVector *,CVector *,int){++anonymous;}
void Weapon(uint8_t *w,int type,int ammo){std::memset(w,0,0x18);std::memcpy(w,&type,4);std::memcpy(w+0xc,&ammo,4);}
bool Cannon(uint8_t *w,uint8_t *shooter,CVector *){Check(game::WeaponType(w)==35 && shooter==vehicle,"native helicopter cannon has aircraft/crew owner");++cannon;return true;}
bool Projectile(uint8_t *shooter,int type,CVector,float){Check(shooter==vehicle && type==16,"native rocket owner");++rockets;return true;}
}
static void *HunterSymbol(void *,const char *name){
#define SYM(n,f) if(!std::strcmp(name,n))return reinterpret_cast<void *>(f)
 SYM("_ZN8CVehiclenwEm",New);SYM("_ZN11CAutomobileC1Eih",Construct);SYM("_ZN6CWorld3AddEP7CEntity",Add);
 SYM("_ZN8CVehicle11SetUpDriverEv",NativeDriver);SYM("_ZN11CAutomobile19TellHeliToGoToCoorsEfffh",GoTo);
 SYM("_ZN8CCarCtrl25RegisterVehicleOfInterestEP8CVehicle",Add);SYM("_ZN8CCarCtrl32RemoveFromInterestingVehicleListEP8CVehicle",Release);
 SYM("_ZN6CPools13GetVehicleRefEP8CVehicle",VehicleRef);SYM("_ZN6CPools9GetPedRefEP4CPed",PedRef);
 SYM("_Z22FireOneInstantHitRoundP7CVectorS0_i",Anonymous);SYM("_ZN15CProjectileInfo13AddProjectileEP7CEntity11eWeaponType7CVectorf",Projectile);
 SYM("_ZN11CPopulation6AddPedE8ePedTypejRK7CVectori",AddPed);SYM("_ZN8CVehicle9SetDriverEP4CPed",SetDriver);
 SYM("_ZN7CEntity17RegisterReferenceEPPS_",Reference);SYM("_ZN4CPed13AddInCarAnimsEP8CVehicleb",Animate);
 SYM("_ZN4CPed19SetPedPositionInCarEv",Seat);SYM("_ZN4CPed31RemoveWeaponWhenEnteringVehicleEv",HideWeapon);
 SYM("_ZN7CWeaponC1E11eWeaponTypei",Weapon);SYM("_ZN7CWeapon4FireEP7CEntityP7CVector",Cannon);
 if(!std::strcmp(name,"_ZN10CStreaming16ms_aInfoForModelE"))return stream;
 if(!std::strcmp(name,"_ZN6CPools11ms_pPedPoolE"))return &pedPoolPtr;
 if(!std::strcmp(name,"_ZN5CGame8currAreaE"))return &area;
 return nullptr;
#undef SYM
}
int main(){
 game::api.FindPlayerPed=Player;game::api.PoolsGetVehicle=Vehicle;game::api.PoolsGetPed=Ped;game::api.RequestModel=Request;
 game::api.GetIsLineOfSightClear=Clear;game::api.ignoreEntity=&ignore;game::api.timeMs=&now;
 *reinterpret_cast<uint8_t **>(pedPool)=soldier;*reinterpret_cast<uint8_t **>(pedPool+8)=pedFlags;*reinterpret_cast<int *>(pedPool+0x10)=16;std::memset(pedFlags,0x80,sizeof pedFlags);
 *reinterpret_cast<uint8_t **>(player+0x778)=wantedData;*reinterpret_cast<int *>(wantedData+0x20)=6;
 stream[155*0x20+0x10]=stream[4*0x20+0x10]=1;gConfig.heliGunners=true;
 Check(hunter::Install(),"all required native crew/combat exports available");hunter::Update(now);
 Check(army==1 && *reinterpret_cast<uint8_t **>(vehicle+0x218)==soldier,"six-star dispatch has a visible soldier operating the guns");
 Check(anims==1 && positioned==1 && refs>=1 && soldier[0x4c0] && *reinterpret_cast<uint8_t **>(soldier+0x4b8)==vehicle,"soldier has native seat, animation and vehicle reference");
 *reinterpret_cast<CVector *>(vehicle+0x38)={0,-30,25};*reinterpret_cast<CVector *>(vehicle+0x08)={1,0,0};
 *reinterpret_cast<CVector *>(vehicle+0x18)={0,1,0};*reinterpret_cast<CVector *>(vehicle+0x28)={0,0,1};
 now+=100;hunter::Update(now);Check(cannon==1 && anonymous==0,"active soldier fires genuine owned Hunter cannon");
 const CVector forward=game::EntityForward(vehicle);Check(forward.x==0 && forward.y==1 && forward.z==0,"aiming restores aircraft orientation after native firing");
 gClear=false;now+=100;hunter::Update(now);Check(cannon==1,"world cover blocks cannon");gClear=true;
 gKnown=false;now+=100;hunter::Update(now);Check(cannon==1,"searching aircraft does not fire without target recognition");gKnown=true;
 *reinterpret_cast<uint8_t **>(vehicle+0x218)=player;
 now+=100;hunter::Update(now);Check(cannon==1 && *reinterpret_cast<uint8_t **>(vehicle+0x218)==player,"player takeover preserves control and stops autonomous fire");
 hunter::gHunter=17;hunter::gSoldier=25;*reinterpret_cast<uint8_t **>(vehicle+0x218)=soldier;
 *reinterpret_cast<float *>(soldier+0x44c)=0;*reinterpret_cast<float *>(soldier+0x354)=100;now+=100;hunter::Update(now);Check(cannon==1 && vehicle[0x19e]==0,"dead soldier cannot keep firing aircraft guns or direct AI flight");
 hunter::gHunter=17;hunter::gSoldier=25;vehicle[0x19e]=20;*reinterpret_cast<uint8_t **>(vehicle+0x218)=nullptr;
 now+=100;hunter::Update(now);Check(cannon==1 && vehicle[0x19e]==0,"native crew removal cannot leave a driverless AI ghost");
 hunter::gHunter=17;hunter::gSoldier=25;vehicle[0x19e]=9;vehicle[0x294]=2;*reinterpret_cast<uint8_t **>(vehicle+0x218)=soldier;
 *reinterpret_cast<float *>(soldier+0x44c)=100;now+=100;hunter::Update(now);
 Check(cannon==1 && vehicle[0x19e]==9 && vehicle[0x294]==2,"script mission ownership survives old dispatch handle");
 const int beforeArmy=army;stream[4*0x20+0x10]=0;now+=100;hunter::Update(now);
 Check(army==beforeArmy && hunter::gHunter<0,"Army model streams before a visible dispatch is created");
 stream[4*0x20+0x10]=1;std::memset(pedFlags,0,sizeof pedFlags);now+=100;hunter::Update(now);
 Check(army==beforeArmy && !*reinterpret_cast<uint8_t **>(vehicle+0x218),"full native ped pool prevents AddPed before allocation");
 pedFlags[15]=0x80;now+=100;hunter::Update(now);Check(army==beforeArmy+1,"crew creation retries after a native ped slot becomes available");
 if(!failures)puts("hunterhook: all passed");return failures?1:0;
}
