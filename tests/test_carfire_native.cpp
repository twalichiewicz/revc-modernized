// Exact pristine native callers/consumers; host bindings replace external
// dependencies, while the tested eligibility/control instructions stay native.
#define main ExistingFireChecks
#include "test_carfire.cpp"
#undef main
#include "nativejetshadowarm.h"
namespace nativefire {
using namespace fixture;using namespace nativejetshadowtest;
Code *activeCode;void *countEntry;int spawned=0,requests=0,existingTrucks=0,refs=0,flee=0,sprays=0;alignas(8) uint8_t streaming[0x2000]{};uintptr_t vtable[64]{};
void Ignore(...){}int Zero(){return 0;}bool Yes(int){return true;}bool No(int){return false;}uint8_t *NoVehicle(){return nullptr;}
uint8_t *Player(){return player;}CVector Coords(){return {0,0,0};}
int CountCars(int model){return model==137?existingTrucks:0;}
void Request(int,int){++requests;}void Ref(uint8_t *,uint8_t **){++refs;}void Flee(uint8_t *){++flee;}
uint8_t *Free(void *){for(int i=0;i<40;i++)if(!manager[8+i*0x38])return manager+8+i*0x38;return nullptr;}
bool Generate(unsigned model,CVector at){Check(model==137&&std::fabs(at.x-60)<.01f,"native response spawns a real firetruck toward the fire location");++spawned;++duty;Store(activeCode->Page(0x1003ba98c),int32_t(1));return true;}
CVector Transform(const float *m,const CVector &v){return {m[12]+m[0]*v.x+m[4]*v.y+m[8]*v.z,m[13]+m[1]*v.x+m[5]*v.y+m[9]*v.z,m[14]+m[2]*v.x+m[6]*v.y+m[10]*v.z};}
CVector Rotate(const float *m,const CVector &v){auto p=Transform(m,v);return {p.x-m[12],p.y-m[13],p.z-m[14]};}
struct Pair {float sine,cosine;};Pair SinCos(float x){return {std::sin(x),std::cos(x)};}float Sin(float x){return std::sin(x);}float Atan(float x,float y){return std::atan2(x,y);}
uint8_t *Target(void *,CVector at,float minimum,float maximum){auto *f=*reinterpret_cast<uint8_t **>(cars[0]+0x280);if(!f)return nullptr;const auto p=*reinterpret_cast<CVector *>(f+4);const float d=std::hypot(at.x-p.x,at.y-p.y);return d>minimum&&d<maximum?f:nullptr;}
void Spray(uint32_t id,CVector *p,CVector *d){Check(id==uint32_t(reinterpret_cast<uintptr_t>(cars[1]))&&p&&d&&std::isfinite(p->x)&&std::isfinite(d->x),"actual native firetruck produces finite native cannon vectors");++sprays;}
__attribute__((naked)) void CountExit(){__asm__ volatile("br x28\n");}
__attribute__((naked)) void Count(void *,uint8_t *){__asm__ volatile(
 "stp x29,x30,[sp,#-0x40]!\n"
 "stp x19,x20,[sp,#0x10]\n"
 "stp x27,x28,[sp,#0x20]\n"
 "sub sp,sp,#0x200\n"
 "add x29,sp,#0x180\n"
 "mov x19,x1\n"
 "mov w20,#225\n"
 "stp xzr,xzr,[sp,#0x90]\n"
 "adr x28,1f\n"
 "blr x0\n"
 "1: add sp,sp,#0x200\n"
 "ldp x27,x28,[sp,#0x20]\n"
 "ldp x19,x20,[sp,#0x10]\n"
 "ldp x29,x30,[sp],#0x40\n"
 "ret\n");}
void ControlCount(uint8_t *v){Count(countEntry,v);}
__attribute__((naked)) void ParkExit(){__asm__ volatile("mov w8,#11\nstrb w8,[x19,#0x19e]\nbr x28\n");}
__attribute__((naked)) void Arrival(void *,uint8_t *){__asm__ volatile(
 "stp x29,x30,[sp,#-0x50]!\n stp x19,x20,[sp,#0x10]\n stp x27,x28,[sp,#0x20]\n sub sp,sp,#0x20\n mov x19,x1\n add x20,x1,#0x295\n ldr w8,[x20]\n adr x28,1f\n blr x0\n 1: add sp,sp,#0x20\n ldp x27,x28,[sp,#0x20]\n ldp x19,x20,[sp,#0x10]\n ldp x29,x30,[sp],#0x50\n ret\n");}
void Load(Code &code,uint64_t pc,uint64_t value,unsigned reg){const auto offset=size_t(static_cast<uint8_t *>(code.Address(pc))-code.code);Store(code.Address(pc),code.Load(offset,value,reg));}
void Skip(Code &code,uint64_t pc,uint64_t target,uint64_t *flag)
{
 const size_t at=code.cursor;code.cursor+=32;Require(code.cursor<0x10000,"native fire gate stub fits");
 auto branch=[&](size_t from,void *to){Store(code.code+from,uint32_t(0x14000000)|(uint32_t((static_cast<uint8_t *>(to)-(code.code+from))/4)&0x3ffffff));};
 Store(code.code+at,uint32_t(0x580000d0));Store(code.code+at+4,uint32_t(0xf9400210));Store(code.code+at+8,uint32_t(0xb4000050));
 auto *destination=code.Address(target);if(!destination)destination=code.code+code.Thunk(reinterpret_cast<void *>(CountExit));branch(at+12,destination);
 Store(code.code+at+16,code.image.Word(pc));branch(at+20,static_cast<uint8_t *>(code.Address(pc))+4);Store(code.code+at+24,uint64_t(flag));
 const size_t site=size_t(static_cast<uint8_t *>(code.Address(pc))-code.code);branch(site,code.code+at);
}
void Globals(Code &code)
{
 Store(code.Page(0x100540008),int32_t(0));Store(code.Page(0x10036926c),int32_t(1000));
 for(auto a:{0x1003ba988,0x1003ba98c,0x1003ba99c,0x1003ba994,0x1003ba984,0x1003ba998,0x1003ba9ac})Store(code.Page(a),int32_t(a==0x1003ba988?1:0));
 memset(streaming,0,sizeof streaming);streaming[0x178+0x1130]=streaming[0x178+0xd0]=1;Store(code.Page(0x10058d660),now);Store(code.Page(0x10058d674),timeStep);
}
}
int main(){if(ExistingFireChecks())return 1;using namespace fixture;using namespace nativefire;
 Code code;activeCode=&code;code.Add(0x100052f40,0x1000531e8);code.Add(0x10024ea38,0x10024f5c4);code.Add(0x10027d608,0x10027dc48);code.Add(0x10024d258,0x10024d3f0);
 code.bindings={{0x10010c0f4,reinterpret_cast<void *>(Player)},{0x10010c0b4,reinterpret_cast<void *>(NoVehicle)},{0x10010c118,reinterpret_cast<void *>(Coords)},
  {0x1001194ec,reinterpret_cast<void *>(Zero)},{0x1000550e8,reinterpret_cast<void *>(CountCars)},{0x10012296c,reinterpret_cast<void *>(Request)},
  {0x100124e88,reinterpret_cast<void *>(Ignore)},{0x10005e5d4,reinterpret_cast<void *>(Generate)},
  {0x1000e71e0,entries.at("_ZNK12CFireManager19GetTotalActiveFiresEv")},{0x1000e70d0,entries.at("_ZN12CFireManager15FindNearestFireE7CVectorPf")},
  {0x100270bb8,reinterpret_cast<void *>(Engine)},{0x100270854,reinterpret_cast<void *>(SetEngine)},
  {0x100143f28,reinterpret_cast<void *>(Zero)},{0x100117418,reinterpret_cast<void *>(Ignore)},
  {0x10020445c,reinterpret_cast<void *>(Ignore)},{0x10024cfd0,reinterpret_cast<void *>(CountExit)}};
 code.Finish();pthread_jit_write_protect_np(0);
 // Redirect only native global bindings; the surrounding instructions and
 // native branch targets remain the supported image's actual instructions.
 for(auto pc:{0x100053024,0x100053130})Load(code,pc,uint64_t(manager)-0xf08,0);
 Load(code,0x100053194,uint64_t(streaming),8);
 Skip(code,0x10024eb7c,0x10024eba0,&gates[1]);Skip(code,0x10027daa8,0x10027dbe4,&gates[2]);Skip(code,0x10024d25c,0x10024cfd0,&gates[0]);
 const size_t at=size_t(static_cast<uint8_t *>(code.Address(0x10024d3d4))-code.code),end=code.Thunk(reinterpret_cast<void *>(CountExit));
 Store(code.code+at,uint32_t(0x5400000d)|((uint32_t((int64_t(end)-int64_t(at))/4)&0x7ffff)<<5));
 sys_icache_invalidate(code.code,0x10000);pthread_jit_write_protect_np(1);
 const auto originalDamage=carfire::gDamage;const auto originalInflict=carfire::gInflict;const auto originalControl=carfire::gControl;
 carfire::gDamage=reinterpret_cast<Damage>(code.Address(0x10024ea38));Reset();Globals(code);F(cars[0],0x30)=-1;
 {carfire::Scope scope(cars[0],1,true);Crash()(cars[0],0,0);}Check(F(cars[0],0x2a0)==1000,"actual native rollover health drain is skipped only under current ownership");
 gConfig.carFire=false;{carfire::Scope scope(cars[0],1,true);Crash()(cars[0],0,0);}Check(F(cars[0],0x2a0)==980,"actual native disabled rollover drain remains native");gConfig.carFire=true;
 carfire::gDamage=originalDamage;carfire::gInflict=reinterpret_cast<Inflict>(code.Address(0x10027d608));Reset();Globals(code);vtable[0x108/8]=reinterpret_cast<uintptr_t>(Blow);*reinterpret_cast<void **>(cars[0])=vtable;
 Hit()(cars[0],nullptr,31,2000,{});Check(blasts==0&&F(cars[0],0x2a0)==0,"actual native lethal fire damage cannot bypass progressive burn");
 F(cars[0],0x2a0)=100;Hit()(cars[0],nullptr,30,2000,{});Check(blasts==1,"actual native explosive damage retains its virtual destruction call");
 carfire::gInflict=originalInflict;Reset();Globals(code);countEntry=code.Address(0x10024d258);carfire::gControl=ControlCount;F(cars[0],0x2a0)=200;
 for(int i=0;i<400;i++)Step(20);Check(blasts==0&&F(cars[0],0x65c)==0,"actual native flame/countdown block stays retired beyond five seconds");
 gConfig.carFire=false;*reinterpret_cast<void **>(cars[0])=vtable;for(int i=0;i<260&&!blasts;i++){now+=20;timeStep=1;Store(code.Page(0x10058d674),timeStep);Control()(cars[0]);}Check(blasts==1,"actual native disabled countdown still invokes destruction");carfire::gControl=originalControl;gConfig.carFire=true;
 Reset();F(cars[0],0x38)=60;Light();now=50000;Globals(code);imageBase=static_cast<uint8_t *>(code.Address(0x100052f40))-0x52f40;carfire::gDispatch=reinterpret_cast<void (*)()>(code.Address(0x100052f40));spawned=requests=0;existingTrucks=0;
 Entry<void (*)()>("_ZN8CCarCtrl28GenerateEmergencyServicesCarEv")();Check(spawned==1&&requests==2&&*reinterpret_cast<int *>(manager)==1,"one real managed fire reaches actual native streaming and emergency spawn path");
 Entry<void (*)()>("_ZN8CCarCtrl28GenerateEmergencyServicesCarEv")();Check(spawned==1,"native responder on-duty gate prevents duplicate dispatch");
 Store(code.Page(0x1003ba98c),int32_t(0));duty=0;now+=1000;Store(code.Page(0x10058d660),now);Entry<void (*)()>("_ZN8CCarCtrl28GenerateEmergencyServicesCarEv")();Check(spawned==1,"native35-second dispatch cooldown remains effective");
 now+=35001;Store(code.Page(0x10058d660),now);existingTrucks=2;Entry<void (*)()>("_ZN8CCarCtrl28GenerateEmergencyServicesCarEv")();Check(spawned==1,"native local firetruck count limit remains effective");
 existingTrucks=0;*reinterpret_cast<int *>(wanted+0x20)=4;Entry<void (*)()>("_ZN8CCarCtrl28GenerateEmergencyServicesCarEv")();Check(spawned==1,"native wanted-level gate remains effective");*reinterpret_cast<int *>(wanted+0x20)=0;
 streaming[0x178+0x1130]=0;Entry<void (*)()>("_ZN8CCarCtrl28GenerateEmergencyServicesCarEv")();Check(spawned==1,"native missing responder model waits for streaming");
 carfire::gDispatch=NativeDispatch;imageBase=nullptr;
 // Native fire registration and water extinction use actual manager records,
 // target references, engine acceptance and active-count instructions.
 Code fires;fires.Add(0x1000e6388,0x1000e6674);fires.Add(0x1000e62a4,0x1000e6388);
 fires.bindings={{0x1000e6754,reinterpret_cast<void *>(Free)},{0x100270bb8,reinterpret_cast<void *>(Engine)},
  {0x100179940,reinterpret_cast<void *>(No)},{0x100179970,reinterpret_cast<void *>(Yes)},{0x1000511c4,reinterpret_cast<void *>(Flee)},
  {0x100143f28,reinterpret_cast<void *>(Zero)},{0x100117418,reinterpret_cast<void *>(Ref)},
  {0x1000e0458,reinterpret_cast<void *>(Ignore)},{0x1000e0018,reinterpret_cast<void *>(Ignore)}};
 fires.Finish();pthread_jit_write_protect_np(0);Load(fires,0x1000e6610,uint64_t(manager)-0xf08,8);Load(fires,0x1000e62b8,uint64_t(manager)-0xf08,8);sys_icache_invalidate(fires.code,0x10000);pthread_jit_write_protect_np(1);
 carfire::gStart=reinterpret_cast<decltype(carfire::gStart)>(fires.Address(0x1000e6388));carfire::gExtinguish=reinterpret_cast<Fn>(fires.Address(0x1000e62a4));
 Reset();Store(fires.Page(0x10058d660),now);Light();Check(*reinterpret_cast<int *>(manager)==1&&refs>0&&flee==1&&Engine(cars[0]+0x34c)==225,"actual native fire registers real count/references and flees occupants through the scoped status loan");
 Extinguish()(*reinterpret_cast<uint8_t **>(cars[0]+0x280));Check(*reinterpret_cast<int *>(manager)==0&&!*reinterpret_cast<void **>(cars[0]+0x280)&&!carfire::Burning(cars[0],true),"actual native water extinction clears target binding/count and managed awareness");
 Code water;water.Add(0x1000e7278,0x1000e7448);water.bindings={{0x100143f28,reinterpret_cast<void *>(Zero)},{0x10020445c,reinterpret_cast<void *>(Ignore)},{0x1000e62a4,entries.at("_ZN5CFire10ExtinguishEv")}};water.Finish();
 Reset();Store(fires.Page(0x10058d660),now);Light();auto *f=*reinterpret_cast<uint8_t **>(cars[0]+0x280);F(f,0x30)=1;Store(water.Page(0x10058d674),5.f);auto wash=reinterpret_cast<bool (*)(void *,CVector,float)>(water.Address(0x1000e7278));
 Check(wash(manager,{0,0,0},5)&&f[0]&&F(f,0x30)<1,"actual native water gradually reduces fire strength");for(int i=0;i<30&&f[0];i++)wash(manager,{0,0,0},5);Check(!f[0]&&!carfire::Burning(cars[0],true)&&*reinterpret_cast<int *>(manager)==0,"actual native water path reaches hooked extinction and retires managed fire");
 Code truck;truck.Add(0x10024f5c4,0x10024fb14);truck.bindings={{0x10010c0b4,reinterpret_cast<void *>(NoVehicle)},{0x1000e7154,entries.at("_ZN12CFireManager33FindFurthestFire_NeverMindFireMenE7CVectorff")},
  {0x1002f50e8,reinterpret_cast<void *>(Atan)},{0x1002f5844,reinterpret_cast<void *>(Sin)},{0x1002f4ef0,reinterpret_cast<void *>(SinCos)},
  {0x100172f48,reinterpret_cast<void *>(Transform)},{0x100172ec0,reinterpret_cast<void *>(Rotate)},{0x100143f28,reinterpret_cast<void *>(Zero)},{0x10022632c,reinterpret_cast<void *>(Spray)}};truck.Finish();pthread_jit_write_protect_np(0);Load(truck,0x10024f600,uint64_t(manager)-0xf08,0);sys_icache_invalidate(truck.code,0x10000);pthread_jit_write_protect_np(1);
 Reset();F(cars[0],0x38)=60;Store(fires.Page(0x10058d660),now);Light();cars[1][0x60]=2|0x18;F(cars[1],0x38)=40;Store(truck.Page(0x10058d660),uint32_t(0x400));Store(truck.Page(0x10058d674),5.f);
 reinterpret_cast<Fn>(truck.Address(0x10024f5c4))(cars[1]);Check(sprays==1,"actual native AI firetruck targets registered fire and emits its water cannon");
 Code arrival;arrival.Add(0x1000507e0,0x10005085c);const auto leave=entries.count("_ZN6CCarAI23TellOccupantsToLeaveCarEP8CVehicle")?entries.at("_ZN6CCarAI23TellOccupantsToLeaveCarEP8CVehicle"):reinterpret_cast<void *>(CrewLeave);
 arrival.bindings={{0x1000e70d0,reinterpret_cast<void *>(fixture::Nearest)},{0x100050a14,leave},{0x1000502e4,reinterpret_cast<void *>(ParkExit)},{0x10004f65c,reinterpret_cast<void *>(CountExit)},{0x10005e060,reinterpret_cast<void *>(Ignore)}};arrival.Finish();pthread_jit_write_protect_np(0);Load(arrival,0x1000507f8,uint64_t(manager)-0xf08,0);sys_icache_invalidate(arrival.code,0x10000);pthread_jit_write_protect_np(1);
 Reset();F(cars[0],0x38)=60;Store(fires.Page(0x10058d660),now);Light();*reinterpret_cast<int16_t *>(cars[1]+0x6a)=137;cars[1][0x60]=2|0x18;cars[1][0x295]|=4;F(cars[1],0x38)=40;F(cars[1],0x1b4)=60;AssignCrew(cars[1]);sprays=0;imageBase=reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(arrival.Address(0x1000507e0))-0x507e0);
 Arrival(arrival.Address(0x1000507e0),cars[1]);reinterpret_cast<Fn>(truck.Address(0x10024f5c4))(cars[1]);Check(crewExits==0&&*reinterpret_cast<void **>(cars[1]+0x218)==crew[0]&&sprays==1,"actual native arrival retains firefighting driver and water until extinction");
 Check(game::api.VehicleFireService&&game::api.VehicleFireService(cars[1]),"active cannon service owns the crew over generic driver intimidation");
 F(cars[1],0x38)=55;sprays=0;const auto arrivalBase=imageBase;imageBase=static_cast<uint8_t *>(truck.Address(0x10024f5c4))-0x24f5c4;carfire::gCannon=reinterpret_cast<Fn>(truck.Address(0x10024f5c4));Entry<Fn>("_ZN11CAutomobile16FireTruckControlEv")(cars[1]);Check(sprays==1,"firefighting truck can continue suppressing a fire inside the old ten-metre dead zone");imageBase=arrivalBase;F(cars[1],0x38)=40;
 Extinguish()(*reinterpret_cast<uint8_t **>(cars[0]+0x280));Control()(cars[1]);Check(cars[1][0x19e]==8,"after extinction deferred service resumes native arrival cleanup");
 Store(arrival.Page(0x1003ba98c),int32_t(1));Arrival(arrival.Address(0x1000507e0),cars[1]);Check(cars[1][0x19e]==1&&!(cars[1][0x295]&4)&&Read<int32_t>(arrival.Page(0x1003ba98c))==0,"actual native cleanup releases road mission and duty count after suppression");
 Entry<Fn>("_ZN6CCarAI23TellOccupantsToLeaveCarEP8CVehicle")(cars[1]);Check(crewExits==1,"direct scripted crew exit remains authoritative");
 imageBase=nullptr;
 Code burning;burning.Add(0x1000e5df4,0x1000e62a4);burning.bindings={{0x10010c0b4,reinterpret_cast<void *>(NoVehicle)},{0x10010c0f4,reinterpret_cast<void *>(NoVehicle)},
  {0x10027d608,entries.at("_ZN8CVehicle13InflictDamageEP7CEntity11eWeaponTypef7CVector")},{0x100172f48,reinterpret_cast<void *>(Transform)},
  {0x100210fec,reinterpret_cast<void *>(Ignore)},{0x100208a10,reinterpret_cast<void *>(Ignore)},
  {0x100143f28,reinterpret_cast<void *>(Zero)},{0x10020445c,reinterpret_cast<void *>(Ignore)},{0x1000e62a4,entries.at("_ZN5CFire10ExtinguishEv")}};burning.Finish();
 alignas(8) uint8_t info[0x100]{};Store(burning.Page(0x1006b2050+150*8),info);carfire::gProcess=reinterpret_cast<Fn>(burning.Address(0x1000e5df4));carfire::gInflict=reinterpret_cast<Inflict>(code.Address(0x10027d608));
 Reset();carmechanics::sample.running=true;carmechanics::sample.electrical=.9f;carmechanics::sample.breach=.4f;Store(fires.Page(0x10058d660),now);*reinterpret_cast<void **>(cars[0])=vtable;F(cars[0],0x2a0)=200;
 for(int i=0;i<150;i++){now+=100;timeStep=5;Store(burning.Page(0x10058d660),now);Store(burning.Page(0x10058d674),timeStep);Control()(cars[0]);if(auto *fire=*reinterpret_cast<uint8_t **>(cars[0]+0x280))Process()(fire);}
 Check(carfire::Burning(cars[0],false)&&blasts==0&&F(cars[0],0x2a0)==0,"actual native fire processing and lethal damage compose without a five-second blast");
 printf("carfire native: %s\n",failures?"FAILED":"all passed");return failures?1:0;
}
