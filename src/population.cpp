#include "population.h"
#include "config.h"
#include "heat.h"
#include "log.h"
#include "populationmodel.h"
#include "populationrules.h"
#include "traffic.h"
#include "wanted.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <limits>
#include <queue>
#include <unordered_map>

using namespace game;
namespace population {
namespace {
constexpr size_t kNodeSize=0x14, kConnections=0x3bd08, kTotal=0x54b78, kCars=0x54b7c;
constexpr int kMaxNodes=9650, kCivilians=3072, kFootPolice=96;
constexpr size_t kPedSize=0x8d0, kAttached=0x720;
constexpr uint32_t kDisabled=1u<<5, kWater=1u<<8, kNoPolice=0x10;
constexpr uint64_t kOfficerToken=uint64_t(1)<<63;
uint8_t **gPedPool, **gModelInfo;
uint8_t *gStreamInfo;
int32_t *gArea, *gAllRandomType;
bool *gCutscene;
uint32_t *gZoneFlags;
CVector *gRestarts;
int16_t *gNumRestarts;
float *gRain, *gPedDensity;
void (*gZoneInfo)(const CVector *,uint8_t *);
int (*gChooseCivilian)(int);
int (*gChooseGang)(int);
uint8_t *(*gAddPed)(int32_t,uint32_t,const CVector &,int32_t);
void (*gRemovePed)(uint8_t *);
bool (*gWander)(uint8_t *,int8_t);
bool (*gCanDelete)(uint8_t *);
bool (*gSphereVisible)(uint8_t *,const CVector &,float);
void (*gSetAlpha)(void *,int);
int (*gPedRef)(uint8_t *);
bool gReady, gBuilt, gEnabled;
int gBuild=-1, gCarCount, gTotalCount;
uint32_t gLastTime, gLastTick, gLastTree, gLogAt;
size_t gCursor;
populationmodel::World gCiviliansWorld, gPoliceWorld;
std::vector<populationmodel::Node> gNodes;
std::vector<bool> gSuitable, gCopRoute;
std::unordered_map<uint64_t,std::vector<CVector>> gRoads;
struct Resident { CVector last{}; uint32_t lostAt=0; bool wasDead=false; };
std::vector<Resident> gCivResidents, gCopResidents;
struct Station { CVector pos{}; int node=-1; populationmodel::Station queue; uint64_t queued[2]{},departed[2]{}; };
std::vector<Station> gStations;
struct Order { uint64_t token; int station; uint32_t queuedAt; populationmodel::Kind kind; };
struct Lifecycle { uint64_t queued[2]{},departed[2]{},full=0,virtualized=0,stale=0,deaths=0; } gLifecycle;
struct SpawnStats { uint64_t near=0,path=0,visible=0,limit=0,pool=0,model=0,geometry=0,failed=0,spawned=0; } gSpawnStats[2];
std::vector<Order> gOrders;
std::vector<float> gDistances;
std::vector<int> gToward;

Vec3 V(const CVector &p) { return {p.x,p.y,p.z}; }
CVector C(Vec3 p) { return {p.x,p.y,p.z}; }
float Flat(const CVector &a,const CVector &b) { return std::hypot(a.x-b.x,a.y-b.y); }
uint64_t Cell(int x,int y) { return (uint64_t(uint32_t(x))<<32)|uint32_t(y); }
const uint8_t *Node(int raw) { return api.thePaths+size_t(raw)*kNodeSize; }
bool Open(int raw)
{
	return raw>=gCarCount && raw<gTotalCount &&
	       !(*reinterpret_cast<const uint32_t *>(Node(raw)+0x10)&(kDisabled|kWater));
}
bool LandPath(int raw)
{
	return raw>=gCarCount && raw<gTotalCount && !(*reinterpret_cast<const uint32_t *>(Node(raw)+0x10)&kWater);
}
int Rate(int raw) { return Node(raw)[0x12]&15; }
CVector Position(int raw) { return NodePos(reinterpret_cast<const CPathNode *>(Node(raw))); }
bool Loaded(int model) { return model>=0 && model<6500 && gStreamInfo[size_t(model)*0x20+0x10]==1; }
bool Attached(const uint8_t *ped) { return *reinterpret_cast<void *const *>(ped+kAttached)!=nullptr; }
bool Visible(const CVector &p)
{
	const CVector head{p.x,p.y,p.z+1};
	if (!gSphereVisible(api.theCamera,head,1.2f)) return false;
	const CVector camera=*reinterpret_cast<const CVector *>(api.theCamera+0x30);
	return api.GetIsLineOfSightClear(camera,head,true,false,false,true,false,true,false);
}
void Info(const CVector &p,uint8_t *out)
{
	std::memset(out,0,68);
	gZoneInfo(&p,out);
}
int Density(const CVector &p)
{
	alignas(8) uint8_t info[68]; Info(p,info);
	return *reinterpret_cast<const uint16_t *>(info+0x2c);
}
int Nearest(const CVector &p)
{
	int best=-1; float distance=std::numeric_limits<float>::max();
	for (size_t i=0;i<gNodes.size();i++) {
		if (!gSuitable[i] || gNodes[i].links.empty() || !Open(int(i)+gCarCount)) continue;
		const Vec3 q=gNodes[i].pos;
		const float z=(q.z-p.z)*3;
		const float d=(q.x-p.x)*(q.x-p.x)+(q.y-p.y)*(q.y-p.y)+z*z;
		if (d<distance) { distance=d; best=int(i); }
	}
	return best;
}
bool NearRoad(const CVector &p)
{
	const int x=int(std::floor(p.x/50)),y=int(std::floor(p.y/50));
	for (int dx=-1;dx<=1;dx++) for (int dy=-1;dy<=1;dy++) {
		const auto it=gRoads.find(Cell(x+dx,y+dy));
		if (it==gRoads.end()) continue;
		for (const CVector &road:it->second)
			if (std::fabs(road.z-p.z)<3 && Flat(road,p)<32) return true;
	}
	return false;
}
void Stations()
{
	const int count=gNumRestarts?std::clamp<int>(*gNumRestarts,0,8):0;
	for (int i=int(gStations.size());i<count;i++) {
		Station station; station.pos=gRestarts[i]; station.node=Nearest(station.pos);
		if (station.node>=0 && Flat(station.pos,C(gNodes[size_t(station.node)].pos))>180) station.node=-1;
		gStations.push_back(station);
	}
}
bool Replacement(uint64_t token,populationmodel::Kind kind,const CVector &origin,uint32_t now,CVector &departure)
{
	Stations();
	auto found=std::find_if(gOrders.begin(),gOrders.end(),[&](const Order &o) { return o.token==token; });
	if (found==gOrders.end()) {
		int station=-1; float best=std::numeric_limits<float>::max();
		for (size_t i=0;i<gStations.size();i++) {
			if (gStations[i].queue.Pending()>=8 || (kind==populationmodel::Kind::Officer && gStations[i].node<0)) continue;
			const float d=Flat(origin,gStations[i].pos);
			if (d<best) { best=d; station=int(i); }
		}
		if (station<0 || !gStations[size_t(station)].queue.Queue(token,kind,now)) { gLifecycle.full++; return false; }
		gOrders.push_back({token,station,now,kind});
		gLifecycle.queued[kind==populationmodel::Kind::Car]++;
		gStations[size_t(station)].queued[kind==populationmodel::Kind::Car]++;
		return false;
	}
	Station &station=gStations[size_t(found->station)];
	if (!station.queue.Ready(token,now)) return false;
	departure=station.pos;
	gLifecycle.departed[kind==populationmodel::Kind::Car]++;
	station.departed[kind==populationmodel::Kind::Car]++;
	station.queue.Collect(token);
	gOrders.erase(found);
	return true;
}

void PutOnPath(populationmodel::Agent &agent,int node)
{
	if (node<0 || size_t(node)>=gNodes.size() || gNodes[size_t(node)].links.empty()) { agent.lost=true; return; }
	agent.node=node; agent.next=gNodes[size_t(node)].links.front(); agent.along=0; agent.lost=false;
}
void Reset(bool release)
{
	if (release && api.PoolsGetPed) {
		for (populationmodel::World *world:{&gCiviliansWorld,&gPoliceWorld})
			for (auto &agent:world->Agents()) {
				uint8_t *ped=agent.handle>=0?api.PoolsGetPed(agent.handle):nullptr;
				if (!ped || PedFromMission(ped) || Attached(ped)) continue;
				// Visible/random people remain the game's responsibility when disabled.
				if (gCanDelete(ped) && !Visible(EntityPos(ped))) gRemovePed(ped);
			}
	}
	gCiviliansWorld=populationmodel::World{}; gPoliceWorld=populationmodel::World{};
	gCivResidents.clear(); gCopResidents.clear(); gNodes.clear(); gSuitable.clear(); gCopRoute.clear();
	gStations.clear(); gOrders.clear(); gRoads.clear(); gDistances.clear(); gToward.clear();
	gBuilt=false; gBuild=-1; gCursor=0; gLastTick=0; gLastTree=0;
	gLifecycle={}; gSpawnStats[0]={}; gSpawnStats[1]={};
}
bool BeginBuild()
{
	gCarCount=*reinterpret_cast<const int32_t *>(api.thePaths+kCars);
	gTotalCount=*reinterpret_cast<const int32_t *>(api.thePaths+kTotal);
	if (gCarCount<=0 || gTotalCount<=gCarCount || gTotalCount>kMaxNodes) return false;
	const size_t n=size_t(gTotalCount-gCarCount);
	gNodes.resize(n); gSuitable.assign(n,false); gCopRoute.assign(n,false);
	for (int i=0;i<gCarCount;i++) {
		if (*reinterpret_cast<const uint32_t *>(Node(i)+0x10)&kWater) continue;
		const CVector p=Position(i);
		gRoads[Cell(int(std::floor(p.x/50)),int(std::floor(p.y/50)))].push_back(p);
	}
	gBuild=gCarCount;
	return true;
}
void FinishBuild()
{
	// Tiny isolated graphs are often inaccessible lots; only connected public routes seed residents.
	std::vector<bool> seen(gNodes.size(),false);
	for (size_t start=0;start<gNodes.size();start++) {
		if (seen[start] || gNodes[start].links.empty()) continue;
		std::vector<int> component{int(start)}; seen[start]=true;
		float x0=gNodes[start].pos.x,x1=x0,y0=gNodes[start].pos.y,y1=y0;
		for (size_t at=0;at<component.size();at++) {
			const auto &node=gNodes[size_t(component[at])];
			x0=std::min(x0,node.pos.x); x1=std::max(x1,node.pos.x); y0=std::min(y0,node.pos.y); y1=std::max(y1,node.pos.y);
			for (int link:node.links) if (!seen[size_t(link)]) { seen[size_t(link)]=true; component.push_back(link); }
		}
		const bool suitable=component.size()>=12 && std::max(x1-x0,y1-y0)>=80;
		for (int node:component) {
			gSuitable[size_t(node)]=suitable;
			gCopRoute[size_t(node)]=suitable && std::max(x1-x0,y1-y0)>=160 && NearRoad(C(gNodes[size_t(node)].pos));
			if (!suitable) gNodes[size_t(node)].demand=0;
		}
	}
	for (auto &node:gNodes) {
		node.links.erase(std::remove_if(node.links.begin(),node.links.end(),[&](int link) { return !gSuitable[size_t(link)]; }),node.links.end());
	}
	auto policeNodes=gNodes;
	for (size_t i=0;i<policeNodes.size();i++)
		policeNodes[i].demand=gCopRoute[i] && Rate(int(i)+gCarCount)>0 ? 2.0f+std::sqrt(std::max(0.0f,gNodes[i].demand))*0.1f : 0;
	gCiviliansWorld.Init(gNodes,kCivilians,0,0x56434956u);
	gPoliceWorld.Init(std::move(policeNodes),0,kFootPolice,0x504f4c49u);
	gCivResidents.resize(gCiviliansWorld.Agents().size()); gCopResidents.resize(gPoliceWorld.Agents().size());
	gBuilt=!gCivResidents.empty() && !gCopResidents.empty();
	Stations();
	Log("population: %zu civilians and %zu foot patrols on %zu ped nodes, %zu shared stations; no player-relative origin",
	    gCivResidents.size(),gCopResidents.size(),gNodes.size(),gStations.size());
}
void Build()
{
	if (gBuild<0 && !BeginBuild()) return;
	for (int budget=0;budget<64 && gBuild<gTotalCount;budget++,gBuild++) {
		const int raw=gBuild; auto &node=gNodes[size_t(raw-gCarCount)]; node.pos=V(Position(raw));
		if (!LandPath(raw)) continue;
		const int first=*reinterpret_cast<const int16_t *>(Node(raw)+0xc);
		const int count=*reinterpret_cast<const uint32_t *>(Node(raw)+0x10)&15;
		if (first<0 || first+count>30600) continue; // connection table ends at 0x4ac18
		for (int k=0;k<count;k++) {
			const int linked=*reinterpret_cast<const uint16_t *>(api.thePaths+kConnections+2*size_t(first+k))&0x3fff;
			if (LandPath(linked) && linked!=raw) node.links.push_back(linked-gCarCount);
		}
		node.demand=float(Density(Position(raw)))*float(Rate(raw))/15;
	}
	if (gBuild==gTotalCount) { FinishBuild(); gBuild++; }
}

struct Local { int total=0,cops=0,civilians=0,occupants=0,bodies=0,free=0; std::vector<CVector> positions; };
Local Count(const CVector &him)
{
	Local out;
	const uint8_t *pool=gPedPool?*gPedPool:nullptr;
	if (!pool) return out;
	uint8_t *entries=*reinterpret_cast<uint8_t *const *>(pool);
	const uint8_t *flags=*reinterpret_cast<const uint8_t *const *>(pool+8);
	const int32_t count=*reinterpret_cast<const int32_t *>(pool+0x10);
	const uint8_t *player=api.FindPlayerPed();
	for (int i=0;i<count;i++) {
		if (flags[i]&0x80) { out.free++; continue; }
		const uint8_t *ped=entries+size_t(i)*kPedSize;
		if (Flat(EntityPos(ped),him)>160 || std::fabs(EntityPos(ped).z-him.z)>12) continue;
		out.total++; // Every local actor still consumes the shared physical budget.
		if (Health(ped)<=0) { out.bodies++; continue; }
		if (PedInVehicle(ped) || Attached(ped)) { out.occupants++; continue; }
		if (ped==player) continue;
		// Sidewalk demand and crowd spacing describe people on foot. Counting
		// passing drivers against pedDensity made a busy road suppress its walkers.
		out.cops+=PedType(ped)==6;
		out.civilians+=PedType(ped)!=6;
		out.positions.push_back(EntityPos(ped));
	}
	return out;
}
void GrowDispatch(const CVector &target,uint32_t now)
{
	if (now-gLastTree<1000) return;
	gLastTree=now;
	const int root=Nearest(target);
	gDistances.assign(gNodes.size(),1e9f); gToward.assign(gNodes.size(),-1);
	if (root<0) return;
	using Item=std::pair<float,int>;
	std::priority_queue<Item,std::vector<Item>,std::greater<Item>> open;
	gDistances[size_t(root)]=0; open.push({0,root});
	while (!open.empty()) {
		const auto [distance,at]=open.top(); open.pop();
		if (distance>gDistances[size_t(at)]) continue;
		for (int next:gNodes[size_t(at)].links) {
			if (!Open(at+gCarCount) || !Open(next+gCarCount)) continue;
			const float through=distance+Flat(C(gNodes[size_t(at)].pos),C(gNodes[size_t(next)].pos));
			if (through<gDistances[size_t(next)]) {
				gDistances[size_t(next)]=through; gToward[size_t(next)]=at; open.push({through,next});
			}
		}
	}
}
void Dispatch(uint32_t now)
{
	const heat::Model &wanted=wanted::Model();
	if (!wanted::Active() || (wanted.state()!=heat::State::Wanted && wanted.state()!=heat::State::Searching)) return;
	GrowDispatch(wanted::Target(),now);
	std::vector<std::pair<float,int>> closest;
	auto &agents=gPoliceWorld.Agents();
	for (size_t i=0;i<agents.size();i++) {
		auto &a=agents[i];
		if (a.handle>=0 || a.lost || a.node<0 || size_t(a.node)>=gDistances.size()) continue;
		closest.push_back({gDistances[size_t(a.node)],int(i)});
	}
	std::sort(closest.begin(),closest.end());
	const size_t count=std::min(closest.size(),size_t(std::clamp(wanted.stars()+2,3,8)));
	for (size_t i=0;i<count;i++) {
		auto &a=agents[size_t(closest[i].second)];
		if (a.along<0.12f && gToward[size_t(a.node)]>=0) {
			a.next=gToward[size_t(a.node)]; // choose at a junction, preserving virtual travel
		}
	}
}
void Travel(populationmodel::World &world,float dt)
{
	// Scripts open/close paths without changing the graph's size. Keep the city IDs and
	// positions; a closed edge waits in place instead of crossing it or reseeding agents.
	struct Previous { size_t index; populationrules::PathStep path; bool open; };
	std::vector<Previous> previous;
	auto &agents=world.Agents();
	for (size_t i=0;i<agents.size();i++) {
		const auto &a=agents[i];
		if (a.handle<0 && !a.lost && a.next>=0)
			previous.push_back({i,{a.node,a.next,a.along},Open(a.node+gCarCount) && Open(a.next+gCarCount)});
	}
	world.Tick(dt);
	for (const Previous &p:previous) {
		auto &a=agents[p.index];
		const auto path=populationrules::Travel(p.path,{a.node,a.next,a.along},p.open,
		    Open(a.node+gCarCount) && Open(a.next+gCarCount));
		a.node=path.node; a.next=path.next; a.along=path.along;
	}
}
void Follow(populationmodel::World &world,std::vector<Resident> &residents,uint32_t now,const CVector &him)
{
	auto &agents=world.Agents();
	for (size_t i=0;i<agents.size();i++) {
		auto &a=agents[i]; Resident &r=residents[i];
		uint8_t *ped=a.handle>=0?api.PoolsGetPed(a.handle):nullptr;
		if (ped && !populationrules::SameOwner(a.handle,gPedRef(ped),PedFromMission(ped),Attached(ped))) {
			a.handle=-1; a.lost=true; r.lostAt=now; gLifecycle.stale++;
			continue;
		}
		if (a.handle>=0 && !ped) {
			a.handle=-1;
			if (!r.wasDead) PutOnPath(a,Nearest(r.last));
		}
		if (ped) {
			r.last=EntityPos(ped);
			if (Health(ped)<=0 && !r.wasDead) { r.wasDead=true; r.lostAt=now; a.lost=true; gLifecycle.deaths++; }
			populationrules::Release release{Flat(r.last,him),Visible(r.last),ped[0x19a]==1,Attached(ped),PedInVehicle(ped),gCanDelete(ped)};
			if (populationrules::CanVirtualize(release)) {
				gRemovePed(ped); a.handle=-1; gLifecycle.virtualized++;
				if (!r.wasDead) PutOnPath(a,Nearest(r.last));
			}
		}
		if (!a.lost) continue;
		if (a.cop) {
			CVector station;
			if (Replacement(kOfficerToken|uint64_t(i),populationmodel::Kind::Officer,r.last,now,station)) {
				// The old dead body is the game's; its replacement starts at a station on a real route.
				a.handle=-1; PutOnPath(a,Nearest(station)); r=Resident{};
			}
		} else if (now-r.lostAt>60000) {
			a.handle=-1; PutOnPath(a,Nearest(r.last)); r=Resident{};
		}
	}
}
bool Ground(CVector &p)
{
	alignas(16) uint8_t point[0x30]{}; uint8_t *entity=nullptr;
	if (!api.ProcessVerticalLine({p.x,p.y,p.z+2},p.z-3,point,entity,true,false,false,true,false,false,nullptr)) return false;
	const CVector hit=*reinterpret_cast<const CVector *>(point),normal=*reinterpret_cast<const CVector *>(point+0x10);
	if (std::fabs(hit.z-p.z)>2 || normal.z<0.65f) return false;
	p.z=hit.z+1.0f;
	return !api.TestSphereAgainstWorld(p,0.38f,nullptr,true,true,true,true,false,false);
}
bool SpawnOne(populationmodel::World &world,std::vector<Resident> &residents,const CVector &him,const Local &local,uint32_t now)
{
	auto &agents=world.Agents();
	if (agents.empty()) return false;
	SpawnStats &stats=gSpawnStats[agents.front().cop];
	for (size_t tries=0;tries<agents.size();tries++) {
		const size_t index=(gCursor+tries)%agents.size(); auto &a=agents[index];
		if (a.handle>=0 || a.lost || a.node<0 || a.next<0 || size_t(a.node)>=gNodes.size()) continue;
		if (!gSuitable[size_t(a.node)] || !Open(a.node+gCarCount) || !Open(a.next+gCarCount) ||
		    !Rate(a.node+gCarCount) || !Rate(a.next+gCarCount) || (a.cop && !gCopRoute[size_t(a.node)])) { stats.path++; continue; }
		CVector p=C(world.Position(a)); const float distance=Flat(p,him);
		if (distance<32 || distance>145 || std::fabs(p.z-him.z)>8) continue;
		stats.near++;
		const int density=Density(p);
		const float weather=gRain?1.0f-0.65f*std::sqrt(std::clamp(*gRain,0.0f,1.0f)):1.0f;
		const float densityScale=gPedDensity?std::clamp(*gPedDensity,0.0f,3.0f):1.0f;
		const int civilianLimit=std::clamp(int(density*0.35f*weather*densityScale),0,44);
		const bool noPolice=gZoneFlags && (*gZoneFlags&kNoPolice);
		int nearby=0;
		for (const CVector &person:local.positions) nearby+=Flat(person,p)<10;
		populationrules::Spawn facts;
		facts.distance=distance; facts.cop=a.cop; facts.local=a.cop?local.cops:local.civilians;
		facts.limit=a.cop?(noPolice?0:6):civilianLimit;
		facts.nearby=nearby; facts.freeSlots=local.free;
		if (local.free<=8) { stats.pool++; continue; }
		if (local.total>=56 || facts.local>=facts.limit || nearby>=4) { stats.limit++; continue; }
		facts.visible=Visible(p);
		if (facts.visible) { stats.visible++; continue; }
		facts.path=gSuitable[size_t(a.node)]; facts.outdoors=*gArea==0;
		if (local.total>=56 || !populationrules::Eligible(facts)) continue;
		alignas(8) uint8_t info[68]; Info(p,info);
		int gang=-1;
		const uint32_t pick=((uint32_t(index)*1664525u+1013904223u)>>8)%1000;
		if (!a.cop && gChooseGang && pick>=*reinterpret_cast<const uint16_t *>(info+0x40)) {
			for (int k=0;k<9;k++)
				if (pick<*reinterpret_cast<const uint16_t *>(info + 0x2e + 2*k)) { gang=k; break; }
		}
		const int model=a.cop?1:gang>=0?gChooseGang(gang):gChooseCivilian(*reinterpret_cast<const uint16_t *>(info+0x42));
		if (model<0 || model>=6500 || !gModelInfo[model]) continue;
		facts.loaded=Loaded(model);
		if (!facts.loaded) { stats.model++; api.RequestModel(model,0); continue; }
		const int type=a.cop?6:gang>=0?7+gang:*reinterpret_cast<const int32_t *>(gModelInfo[model]+0x44);
		if (!a.cop && type!=4 && type!=5 && type!=18 && type!=20 && (type<7 || type>15)) continue;
		CVector from=Position(a.node+gCarCount),to=Position(a.next+gCarCount);
		from.z+=1; to.z+=1;
		facts.clear=api.GetIsLineOfSightClear(from,to,true,false,false,true,false,false,false);
		facts.ground=Ground(p);
		if (!populationrules::Eligible(facts)) { stats.geometry++; continue; }
		uint8_t *ped=gAddPed(type,a.cop?0:uint32_t(model),p,0);
		if (!ped) { stats.failed++; return false; }
		// No mission marker: the engine may cull this local representation normally.
		if (!gWander(ped,int8_t(index%8))) { stats.failed++; gRemovePed(ped); continue; }
		if (void *clump=*reinterpret_cast<void *const *>(ped+0x58); clump && gSetAlpha) gSetAlpha(clump,0);
		a.handle=gPedRef(ped); residents[index].last=p; residents[index].wasDead=false;
		gCursor=index+1;
		stats.spawned++;
		(void)now;
		return true;
	}
	return false;
}
}

bool Active()
{
	return Ready() && gArea && *gArea==0 && !*gCutscene &&
	       (!gAllRandomType || *gAllRandomType<=1);
}
bool Ready() { return traffic::OwnsPopulation(gReady,gConfig.worldPopulation) && gBuilt; }
bool ReinforceCar(uint64_t token,const CVector &origin,uint32_t now,CVector *departure)
{
	if (!Ready()) return false;
	CVector station;
	const bool ready=Replacement(token&~kOfficerToken,populationmodel::Kind::Car,origin,now,station);
	if (ready && departure) *departure=station;
	return ready;
}
std::vector<CVector> FootPatrols()
{
	std::vector<CVector> positions;
	if (!Ready()) return positions;
	for (const auto &a:gPoliceWorld.Agents()) if (a.handle<0 && !a.lost) positions.push_back(C(gPoliceWorld.Position(a)));
	return positions;
}
void Frame()
{
	if (!gReady) return;
	const uint32_t now=*api.timeMs;
	if (gLastTime && (now<gLastTime || now-gLastTime>10000)) Reset(false);
	gLastTime=now;
	if (!traffic::OwnsPopulation(gReady,gConfig.worldPopulation)) {
		if (gEnabled) Reset(true);
		gEnabled=false; return;
	}
	gEnabled=true;
	if (!api.FindPlayerPed() || *gCutscene) return;
	if (!gBuilt) { if (*gArea==0) Build(); return; }
	if (now-gLastTick<250) return;
	const float dt=std::min(1.0f,float(now-gLastTick)/1000); gLastTick=now;
	uint8_t *player=api.FindPlayerPed();
	const CVector him=PedInVehicle(player)?EntityPos(PedVehicle(player)):EntityPos(player);
	Follow(gCiviliansWorld,gCivResidents,now,him); Follow(gPoliceWorld,gCopResidents,now,him);
	Dispatch(now);
	Travel(gCiviliansWorld,dt); Travel(gPoliceWorld,dt);
	if (!Active()) return;
	Local local=Count(him);
	// A cop spawn previously consumed the entire tick, suppressing civilian
	// model requests as well as materialization. Give the two city populations
	// independent bounded opportunities; recount each successful native AddPed
	// so the shared pool, crowd and local limits still hold within this tick.
	for (int budget=0;budget<2;budget++) {
		if (!SpawnOne(gCiviliansWorld,gCivResidents,him,local,now)) break;
		local=Count(him);
	}
	if (SpawnOne(gPoliceWorld,gCopResidents,him,local,now)) local=Count(him);
	if (now-gLogAt>30000) {
		gLogAt=now; size_t pending=0;
		for (const Station &station:gStations) pending+=station.queue.Pending();
		uint32_t oldest=0;
		for (const Order &order:gOrders) oldest=std::max(oldest,now-order.queuedAt);
		Log("population: local foot %d civilians/%d cops, %d occupants/%d bodies (%d total), pool %d free; %zu replacements queued, oldest %us; "
		    "lifecycle queued foot/car %llu/%llu departed %llu/%llu full %llu, virtualized %llu stale %llu dead %llu",
		    local.civilians,local.cops,local.occupants,local.bodies,local.total,local.free,pending,oldest/1000,
		    (unsigned long long)gLifecycle.queued[0],(unsigned long long)gLifecycle.queued[1],
		    (unsigned long long)gLifecycle.departed[0],(unsigned long long)gLifecycle.departed[1],
		    (unsigned long long)gLifecycle.full,(unsigned long long)gLifecycle.virtualized,
		    (unsigned long long)gLifecycle.stale,(unsigned long long)gLifecycle.deaths);
		for (int cop=0;cop<2;cop++) {
			const SpawnStats &s=gSpawnStats[cop];
			Log("population: %s materialization near %llu spawned %llu, gates path %llu visible %llu density/crowd %llu "
			    "pool %llu model %llu geometry %llu native %llu (last sample)",cop?"foot police":"civilian",
			    (unsigned long long)s.near,(unsigned long long)s.spawned,(unsigned long long)s.path,
			    (unsigned long long)s.visible,(unsigned long long)s.limit,(unsigned long long)s.pool,
			    (unsigned long long)s.model,(unsigned long long)s.geometry,(unsigned long long)s.failed);
			gSpawnStats[cop]={};
		}
		for (size_t i=0;i<gStations.size();i++) {
			size_t foot=0,cars=0; uint32_t age=0;
			for (const Order &order:gOrders) if (order.station==int(i)) {
				(order.kind==populationmodel::Kind::Officer?foot:cars)++; age=std::max(age,now-order.queuedAt);
			}
			const Station &station=gStations[i];
			Log("population: station %zu pending foot/car %zu/%zu oldest %us; queued %llu/%llu departed %llu/%llu",
			    i,foot,cars,age/1000,(unsigned long long)station.queued[0],(unsigned long long)station.queued[1],
			    (unsigned long long)station.departed[0],(unsigned long long)station.departed[1]);
		}
	}
}
bool Install()
{
	auto sym=[](const char *name) { return dlsym(RTLD_MAIN_ONLY,name); };
	gPedPool=static_cast<uint8_t **>(sym("_ZN6CPools11ms_pPedPoolE"));
	gModelInfo=static_cast<uint8_t **>(sym("_ZN10CModelInfo16ms_modelInfoPtrsE"));
	gStreamInfo=static_cast<uint8_t *>(sym("_ZN10CStreaming16ms_aInfoForModelE"));
	gArea=static_cast<int32_t *>(sym("_ZN5CGame8currAreaE"));
	gCutscene=static_cast<bool *>(sym("_ZN12CCutsceneMgr10ms_runningE"));
	gAllRandomType=static_cast<int32_t *>(sym("_ZN11CPopulation23m_AllRandomPedsThisTypeE"));
	gZoneFlags=static_cast<uint32_t *>(sym("_ZN10CCullZones19CurrentFlags_PlayerE"));
	gRestarts=static_cast<CVector *>(sym("_ZN8CRestart19PoliceRestartPointsE"));
	gNumRestarts=static_cast<int16_t *>(sym("_ZN8CRestart22NumberOfPoliceRestartsE"));
	gRain=static_cast<float *>(sym("_ZN8CWeather4RainE"));
	gPedDensity=static_cast<float *>(sym("_ZN11CPopulation20PedDensityMultiplierE"));
	gZoneInfo=reinterpret_cast<decltype(gZoneInfo)>(sym("_ZN9CTheZones23GetZoneInfoForTimeOfDayEPK7CVectorP9CZoneInfo"));
	gChooseCivilian=reinterpret_cast<decltype(gChooseCivilian)>(sym("_ZN11CPopulation24ChooseCivilianOccupationEi"));
	gChooseGang=reinterpret_cast<decltype(gChooseGang)>(sym("_ZN11CPopulation20ChooseGangOccupationEi"));
	gAddPed=reinterpret_cast<decltype(gAddPed)>(sym("_ZN11CPopulation6AddPedE8ePedTypejRK7CVectori"));
	gRemovePed=reinterpret_cast<decltype(gRemovePed)>(sym("_ZN11CPopulation9RemovePedEP4CPed"));
	gWander=reinterpret_cast<decltype(gWander)>(sym("_ZN4CPed13SetWanderPathEa"));
	gCanDelete=reinterpret_cast<decltype(gCanDelete)>(sym("_ZN4CPed12CanBeDeletedEv"));
	gSphereVisible=reinterpret_cast<decltype(gSphereVisible)>(sym("_ZN7CCamera15IsSphereVisibleERK7CVectorf"));
	gSetAlpha=reinterpret_cast<decltype(gSetAlpha)>(sym("_ZN18CVisibilityPlugins13SetClumpAlphaEPN2rw5ClumpEi"));
	gPedRef=reinterpret_cast<decltype(gPedRef)>(sym("_ZN6CPools9GetPedRefEP4CPed"));
	gReady=gPedPool && gModelInfo && gStreamInfo && gArea && gCutscene && gZoneInfo && gChooseCivilian &&
	       gAddPed && gRemovePed && gWander && gCanDelete && gSphereVisible && gPedRef && gRestarts && gNumRestarts &&
	       api.thePaths && api.timeMs && api.timeStep && api.PoolsGetPed && api.RequestModel &&
	       api.ProcessVerticalLine && api.TestSphereAgainstWorld && api.GetIsLineOfSightClear;
	if (!gReady) Log("population: required path/pool/spawn APIs missing; vanilla outdoor population retained");
	else if (!traffic::OwnsPopulation(gReady,true))
		Log("population: outdoor generation interception missing; vanilla population retained");
	return traffic::OwnsPopulation(gReady,true);
}
}
