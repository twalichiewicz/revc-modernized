#define main ForceFixturesMain
#include "test_planeflight.cpp"
#undef main
#include <array>
#include <map>
#include <string>
alignas(16) static uint8_t gDodo[0x6f0]{},gModel[0x200]{},gCol[0x70]{},gSphere[0x14]{},gPool[0x20]{};
static uint8_t *gPoolPointer=gPool,*gModelTable[6500]{};
static int gPlaneClass;
static uint8_t gPoolFlags[8]{},gStreaming[8000*0x20]{};
static int gArea,gGeneration=17,gConstructed,gDeleted,gAdded,gRemoved,gRequested,gOriginalPlane,gPhysical,gWarp,gOut,gSeat,gOriginalEnter,gOriginalExit;
static bool gMission,gCutscene,gExit,gBlocked,gAllocationFails,gAllocated,gRejectExitHook;
static int gDamage,gExplosions,gKills;
static uint32_t gNativeFrame=100;
static uint8_t *gIgnored;
static std::map<std::string,void *> gRosterHooks;
static uint8_t *LookupVehicle(int ref){return gAllocated&&ref==gGeneration?gDodo:nullptr;}
static void *Allocate(size_t size){CHECK(size==0x388);if(gAllocationFails)return nullptr;gAllocated=true;gPoolFlags[0]=uint8_t(gGeneration);return gDodo;}
static void Construct(uint8_t *v,int model,uint8_t owner)
{
	CHECK(v==gDodo&&model==181&&owner==1);++gConstructed;
	std::memset(v,0,0x388);V(v,8)={1,0,0};V(v,0x18)={0,1,0};V(v,0x28)={0,0,1};
	*reinterpret_cast<void **>(v)=&gPlaneClass;
	*reinterpret_cast<int16_t *>(v+0x6a)=181;*reinterpret_cast<int32_t *>(v+0x348)=4;
	*reinterpret_cast<float *>(v+0xcc)=*reinterpret_cast<float *>(v+0xd0)=1e8f;
	*reinterpret_cast<float *>(v+0x2a0)=1000;*reinterpret_cast<uint32_t *>(v+0x60)=2|(9<<3)|0x400000;
	*reinterpret_cast<void **>(v+0x58)=gModel;v[0x294]=owner;
}
static void Destroy(uint8_t *v){CHECK(v==gDodo);++gDeleted;gAllocated=false;gPoolFlags[0]=0x80;}
static void Add(uint8_t *v){CHECK(v==gDodo);++gAdded;}static void Remove(uint8_t *v){CHECK(v==gDodo);++gRemoved;}
static void NativePlane(uint8_t *){++gOriginalPlane;}
static void Physical(uint8_t *v){++gPhysical;V(v,0x84).z-=.008f*gStep;*reinterpret_cast<float *>(v+0x13c)=0;}
static void NativeSeat(uint8_t *ped){++gSeat;CHECK(game::PedVehicle(ped)==gDodo);V(ped,0x38)=V(gDodo,0x38);}
static void Enter(uint8_t *,uint8_t *,uint32_t){++gOriginalEnter;}
static void Exit(uint8_t *,uint8_t *,uint32_t){++gOriginalExit;}
static void Damage(uint8_t *v,uint8_t *,int,float damage,CVector){++gDamage;*reinterpret_cast<float *>(v+0x2a0)=std::max(0.0f,game::VehicleHealth(v)-damage);}
static void BoundDamage(uint8_t *v,uint8_t *by,int weapon,float damage,CVector point)
{
	using F=void (*)(uint8_t *,uint8_t *,int,float,CVector);
	for(const auto &hook:gRosterHooks)if(hook.first.find("InflictDamage")!=std::string::npos)return reinterpret_cast<F>(hook.second)(v,by,weapon,damage,point);
	Damage(v,by,weapon,damage,point);
}
static void Kill(uint8_t *){++gKills;}
static bool Explosion(uint8_t *,uint8_t *,int type,const CVector &,uint32_t,bool){CHECK(type==6);++gExplosions;return true;}
static void Update(void *){}
static bool OnMission(){return gMission;}
static bool ExitButton(uint8_t *){return gExit;}
static void Request(int model,int flags){
	CHECK(model==181||(model>=6500&&model<7885));if(model==181)++gRequested;
	auto *stream=gStreaming+size_t(model)*0x20;
	if(!stream[0x10]){stream[0x10]=2;stream[0x11]=uint8_t(flags);}
	else stream[0x11]|=uint8_t(flags);
}
static void Deletable(int model){
	CHECK(model==181||(model>=6500&&model<7885));auto *stream=gStreaming+size_t(model)*0x20;
	stream[0x11]&=~uint8_t(1);
	if(model>=6500&&stream[0x10]!=1&&!(stream[0x11]&2))stream[0x10]=0;
}
static void Objective(uint8_t *ped,int objective,void *){*reinterpret_cast<int32_t *>(ped+0x19c)=objective;}
static void Warp(uint8_t *ped,uint8_t *plane)
{
	CHECK(game::PedObjective(ped)==18&&plane==gDodo);++gWarp;
	ped[0x4c0]=1;*reinterpret_cast<uint8_t **>(ped+0x4b8)=plane;*reinterpret_cast<uint8_t **>(plane+0x218)=ped;
	*reinterpret_cast<int32_t *>(ped+0x2fc)=game::kPedDriving;plane[0x60]=2;plane[0x295]|=0x10;
}
static void Out(void *,void *arg)
{
	++gOut;auto *ped=static_cast<uint8_t *>(arg);CHECK(game::PedVehicle(ped)==gDodo);
	ped[0x4c0]=0;*reinterpret_cast<uint8_t **>(gDodo+0x218)=nullptr;
	*reinterpret_cast<int32_t *>(ped+0x2fc)=game::kPedIdle;gDodo[0x60]=2|(4<<3);
}
static void Teleport(uint8_t *ped,CVector position){V(ped,0x38)=position;}
static bool NativeRocket(const CVector &position)
{
	// Exact native CPlane::TestRocketCollision0x279088 producer: valid native
	// pool slot/model180or181, range25, frame380 and accepted-hit byte384.
	for(int i=7;i>=0;--i){
		if(gPoolFlags[i]&0x80)continue;
		auto *v=gDodo+size_t(i)*0x6f0;
		if((game::EntityModel(v)&~1)!=180||v[0x384]||Magnitude(Plus(position,Times(V(v,0x38),-1)))>=25)continue;
		*reinterpret_cast<uint32_t *>(v+0x380)=gNativeFrame;v[0x384]=1;return true;
	}
	return false;
}
static bool Ground(const CVector &from,float to,uint8_t *point,uint8_t *&entity,bool buildings,bool vehicles,bool peds,bool objects,bool dummies,bool,void *)
{
	CHECK(buildings&&!vehicles&&!peds&&!objects&&!dummies);entity=nullptr;
	if(from.z<0||to>0)return false;V(point,0)={from.x,from.y,0};V(point,0x10)={0,0,1};return true;
}
static uint8_t *Sphere(CVector centre,float radius,uint8_t *,bool buildings,bool vehicles,bool peds,bool objects,bool dummies,bool)
{
	CHECK(buildings&&vehicles&&objects&&!dummies);(void)peds;
	CHECK(centre.z-radius>-.05f);return gBlocked?gPlayer:nullptr;
}
static bool Clear(const CVector &,const CVector &,bool buildings,bool vehicles,bool peds,bool objects,bool dummies,bool,bool)
{
	CHECK(buildings&&vehicles&&!peds&&objects&&!dummies);CHECK(gIgnored==gDodo);return !gBlocked;
}
static void *RosterSymbol(const char *name)
{
#define B(n,f) if(std::strstr(name,n))return reinterpret_cast<void *>(f)
	B("CVehiclenwEm",Allocate);B("CPlaneC1Eih",Construct);B("CPlaneD0Ev",Destroy);
	B("CWorld3Add",Add);B("CWorld6Remove",Remove);B("CPhysical14ProcessControl",Physical);
	B("WarpPedIntoCar",Warp);B("PedSetOutCarCB",Out);B("CPed8Teleport",Teleport);
	B("CMatrix8UpdateRW",Update);B("CEntity13UpdateRwFrame",Update);
	B("IsPlayerOnAMission",OnMission);B("ExitVehicleJustDown",ExitButton);
	B("InflictDamage",BoundDamage);B("KillPedsInVehicle",Kill);B("AddExplosion",Explosion);
	B("SetModelIsDeletable",Deletable);
#undef B
	if(std::strstr(name,"currArea"))return &gArea;
	if(std::strstr(name,"ms_running"))return &gCutscene;
	if(std::strstr(name,"m_FrameCounter"))return &gNativeFrame;
	if(std::strstr(name,"ms_aInfoForModel"))return gStreaming;
	if(std::strstr(name,"ms_pVehiclePool"))return &gPoolPointer;
	return nullptr;
}
static void *Original(const char *name,void *hook)
{
	// Real hooks::Chain rejects its own already-installed wrapper. An install
	// interrupted at a later symbol must keep earlier originals on retry.
	if((gRejectExitHook&&std::strstr(name,"SetExitCar"))||gRosterHooks[name]==hook)return nullptr;
	gRosterHooks[name]=hook;
	if(std::strstr(name,"CPlane14ProcessControl"))return reinterpret_cast<void *>(NativePlane);
	if(std::strstr(name,"SetPedPositionInCar"))return reinterpret_cast<void *>(NativeSeat);
	if(std::strstr(name,"SetEnterCar"))return reinterpret_cast<void *>(Enter);
	if(std::strstr(name,"SetExitCar"))return reinterpret_cast<void *>(Exit);
	if(std::strstr(name,"InflictDamage"))return reinterpret_cast<void *>(Damage);
	return nullptr;
}
template<class F>static F Hook(const char *part)
{
	for(const auto &entry:gRosterHooks)if(entry.first.find(part)!=std::string::npos)return reinterpret_cast<F>(entry.second);
	return nullptr;
}
static void RosterReset()
{
	Reset();gExtraSymbol=RosterSymbol;gChainOriginal=Original;
	gArea=0;gMission=gCutscene=gExit=gBlocked=gAllocationFails=gAllocated=false;
	gConstructed=gDeleted=gAdded=gRemoved=gRequested=gOriginalPlane=gPhysical=gWarp=gOut=gSeat=gOriginalEnter=gOriginalExit=0;
	gDamage=gExplosions=gKills=0;gIgnored=gBoat;
	std::memset(gDodo,0,sizeof gDodo);std::memset(gModel,0,sizeof gModel);std::memset(gCol,0,sizeof gCol);
	std::memset(gStreaming,0,sizeof gStreaming);std::fill(std::begin(gPoolFlags),std::end(gPoolFlags),0x80);
	*reinterpret_cast<uint8_t **>(gPool)=gDodo;*reinterpret_cast<uint8_t **>(gPool+8)=gPoolFlags;*reinterpret_cast<int32_t *>(gPool+16)=8;
	*reinterpret_cast<uint8_t **>(gModel+0x20)=gCol;gModel[0x1d]=6;*reinterpret_cast<int16_t *>(gModel+0x2e)=5;
	V(gCol,0x10)={-2.91f,-6.78f,-1.285f};V(gCol,0x1c)={2.92f,3.91f,1.79f};
	*reinterpret_cast<int16_t *>(gCol+0x28)=1;*reinterpret_cast<uint8_t **>(gCol+0x38)=gSphere;
	V(gSphere,0)={0,0,0};*reinterpret_cast<float *>(gSphere+12)=.5f;
	gModelTable[181]=gModel;game::api.modelInfoPtrs=gModelTable;game::api.RequestModel=Request;
	game::api.PoolsGetVehicle=LookupVehicle;game::api.ProcessVerticalLine=Ground;game::api.TestSphereAgainstWorld=Sphere;game::api.SetObjective=Objective;
	game::api.GetIsLineOfSightClear=Clear;game::api.ignoreEntity=&gIgnored;
	gPlayer[0x4c0]=0;*reinterpret_cast<float *>(gPlayer+0x44c)=100;*reinterpret_cast<int32_t *>(gPlayer+0x2fc)=game::kPedIdle;
	V(gPlayer,0x38)={-1500,-1130,1.04f};
	gRejectExitHook=true;CHECK(!planeflight::Install());
	gRejectExitHook=false;CHECK(planeflight::Install());
}
int main()
{
	RosterReset();
	gMission=true;planeflight::Frame(100);gMission=false;
	gCutscene=true;planeflight::Frame(200);gCutscene=false;
	gArea=1;planeflight::Frame(300);gArea=0;
	CHECK(gRequested==0&&gConstructed==0);
	std::fill(std::begin(gPoolFlags),std::end(gPoolFlags),0);gPoolFlags[0]=gPoolFlags[1]=gPoolFlags[2]=0x80;
	planeflight::Frame(500);CHECK(gRequested==0&&gConstructed==0); // reserve native pool capacity
	std::fill(std::begin(gPoolFlags),std::end(gPoolFlags),0x80);
	planeflight::Frame(3000);CHECK(gRequested==1&&gConstructed==0);
	gStreaming[181*0x20+0x10]=1;gAllocationFails=true;planeflight::Frame(6000);
	CHECK(gConstructed==0&&gAdded==0);gAllocationFails=false;
	gBlocked=true;planeflight::Frame(9000);CHECK(gConstructed==0);gBlocked=false;
	planeflight::Frame(12000);
	CHECK(gConstructed==1&&gAdded==1&&gAllocated);
	std::array<uint8_t,sizeof gDodo> identitySnapshot{};std::memcpy(identitySnapshot.data(),gDodo,sizeof gDodo);
	CHECK(planeflight::Owns(gDodo)&&!planeflight::Owns(gBoat)&&!planeflight::Owns(nullptr));
	CHECK(!std::memcmp(identitySnapshot.data(),gDodo,sizeof gDodo)); // public identity read cannot apply forces or mutate native state
	CHECK(game::EntityModel(gDodo)==181&&*reinterpret_cast<int32_t *>(gDodo+0x348)==4);
	CHECK((game::EntityFlags(gDodo)&0x400100)==0x100);CHECK(*reinterpret_cast<float *>(gDodo+0xcc)<10000);
	CHECK((gDodo[0x295]&8)!=0); // native plane retention flag prevents ordinary traffic removal
	planeflight::Frame(12010);CHECK(gConstructed==1); // one owned plane
	auto control=Hook<void (*)(uint8_t *)>("CPlane14ProcessControl");
	auto seat=Hook<void (*)(uint8_t *)>("SetPedPositionInCar");
	auto enter=Hook<void (*)(uint8_t *,uint8_t *,uint32_t)>("SetEnterCar");
	auto exit=Hook<void (*)(uint8_t *,uint8_t *,uint32_t)>("SetExitCar");
	CHECK(control&&seat&&enter&&exit);if(!control||!seat||!enter||!exit)return 1;
	enter(gPlayer,gBoat,0);exit(gPlayer,gBoat,0);CHECK(gOriginalEnter==1&&gOriginalExit==1);
	V(gPlayer,0x38)=V(gDodo,0x38);V(gPlayer,0x38).x+=2;
	gMission=true;enter(gPlayer,gDodo,0);gMission=false;
	gCutscene=true;enter(gPlayer,gDodo,0);gCutscene=false;CHECK(gWarp==0);
	gExit=true;gBlocked=true;planeflight::Frame(13000);
	CHECK(gWarp==0&&gIgnored==gBoat);gBlocked=false;planeflight::Frame(13001);
	CHECK(gWarp==1&&game::PedInVehicle(gPlayer)&&*reinterpret_cast<uint8_t **>(gDodo+0x218)==gPlayer);
	seat(gPlayer);CHECK(gSeat>0&&std::fabs(V(gPlayer,0x38).y-V(gDodo,0x38).y)>1);
	gExit=false;planeflight::Frame(2);CHECK(planeflight::Apply(gDodo,5)); // a clock epoch change cannot release a still-valid native actor
	gMoves=gTurns=0;
	gExit=false;gThrottle=0;gBrake=255;gRoll=96;V(gDodo,0x84)={0,-.04f,0};control(gDodo);
	CHECK(std::fabs(V(gDodo,0x84).y)*50<1.9f&&V(gDodo,0x90).z<0&&std::fabs(V(gDodo,0x90).y)<1e-6f);
	gThrottle=255;gBrake=gRoll=0;V(gDodo,0x84)={0,-1.2f,0};control(gDodo);CHECK(gPhysical==2&&gOriginalPlane==0&&gMoves>0);
	*reinterpret_cast<int16_t *>(gBoat+0x6a)=181;*reinterpret_cast<int32_t *>(gBoat+0x348)=4;
	control(gBoat);CHECK(gOriginalPlane==1); // same-model ambient/unowned native control retained
	*reinterpret_cast<void **>(gDodo+0x58)=gModel+8;control(gDodo);
	CHECK(gOriginalPlane==1&&gPhysical==3); // streaming a new clump cannot hand a playable CPlane to its ambient path
	V(gDodo,0x84)={};const float parked=V(gDodo,0x38).z;V(gDodo,0x38).z=1000;
	exit(gPlayer,gDodo,0);CHECK(gOut==0&&game::PedInVehicle(gPlayer)); // no mid-air ground teleport
	V(gDodo,0x38).z=parked;
	V(gDodo,0x84)={};gBlocked=true;exit(gPlayer,gDodo,0);CHECK(gOut==0&&game::PedInVehicle(gPlayer));
	planeflight::Frame(2999);gBlocked=false;gExit=true;exit(gPlayer,gDodo,0);
	CHECK(gOut==1&&!game::PedInVehicle(gPlayer));
	planeflight::Frame(3000);CHECK(gWarp==1&&!game::PedInVehicle(gPlayer)); // native hook and Frame see the same F press
	gExit=false;
	CHECK(std::fabs(V(gPlayer,0x38).z-1.04f)<.01f);
	CHECK(gIgnored==gBoat);
	*reinterpret_cast<float *>(gDodo+0x13c)=8*(*reinterpret_cast<float *>(gDodo+0xcc))/50;
	control(gDodo);const float collisionHealth=game::VehicleHealth(gDodo);
	CHECK(gDamage==1&&collisionHealth<1000&&collisionHealth>250&&gExplosions==0);
	control(gDodo);CHECK(game::VehicleHealth(gDodo)==collisionHealth&&gDamage==1); // native ProcessControl clears consumed contact
	auto damage=Hook<void (*)(uint8_t *,uint8_t *,int,float,CVector)>("InflictDamage");CHECK(damage);
	if(damage){
		damage(gDodo,nullptr,24,2000,{});CHECK(gDamage==2&&gExplosions==1&&gKills==1&&game::Wrecked(gDodo));
		damage(gDodo,nullptr,24,2000,{});CHECK(gExplosions==1); // fatal transition once
		damage(gBoat,nullptr,24,1,{});CHECK(gDamage==4&&gExplosions==1); // original native damage elsewhere
	}
	gExit=true;planeflight::Frame(7000);CHECK(gWarp==1); // a wreck is not boardable
	*reinterpret_cast<uint8_t **>(gDodo+0x218)=gBoat;V(gPlayer,0x38)={0,0,0};planeflight::Frame(8000);
	CHECK(gRemoved==0&&gDeleted==0); // cleanup cannot delete another occupied vehicle
	*reinterpret_cast<uint8_t **>(gDodo+0x218)=nullptr;planeflight::Frame(8001);CHECK(gRemoved==1&&gDeleted==1);
	// Invalidate a live owned record by reusing its native slot generation.
	V(gPlayer,0x38)={-1500,-1130,1.04f};gExit=false;planeflight::Frame(11000);
	CHECK(gConstructed==2&&gAdded==2&&gAllocated);
	gNativeFrame=0xfffffff0u;
	CHECK(!NativeRocket(Plus(V(gDodo,0x38),{0,0,25}))); // native range is unchanged
	CHECK(NativeRocket(Plus(V(gDodo,0x38),{0,0,24.9f}))&&!NativeRocket(V(gDodo,0x38)));
	gNativeFrame=3;control(gDodo);CHECK(!game::Wrecked(gDodo)&&gExplosions==1); //19 frames across wrap
	gNativeFrame=4;control(gDodo);
	CHECK(game::Wrecked(gDodo)&&gExplosions==2&&gKills==2&&game::VehicleHealth(gDodo)==0);
	control(gDodo);CHECK(gExplosions==2); // accepted native witness cannot retrigger crash
	gBoat[0x294]=2;gBoat[0x384]=1;*reinterpret_cast<uint32_t *>(gBoat+0x380)=0xfffffff0u;
	control(gBoat);CHECK(gOriginalPlane==2&&gExplosions==2); // unowned/mission CPlane keeps its native handler
	gGeneration=18;CHECK(!planeflight::Owns(gDodo));control(gDodo);planeflight::Frame(11001);
	CHECK(gOriginalPlane==3&&gDeleted==1&&gRemoved==1);
	if(!failures)std::puts("planeflight roster: all passed");return failures?1:0;
}
