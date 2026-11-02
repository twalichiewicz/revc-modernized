// Exact macVC control, collision and RT-candidate seams. Native calls are replayed
// against in-memory entities; no running game or save files are touched.
#include <dlfcn.h>
static void *HeliSymbol(void *, const char *);
#define dlsym HeliSymbol
#include "../src/heli.cpp"
#undef dlsym
#include "../src/nativebullet.h"
#include <array>
#include <cstdio>

namespace game { Api api{}; }
Config gConfig;
void Log(const char *, ...) {}
namespace wanted { bool Active(){return false;} bool OnHim(){return true;} CVector Target(){return {};} }
namespace hunter { void Update(uint32_t){} }
static float gDaylight;
namespace shaders {
bool Ready(){return true;} Sky CurrentSky(){Sky s{};s.daylight=gDaylight;return s;}
CVector CameraPosition(){return {0,0,0};} SavedStates::SavedStates():values_{}{} SavedStates::~SavedStates(){(void)values_[0];}
}
static int gFailures,gNativeControl,gNativePreRender,gPushed,gNativeBreakups;
static uint32_t gNow=1000;
static float gStep=1;
alignas(16) static uint8_t gHeli[0x430]{},gOther[0x430]{},gPlayer[0x900]{};
static uint8_t *gAircraft[4]{gHeli,nullptr,nullptr,nullptr}, *gIgnore;
static heli::PointLight gLights[64];
static int16_t gNumLights;
static std::array<float,17> gCandidate;
static void (*gRtEmitter)(const void *);
static bool gWall;
static int gHandle=17,gOtherHandle=21;
static uint32_t gReturnOffset=0x16a55c;
static CVector gDrawnLens{},gDrawnBeam{};
static float gWallX=30;
static CVector gCorona;
static uint8_t gCoronaRed;
static uint8_t *gImpactVictim;
static unsigned gImpacts;
static bool gShotBlocked;
alignas(8) static std::array<uint8_t,nativebullet::kCount*nativebullet::kStride> gBullets{};
static bool gDeferredSight;
static float gDeferredHitT=.99f;
static unsigned gDeferredDamage,gLegacySniperCalls,gBulletUpdates;
static bool gRecycleBullet;
static void NativeBulletUpdate();
static void (*gBulletUpdate)()=NativeBulletUpdate;
#define CHECK(c) do {if(!(c)){std::printf("FAIL %d: %s\n",__LINE__,#c);++gFailures;}}while(0)
static bool Near(float a,float b){return std::fabs(a-b)<0.002f;}
static uint8_t *Player(){return gPlayer;}
static int Ref(uint8_t *h){return h==gHeli?gHandle:gOtherHandle;}
static void Noop(uint8_t *){}
static void Matrix(void *){}
static bool ShotClear(const CVector &,const CVector &,bool,bool,bool,bool,bool,bool,bool){return !gShotBlocked;}
static void NativeImpact(uint8_t *,uint8_t *,uint8_t *victim,CVector *,CVector *,uint8_t *,CVector2D){gImpactVictim=victim;++gImpacts;}
static void NativePreRender(uint8_t *){++gNativePreRender;}
static void NativeUpdate()
{
	if(gHeli[0x390]==3 && gNow+7000>*reinterpret_cast<uint32_t *>(gHeli+0x39c))++gNativeBreakups;
}
static void State(int,void *){}
static int StateGet(int,void *out){*static_cast<void **>(out)=nullptr;return 1;}
static int End(){return 1;}
static int Draw(int,uint16_t *,int){return 1;}
static void *Transform(heli::Im3DVertex *v,uint32_t count,float *m,uint32_t flags)
{
	if(!flags && count==42) {
		CVector local{};
		for(unsigned i=0;i<count;++i){local.x+=v[i].x/count;local.y+=v[i].y/count;local.z+=v[i].z/count;}
		gDrawnLens={m[12]+local.x*m[0]+local.y*m[4]+local.z*m[8],
		            m[13]+local.x*m[1]+local.y*m[5]+local.z*m[9],
		            m[14]+local.x*m[2]+local.y*m[6]+local.z*m[10]};
	} else if(!flags && count==56) {
		gDrawnBeam={};
		for(unsigned i=0;i<count;i+=4){gDrawnBeam.x+=(v[i].x+v[i+3].x)/28;gDrawnBeam.y+=(v[i].y+v[i+3].y)/28;gDrawnBeam.z+=(v[i].z+v[i+3].z)/28;}
	}
	return v;
}
static void Capture(const void *c){std::memcpy(gCandidate.data(),c,0x44);++gPushed;}
static bool Sight(const CVector &from,const CVector &to,uint8_t *point,uint8_t *&entity,bool,bool,bool,bool,bool,bool,bool,bool)
{
	if(gDeferredSight){
		*reinterpret_cast<CVector *>(point)={from.x+(to.x-from.x)*gDeferredHitT,from.y+(to.y-from.y)*gDeferredHitT,from.z+(to.z-from.z)*gDeferredHitT};
		*reinterpret_cast<CVector *>(point+0x10)={1,0,0};entity=gOther;return true;
	}
	if(!gWall || !(from.x<gWallX && to.x>=gWallX))return false;
	const float t=(gWallX-from.x)/(to.x-from.x);
	*reinterpret_cast<CVector *>(point)={gWallX,from.y+(to.y-from.y)*t,from.z+(to.z-from.z)*t};
	*reinterpret_cast<CVector *>(point+0x10)={-1,0,0};entity=gOther;return true;
}
static void NativeBulletUpdate()
{
	++gBulletUpdates;
	for(size_t i=0;i<nativebullet::kCount;++i){
		const auto bullet=nativebullet::Read(gBullets.data(),i);if(bullet.active!=1)continue;
		CVector from{bullet.position.x,bullet.position.y,bullet.position.z};
		CVector to{from.x+bullet.velocity.x*gStep*.5f,from.y+bullet.velocity.y*gStep*.5f,from.z+bullet.velocity.z*gStep*.5f};
		alignas(16) uint8_t point[0x30]{};uint8_t *victim=nullptr;
		if(Sight(from,to,point,victim,true,true,true,true,true,false,false,true)){
			// macVC applies inline native damage before TestSniperCollision and
			// never dispatches CWeapon::DoBulletImpact for this record.
			++gDeferredDamage;gImpactVictim=victim;gBullets[i*nativebullet::kStride+0x14]=0;
		}
		++gLegacySniperCalls;
	}
}
static uint8_t *Sphere(CVector at,float r,uint8_t *,bool,bool,bool,bool,bool,bool)
{return gWall && at.x+r>gWallX?gOther:nullptr;}
static void NativeControl(uint8_t *h)
{
	++gNativeControl;
	// Native ProcessControl writes speed, then integrates position before it returns.
	// Repeated pursuit attempts must not erase the post-control escape/collision result.
	*reinterpret_cast<CVector *>(h+0x84)={0.8f,0,0};
	auto &p=*reinterpret_cast<CVector *>(h+0x38);p.x+=0.8f*gStep;
}
static void Corona(uint32_t id,uint8_t red,uint8_t,uint8_t,uint8_t,const CVector &p,float,float,uint8_t,int8_t,uint8_t,uint8_t,uint8_t,float,bool,float)
{if(id==uint32_t(reinterpret_cast<uintptr_t>(gHeli))){gCorona=p;gCoronaRed=red;}}
static void *Particle(int32_t,const CVector &,const CVector &,uint8_t *,float,int32_t,int32_t,int32_t,int32_t)
{
	if(gRecycleBullet){
		gRecycleBullet=false;auto record=nativebullet::Read(gBullets.data(),0);record.creator=gOther;record.position={500,500,500};
		std::memcpy(gBullets.data(),&record,sizeof record);
	}
	return nullptr;
}
// The optimized native candidate push takes its 0x44-byte argument in x0 only.
// A real arm64 call edge supplies a return PC just as CRTShadows::Render does.
extern "C" void HeliEmit(const void *,void (*)(const void *));
extern "C" const char HeliEmitReturn;
asm(".text\n.p2align 2\n.globl _HeliEmit\n_HeliEmit:\n"
    "stp x29,x30,[sp,#-16]!\nmov x29,sp\nblr x1\n"
    ".globl _HeliEmitReturn\n_HeliEmitReturn:\nldp x29,x30,[sp],#16\nret\n");
namespace hooks {
const void *ImageBase(){return reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(&HeliEmitReturn)-gReturnOffset);}
bool IsCallTo(uint32_t off,const char *){return off==0x16a55c || off==0x16a6b4 || off==0x16a80c || off==0x16a964;}
void *Chain(const char *name,void *fn)
{
	if(!std::strcmp(name,"_ZN11CBulletInfo6UpdateEv")){
		auto previous=gBulletUpdate;gBulletUpdate=reinterpret_cast<decltype(gBulletUpdate)>(fn);return reinterpret_cast<void *>(previous);
	}
	return nullptr;
}
void *Install(const char *name,void *fn)
{
	if(std::strstr(name,"9Candidate")){gRtEmitter=reinterpret_cast<decltype(gRtEmitter)>(fn);return reinterpret_cast<void *>(Capture);}
	if(std::strstr(name,"ProcessControl"))return reinterpret_cast<void *>(NativeControl);
	if(std::strstr(name,"PreRenderAlways"))return reinterpret_cast<void *>(NativePreRender);
	if(std::strstr(name,"DoBulletImpact"))return reinterpret_cast<void *>(NativeImpact);
	return reinterpret_cast<void *>(Noop);
}
}
static void *HeliSymbol(void *,const char *name)
{
	if(!std::strcmp(name,"gaBulletInfo"))return gBullets.data();
	if(!std::strcmp(name,"_ZN5CHeli6pHelisE"))return gAircraft;
	if(std::strstr(name,"GetVehicleRef"))return reinterpret_cast<void *>(Ref);
	if(std::strstr(name,"RemoveAndAdd") || std::strstr(name,"UpdateRwFrame"))return reinterpret_cast<void *>(Noop);
	if(std::strstr(name,"UpdateRWEv"))return reinterpret_cast<void *>(Matrix);
	if(std::strstr(name,"AddParticle"))return reinterpret_cast<void *>(Particle);
	if(std::strstr(name,"RegisterCorona"))return reinterpret_cast<void *>(Corona);
	if(std::strstr(name,"CPointLights7aLights"))return gLights;
	if(std::strstr(name,"CPointLights9NumLights"))return &gNumLights;
	return nullptr;
}
static void Reset(float x=0)
{
	std::memset(gHeli,0,sizeof gHeli);std::memset(gPlayer,0,sizeof gPlayer);
	*reinterpret_cast<CVector *>(gHeli+0x08)={1,0,0};*reinterpret_cast<CVector *>(gHeli+0x18)={0,1,0};
	*reinterpret_cast<CVector *>(gHeli+0x28)={0,0,1};*reinterpret_cast<CVector *>(gHeli+0x38)={x,0,40};
	*reinterpret_cast<int16_t *>(gHeli+0x6a)=165;*reinterpret_cast<float *>(gHeli+0x3ac)=1;
	*reinterpret_cast<float *>(gHeli+0x394)=0;*reinterpret_cast<float *>(gHeli+0x398)=30;
	heli::gWear[0]=heli::Wear{};gNumLights=0;gPushed=0;gDaylight=0;gWall=false;gAircraft[0]=gHeli;gShotBlocked=false;gImpacts=0;gImpactVictim=nullptr;
	gBullets.fill(0);gDeferredSight=false;gDeferredHitT=.99f;gDeferredDamage=0;gLegacySniperCalls=0;gBulletUpdates=0;gRecycleBullet=false;
}
static void EmitNativeCone()
{
	// Fixture bytes produced by CRTShadows::Render 0x16a418..558. The old apex
	// is the fuselage root minus one metre. The immutable record is copied.
	std::array<float,17> c{game::EntityPos(gHeli).x,0,39,110,3.2f,0,0,-1,2,1,.96f,.87f,1,0,.987f,.08f,-.5f};
	const auto original=c;
	HeliEmit(c.data(),gRtEmitter?gRtEmitter:Capture);
	CHECK(c==original);
}
int main()
{
	game::api.timeMs=&gNow;game::api.timeStep=&gStep;game::api.FindPlayerPed=Player;
	game::api.ProcessLineOfSight=Sight;game::api.TestSphereAgainstWorld=Sphere;game::api.ignoreEntity=&gIgnore;game::api.GetIsLineOfSightClear=ShotClear;
	gConfig.heliDamage=true;gConfig.heliGunners=false;CHECK(heli::Install());
	Reset();heli::Hook_PreRenderAlways(gHeli);EmitNativeCone();
	CHECK(gPushed==1 && !Near(gCandidate[0],0) && gCandidate[1]>2 && gCandidate[2]>39);
	CHECK(Near(gCandidate[0],gCorona.x) && Near(gCandidate[1],gCorona.y) && Near(gCandidate[2],gCorona.z));
	CHECK(gNativePreRender==0);CHECK(gNumLights==1);
	heli::gIm3DTransform=Transform;heli::gIm3DRenderIndexed=Draw;heli::gIm3DEnd=End;heli::gStateGet=StateGet;
	heli::orig_Render=Noop;game::api.RwRenderStateSet=State;
	heli::Hook_Render(gHeli);
	CHECK(Near(gDrawnLens.x,gCandidate[0]) && Near(gDrawnLens.y,gCandidate[1]) && Near(gDrawnLens.z,gCandidate[2]));
	CHECK(Near(gDrawnBeam.x,gCandidate[0]) && Near(gDrawnBeam.y,gCandidate[1]) && Near(gDrawnBeam.z,gCandidate[2]));
	CHECK(Near(gLights[0].position.x,gCandidate[0]) && Near(gLights[0].position.y,gCandidate[1]));
	// Render may run after control changed the bank/heading; the RT source and
	// actual submitted mesh must agree even without another PreRenderAlways.
	*reinterpret_cast<CVector *>(gHeli+0x08)={0,1,0};*reinterpret_cast<CVector *>(gHeli+0x18)={-1,0,0};
	gPushed=0;EmitNativeCone();heli::Hook_Render(gHeli);
	CHECK(Near(gDrawnLens.x,gCandidate[0]) && Near(gDrawnLens.y,gCandidate[1]) && Near(gDrawnLens.z,gCandidate[2]));
	// A direct accepted bullet through the visible glass is enough to break a
	// fragile lamp. It must not need repeated confirmed impacts or hull damage.
	Reset();*reinterpret_cast<float *>(gHeli+0x394)=-.35f;*reinterpret_cast<float *>(gHeli+0x398)=2.25f;
	heli::Hook_PreRenderAlways(gHeli);heli::Hook_Render(gHeli);
	CVector lensFrom{gDrawnLens.x,gDrawnLens.y,gDrawnLens.z-20},lensTo{gDrawnLens.x,gDrawnLens.y,gDrawnLens.z+1};
	alignas(16) uint8_t lensPoint[0x30]{};uint32_t lensWeapon=26;
	heli::Hook_DoBulletImpact(reinterpret_cast<uint8_t *>(&lensWeapon),gPlayer,nullptr,&lensFrom,&lensTo,lensPoint,{});
	CHECK(heli::LightOut(0) && heli::gWear[0].lampHits==1 && *reinterpret_cast<int32_t *>(gHeli+0x3f4)==0);
	// FireSniper creates a deferred record. Its Update bypasses DoBulletImpact,
	// damages a world victim inline, then invokes the old proximity-only test.
	// The installed Update chain must catch the actual visible glass FIRST.
	auto deferred=[&](int type,CVector from,CVector to,uint8_t *creator=gPlayer){
		nativebullet::Record record{};record.type=type;record.creator=creator;record.deadline=float(gNow+1000);record.active=1;
		record.position={from.x,from.y,from.z};record.velocity={(to.x-from.x)*2/gStep,(to.y-from.y)*2/gStep,(to.z-from.z)*2/gStep};record.damage=125;
		record.reserved=0xabcdef12;record.padding[1]=0x76;record.trailing[5]=0x98;
		std::memcpy(gBullets.data(),&record,sizeof record);return record;
	};
	for(int type:{28,29}){
		Reset();*reinterpret_cast<float *>(gHeli+0x394)=-.35f;*reinterpret_cast<float *>(gHeli+0x398)=2.25f;
		heli::Hook_PreRenderAlways(gHeli);heli::Hook_Render(gHeli);
		CVector from{gDrawnLens.x,gDrawnLens.y,gDrawnLens.z-20},to{gDrawnLens.x,gDrawnLens.y,gDrawnLens.z+1};
		auto before=deferred(type,from,to);gDeferredSight=true;gIgnore=gOther;gBulletUpdate();
		CHECK(heli::LightOut(0) && heli::gWear[0].lampHits==1 && gDeferredDamage==0 && gLegacySniperCalls==0);
		CHECK(gBulletUpdates==1 && gImpacts==0 && gIgnore==gOther && *reinterpret_cast<int32_t *>(gHeli+0x3f4)==0);
		before.active=0;CHECK(!std::memcmp(gBullets.data(),&before,sizeof before));
		// A native building/vehicle/ped before the lamp keeps the whole hit.
		Reset();*reinterpret_cast<float *>(gHeli+0x394)=-.35f;*reinterpret_cast<float *>(gHeli+0x398)=2.25f;
		deferred(type,from,to);gDeferredSight=true;gDeferredHitT=.25f;gBulletUpdate();
		CHECK(!heli::LightOut(0) && gDeferredDamage==1 && gImpactVictim==gOther && gLegacySniperCalls==1);
		// Shooting down through the cabin reaches body/rotor before glass.
		Reset();*reinterpret_cast<float *>(gHeli+0x394)=-.35f;*reinterpret_cast<float *>(gHeli+0x398)=2.25f;
		deferred(type,to,from);gBulletUpdate();CHECK(!heli::LightOut(0) && gLegacySniperCalls==1 && gImpacts==0);
		// NPC deferred rounds retain native ownership.
		deferred(type,from,to,gOther);gBulletUpdate();CHECK(!heli::LightOut(0) && gLegacySniperCalls==2 && gImpacts==0);
		// One damage callback may recycle the row for another bullet. It must
		// survive instead of being deactivated by our captured old contact.
		Reset();*reinterpret_cast<float *>(gHeli+0x394)=-.35f;*reinterpret_cast<float *>(gHeli+0x398)=2.25f;
		deferred(type,from,to);gRecycleBullet=true;gBulletUpdate();
		CHECK(heli::LightOut(0) && nativebullet::Read(gBullets.data(),0).active==1 && nativebullet::Read(gBullets.data(),0).creator==gOther);
		// Disabled part damage retains the complete deferred native path.
		Reset();deferred(type,from,to);gConfig.heliDamage=false;gBulletUpdate();gConfig.heliDamage=true;
		CHECK(!heli::LightOut(0) && gLegacySniperCalls==1 && gImpacts==0);
	}
	for(float step:{.25f,2.0f}){
		Reset();gStep=step;*reinterpret_cast<float *>(gHeli+0x394)=-.35f;*reinterpret_cast<float *>(gHeli+0x398)=2.25f;
		heli::Hook_PreRenderAlways(gHeli);heli::Hook_Render(gHeli);
		CVector from{gDrawnLens.x,gDrawnLens.y,gDrawnLens.z-20},to{gDrawnLens.x,gDrawnLens.y,gDrawnLens.z+1};
		auto before=deferred(28,from,to);std::memcpy(gBullets.data()+99*nativebullet::kStride,gBullets.data(),nativebullet::kStride);
		std::memset(gBullets.data(),0,nativebullet::kStride);gBulletUpdate();before.active=0;
		CHECK(heli::LightOut(0) && gLegacySniperCalls==0 && !std::memcmp(gBullets.data()+99*nativebullet::kStride,&before,sizeof before));
	}
	gStep=1;
	// Replay a real native DoBulletImpact miss ray through the front barrel's
	// VISIBLE submitted side, close to the bezel. This part lies beyond the old
	// middle sphere while remaining inside the actual 0.34 m housing.
	Reset();*reinterpret_cast<float *>(gHeli+0x394)=-.35f;*reinterpret_cast<float *>(gHeli+0x398)=2.25f;
	heli::Hook_PreRenderAlways(gHeli);heli::Hook_Render(gHeli);
	const auto front=heli::gLit.v[1],back=heli::gLit.v[0];
	const CVector barrel=heli::World(gHeli,{front.x*.98f+back.x*.02f,front.y*.98f+back.y*.02f,front.z*.98f+back.z*.02f});
	CVector shotFrom{barrel.x+20,barrel.y,barrel.z},shotTo{barrel.x-1,barrel.y,barrel.z};
	alignas(16) uint8_t colPoint[0x30]{};uint32_t weapon=26;
	// A native closer building/vehicle hit must keep ownership of the shot.
	*reinterpret_cast<CVector *>(colPoint)={shotFrom.x-5,shotFrom.y,shotFrom.z};
	heli::Hook_DoBulletImpact(reinterpret_cast<uint8_t *>(&weapon),gPlayer,gOther,&shotFrom,&shotTo,colPoint,{});
	CHECK(!heli::LightOut(0) && heli::gWear[0].lampHits==0 && gImpactVictim==gOther);
	gShotBlocked=true;
	for(int i=0;i<3;++i)heli::Hook_DoBulletImpact(reinterpret_cast<uint8_t *>(&weapon),gPlayer,nullptr,&shotFrom,&shotTo,colPoint,{});
	CHECK(!heli::LightOut(0) && heli::gWear[0].lampHits==0);
	gShotBlocked=false;
	heli::Hook_DoBulletImpact(reinterpret_cast<uint8_t *>(&weapon),gPlayer,nullptr,&shotFrom,&shotTo,colPoint,{});
	CHECK(heli::LightOut(0) && gImpactVictim==gHeli);
	gNumLights=0;gPushed=0;heli::Hook_PreRenderAlways(gHeli);EmitNativeCone();heli::Hook_Render(gHeli);
	CHECK(gPushed==0 && gNumLights==0 && gCoronaRed==0 && heli::gGlow.i.empty());
	// Native controller intensity updates cannot relight a lamp after its glass breaks.
	*reinterpret_cast<float *>(gHeli+0x3ac)=1;gNow+=20;gPushed=0;EmitNativeCone();CHECK(gPushed==0);
	// Two visible aircraft on the same shot line: the nearer can receives the
	// hits, its own RT slot stays dark, and the untouched can stays illuminated.
	Reset();*reinterpret_cast<float *>(gHeli+0x394)=-.35f;*reinterpret_cast<float *>(gHeli+0x398)=2.25f;
	std::memcpy(gOther,gHeli,sizeof gOther);*reinterpret_cast<CVector *>(gOther+0x38)={10,0,40};
	*reinterpret_cast<float *>(gOther+0x394)=9.65f;gAircraft[1]=gOther;heli::gWear[1]=heli::Wear{};
	heli::Hook_DoBulletImpact(reinterpret_cast<uint8_t *>(&weapon),gPlayer,nullptr,&shotFrom,&shotTo,colPoint,{});
	CHECK(heli::LightOut(1) && !heli::LightOut(0) && gImpactVictim==gOther);
	gReturnOffset=0x16a6b4;gPushed=0;EmitNativeCone();CHECK(gPushed==0);
	gReturnOffset=0x16a55c;gPushed=0;EmitNativeCone();CHECK(gPushed==1);
	// Reusing the second pool slot creates a fresh lamp; ordinary updates do not.
	*reinterpret_cast<float *>(gOther+0x3ac)=1;gReturnOffset=0x16a6b4;gPushed=0;EmitNativeCone();CHECK(gPushed==0);
	++gOtherHandle;gPushed=0;EmitNativeCone();CHECK(gPushed==1 && !heli::LightOut(1));
	// Deferred rifles resolve the same two-aircraft nearest mesh ordering.
	heli::gWear[0]=heli::Wear{};heli::gWear[1]=heli::Wear{};gBullets.fill(0);
	deferred(28,shotFrom,shotTo);gBulletUpdate();
	CHECK(heli::LightOut(1) && !heli::LightOut(0) && gLegacySniperCalls==0);
	gAircraft[1]=nullptr;gReturnOffset=0x16a55c;
	Reset();
	// Daylight or a shot-out lamp cannot leave a second native volumetric cone.
	gDaylight=1;gPushed=0;EmitNativeCone();CHECK(gPushed==0);
	gDaylight=0;heli::gWear[0].lampOut=true;EmitNativeCone();CHECK(gPushed==0);
	Reset();gWall=true;gWallX=30;
	for(int i=0;i<20;++i){gNow+=20;heli::Hook_ProcessControl(gHeli);}
	// A 40 m/s approach sees a tall wall ahead and changes path before touching it.
	CHECK(game::EntityPos(gHeli).x<10);
	CHECK(std::fabs(game::EntityPos(gHeli).y)>1 || game::EntityPos(gHeli).z>41);
	CHECK(heli::gWear[0].crash.phase==aircraft::Phase::Flying);
	// An unavoidable actual rotor/body impact at speed is fatal, even while flying.
	Reset(23.8f);gWall=true;gWallX=30;gNow+=20;heli::Hook_ProcessControl(gHeli);
	CHECK(heli::gWear[0].crash.phase==aircraft::Phase::Impacted);
	CHECK(gHeli[0x390]==3 && *reinterpret_cast<uint32_t *>(gHeli+0x39c)<gNow);
	CHECK(game::EntityPos(gHeli).x<24.4f);
	// World streaming can introduce solid geometry along an active escape. That
	// planned 12 m/s movement must also detect the actual physical strike.
	Reset(24.2f);gWall=true;gWallX=30;gNow+=20;
	heli::WearOf(gHeli)->escape={1,0,0};heli::gWear[0].escapeUntil=gNow+1000;
	heli::Hook_ProcessControl(gHeli);CHECK(heli::gWear[0].crash.phase==aircraft::Phase::Impacted);
	// Crew/weapon failures retain momentum and suppress the native airborne timer
	// until a swept contact enables breakup. The native controller cannot undo it.
	Reset();*reinterpret_cast<CVector *>(gHeli+0x84)={.2f,0,0};
	auto *wear=heli::WearOf(gHeli);heli::BeginCrash(gHeli,*wear,aircraft::Failure::Pilot);
	heli::orig_UpdateHelis=NativeUpdate;gNativeBreakups=0;
	heli::Hook_UpdateHelis();CHECK(gNativeBreakups==0 && gHeli[0x390]==3);
	const int controls=gNativeControl;gNow+=20;heli::Hook_ProcessControl(gHeli);
	CHECK(gNativeControl==controls && game::EntityPos(gHeli).z<40 && game::EntityPos(gHeli).x>0);
	CHECK(wear->crash.phase==aircraft::Phase::Descending && wear->crash.cause==aircraft::Failure::Pilot);
	wear->crash.phase=aircraft::Phase::Impacted;*reinterpret_cast<uint32_t *>(gHeli+0x39c)=gNow-1;
	heli::Hook_UpdateHelis();CHECK(gNativeBreakups==1);
	// Feature flags, mission aircraft and later slots preserve the native path.
	Reset();gConfig.heliDamage=false;gPushed=0;EmitNativeCone();CHECK(gPushed==1 && Near(gCandidate[0],0));
	heli::Hook_ProcessControl(gHeli);CHECK(Near(game::EntityPos(gHeli).x,.8f));
	gConfig.heliDamage=true;gHeli[0x3b1]=2;gPushed=0;EmitNativeCone();CHECK(gPushed==1 && Near(gCandidate[0],.8f));
	gHeli[0x3b1]=0;gReturnOffset=0x16a80c;gAircraft[2]=gHeli;gAircraft[0]=nullptr;gPushed=0;EmitNativeCone();
	CHECK(gPushed==1 && Near(gCandidate[0],.8f));gAircraft[2]=nullptr;gAircraft[0]=gHeli;gReturnOffset=0x16a55c;
	// A recycled pool slot or loading an older timeline cannot inherit damage.
	Reset();heli::WearOf(gHeli)->lampOut=true;gHandle=18;gPushed=0;EmitNativeCone();CHECK(gPushed==1);
	heli::WearOf(gHeli)->lampOut=true;gNow=1;gPushed=0;EmitNativeCone();CHECK(gPushed==1);
	CHECK(game::EntityPos(gPlayer).x==0 && game::EntityPos(gPlayer).z==0);
	Reset();CHECK(!heli::FailRotor(nullptr,true));CHECK(heli::FailRotor(gHeli,true));CHECK(heli::gWear[0].crash.phase==aircraft::Phase::Descending && heli::gWear[0].crash.cause==aircraft::Failure::Rotor);
	Reset();CHECK(heli::FailRotor(gHeli,false));CHECK(heli::gWear[0].rotorOut && heli::gWear[0].crash.phase==aircraft::Phase::Descending);
	heli::gWear[0].crash.phase=aircraft::Phase::Impacted;heli::gWear[0].rotorOut=false;*reinterpret_cast<uint32_t *>(gHeli+0x39c)=gNow-1;
	CHECK(heli::FailRotor(gHeli,false));CHECK(*reinterpret_cast<uint32_t *>(gHeli+0x39c)==gNow-1 && heli::gWear[0].crash.phase==aircraft::Phase::Impacted);
	Reset();gConfig.heliDamage=false;CHECK(!heli::FailRotor(gHeli,true));CHECK(heli::gWear[0].crash.phase==aircraft::Phase::Flying);gConfig.heliDamage=true;
	if(!gFailures)std::puts("helihook: all passed");return gFailures?1:0;
}
