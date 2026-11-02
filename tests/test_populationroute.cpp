// Replay a route through the real population Frame/materialization boundary.
// Engine pools, streaming and ground are external; city identities, travel,
// ownership, budgets, generation checks and station queues are production code.
#include "../src/population.cpp"
#include <cstdio>
#include <cstdarg>
namespace game { Api api{}; }
Config gConfig;
static int gLogs;
void Log(const char *, ...) { gLogs++; }
namespace traffic { bool OwnsPopulation(bool ready,bool enabled) { return ready && enabled; } }
namespace wanted {
bool Active() { return false; }
const heat::Model &Model() { static heat::Model model; return model; }
CVector Target() { return {}; }
}
constexpr int kSlots=80,kRouteNodes=120;
alignas(16) static uint8_t gPlayer[0x8d0],gPeople[kSlots][0x8d0],gCamera[4096],gPathData[0x54ba0];
alignas(8) static uint8_t gPool[32],gModels[2][0x60],gStreaming[6500*0x20];
static uint8_t *gPoolPtr=gPool,*gModelPointers[6500];
static uint8_t gFlags[kSlots];
static int gAreaValue,gRandomType,gCivilianAdds,gCopAdds,gRequests,gZonePedDemand=100;
static uint32_t gNow=2000;
static float gStep=12.5f,gRainValue=0,gDensityValue=1;
static bool gCut,gSightVisible=false;
static uint8_t *Player() { return gPlayer; }
static int Ref(uint8_t *ped) { const int index=int((ped-gPeople[0])/0x8d0); return (index<<8)|(gFlags[index]&0x7f); }
static uint8_t *GetPed(int handle) { const int index=handle>>8; return index>=0 && index<kSlots && !(gFlags[index]&0x80) && (gFlags[index]&0x7f)==(handle&0x7f)?gPeople[index]:nullptr; }
static uint8_t *AddPed(int32_t type,uint32_t,const CVector &at,int32_t) {
	for(int i=0;i<kSlots;i++) if(gFlags[i]&0x80) {
		gFlags[i]=uint8_t((gFlags[i]+1)&0x7f); std::memset(gPeople[i],0,sizeof(gPeople[i]));
		*reinterpret_cast<CVector *>(gPeople[i]+0x38)=at; *reinterpret_cast<int32_t *>(gPeople[i]+0x4f8)=type;
		*reinterpret_cast<float *>(gPeople[i]+0x44c)=100; gPeople[i][0x19a]=1;
		if(type==6) gCopAdds++; else gCivilianAdds++;
		return gPeople[i];
	}
	return nullptr;
}
static void RemovePed(uint8_t *ped) { gFlags[Ref(ped)>>8]|=0x80; }
static bool Wander(uint8_t *,int8_t) { return true; }
static bool CanDelete(uint8_t *) { return true; }
static bool SphereVisible(uint8_t *,const CVector &,float) { return gSightVisible; }
static bool Sight(const CVector &,const CVector &,bool,bool,bool,bool,bool,bool,bool) { return true; }
static bool Ground(const CVector &from,float,uint8_t *point,uint8_t *&,bool,bool,bool,bool,bool,bool,void *) {
	*reinterpret_cast<CVector *>(point)={from.x,from.y,0}; *reinterpret_cast<CVector *>(point+0x10)={0,0,1}; return true;
}
static uint8_t *Clear(CVector,float,uint8_t *,bool,bool,bool,bool,bool,bool) { return nullptr; }
static void Info(const CVector *,uint8_t *out) { std::memset(out,0,68); *reinterpret_cast<uint16_t *>(out+0x2c)=uint16_t(gZonePedDemand); *reinterpret_cast<uint16_t *>(out+0x40)=1000; }
static int Choose(int) { return 10; }
static void Request(int model,int) { if(model==10) gRequests++; }
static void Seed() {
	using namespace population;
	gCiviliansWorld=populationmodel::World{}; gPoliceWorld=populationmodel::World{}; gNodes.clear();
	std::memset(gPathData,0,sizeof(gPathData));
	gCarCount=1; gTotalCount=kRouteNodes+1;
	for(int i=0;i<kRouteNodes;i++) {
		// Fixed street route, no seeding or relocation at the player's position.
		const float x=float(i%30)*12, y=float(i/30)*15;
		const int next=i%30==29?i-29:i+1,previous=i%30==0?i+29:i-1;
		gNodes.push_back({{x,y,0},100,{previous,next}});
		uint8_t *node=gPathData+size_t(i+1)*0x14;
		*reinterpret_cast<int16_t *>(node+4)=int16_t(x*8); *reinterpret_cast<int16_t *>(node+6)=int16_t(y*8);
		node[0x10]=2; node[0x12]=15;
	}
	gSuitable.assign(kRouteNodes,true); gCopRoute.assign(kRouteNodes,true);
	gCiviliansWorld.Init(gNodes,400,0,731); gPoliceWorld.Init(gNodes,0,96,732);
	gCivResidents.assign(400,{}); gCopResidents.assign(96,{});
	gCursor=0; gOrders.clear(); gStations.clear(); gLifecycle={}; gSpawnStats[0]={}; gSpawnStats[1]={}; gBuilt=gReady=true; gEnabled=true;
	gLastTime=gNow; gLastTick=0; gLogAt=0;
	gCivilianAdds=gCopAdds=gRequests=0; gZonePedDemand=100;
	std::memset(gFlags,0x80,sizeof(gFlags)); std::memset(gPeople,0,sizeof(gPeople));
	gStreaming[10*0x20+0x10]=1; gStreaming[1*0x20+0x10]=1;
}
static void Tick(float x) {
	*reinterpret_cast<CVector *>(gPlayer+0x38)={x,0,1}; gNow+=250; population::Frame();
}
int main() {
	api.FindPlayerPed=Player; api.timeMs=&gNow; api.timeStep=&gStep; api.theCamera=gCamera; api.thePaths=gPathData;
	api.PoolsGetPed=GetPed; api.ProcessVerticalLine=Ground; api.TestSphereAgainstWorld=Clear; api.GetIsLineOfSightClear=Sight; api.RequestModel=Request;
	*reinterpret_cast<uint8_t **>(gPool)=gPeople[0]; *reinterpret_cast<uint8_t **>(gPool+8)=gFlags; *reinterpret_cast<int32_t *>(gPool+0x10)=kSlots;
	population::gPedPool=&gPoolPtr; population::gModelInfo=gModelPointers; population::gStreamInfo=gStreaming;
	population::gArea=&gAreaValue; population::gCutscene=&gCut; population::gAllRandomType=&gRandomType;
	population::gRain=&gRainValue; population::gPedDensity=&gDensityValue; population::gZoneInfo=Info; population::gChooseCivilian=Choose;
	population::gAddPed=AddPed; population::gRemovePed=RemovePed; population::gWander=Wander; population::gCanDelete=CanDelete;
	population::gSphereVisible=SphereVisible; population::gPedRef=Ref;
	gModelPointers[1]=gModels[0]; gModelPointers[10]=gModels[1]; *reinterpret_cast<int32_t *>(gModels[1]+0x44)=4;
	gConfig.worldPopulation=true;
	int failures=0;
#define CHECK(c,msg) do { if(!(c)) { std::printf("FAIL: %s\n",msg); failures++; } } while(0)
	Seed(); Tick(0);
	CHECK(gCivilianAdds>=1 && gCopAdds>=1,"police materialization monopolizes the only tick slot while civilian street remains empty");
	// Retire only streamed representations after each route section. Virtual
	// city identities are retained and must be available on a second visit.
	for(int lap=0;lap<2;lap++) {
		const int before=gCivilianAdds;
		for(int step=0;step<20;step++) {
			Tick(float(step)*12);
			for(int i=0;i<kSlots;i++) if(!(gFlags[i]&0x80) && std::fabs(EntityPos(gPeople[i]).x-float(step)*12)>160) RemovePed(gPeople[i]);
		}
		CHECK(gCivilianAdds-before>=12,"driving route fails to refill civilians as old representations leave range");
	}
	CHECK(population::gCiviliansWorld.Agents().size()==400 && population::gPoliceWorld.Agents().size()==96,"driving creates or loses city identities");
	// No pursuit: a stream of native car occupants must not satisfy the
	// pedestrian zone demand while the sidewalk has no materialized people.
	Seed(); population::gPoliceWorld.Agents().clear(); population::gCopResidents.clear(); gZonePedDemand=20;
	for(int i=0;i<10;i++) { uint8_t *driver=AddPed(4,10,{float(i*12),0,1},0); driver[0x4c0]=1; }
	gCivilianAdds=0; Tick(0);
	CHECK(gCivilianAdds>=1,"no-star traffic occupants consume the whole sidewalk pedestrian budget");
	// Streaming/model gate: unloaded candidates request a model, then retry
	// the same city agents after the engine reports it loaded.
	Seed(); gStreaming[10*0x20+0x10]=0; Tick(0);
	CHECK(!gCivilianAdds && gRequests>0,"unloaded civilian model was spawned or never requested while cops spawn");
	gStreaming[10*0x20+0x10]=1; Tick(0); CHECK(gCivilianAdds>0,"loaded civilian model never materializes after request");
	Seed(); gSightVisible=true; Tick(0); CHECK(!gCivilianAdds && !gCopAdds,"visible street candidates pop into view"); gSightVisible=false;
	Seed(); for(int i=0;i<kSlots-8;i++) gFlags[i]=1; Tick(0); CHECK(!gCivilianAdds && !gCopAdds,"pool safety reservation consumed");
	Seed(); gAreaValue=1; Tick(0); CHECK(!gCivilianAdds && !gCopAdds,"indoor materialization seized native ownership"); gAreaValue=0;
	Seed(); gRandomType=7; Tick(0); CHECK(!gCivilianAdds && !gCopAdds,"mission random-ped override seized"); gRandomType=0;
	// Car and officer replacements share one station service queue. Exercise
	// the real ReinforceCar/Replacement path, including token deduplication.
	Seed(); population::Station station; station.pos={0,0,0}; station.node=0; population::gStations.push_back(station);
	CVector departure;
	const uint32_t queuedAt=gNow;
	CHECK(!population::ReinforceCar(7,{10,0,0},gNow,&departure),"car replacement departs before service time");
	CHECK(!population::ReinforceCar(7,{10,0,0},gNow,&departure) && population::gOrders.size()==1,"car replacement duplicated while waiting");
	CHECK(!population::Replacement(population::kOfficerToken|1,populationmodel::Kind::Officer,{10,0,0},gNow,departure),"officer replacement bypasses station queue");
	CHECK(population::ReinforceCar(7,{10,0,0},queuedAt+45000,&departure),"ready car replacement never departs its station");
	CHECK(!population::Replacement(population::kOfficerToken|1,populationmodel::Kind::Officer,{10,0,0},queuedAt+45000,departure),"officer replacement ignores prior car service");
	CHECK(population::Replacement(population::kOfficerToken|1,populationmodel::Kind::Officer,{10,0,0},queuedAt+65000,departure),"ready officer replacement never departs");
	CHECK(population::gOrders.empty() && population::gLifecycle.queued[0]==1 && population::gLifecycle.queued[1]==1 &&
	      population::gLifecycle.departed[0]==1 && population::gLifecycle.departed[1]==1,"station lifecycle does not close queued/departed accounting");
	if(failures) return 1;
	std::puts("populationroute: all passed");
}
