// Replay the actual atomic callback and native geometry dirty/instance sequence.
#include <dlfcn.h>
static void *EstateSymbol(void *,const char *);
#define dlsym EstateSymbol
#include "../src/estatelighting.cpp"
#undef dlsym
#include <cstdio>
#include <stdexcept>
#include <limits>
static int failures,gLocks,gUnlocks,gInstances,gDraws,gEnumerations;
static bool gThrow,gChain;
static void *(*gHook)(void *);
static uint32_t gFlags=0x1002f;
static uint16_t gDirty;
static uint32_t gDrawFlags;
static uint8_t gModelInfo[0x60]{},gUnregistered[0x60]{};
static void *gModelTable[6500]{};
static void *gModelReturn=gModelInfo;
static Vec3 gVertices[3]={{0,0,0},{1,0,0},{0,1,0}};
static estatelighting::Triangle gTriangles[1]={{0,1,2,0}};
static CRGBA gColours[3]={{0,0,0,64},{255,255,255,128},{80,80,80,255}},gGpu[3]{};
static float gMatrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0};
static float gLightMatrix[16]={1,0,0,0,0,1,0,0,0,0,-1,0,0,0,0,0};
alignas(8) static uint8_t gLight[0x60]{};
static estatelighting::Surface gSurface{1,0,1};
static int gAtomic,gGeometry,gWorld;
static void *gScene[2]={&gWorld,nullptr};
static std::vector<int> gEvents;
// Exact native CPointLights::aLights entry (AddLight 0x208a70..ae8).
// This executable has 64 entries; the reference source has only 32.
struct NativePointLight {
	Vec3 position{},direction{};
	float radius=0;
	Vec3 colour{};
	uint8_t type=0,fog=0,extraShadows=0,pad=0;
};
static_assert(sizeof(NativePointLight)==0x2c && offsetof(NativePointLight,colour)==0x1c && offsetof(NativePointLight,type)==0x28);
static int16_t gPointCount;
static NativePointLight gPointLights[64];
static void *gExtraDirectionals[4]{};
static float gAmbient=.2f;
static bool gWorldDirectional=true;
static bool gNativeBindings=true;
static void (*gEntityHook)(void *);
static uint32_t gFrame=1,gTime=1000;
static int32_t gArea=2,gEffectCount=1;
static float gSprite=1;
alignas(8) static uint8_t gEntity[0x78]{},gFloorInfo[0x60]{};
static uint8_t gPoolFlags[1]{5};
static estatelighting::BuildingPool gPool{gEntity,gPoolFlags,1,0},*gPoolPtr=&gPool;
static estatelighting::Effect gEffects[16];
static int gNativeEffectCalls;
static void NativeEntityLights(void *){++gNativeEffectCalls;}
#define CHECK(c) do{if(!(c)){std::printf("FAIL %d: %s\n",__LINE__,#c);++failures;}}while(0)
static const estatelighting::Effect *Effect(void *model,int index)
{
	CHECK(model==gModelTable[2467]&&index>=0&&index<gEffectCount);
	return index>=0&&index<gEffectCount?gEffects+index:nullptr;
}
static void CaptureFloor(){if(gEntityHook)gEntityHook(gEntity);}

void Log(const char *,...){}
static void *Native(void *a)
{
	++gDraws;gDrawFlags=gFlags;
	if(gDirty&8){std::memcpy(gGpu,gColours,sizeof gGpu);gDirty=0;++gInstances;gEvents.push_back(3);}
	if(gThrow)throw std::runtime_error("native render fixture");
	return a;
}
namespace hooks {
void *Chain(const char *name,void *h){
	if(std::strstr(name,"ProcessLightsForEntity")){gEntityHook=reinterpret_cast<decltype(gEntityHook)>(h);return reinterpret_cast<void *>(NativeEntityLights);}
	gChain=true;gHook=reinterpret_cast<decltype(gHook)>(h);return reinterpret_cast<void *>(Native);
}
}
static void *Model(void *){return gModelReturn;}
static void *Geometry(const void *){return &gGeometry;}
static void *Morph(const void *,int){return &gGeometry;}
static int NumVertices(const void *){return 3;}
static int NumTriangles(const void *){return 1;}
static Vec3 *Vertices(const void *){return gVertices;}
static estatelighting::Triangle *Triangles(const void *){return gTriangles;}
static CRGBA *Colours(const void *){return gColours;}
static uint32_t Flags(const void *){return gFlags;}
static void *SetFlags(void *g,uint32_t f){gFlags=f;return g;}
static void *Lock(void *g,int mask){CHECK(mask==8);gDirty|=mask;++gLocks;gEvents.push_back(1);return g;}
static void *Unlock(void *g){++gUnlocks;gEvents.push_back(2);return g;}
static void *Frame(const void *p){return p==gLight?static_cast<void *>(gLightMatrix):gMatrix;}
static float *LTM(void *p){return static_cast<float *>(p);}
static const estatelighting::Surface *Properties(const void *){return &gSurface;}
static void *Materials(void *g,void *(*visit)(void *,void *),void *data){visit(&gSurface,data);return g;}
static void Enumerate(void *,void *,estatelighting::WorldLights *w)
{
	CHECK((gFlags&0x10)!=0);++gEnumerations;
	w->numAmbients=1;w->ambient[0]=w->ambient[1]=w->ambient[2]=gAmbient;
	w->numDirectionals=gWorldDirectional?1:0;if(gWorldDirectional)w->directionals[0]=gLight;w->numLocals=0;
}
static void *EstateSymbol(void *,const char *n)
{
	if(!std::strcmp(n,"Scene"))return gScene;
	if(std::strstr(n,"ms_modelInfoPtrs"))return gModelTable;
	if(!std::strcmp(n,"_ZN12CPointLights9NumLightsE"))return gNativeBindings?&gPointCount:nullptr;
	if(!std::strcmp(n,"_ZN12CPointLights7aLightsE"))return gNativeBindings?gPointLights:nullptr;
	if(!std::strcmp(n,"pExtraDirectionals"))return gExtraDirectionals;
	if(!std::strcmp(n,"_ZN6CPools16ms_pBuildingPoolE"))return &gPoolPtr;
	if(!std::strcmp(n,"_ZN6CTimer14m_FrameCounterE"))return &gFrame;
	if(!std::strcmp(n,"_ZN6CTimer22m_snTimeInMillisecondsE"))return &gTime;
	if(!std::strcmp(n,"_ZN5CGame8currAreaE"))return &gArea;
	if(!std::strcmp(n,"_ZN10CModelInfo16ms_2dEffectStoreE"))return &gEffectCount;
	if(!std::strcmp(n,"_ZN10CTimeCycle26m_fCurrentSpriteBrightnessE"))return &gSprite;
	if(!std::strcmp(n,"_ZN14CBaseModelInfo11Get2dEffectEi"))return reinterpret_cast<void *>(Effect);
#define B(part,fn) if(std::strstr(n,part))return reinterpret_cast<void *>(fn)
	B("GetAtomicModelInfo",Model);B("RpAtomicGetGeometry",Geometry);B("GetMorphTarget",Morph);
	B("GetNumVertices",NumVertices);B("GetNumTriangles",NumTriangles);B("GetVertices",Vertices);
	B("GetTriangles",Triangles);B("GetPreLightColors",Colours);B("GetFlags",Flags);B("SetFlags",SetFlags);
	B("RpGeometryLock",Lock);B("RpGeometryUnlock",Unlock);B("RpAtomicGetFrame",Frame);B("RpLightGetFrame",Frame);
	B("RwFrameGetLTM",LTM);B("GetSurfaceProperties",Properties);B("ForAllMaterials",Materials);B("enumerateLights",Enumerate);
#undef B
	return nullptr;
}
static void Reset()
{
	std::memset(gModelInfo,0,sizeof gModelInfo);std::strcpy(reinterpret_cast<char *>(gModelInfo+8),"man_hall_floor");
	gFlags=0x1002f;gDirty=0;gModelTable[2467]=gModelInfo;gModelTable[2499]=gModelInfo;gModelReturn=gModelInfo;gLocks=gUnlocks=gInstances=gDraws=gEnumerations=0;gEvents.clear();gThrow=false;
	gVertices[1]={1,0,0};gVertices[2]={0,1,0};gTriangles[0]={0,1,2,0};
	gColours[0]={0,0,0,64};gColours[1]={255,255,255,128};gColours[2]={80,80,80,255};std::memcpy(gGpu,gColours,sizeof gGpu);
	gLight[1]=1;*reinterpret_cast<Vec3 *>(gLight+0x2c)={.6f,.6f,.6f};
	gAmbient=.2f;gWorldDirectional=true;gPointCount=0;std::fill(std::begin(gPointLights),std::end(gPointLights),NativePointLight{});
	std::fill(std::begin(gExtraDirectionals),std::end(gExtraDirectionals),nullptr);
	++gFrame;gTime+=17;gArea=2;gEffectCount=1;gSprite=1;gPoolPtr=&gPool;gPoolFlags[0]=5;gPool={gEntity,gPoolFlags,1,0};gScene[0]=&gWorld;
	gModelInfo[0x1d]=1;gModelInfo[0x1e]=1;
	std::memcpy(gFloorInfo,gModelInfo,sizeof gFloorInfo);
	std::memset(gEntity,0,sizeof gEntity);std::memcpy(gEntity+8,gMatrix,sizeof gMatrix);
	*reinterpret_cast<void **>(gEntity+0x58)=&gAtomic;
	*reinterpret_cast<uint32_t *>(gEntity+0x60)=0x40001;
	*reinterpret_cast<int16_t *>(gEntity+0x6a)=2467;gEntity[0x6d]=2;
	gEffects[0]={};gEffects[0].position={100,100,100};gEffects[0].radius=1;
	CaptureFloor();
}
static void NativeRoomProducerDraw()
{
	Reset();
	// Actual streamed flags and actual captured estate ambient: the world's
	// direct sun and locals are disabled for the interior building draw.
	gFlags=0x0f;gWorldDirectional=false;gAmbient=55.0f/255;
	// Native model2467 IDE effect: a real always-on white room pointlight,
	// radius13m, 3.57634m above its authored floor. Brightness1 is already in
	// native numeric shader units; no gamma/exposure conversion belongs here.
	gPointLights[0].position={0,0,3.57634f};gPointLights[0].radius=13;
	gPointLights[0].colour={1,1,1};gPointCount=1;
	const auto original=std::array<CRGBA,3>{gColours[0],gColours[1],gColours[2]};
	(gHook?gHook:Native)(&gAtomic);
	CHECK(gGpu[0].r==255 && gGpu[1].r==255 && gGpu[2].r==255);
	CHECK(gGpu[0].a==64 && gGpu[1].a==128 && gGpu[2].a==255);
	CHECK(gFlags==0x0f && !std::memcmp(gColours,original.data(),sizeof gColours));
	// A live producer change must reach cached geometry on the next draw.
	gPointLights[0].colour={.25f,.25f,.25f};(gHook?gHook:Native)(&gAtomic);
	CHECK(gGpu[0].r==119);
	// Match GenerateLightsAffectingObject's plateau/outer-half fade, rather
	// than RenderWare point lights' different falloff.
	gPointLights[0].position.z=6.5f;(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==119);
	gPointLights[0].position.z=9.75f;(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==87);
	gPointLights[0].position.z=13;(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==55);
	gPointLights[0].position.z=3.57634f;
	gPointLights[0].type=1;gPointLights[0].direction={0,0,-1};
	(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==119);
	gPointLights[0].direction={0,0,1};(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==55);
	gPointLights[0].type=0;
	// Temporary RpWorld directionals are generated from this same native
	// producer for peds/objects. Counting both would apply its light twice.
	gWorldDirectional=true;gExtraDirectionals[0]=gLight;
	(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==119);
	gExtraDirectionals[0]=nullptr;(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==255);
	gWorldDirectional=false;
	// Fog-only effects are not emitting room light; the native list is read
	// each draw, including its valid last slot and frame reset to zero lights.
	gPointLights[0].type=4;(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==55);
	gPointLights[63]=gPointLights[0];gPointLights[63].type=0;gPointCount=64;
	(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==119);
	gPointCount=0;(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==55);
	gPointCount=-1;(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==55);
	gPointCount=32767;(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==119);
	for(int invalid=0;invalid<7;++invalid){
		gPointCount=1;gPointLights[0]=gPointLights[63];
		switch(invalid){
		case 0:gPointLights[0].colour.x=std::numeric_limits<float>::quiet_NaN();break;
		case 1:gPointLights[0].colour.x=-1;break;
		case 2:gPointLights[0].position.z=std::numeric_limits<float>::infinity();break;
		case 3:gPointLights[0].radius=0;break;
		case 4:gPointLights[0].radius=std::numeric_limits<float>::infinity();break;
		case 5:gPointLights[0].type=2;break;
		case 6:gPointLights[0].direction.x=std::numeric_limits<float>::quiet_NaN();break;
		}
		(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==55);
	}
}
static void AuthoredRoomProducerDraw()
{
	auto prepare=[] {
		Reset();gFlags=0x0f;gWorldDirectional=false;gAmbient=55.0f/255;gSprite=.25f;
		gEffects[0].position={0,0,3.57634f};gEffects[0].radius=13;gEffects[0].colour={255,255,255,200};
		gEffects[1]=gEffects[0];gEffects[1].radius=0; // a corona is not an emitter
		gEffects[2]=gEffects[0];gEffects[2].type=1; // neither is a particle record
		gEffectCount=3;gModelInfo[0x1e]=3;
		gPointCount=1;gPointLights[0].position=gEffects[0].position;gPointLights[0].radius=13;
		gPointLights[0].colour={.1f,.1f,.1f};
		const auto native=gPointLights[0];const int calls=gNativeEffectCalls;
		CaptureFloor();CHECK(gNativeEffectCalls==calls+1);
		CHECK(gPointCount==1&&!std::memcmp(&native,gPointLights,sizeof native));
		CHECK(estatelighting::gRooms.count==1&&estatelighting::gRooms.effects[0]==gEffects);
	};
	prepare();(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==119);
	// Moving the camera can remove/fade the native ped/car list; the same
	// authored stationary room source still reaches a static estate receiver.
	gPointLights[0].colour={.01f,.01f,.01f};(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==119);
	gPointCount=0;(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==119);
	CaptureFloor();CHECK(estatelighting::gRooms.count==1); // repeated registration never duplicates a lamp
	gSprite=.5f;CaptureFloor();(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==183);
	for(int invalid=0;invalid<17;++invalid){
		prepare();
		switch(invalid){
		case 0:++gFrame;break;case 1:--gTime;break;case 2:gArea=0;break;
		case 3:gPoolFlags[0]^=1;break;case 4:gPoolFlags[0]|=0x80;break;
		case 5:*reinterpret_cast<void **>(gEntity+0x58)=&gGeometry;break;
		case 6:gModelTable[2467]=gUnregistered;break;
		case 7:*reinterpret_cast<int16_t *>(gEntity+0x6a)=2470;break;
		case 8:*reinterpret_cast<uint32_t *>(gEntity+0x60)|=0x1000000;break;
		case 9:*reinterpret_cast<float *>(gEntity+0x38)=1;break;
		case 10:gEffectCount=0;break;case 11:gModelInfo[0x1e]=0;break;
		case 12:gScene[0]=&gGeometry;break;
		case 13:gPool.flags=reinterpret_cast<uint8_t *>(1);break; // pool validated before old-entity access
		case 14:gPool.objects=nullptr;break;
		case 15:gEntity[0x6d]=0;break;case 16:*reinterpret_cast<float *>(gEntity+0x30)=.5f;break;
		}
		const int locks=gLocks;(gHook?gHook:Native)(&gAtomic);
		CHECK(gLocks==locks&&gGpu[0].r==0&&gGpu[1].r==255&&gFlags==0x0f);
	}
	prepare();gModelInfo[0x1e]=64;gEffectCount=3;CaptureFloor();
	CHECK(!estatelighting::gRooms.valid); // malformed metadata never calls out-of-range Get2dEffect
	prepare();gEffects[0].radius=-1;CaptureFloor();CHECK(!estatelighting::gRooms.valid);
}
static void LoadedInteriorDraws()
{
	// Native macVC load boundary, not the flags serialized in the DFF:
	// SetModelInfoFlags (0xe40b0) maps IDE bit5 to model bit9;
	// CSimpleModelInfo::SetAtomic (0x17aa70..88) then clears geometry LIGHT.
	// CFileLoader::SetRelatedModelInfoCB (0xe2c5c..64) installs the exported
	// AtomicDefaultRenderCallBack via SetAtomicRenderCallback(nullptr).
	struct Model {int id;const char *name;uint32_t ide;};
	for(const Model model: {Model{2467,"man_hall_floor",32}, {2470,"man_hall_wall1",32},
	    {2482,"man_scrface_walls",32}, {2484,"man_scrfce_floor1",32}}){
		Reset();std::fill(std::begin(gModelTable),std::end(gModelTable),nullptr);
		gModelTable[model.id]=gModelInfo;
		if(model.id!=2467)gModelTable[2467]=gFloorInfo;
		std::strcpy(reinterpret_cast<char *>(gModelInfo+8),model.name);
		CaptureFloor();
		const uint16_t modelFlags=uint16_t(((model.ide&2)<<4)|((model.ide&1)<<2)|
		    ((model.ide&12)?0x40:0)|(((model.ide>>3)&255)<<7));
		*reinterpret_cast<uint16_t *>(gModelInfo+0x56)=modelFlags;
		if(modelFlags&0x200)gFlags&=~0x20u;
		CHECK(gFlags==0x1000f); // all four real mansion interior IDE rows use 32
		const auto original=std::array<CRGBA,3>{gColours[0],gColours[1],gColours[2]};
		void *(*nativeCallback)(void *)=gHook?gHook:Native;
		nativeCallback(&gAtomic);
		CHECK(gGpu[0].r==204 && gGpu[1].r==204 && gGpu[2].r==204);
		CHECK(gInstances==1 && gEnumerations==1 && gLocks==2 && gDirty==8);
		CHECK(!std::memcmp(gColours,original.data(),sizeof gColours) && gFlags==0x1000f);
		std::printf("loaded estate %s: flags 0x%x, first GPU colour %u\n",model.name,gFlags,gGpu[0].r);
		// The same loaded flags cannot authorize a different interior mesh.
		std::strcpy(reinterpret_cast<char *>(gModelInfo+8),"hotel_floor");
		nativeCallback(&gAtomic);CHECK(gLocks==2 && gGpu[0].r==0 && gGpu[1].r==255);
	}
}
int main()
{
	Reset();CHECK(estatelighting::Install());CHECK(gChain&&gEntityHook);CaptureFloor();
	const auto original=std::array<CRGBA,3>{gColours[0],gColours[1],gColours[2]};
	(gHook?gHook:Native)(&gAtomic);
	CHECK(gGpu[0].r==204 && gGpu[1].r==204 && gGpu[2].r==204);
	CHECK(gGpu[0].a==64 && gGpu[1].a==128 && gGpu[2].a==255);
	CHECK((gDrawFlags&0x30)==0 && gEnumerations==1); // no second LIGHT pass or nonexistent GPU normals
	CHECK(gEvents==std::vector<int>({1,2,3,1,2}) && gLocks==2 && gUnlocks==2 && gDirty==8);
	gEvents.clear();
	*reinterpret_cast<Vec3 *>(gLight+0x2c)={.2f,.2f,.2f};
	(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==102); // live light changes use the cached mesh, not cached illumination
	*reinterpret_cast<Vec3 *>(gLight+0x2c)={.6f,.6f,.6f};
	CHECK(!std::memcmp(gColours,original.data(),sizeof gColours) && gFlags==0x1002f);
	CHECK(gEvents==std::vector<int>({1,2,3,1,2}) && gLocks==4 && gUnlocks==4 && gDirty==8);
	// Another object sharing this geometry must reinstance the original data.
	std::strcpy(reinterpret_cast<char *>(gModelInfo+8),"hotel_floor");
	(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==0 && gGpu[1].r==255);
	CHECK(gLocks==4 && gInstances==3);
	// Reused geometry addresses and same-sized streamed meshes cannot reuse normals.
	std::strcpy(reinterpret_cast<char *>(gModelInfo+8),"man_frntstep");gTriangles[0]={0,2,1,0};
	(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==51 && gGpu[1].r==51);
	gTriangles[0]={0,1,2,0};gVertices[2]={0,0,1};
	(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==51);
	// Material/fade alpha and pending dirty data survive an exceptional draw.
	gThrow=true;try{(gHook?gHook:Native)(&gAtomic);}catch(const std::runtime_error &){}
	CHECK(!std::memcmp(gColours,original.data(),sizeof gColours) && gFlags==0x1002f && gDirty==8);
	gThrow=false;
	std::memcpy(gUnregistered,gModelInfo,sizeof gModelInfo);gModelReturn=gUnregistered;
	const int ownedLocks=gLocks;(gHook?gHook:Native)(&gAtomic);CHECK(gLocks==ownedLocks);
	gModelReturn=reinterpret_cast<void *>(0x100);(gHook?gHook:Native)(&gAtomic);CHECK(gLocks==ownedLocks);
	gModelReturn=gModelInfo;gTriangles[0].c=65535;const int locks=gLocks;
	(gHook?gHook:Native)(&gAtomic);CHECK(gLocks==locks && gFlags==0x1002f);
	// A temporarily invalid streamed mesh must not retain a valid fingerprint
	// while its cached normals have already been reset for rebuilding.
	Reset();(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==204);
	gVertices[2].y=std::numeric_limits<float>::quiet_NaN();const int validLocks=gLocks;
	(gHook?gHook:Native)(&gAtomic);CHECK(gLocks==validLocks);
	gVertices[2]={0,1,0};(gHook?gHook:Native)(&gAtomic);CHECK(gGpu[0].r==204);
	LoadedInteriorDraws();
	NativeRoomProducerDraw();
	AuthoredRoomProducerDraw();
	// Missing a native producer binding must retain native baked materials;
	// installing an ambient-only substitute would repeat the live regression.
	Reset();gNativeBindings=false;gHook=nullptr;gChain=false;
	CHECK(!estatelighting::Install()&&!gChain&&!gHook);
	(gHook?gHook:Native)(&gAtomic);CHECK(gLocks==0&&gGpu[0].r==0&&gGpu[1].r==255);
	if(!failures)std::puts("estatehook: all passed");return failures?1:0;
}
