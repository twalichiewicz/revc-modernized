// Actual model-registration/streaming -> native CPlane -> flight/entry hooks.
#define main PlaneForceChecks
#include "test_planeflight.cpp"
#undef main
static void *JetSymbol(void *,const char *);
#include <cstdarg>
#include <string>
#include <vector>
static std::vector<std::string> boardingLogs;
static void BoardingLog(const char *format,...){char text[1024];va_list ap;va_start(ap,format);std::vsnprintf(text,sizeof text,format,ap);va_end(ap);boardingLogs.emplace_back(text);}
#define dlsym JetSymbol
#define Log BoardingLog
#include "../src/airportjets.cpp"
#undef Log
#undef dlsym
#include <map>
#include <string>

// The roster tests isolate native actor/model registration. The separate real
// asset fixture executes the actual geometry preparation and collision fitter.
namespace airportjetmodel {
int animations=0;float lastLeft=0,lastRight=0;std::array<jetwheels::WheelPose,3> lastWheels{};Pose lastPose{};
bool requireDonor=false;void *lastDonor=nullptr;
bool Install(){return true;}bool Prepare(void *,void *donor){lastDonor=donor;return !requireDonor||donor;}
void Animate(void *,const Pose &pose){++animations;lastPose=pose;lastWheels=pose.wheels;lastLeft=pose.leftFlap;lastRight=pose.rightFlap;}
void Animate(void *clump,const std::array<jetwheels::WheelPose,3> &wheels,float left,float right){Pose pose;pose.wheels=wheels;pose.leftFlap=left;pose.rightFlap=right;Animate(clump,pose);}
}
namespace jetwheels {
std::array<WheelPose,3> testPose{};
bool Snapshot(const uint8_t *,std::array<WheelPose,3> &out){out=testPose;return true;}
}
namespace {
alignas(16) uint8_t jetVehicles[14][0x6f0]{},buildings[7][0x78]{},source[0x60]{},dodoInfo[0x258]{},policeInfo[0x258]{},col[0x68]{},sphereBytes[40]{},boxBytes[56]{};
alignas(16) uint8_t modelStore[8+110*0x258]{},streamInfo[8000*32]{},buildingPool[32]{},vehiclePool[32]{};
uint8_t *buildingPoolPointer=buildingPool,*vehiclePoolPointer=vehiclePool,*models[6500]{};
uint8_t buildingFlags[7]{},vehicleFlags[14]{};uint32_t frameNumber=100;
int area=0,allocated=0,added=0,removed=0,warped=0,exited=0,requests=0,lastRequest=-1,lastFlags=0,physical=0,nativeControls=0,explosions=0;
int destroyed=0,preRendered=0,pedGeneration=5;uint32_t gameMs=1000;
alignas(16) uint8_t boardingAnims[5][0x58]{},boardingHierarchies[5][0x30]{};
uint8_t *boardingAssoc[173]{};uint32_t animClock=0;bool freezeBoardingAnimation=false;
float nativeSeatInitial=-1,nativeSeatReuse=-1;
std::vector<int> playedBoardingAnims;std::vector<CVector> boardingPositions;
void (*boardingBoundary)(const char *)=nullptr;
void (*boardingSeatWitness)(uint8_t *,uint8_t *)=nullptr;
bool mission=false,cutsceneRunning=false,button=false,horn=false,nativeHorn=false,blocked=false,sourceInWorld[7]{},recycleSource=false;
bool requireExplicitSphereIgnore=false,recycleDoorVehicle=false,sourceOccludesGround=false,groundStuck=false,groundMissing=false,groundRecycle=false,foreignGround=false;int groundCalls=0;float foreignGroundHeight=16.5f;
uint8_t *ignored=nullptr;int planeVtable=0;CVector mesh[28]{};int clumpMarker=0,policeMarker=0;
std::map<std::string,void *> chain;
int PedReference(uint8_t *ped){return ped==gPlayer?pedGeneration:-1;}
uint8_t *PedAtReference(int ref){return ref==pedGeneration?gPlayer:nullptr;}
uint8_t *Vehicle(int ref){const unsigned i=unsigned(ref)>>8;return i<14&&!(vehicleFlags[i]&128)&&vehicleFlags[i]==uint8_t(ref)?jetVehicles[i]:nullptr;}
void *AllocateJet(size_t size){CHECK(size==0x388);for(int i=0;i<14;++i)if(vehicleFlags[i]&128){vehicleFlags[i]=uint8_t(i+5);++allocated;return jetVehicles[i];}return nullptr;}
void ConstructJet(uint8_t *v,int model,uint8_t creator){
 CHECK(model>=240&&model<=249&&models[model]&&models[model][0x1d]==6);
 CHECK(*reinterpret_cast<int32_t *>(models[model]+0x4c)==4&&*reinterpret_cast<void **>(models[model]+0x30));
 std::memset(v,0,0x388);*reinterpret_cast<void **>(v)=&planeVtable;*reinterpret_cast<void **>(v+0x58)=&clumpMarker;
 *reinterpret_cast<int16_t *>(v+0x6a)=int16_t(model);*reinterpret_cast<int32_t *>(v+0x348)=4;v[0x294]=creator;v[0x295]=16;v[0x297]=64;
 *reinterpret_cast<uint32_t *>(v+0x60)=2|0x400000|(9<<3);*reinterpret_cast<float *>(v+0x2a0)=1000;
 V(v,8)={1,0,0};V(v,0x18)={0,1,0};V(v,0x28)={0,0,1};
 ++*reinterpret_cast<uint16_t *>(models[model]+0x2c); // native CEntity::CreateRwObject AddRef after proper clump creation
 if(recycleSource){recycleSource=false;++buildingFlags[0];*reinterpret_cast<int16_t *>(buildings[0]+0x6a)=638;V(buildings[0],0x38).x+=50;}
}
void DestroyJet(uint8_t *v){++destroyed;const int model=game::EntityModel(v);if(model>=0&&model<6500&&models[model]){auto &refs=*reinterpret_cast<uint16_t *>(models[model]+0x2c);CHECK(refs>0);if(refs)--refs;}const ptrdiff_t i=(v-jetVehicles[0])/0x6f0;CHECK(i>=0&&i<14);if(i>=0&&i<14)vehicleFlags[i]|=128;}
void AddWorld(uint8_t *v){++added;for(int i=0;i<7;++i)if(v==buildings[i])sourceInWorld[i]=true;}
void RemoveWorld(uint8_t *v){++removed;for(int i=0;i<7;++i)if(v==buildings[i])sourceInWorld[i]=false;}
void PhysicalJet(uint8_t *v){++physical;V(v,0x84).z-=.008f*gStep;*reinterpret_cast<float *>(v+0x13c)=0;}
void OriginalControl(uint8_t *){++nativeControls;}
void OriginalPreRender(uint8_t *){++preRendered;}
void OriginalSeat(uint8_t *){}
void OriginalEnter(uint8_t *,uint8_t *,uint32_t){}
void OriginalExit(uint8_t *,uint8_t *,uint32_t){}
void DamageJet(uint8_t *v,uint8_t *by,int,float amount,CVector){
 // Native InflictDamage0x27d624 requires bCanBeDamaged(+297bit6).
 // A PLAYER actor bypasses the nonplayer aggressor restriction; collision
 // proof and wreck guards still apply to the collision-type witness39.
 if(!(v[0x297]&64)||game::Wrecked(v)||(game::EntityFlags(v)&(1u<<27)))return;
 if((v[0x60]>>3)!=0&&(game::EntityFlags(v)&(1u<<29))&&by!=gPlayer&&by!=game::PedVehicle(gPlayer))return;
 *reinterpret_cast<float *>(v+0x2a0)=std::max(0.0f,game::VehicleHealth(v)-amount);
}
void BoundDamageJet(uint8_t *v,uint8_t *by,int weapon,float amount,CVector at){
 for(const auto &entry:chain)if(entry.first.find("InflictDamage")!=std::string::npos){reinterpret_cast<void (*)(uint8_t *,uint8_t *,int,float,CVector)>(entry.second)(v,by,weapon,amount,at);return;}
 DamageJet(v,by,weapon,amount,at);
}
uint8_t *BlendBoarding(uint8_t *,int,int,float);
void WarpJet(uint8_t *p,uint8_t *v){
 ++warped;if(boardingSeatWitness)boardingSeatWitness(p,v);p[0x4c0]=1;*reinterpret_cast<uint8_t **>(p+0x4b8)=v;
 // Original Warp binds the driver and AddInCarAnims only for objective18.
 if(game::PedObjective(p)!=18)return;
 *reinterpret_cast<uint8_t **>(v+0x218)=p;*reinterpret_cast<int32_t *>(p+0x2fc)=50;v[0x60]=2;v[0x295]|=16;
 auto *sit=BlendBoarding(game::EntityClump(p),0,102,100);*reinterpret_cast<uint8_t **>(p+0x2a0)=sit;
 for(auto *a:boardingAssoc)if(a&&!(*reinterpret_cast<uint16_t *>(a+0x3e)&0x10))*reinterpret_cast<uint16_t *>(a+0x3e)&=~uint16_t(1);
}
void OutJet(void *,void *p){++exited;auto *ped=static_cast<uint8_t *>(p);auto *v=game::PedVehicle(ped);*reinterpret_cast<uint8_t **>(v+0x218)=nullptr;ped[0x4c0]=0;*reinterpret_cast<int32_t *>(ped+0x2fc)=game::kPedIdle;v[0x60]=2|(4<<3);}
void TeleportJet(uint8_t *p,CVector at){V(p,0x38)=at;if(p==gPlayer)boardingPositions.push_back(at);if(boardingBoundary)boardingBoundary("teleport");}
uint8_t *BoardingAssociation(uint8_t *,uint32_t anim){if(boardingBoundary)boardingBoundary("association");return anim<173?boardingAssoc[anim]:nullptr;}
uint8_t *BlendBoarding(uint8_t *clump,int group,int anim,float blend){
 CHECK(clump==game::EntityClump(gPlayer));CHECK(((group==30||group==52)&&(anim==0||anim==3))||(group==0&&(anim==76||anim==80||anim==102)));
 const int slot=anim==0?0:anim==76?1:anim==80?2:anim==3?4:3;
 const char *name=anim==0?(group==52?"WALK_back":"WALK_player"):anim==76?"CAR_open_LHS":anim==80?"CAR_getin_LHS":anim==3?"IDLE_stance":"CAR_sit";
 auto *a=boardingAnims[slot],*h=boardingHierarchies[slot];
 const uint16_t flags=anim==0?0x162:anim==102?4:anim==3?2:0x18;
 const bool reuse=boardingAssoc[anim]==a&&!std::strcmp(reinterpret_cast<char *>(h),name);
 if(!reuse){
  bool oldFullBody=false;
  for(auto *old:boardingAssoc)if(old&&!(*reinterpret_cast<uint16_t *>(old+0x3e)&0x10))oldFullBody=true;
  for(auto *old:boardingAssoc)if(old&&(((*reinterpret_cast<uint16_t *>(old+0x3e)^flags)&0x10)==0)){
   *reinterpret_cast<float *>(old+0x2c)=-blend*std::max(*reinterpret_cast<float *>(old+0x28),.25f);*reinterpret_cast<uint16_t *>(old+0x3e)|=4;
  }
  std::memset(a,0,0x58);std::memset(h,0,0x30);std::strcpy(reinterpret_cast<char *>(h),name);
  *reinterpret_cast<float *>(h+0x24)=anim==0?(group==52?1.0f:1.5333333f):anim==76?.73333335f:anim==80?.6666667f:anim==3?2.5f:.033333335f;
  *reinterpret_cast<uint8_t **>(a+0x20)=h;*reinterpret_cast<float *>(a+0x34)=1;
  const float initial=anim==102&&nativeSeatInitial>=0?nativeSeatInitial:((flags&0x10)||oldFullBody?0:1);
  *reinterpret_cast<float *>(a+0x28)=anim==102&&nativeSeatReuse>=0?nativeSeatReuse:initial;
  *reinterpret_cast<float *>(a+0x2c)=initial==1?0:blend;
  *reinterpret_cast<uint16_t *>(a+0x3c)=uint16_t(anim);*reinterpret_cast<uint16_t *>(a+0x3e)=flags|1;
  boardingAssoc[anim]=a;
 }
 if(reuse||(anim==102&&nativeSeatReuse>=0))*reinterpret_cast<float *>(a+0x2c)=(1-*reinterpret_cast<float *>(a+0x28))*blend;
 playedBoardingAnims.push_back(anim);if(boardingBoundary)boardingBoundary("blend");return a;
}
[[maybe_unused]] void JetFrame(uint32_t now){
 const float dt=animClock&&now>=animClock?std::min(now-animClock,uint32_t(100))*.001f:0;animClock=now;
 if(!freezeBoardingAnimation)for(auto *&a:boardingAssoc)if(a){auto &weight=*reinterpret_cast<float *>(a+0x28);const float delta=*reinterpret_cast<float *>(a+0x2c);weight=std::clamp(weight+dt*delta,0.0f,1.0f);if(weight==0&&delta<0){a=nullptr;continue;}auto *h=*reinterpret_cast<uint8_t **>(a+0x20);auto &t=*reinterpret_cast<float *>(a+0x30);if(*reinterpret_cast<uint16_t *>(a+0x3e)&1)t+=dt**reinterpret_cast<float *>(a+0x34);const float length=*reinterpret_cast<float *>(h+0x24);if(t>=length)t=(*reinterpret_cast<uint16_t *>(a+0x3e)&2)?t-length:length;}
 airportjets::Frame(now);
}
void BoardingTime(uint8_t *assoc,float time){*reinterpret_cast<float *>(assoc+0x30)=time;if(boardingBoundary)boardingBoundary("time");}
void UpdateJet(void *){}
bool Mission(){return mission;}bool ExitButton(uint8_t *){return button;}bool HornButton(uint8_t *pad){
 if(!nativeHorn)return horn;if(PadField(pad,0xf0))return false;
 // Actual macVC GetHorn0x108550: mode table1003294e0 contains these
 // uint64 byte offsets. Keyboard merges VEHICLE_HORN into normal+24.
 constexpr int fields[4]={0x24,0x8,0xc,0x24};
 if(!game::api.padAffectedByController||*game::api.padAffectedByController!=1)return PadField(pad,0x24)!=0;
 const unsigned mode=unsigned(PadField(pad,0xec));return mode<4&&PadField(pad,fields[mode])!=0;
}
void ObjectiveJet(uint8_t *p,int n,void *){*reinterpret_cast<int32_t *>(p+0x19c)=n;if(boardingBoundary)boardingBoundary("objective");}
void *AddModel(int id){auto &count=*reinterpret_cast<int32_t *>(modelStore);CHECK(count<110&&!models[id]);uint8_t *mi=modelStore+8+size_t(count++)*0x258;std::memset(mi,0,0x258);mi[0x1d]=6;*reinterpret_cast<int32_t *>(mi+0x38)=-1;*reinterpret_cast<int32_t *>(mi+0x250)=-1;models[id]=mi;return mi;}
void AddReference(uint8_t *mi){++*reinterpret_cast<uint16_t *>(mi+0x2c);}
void RemoveReference(uint8_t *mi){CHECK(*reinterpret_cast<uint16_t *>(mi+0x2c)>0);--*reinterpret_cast<uint16_t *>(mi+0x2c);}
bool CdPosition(uint8_t *info,uint32_t &position,uint32_t &size){std::memcpy(&position,info+0x14,4);std::memcpy(&size,info+0x18,4);return size!=0;}
void SetCdPosition(uint8_t *info,uint32_t position,uint32_t size){std::memcpy(info+0x14,&position,4);std::memcpy(info+0x18,&size,4);}
void RequestJet(int id,int flags){auto *info=streamInfo+size_t(id)*32;const uint8_t state=info[0x10];if(id<6500&&state==0&&models[id])RequestJet(6500+*reinterpret_cast<int16_t *>(models[id]+0x2e),flags);++requests;lastRequest=id;lastFlags=flags;if(state==0){info[0x10]=2;info[0x11]=uint8_t(flags);}else info[0x11]|=uint8_t(flags);}
void Deletable(int id){streamInfo[size_t(id)*32+0x11]&=~uint8_t(1);}
bool FloorJet(const CVector &from,float to,uint8_t *point,uint8_t *&entity,bool buildingsOnly,bool cars,bool peds,bool objects,bool dummies,bool,void *){
 CHECK(buildingsOnly&&!cars&&!peds&&!objects&&!dummies);++groundCalls;entity=nullptr;if(recycleDoorVehicle){recycleDoorVehicle=false;++vehicleFlags[0];}
 // Native ProcessVerticalLineSectorList does NOT consult pIgnoreEntity. It
 // returns the highest collision from every enabled building in this ray.
 float height=-1e9f,normal=1;auto candidate=[&](float z,uint8_t *owner,float n){if(from.z>=z&&to<=z&&z>height){height=z;entity=owner;normal=n;}};
 if(sourceOccludesGround&&sourceInWorld[0]){candidate(groundStuck?from.z:17.733f,buildings[0],1);candidate(15.9f,buildings[0],-1);}
 if(foreignGround)candidate(foreignGroundHeight,buildings[1],1);
 if(!groundMissing)candidate(13.8f,nullptr,1);
 if(groundRecycle){groundRecycle=false;++buildingFlags[0];*reinterpret_cast<int16_t *>(buildings[0]+0x6a)=638;}
 if(height<-1e8f)return false;V(point,0)={from.x,from.y,height};V(point,0x10)={0,0,normal};return true;
}
uint8_t *SphereJet(CVector,float,uint8_t *ignore,bool,bool,bool,bool,bool,bool){return blocked?buildings[1]:requireExplicitSphereIgnore&&ignore!=ignored?ignored:nullptr;}
bool SightJet(const CVector &,const CVector &,bool,bool,bool,bool,bool,bool,bool){return !blocked;}
void *EachJet(void *,void *(*cb)(void *,void *),void *data){cb(&clumpMarker,data);return &clumpMarker;}
void *GeometryJet(const void *){return mesh;}int NumVerticesJet(const void *){return 28;}
void *AtomicFrameJet(const void *){return &clumpMarker;}
const float *LTMJet(void *){static const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};return identity;}
const char *FrameNameJet(void *){return "jet_hull";}
void *MorphJet(const void *,int){return mesh;}const CVector *VerticesJet(const void *){return mesh;}
int TriangleCountJet(const void *){return 2;}
const airportjets::Triangle *TrianglesJet(const void *){static const airportjets::Triangle triangles[2]={{{0,6,21},0},{{6,27,21},0}};return triangles;}
void KillJet(uint8_t *){}bool ExplosionJet(uint8_t *,uint8_t *,int,const CVector &,uint32_t,bool){++explosions;return true;}
void ShutdownModels(){}
void *OriginalJet(const char *name,void *hook){
 void *old=chain[name];chain[name]=hook;if(old)return old;
 if(std::strstr(name,"ProcessControl"))return reinterpret_cast<void *>(OriginalControl);
 if(std::strstr(name,"PreRender"))return reinterpret_cast<void *>(OriginalPreRender);
 if(std::strstr(name,"SetPedPositionInCar"))return reinterpret_cast<void *>(OriginalSeat);
 if(std::strstr(name,"SetEnterCar"))return reinterpret_cast<void *>(OriginalEnter);
 if(std::strstr(name,"SetExitCar"))return reinterpret_cast<void *>(OriginalExit);
 if(std::strstr(name,"InflictDamage"))return reinterpret_cast<void *>(DamageJet);
 if(std::strstr(name,"ShutDown")||std::strstr(name,"ClearForRestart"))return reinterpret_cast<void *>(ShutdownModels);
 return nullptr;
}
template<class F> F JetHook(const char *name){for(auto &entry:chain)if(entry.first.find(name)!=std::string::npos)return reinterpret_cast<F>(entry.second);return nullptr;}
[[maybe_unused]] void Setup(){
 std::fill(boardingAssoc,boardingAssoc+173,nullptr);animClock=0;freezeBoardingAnimation=false;nativeSeatInitial=nativeSeatReuse=-1;playedBoardingAnims.clear();boardingPositions.clear();boardingLogs.clear();boardingBoundary=nullptr;boardingSeatWitness=nullptr;
 Reset();recycleDoorVehicle=false;sourceOccludesGround=groundStuck=groundMissing=groundRecycle=foreignGround=false;groundCalls=0;gExtraSymbol=[](const char *name){return JetSymbol(nullptr,name);};gChainOriginal=OriginalJet;
 std::fill(vehicleFlags,vehicleFlags+14,128);std::fill(buildingFlags,buildingFlags+7,5);std::fill(sourceInWorld,sourceInWorld+7,true);
 std::memset(models,0,sizeof models);std::memset(streamInfo,0,sizeof streamInfo);std::memset(modelStore,0,sizeof modelStore);*reinterpret_cast<int32_t *>(modelStore)=107;
 *reinterpret_cast<uint8_t **>(buildingPool)=buildings[0];*reinterpret_cast<uint8_t **>(buildingPool+8)=buildingFlags;*reinterpret_cast<int32_t *>(buildingPool+16)=7;
 *reinterpret_cast<uint8_t **>(vehiclePool)=jetVehicles[0];*reinterpret_cast<uint8_t **>(vehiclePool+8)=vehicleFlags;*reinterpret_cast<int32_t *>(vehiclePool+16)=14;
 models[636]=source;source[0x1d]=1;*reinterpret_cast<int16_t *>(source+0x2e)=12;*reinterpret_cast<uint8_t **>(source+0x20)=col;
 models[227]=policeInfo;policeInfo[0x1d]=6;std::strcpy(reinterpret_cast<char *>(policeInfo+8),"polmav");*reinterpret_cast<int16_t *>(policeInfo+0x2e)=13;*reinterpret_cast<void **>(policeInfo+0x30)=&policeMarker;*reinterpret_cast<uint16_t *>(policeInfo+0x2c)=0;streamInfo[227*32+0x10]=1;
 models[181]=dodoInfo;dodoInfo[0x1d]=6;*reinterpret_cast<int32_t *>(dodoInfo+0x4c)=4;*reinterpret_cast<int16_t *>(dodoInfo+0x56)=11;
 V(col,0)={0,0,0};*reinterpret_cast<float *>(col+12)=14;V(col,0x10)={-10,-9,-3};V(col,0x1c)={10,9,3};
 *reinterpret_cast<int16_t *>(col+0x28)=2;*reinterpret_cast<uint8_t **>(col+0x38)=sphereBytes;V(sphereBytes,0)={-1,-1,-1.65f};V(sphereBytes,20)={1,-1,-1.65f};*reinterpret_cast<float *>(sphereBytes+12)=*reinterpret_cast<float *>(sphereBytes+32)=.666f;
 *reinterpret_cast<int16_t *>(col+0x2a)=2;*reinterpret_cast<uint8_t **>(col+0x48)=boxBytes;
 SetCdPosition(streamInfo+636*32,2000,64);
 for(int i=0;i<7;++i){*reinterpret_cast<int16_t *>(buildings[i]+0x6a)=636;*reinterpret_cast<uint32_t *>(buildings[i]+0x60)=1|0x100;V(buildings[i],8)={1,0,0};V(buildings[i],0x18)={0,1,0};V(buildings[i],0x28)={0,0,1};V(buildings[i],0x38)={-1765.0f+500*i,-1271,15.997f};}
 for(int i=0;i<28;++i)mesh[i]={float((i%7)-3)*3,float((i/7)-2)*3,float(i%3)*.3f};
 gPlayer[0x4c0]=0;*reinterpret_cast<uint32_t *>(gPlayer+0x60)=3|game::kEntityUsesCollision;*reinterpret_cast<float *>(gPlayer+0x44c)=100;*reinterpret_cast<int32_t *>(gPlayer+0x2fc)=game::kPedIdle;V(gPlayer,0x38)={-1767,-1271,14.84f};
 game::api.modelInfoPtrs=models;game::api.PoolsGetVehicle=Vehicle;game::api.PoolsGetPed=PedAtReference;game::api.RequestModel=RequestJet;game::api.ProcessVerticalLine=FloorJet;game::api.TestSphereAgainstWorld=SphereJet;game::api.GetIsLineOfSightClear=SightJet;game::api.SetObjective=ObjectiveJet;game::api.ignoreEntity=&ignored;
 game::api.timeMs=&gameMs;
 *reinterpret_cast<void **>(gPlayer+0x58)=&clumpMarker;game::api.BlendAnimation=BlendBoarding;game::api.GetAssociation=BoardingAssociation;
}
}
static void *JetSymbol(void *,const char *name){
#define B(n,f) if(std::strstr(name,n))return reinterpret_cast<void *>(f)
 B("GetPedRef",PedReference);B("CVehiclenwEm",AllocateJet);B("CPlaneC1Eih",ConstructJet);B("CPlaneD0Ev",DestroyJet);B("CWorld3Add",AddWorld);B("CWorld6Remove",RemoveWorld);B("CPhysical14ProcessControl",PhysicalJet);
 B("WarpPedIntoCar",WarpJet);B("PedSetOutCarCB",OutJet);B("CPed8Teleport",TeleportJet);B("CMatrix8UpdateRW",UpdateJet);B("CEntity13UpdateRwFrame",UpdateJet);
 B("CAnimBlendAssociation14SetCurrentTime",BoardingTime);
 B("AddVehicleModel",AddModel);B("CBaseModelInfo6AddRef",AddReference);B("CBaseModelInfo9RemoveRef",RemoveReference);B("GetCdPosnAndSize",CdPosition);B("SetCdPosnAndSize",SetCdPosition);B("SetModelIsDeletable",Deletable);B("SetModelTxdIsDeletable",Deletable);
 B("IsPlayerOnAMission",Mission);B("ExitVehicleJustDown",ExitButton);B("GetHorn",HornButton);B("InflictDamage",BoundDamageJet);B("KillPedsInVehicle",KillJet);B("AddExplosion",ExplosionJet);
 B("RpClumpForAllAtomics",EachJet);B("RpAtomicGetGeometry",GeometryJet);B("RpGeometryGetNumVertices",NumVerticesJet);B("RpGeometryGetMorphTarget",MorphJet);B("RpMorphTargetGetVertices",VerticesJet);
 B("RpGeometryGetNumTriangles",TriangleCountJet);B("RpGeometryGetTriangles",TrianglesJet);
 B("RpAtomicGetFrame",AtomicFrameJet);B("RwFrameGetLTM",LTMJet);B("GetFrameNodeName",FrameNameJet);
 B("GetSteeringLeftRight",Roll);B("GetSteeringUpDown",Pitch);B("GetCarGunUpDown",Gun);B("GetCarGunLeftRight",GunYaw);B("GetLookLeft",Left);B("GetLookRight",Right);
#undef B
 if(std::strstr(name,"ms_pBuildingPool"))return &buildingPoolPointer;if(std::strstr(name,"ms_pVehiclePool"))return &vehiclePoolPointer;
 if(std::strstr(name,"ms_aInfoForModel"))return streamInfo;if(std::strstr(name,"ms_vehicleModelStore"))return modelStore;
 if(std::strstr(name,"currArea"))return &area;if(std::strstr(name,"ms_running"))return &cutsceneRunning;if(std::strstr(name,"m_FrameCounter"))return &frameNumber;
 return nullptr;
}
#ifndef AIRPORTJETS_NO_MAIN
int main(){
 Setup();CHECK(planeflight::Install());CHECK(airportjets::Install());if(failures)return 1;
 *reinterpret_cast<int32_t *>(modelStore)=110;JetFrame(900);CHECK(lastRequest==-1&&!sourceInWorld[0]);*reinterpret_cast<int32_t *>(modelStore)=107;
 JetFrame(1000);CHECK(lastRequest>=240&&lastRequest<=249&&(lastFlags&1));CHECK(allocated==0&&!sourceInWorld[0]);
 if(lastRequest<240)return 1;
 const int id=lastRequest;CHECK(models[id]&&models[id][0x1d]==6&&models[636]==source&&source[0x1d]==1);
 CHECK(*reinterpret_cast<uint16_t *>(source+0x2c)>0);uint32_t pos=0,size=0;CHECK(CdPosition(streamInfo+id*32,pos,size)&&pos==2000&&size==64);
 *reinterpret_cast<void **>(models[id]+0x30)=&clumpMarker;streamInfo[id*32+0x10]=1;
 blocked=true;JetFrame(1050);CHECK(allocated==0&&!sourceInWorld[0]);blocked=false;
 recycleSource=true;JetFrame(1100);CHECK(!sourceInWorld[0]&&destroyed==1&&!airportjets::Owns(jetVehicles[0]));
 // Source replacement belongs to native world code until the synchronous
 // reset, when a newly initialized placement can be claimed afresh.
 if(auto reset=JetHook<void (*)()>("ClearForRestart"))reset();sourceInWorld[0]=true;
 *reinterpret_cast<int16_t *>(buildings[0]+0x6a)=636;V(buildings[0],0x38).x=-1765;
 JetFrame(1200);CHECK(allocated>0&&!sourceInWorld[0]);if(allocated==0)return 1;
 CHECK(airportjets::Owns(jetVehicles[0])&&planeflight::Owns(jetVehicles[0]));CHECK(!airportjets::Owns(gBoat));
 CHECK(airportjets::GearDown(jetVehicles[0])==1&&airportjets::GearDown(gBoat)==0);
 ++vehicleFlags[0];CHECK(airportjets::GearDown(jetVehicles[0])==0);--vehicleFlags[0];
 CHECK(airportjets::UsesRegisteredModel(jetVehicles[0])&&!airportjets::UsesRegisteredModel(gBoat));
 jetVehicles[0][0x294]=2;CHECK(!airportjets::Owns(jetVehicles[0])&&airportjets::UsesRegisteredModel(jetVehicles[0]));jetVehicles[0][0x294]=1;
 CHECK(*reinterpret_cast<int32_t *>(jetVehicles[0]+0x348)==4&&game::EntityModel(jetVehicles[0])==id);
 CHECK(!(jetVehicles[0][0x295]&16)); // parked constructors do not leave seven audible engines running
 CHECK(*reinterpret_cast<uint16_t *>(models[id]+0x2c)==1&&!(streamInfo[id*32+0x11]&1)); // real actor refs replace the async pin
 auto control=JetHook<void (*)(uint8_t *)>("CPlane14ProcessControl");CHECK(control);if(control){
  static int physicalClaims=0;game::api.AirframeFlightUsed=+[](uint8_t *){++physicalClaims;};
  control(jetVehicles[0]);CHECK(physical>0&&nativeControls==0);
  CHECK(physicalClaims==1); // Empty parked jets still own gravity/collision physics.
  game::api.AirframeFlightUsed=nullptr;
 }
 // Native entry must set driver/status/engine, then the real plane forces own it.
 auto enter=JetHook<void (*)(uint8_t *,uint8_t *,uint32_t)>("SetEnterCar");CHECK(enter);
 V(gPlayer,0x38)=V(jetVehicles[0],0x38);V(gPlayer,0x38).x-=1.6f;V(gPlayer,0x38).y+=4.5f;V(gPlayer,0x38).z=14.84f;
 if(enter){gameMs=1300;enter(gPlayer,jetVehicles[0],0);CHECK(warped==0&&!game::PedInVehicle(gPlayer));
  // A real native-shaped request must leave time for the physical door/steps
  // to open before WarpPedIntoCar can take ownership of the player.
  for(gameMs=1350;gameMs<=15000&&!game::PedInVehicle(gPlayer);gameMs+=50)JetFrame(gameMs);
  CHECK(warped==1&&game::PedVehicle(gPlayer)==jetVehicles[0]);}
 if(warped)CHECK(jetVehicles[0][0x295]&16); // native Warp starts the real engine
 auto render=JetHook<void (*)(uint8_t *)>("CPlane9PreRender");CHECK(render);
 if(render){const int before=airportjetmodel::animations;render(gBoat);CHECK(airportjetmodel::animations==before&&preRendered>0);
  jetwheels::testPose[0]={.7f,.05f,.2f,true};gPitch=gRoll=64;render(jetVehicles[0]);
  CHECK(airportjetmodel::animations==before+1&&std::fabs(airportjetmodel::lastWheels[0].spin-.7f)<.0001f);
  CHECK(std::fabs(airportjetmodel::lastLeft-.275f)<.0001f&&std::fabs(airportjetmodel::lastRight+.025f)<.0001f);gPitch=gRoll=0;
 }
 if(warped){V(jetVehicles[0],0x84)={0,1.2f,0};gMoves=gTurns=0;CHECK(planeflight::Apply(jetVehicles[0],5)&&gMoves>0&&gTurns>0);}
 if(warped&&control){
  jetVehicles[0][0x297]=0;*reinterpret_cast<float *>(jetVehicles[0]+0x13c)=800;control(jetVehicles[0]);CHECK(game::VehicleHealth(jetVehicles[0])==1000);
  jetVehicles[0][0x297]=64;*reinterpret_cast<float *>(jetVehicles[0]+0x13c)=800;control(jetVehicles[0]);const float once=game::VehicleHealth(jetVehicles[0]);CHECK(once<1000&&once>250);control(jetVehicles[0]);CHECK(game::VehicleHealth(jetVehicles[0])==once);
 }
 auto leave=JetHook<void (*)(uint8_t *,uint8_t *,uint32_t)>("SetExitCar");CHECK(leave);
 if(leave){V(jetVehicles[0],0x84)={};gameMs+=500;button=false;requireExplicitSphereIgnore=true;leave(gPlayer,jetVehicles[0],0);
  CHECK(exited==0&&game::PedInVehicle(gPlayer));
  const uint32_t leftAt=gameMs;for(gameMs+=50;gameMs<=leftAt+1100;gameMs+=50)JetFrame(gameMs);
  CHECK(exited==1&&warped==1&&!game::PedInVehicle(gPlayer));
  gameMs+=500;button=true;JetFrame(gameMs);button=false;
  CHECK(warped==1);const uint32_t boardAt=gameMs;for(gameMs+=50;gameMs<=boardAt+15000&&!game::PedInVehicle(gPlayer);gameMs+=50)JetFrame(gameMs);
  CHECK(warped==2&&game::PedInVehicle(gPlayer));
 }
 auto damage=JetHook<void (*)(uint8_t *,uint8_t *,int,float,CVector)>("InflictDamage");CHECK(damage);
 if(damage){damage(jetVehicles[0],gPlayer,24,2000,{});CHECK(explosions==1&&game::Wrecked(jetVehicles[0]));damage(jetVehicles[0],gPlayer,24,2000,{});CHECK(explosions==1);}
 auto restart=JetHook<void (*)()>("ClearForRestart");CHECK(restart);
 if(restart){gPlayer[0x4c0]=0;*reinterpret_cast<uint8_t **>(jetVehicles[0]+0x218)=nullptr;restart();CHECK(sourceInWorld[0]&&!airportjets::Owns(jetVehicles[0])&&(vehicleFlags[0]&128));}
 CHECK(*reinterpret_cast<uint16_t *>(source+0x2c)==1); // private source/COL pin survives restart, independent of actor refs
 auto shutdown=JetHook<void (*)()>("CModelInfo8ShutDown");CHECK(shutdown);if(shutdown){shutdown();CHECK(*reinterpret_cast<uint16_t *>(source+0x2c)==0);}
 // Same persistent streaming metadata survives native restart. Release just
 // our new keep bits, preserving an existing script bit and foreign model240.
 Setup();models[240]=dodoInfo;streamInfo[(6500+12)*32+0x11]=2;lastRequest=-1;
 JetFrame(3000);CHECK(lastRequest==241&&models[240]==dodoInfo);
 CHECK((streamInfo[241*32+0x11]&1)&&(streamInfo[(6500+12)*32+0x11]&3)==3);
 if(restart){restart();CHECK(!(streamInfo[241*32+0x11]&1)&&streamInfo[(6500+12)*32+0x11]==2);}
 if(shutdown)shutdown();
 Setup();const int beforeSeven=allocated;
 for(int i=0;i<7;++i)V(buildings[i],0x38).x=-1765.0f+30*(i-3);
 JetFrame(4000);const int sevenModel=lastRequest;CHECK(sevenModel>=240&&sevenModel<=249);
 if(sevenModel>=240&&sevenModel<=249){*reinterpret_cast<void **>(models[sevenModel]+0x30)=&clumpMarker;streamInfo[sevenModel*32+0x10]=1;JetFrame(4100);
  CHECK(allocated-beforeSeven==7);for(int i=0;i<7;++i)CHECK(!sourceInWorld[i]&&airportjets::Owns(jetVehicles[i]));
  if(restart){restart();for(int i=0;i<7;++i)CHECK(sourceInWorld[i]&&!airportjets::Owns(jetVehicles[i]));}
 }if(shutdown)shutdown();
 // Real source227 streaming must finish before a template can reference its
 // material/texture; pending keep bits cancel without touching script pins.
 Setup();*reinterpret_cast<uint16_t *>(policeInfo+0x2c)=3;airportjetmodel::requireDonor=true;streamInfo[227*32+0x10]=0;*reinterpret_cast<void **>(policeInfo+0x30)=nullptr;
 streamInfo[(6500+13)*32+0x11]=2;JetFrame(5000);const int donorJetModel=lastRequest;
 CHECK(donorJetModel>=240&&donorJetModel<=249);
 if(donorJetModel>=240&&donorJetModel<=249){*reinterpret_cast<void **>(models[donorJetModel]+0x30)=&clumpMarker;streamInfo[donorJetModel*32+0x10]=1;
  const int before=allocated;JetFrame(5100);CHECK(allocated==before&&lastRequest==227);
  CHECK((streamInfo[227*32+0x11]&1)&&(streamInfo[(6500+13)*32+0x11]&3)==3);
  gConfig.planes=false;JetFrame(5200);CHECK(!(streamInfo[227*32+0x11]&1)&&streamInfo[(6500+13)*32+0x11]==2);gConfig.planes=true;
  JetFrame(5300);CHECK(streamInfo[227*32+0x11]&1);
  const CVector originalPlayer=V(gPlayer,0x38);auto findPlayer=game::api.FindPlayerPed;
  for(int cancellation=0;cancellation<7;++cancellation){
   switch(cancellation){case 0:area=1;break;case 1:mission=true;break;case 2:cutsceneRunning=true;break;case 3:*reinterpret_cast<float *>(gPlayer+0x44c)=0;break;case 4:gPlayer[0x4c0]=1;break;case 5:V(gPlayer,0x38).x=10000;break;case 6:game::api.FindPlayerPed=[]()->uint8_t *{return nullptr;};break;}
   JetFrame(5400+200*cancellation);
   // Approaching in a car now keeps both donor/model requests alive.
   if(cancellation==4)CHECK((streamInfo[227*32+0x11]&1)&&(streamInfo[(6500+13)*32+0x11]&3)==3&&allocated==before);
   else CHECK(!(streamInfo[227*32+0x11]&1)&&streamInfo[(6500+13)*32+0x11]==2&&allocated==before);
   area=0;mission=cutsceneRunning=false;*reinterpret_cast<float *>(gPlayer+0x44c)=100;gPlayer[0x4c0]=0;V(gPlayer,0x38)=originalPlayer;game::api.FindPlayerPed=findPlayer;
   JetFrame(5500+200*cancellation);CHECK(streamInfo[227*32+0x11]&1);
  }
  streamInfo[227*32+0x10]=1;*reinterpret_cast<void **>(policeInfo+0x30)=&policeMarker;JetFrame(7000);
  CHECK(allocated>before&&airportjetmodel::lastDonor==&policeMarker&&*reinterpret_cast<uint16_t *>(policeInfo+0x2c)==4);
  CHECK(!(streamInfo[227*32+0x11]&1)&&streamInfo[(6500+13)*32+0x11]==2);
  if(restart)restart();CHECK(*reinterpret_cast<uint16_t *>(policeInfo+0x2c)==4); // prepared template still owns source/TXD residency
 }if(shutdown)shutdown();CHECK(*reinterpret_cast<uint16_t *>(policeInfo+0x2c)==3);
 // Systems run against actual model/pool generations and hooked native
 // boarding/control consumers. These assertions catch instant warps, frozen
 // gear/door poses, control mismatches and delayed actions after cancellation.
 auto spawnSystems=[&](uint32_t start){Setup();mission=cutsceneRunning=button=horn=nativeHorn=blocked=false;requireExplicitSphereIgnore=false;airportjetmodel::requireDonor=false;
  gameMs=start;JetFrame(gameMs);const int model=lastRequest;CHECK(model>=240&&model<=249);
  if(model<240||model>249)return false;*reinterpret_cast<void **>(models[model]+0x30)=&clumpMarker;streamInfo[model*32+0x10]=1;
  gameMs+=100;JetFrame(gameMs);return airportjets::Owns(jetVehicles[0]);};
 auto advance=[&](int millis){const uint32_t end=gameMs+millis;while(gameMs<end){gameMs+=50;JetFrame(gameMs);}};
 auto pose=[&](){render(jetVehicles[0]);CHECK(airportjets::GearDown(jetVehicles[0])==airportjetmodel::lastPose.gearDown);return airportjetmodel::lastPose;};
 if(spawnSystems(20000)){
  auto *v=jetVehicles[0];*reinterpret_cast<int32_t *>(gPlayer+0x19c)=18;WarpJet(gPlayer,v);V(v,0x38).z=60;V(v,0x84)={0,1.2f,.1f};
  CHECK(pose().gearDown==1);advance(500);CHECK(pose().gearDown<1&&pose().gearDown>.5f);
  advance(2500);CHECK(pose().gearDown==0);
  horn=true;advance(100);const float began=pose().gearDown;CHECK(began>0&&began<.1f);
  advance(3000);CHECK(pose().gearDown==1); // held horn toggles only once
  horn=false;advance(50);horn=true;advance(3000);CHECK(pose().gearDown==0);
  V(v,0x38).z=22;V(v,0x84).z=-.1f;advance(1000);CHECK(pose().gearDown==0); // manual command owns this approach
  // Ground safety overrides a retract command and clears its flight lease.
  horn=false;V(v,0x38).z=16.074784f;V(v,0x84)={};advance(3000);CHECK(pose().gearDown==1);
  horn=true;advance(500);CHECK(pose().gearDown==1);horn=false;
  // A new takeoff resumes automatic gear; descent near real ground extends.
  V(v,0x38).z=50;V(v,0x84)={0,1.2f,.1f};advance(3000);CHECK(pose().gearDown==0);
  V(v,0x38).z=25;V(v,0x84).z=-.1f;advance(3000);CHECK(pose().gearDown==1);
  // Render tail axes match the force consumer, including keyboard yaw and
  // disabled-input guards, while physical LOOK-stick motion stays separate.
  gPitch=64;gRight=1;CHECK(std::fabs(pose().elevator-.15f)<.0001f&&std::fabs(pose().rudder-.35f)<.0001f);
  *reinterpret_cast<uint16_t *>(gPad+0xf0)=1;CHECK(pose().elevator==0&&pose().rudder==0);*reinterpret_cast<uint16_t *>(gPad+0xf0)=0;
  gPitch=gRight=0;gGunYaw=128;CHECK(pose().rudder==0);gGunYaw=0;
  // Rewind must discard manual ownership and animation progress.
  gameMs=1;JetFrame(gameMs);CHECK(pose().gearDown==1&&pose().doorOpen==0);
  gPlayer[0x4c0]=0;*reinterpret_cast<uint8_t **>(v+0x218)=nullptr;if(restart)restart();
 }if(shutdown)shutdown();
 // The ground closes at real metres/second while the state machine runs
 // on native frame times. A fixed18m trigger cannot finish2.4s gear travel
 // during these ordinary approaches; contact requires the locked-down gear.
 for(float sink:{10.0f,20.0f,40.0f})if(spawnSystems(200000+uint32_t(sink)*1000)){
  auto *v=jetVehicles[0];*reinterpret_cast<int32_t *>(gPlayer+0x19c)=18;WarpJet(gPlayer,v);V(v,0x38).z=220;V(v,0x84)={0,1.2f,.1f};advance(3000);CHECK(pose().gearDown==0);
  V(v,0x84).z=-sink/50;float startedAt=0;bool fullyBeforeGround=false;
  for(int tick=0;tick<1000;++tick){V(v,0x38).z-=sink*.05f;advance(50);const auto state=pose();const float clearance=V(v,0x38).z-15.994784f;
   if(!startedAt&&state.gearDown>0)startedAt=clearance;if(clearance>0&&state.gearDown==1)fullyBeforeGround=true;
   if(clearance<=0){CHECK(fullyBeforeGround&&state.gearDown==1);break;}
  }
  CHECK(startedAt>sink*2.4f);V(v,0x38).z=40;V(v,0x84)={0,1.2f,0};advance(3000);CHECK(pose().gearDown==1); // leveling low cannot retract again
  gPlayer[0x4c0]=0;*reinterpret_cast<uint8_t **>(v+0x218)=nullptr;if(restart)restart();if(shutdown)shutdown();
 }
 if(spawnSystems(290000)){
  auto *v=jetVehicles[0];*reinterpret_cast<int32_t *>(gPlayer+0x19c)=18;WarpJet(gPlayer,v);V(v,0x38).z=220;V(v,0x84)={0,1.2f,.1f};advance(3000);horn=true;advance(3000);CHECK(pose().gearDown==1);
  ++pedGeneration;advance(3000);CHECK(pose().gearDown==0); // a new current driver cannot inherit the old manual lease
  horn=false;gPlayer[0x4c0]=0;*reinterpret_cast<uint8_t **>(v+0x218)=nullptr;if(restart)restart();if(shutdown)shutdown();
 }
 // Retained owned actors can still land when the menu disables pad input
 // or the feature is switched off. Mission/cutscene policy stays hands-off,
 // while an already commanded mechanical transition still reaches its end.
 for(int context=0;context<4;++context)if(spawnSystems(300000+10000*context)){
  auto *v=jetVehicles[0];*reinterpret_cast<int32_t *>(gPlayer+0x19c)=18;WarpJet(gPlayer,v);V(v,0x38).z=220;V(v,0x84)={0,1.2f,.1f};advance(3000);CHECK(pose().gearDown==0);
  auto setContext=[&](bool active){switch(context){case 0:*reinterpret_cast<uint16_t *>(gPad+0xf0)=active;break;case 1:gConfig.planes=!active;break;case 2:mission=active;break;case 3:cutsceneRunning=active;break;}};
  setContext(true);V(v,0x84).z=-.4f;bool deployedBeforeGround=false;
  for(int tick=0;tick<210;++tick){V(v,0x38).z-=1;advance(50);const float clearance=V(v,0x38).z-15.994784f;
   if(clearance>0&&airportjets::GearDown(v)==1)deployedBeforeGround=true;
   if(clearance<=0)break;
  }
  CHECK(airportjets::Owns(v));
  if(context<2)CHECK(deployedBeforeGround&&pose().gearDown==1);else CHECK(pose().gearDown==0); // no new mission auto command
  setContext(false);V(v,0x38).z=220;V(v,0x84)={0,1.2f,.1f};horn=false;advance(3000);CHECK(pose().gearDown==0);
  horn=true;advance(500);CHECK(pose().gearDown>0&&pose().gearDown<1);
  setContext(true);V(v,0x38).z=40;V(v,0x84).z=-.4f;advance(3000);CHECK(pose().gearDown==1);
  if(context>=2){V(v,0x38).z=220;V(v,0x84).z=.1f;horn=false;advance(50);horn=true;advance(3000);CHECK(pose().gearDown==1);} // no new mission manual/retract command
  setContext(false);horn=false;gPlayer[0x4c0]=0;*reinterpret_cast<uint8_t **>(v+0x218)=nullptr;if(restart)restart();if(shutdown)shutdown();
 }
 // Exercise the real getter's dispatch against merged pad state, rather
 // than assuming L3 or a particular physical keyboard/controller binding.
 for(int mode=-1;mode<4;++mode)if(spawnSystems(100000+10000*(mode+1))){
  auto *v=jetVehicles[0];*reinterpret_cast<int32_t *>(gPlayer+0x19c)=18;WarpJet(gPlayer,v);V(v,0x38).z=60;V(v,0x84)={0,1.2f,.1f};nativeHorn=true;
  gControllerInput=mode<0?0:1;*reinterpret_cast<int16_t *>(gPad+0xec)=int16_t(mode<0?99:mode);
  const int field=mode==1?0x8:mode==2?0xc:0x24;advance(3000);CHECK(pose().gearDown==0);
  *reinterpret_cast<int16_t *>(gPad+field)=255;advance(3000);CHECK(pose().gearDown==1);advance(1000);CHECK(pose().gearDown==1);
  *reinterpret_cast<int16_t *>(gPad+field)=0;advance(50);*reinterpret_cast<int16_t *>(gPad+field)=255;advance(3000);CHECK(pose().gearDown==0);
  // Disabling native input makes GetHorn returnfalse. Holding the same
  // physical button through re-enable must not synthesize a new gear press.
  *reinterpret_cast<uint16_t *>(gPad+0xf0)=1;advance(500);*reinterpret_cast<uint16_t *>(gPad+0xf0)=0;advance(500);CHECK(pose().gearDown==0);
  gPlayer[0x4c0]=0;*reinterpret_cast<uint8_t **>(v+0x218)=nullptr;if(restart)restart();if(shutdown)shutdown();
 }
 for(int cancel=0;cancel<12;++cancel)if(spawnSystems(30000+5000*cancel)){
  auto *v=jetVehicles[0];V(gPlayer,0x38)=V(v,0x38);V(gPlayer,0x38).x-=1.6f;V(gPlayer,0x38).y+=3.7f;V(gPlayer,0x38).z=14.84f;
  const int beforeWarp=warped;enter(gPlayer,v,0);advance(900);CHECK(warped==beforeWarp&&!playedBoardingAnims.empty()&&airportjets::gJets[0].doorPed==gPlayer);
  switch(cancel){case 0:V(gPlayer,0x38).x+=10;break;case 1:mission=true;break;case 2:cutsceneRunning=true;break;case 3:*reinterpret_cast<uint16_t *>(gPad+0xf0)=1;break;
   case 4:gConfig.planes=false;break;case 5:*reinterpret_cast<float *>(gPlayer+0x44c)=0;break;case 6:*reinterpret_cast<int32_t *>(gPlayer+0x2fc)=50;break;case 7:v[0x294]=2;break;case 8:blocked=true;break;case 9:*reinterpret_cast<int32_t *>(gPlayer+0x19c)=7;break;case 10:advance(450);recycleDoorVehicle=true;break;case 11:++pedGeneration;break;}
  advance(2000);CHECK(warped==beforeWarp&&!game::PedInVehicle(gPlayer));if(airportjets::Owns(v))CHECK(pose().doorOpen==0);
  mission=cutsceneRunning=false;gConfig.planes=true;*reinterpret_cast<uint16_t *>(gPad+0xf0)=0;*reinterpret_cast<float *>(gPlayer+0x44c)=100;v[0x294]=1;
  if(restart)restart();if(shutdown)shutdown();
 }
 if(spawnSystems(90000)){
  auto *v=jetVehicles[0];V(gPlayer,0x38)=V(v,0x38);V(gPlayer,0x38).x-=1.6f;V(gPlayer,0x38).y+=3.7f;V(gPlayer,0x38).z=14.84f;
  const int beforeWarp=warped,beforeExit=exited;enter(gPlayer,v,0);for(int tick=0;tick<200&&!game::PedInVehicle(gPlayer);++tick)advance(50);
  CHECK(warped==beforeWarp+1&&game::PedInVehicle(gPlayer)&&pose().doorOpen==1);advance(1100);CHECK(pose().doorOpen==0);
  leave(gPlayer,v,0);advance(300);CHECK(exited==beforeExit&&pose().doorOpen>0&&pose().doorOpen<1);
  advance(500);CHECK(exited==beforeExit+1&&!game::PedInVehicle(gPlayer)&&pose().doorOpen==1);
  CHECK(std::fabs(V(gPlayer,0x38).x-V(v,0x38).x+1.6f)<.001f&&std::fabs(V(gPlayer,0x38).y-V(v,0x38).y-3.7f)<.001f);
  advance(1100);CHECK(pose().doorOpen==0);if(restart)restart();
 }if(shutdown)shutdown();
 // Original636 is hidden before any query. Even a stuck original-surface
 // witness cannot intercept the runway; foreign support stays eligible.
 for(int scenario=0;scenario<6;++scenario){Setup();sourceOccludesGround=true;groundStuck=scenario==1;groundMissing=scenario==2;groundRecycle=scenario==3;foreignGround=scenario>=4;foreignGroundHeight=scenario==5?17.73299f:16.5f;
  ignored=buildings[6];const int before=allocated,beforeDestroyed=destroyed;const uint32_t now=8000+1000*scenario;JetFrame(now);const int model=lastRequest;
  CHECK(model>=240&&model<=249);if(model>=240&&model<=249){*reinterpret_cast<void **>(models[model]+0x30)=&clumpMarker;streamInfo[model*32+0x10]=1;JetFrame(now+100);
   CHECK(ignored==buildings[6]);
   if(scenario<=1){CHECK(allocated==before+1&&!sourceInWorld[0]&&airportjets::Owns(jetVehicles[0]));CHECK(std::fabs(V(jetVehicles[0],0x38).z-16.074784f)<.0001f);CHECK(game::VehicleHealth(jetVehicles[0])==1000&&V(jetVehicles[0],0x84).z==0);}
   else if(scenario>=4){const float expected=scenario==5?20.007774f:18.774784f;CHECK(allocated==before+1&&std::fabs(V(jetVehicles[0],0x38).z-expected)<.0001f);} // foreign support must remain eligible
   else CHECK(allocated==before&&destroyed==beforeDestroyed&&!sourceInWorld[0]&&!airportjets::Owns(jetVehicles[0]));
   CHECK(groundCalls<=18);if(restart)restart();
  }if(shutdown)shutdown();
 }
 if(!failures)std::puts("airportjets: all passed");return failures?1:0;
}
#endif
