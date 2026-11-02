#include "estatelighting.h"
#include "types.h"
#include "hooks.h"
#include "log.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <dlfcn.h>
#include <strings.h>
#include <vector>
namespace estatelighting {
namespace {
struct Triangle { uint16_t a,b,c,material; };
struct Surface { float ambient,specular,diffuse; };
struct WorldLights {
	int32_t numAmbients;
	float ambient[4];
	int32_t numDirectionals;
	void **directionals;
	int32_t numLocals;
	void **locals;
};
static_assert(sizeof(WorldLights)==0x30 && offsetof(WorldLights,directionals)==0x18 && offsetof(WorldLights,locals)==0x28);
// Native AddLight/GenerateLightsAffectingObject use this exact 44-byte layout.
// The executable's AddLight limit is 64 (0x208a18), unlike the reference's 32.
struct PointLight {
	Vec3 position{},direction{};
	float radius=0;
	Vec3 colour{};
	uint8_t type=0,fog=0,extraShadows=0,pad=0;
};
static_assert(sizeof(PointLight)==0x2c && offsetof(PointLight,radius)==0x18 &&
              offsetof(PointLight,colour)==0x1c && offsetof(PointLight,type)==0x28);
constexpr int kPointLights=64,kRoomLights=16,kWorldLights=8,kNativePoint=0x100;
const int16_t *gPointCount;
const PointLight *gPointLights;
void **gExtraDirectionals;
struct Effect {
	Vec3 position{};CRGBA colour{};uint8_t type=0,pad[7]{};
	float distance=0,radius=0,size=0,shadowSize=0;
	uint8_t lightType=0,road=0,flare=0,shadowIntensity=0,flags=0,pad2[3]{};
	void *corona=nullptr,*shadow=nullptr;
};
static_assert(sizeof(Effect)==0x40 && offsetof(Effect,radius)==0x1c && offsetof(Effect,lightType)==0x28);
struct BuildingPool {uint8_t *objects,*flags;int32_t size,first;};
static_assert(offsetof(BuildingPool,flags)==8 && offsetof(BuildingPool,size)==0x10);
BuildingPool **gBuildingPool;
const uint32_t *gFrame,*gTime;
const int32_t *gArea,*gEffectCount;
const float *gSpriteBrightness;
const Effect *(*gEffect)(void *,int);
void (*orig_EntityLights)(void *);
// The layouts are verified against this executable's enumerateLights,
// setLights, RpGeometryTriangleSetVertexIndices and defaultInstanceCB.
struct Access {
	void *(*Model)(void *);
	void *(*Geometry)(const void *);
	void *(*Morph)(const void *,int);
	int (*NumVertices)(const void *),(*NumTriangles)(const void *);
	const Vec3 *(*Vertices)(const void *);
	const Triangle *(*Triangles)(const void *);
	CRGBA *(*Colours)(const void *);
	uint32_t (*Flags)(const void *);
	void *(*SetFlags)(void *,uint32_t);
	void *(*Lock)(void *,int),*(*Unlock)(void *);
	void *(*AtomicFrame)(const void *),*(*LightFrame)(const void *);
	const float *(*LTM)(void *);
	const Surface *(*Properties)(const void *);
	void *(*Materials)(void *,void *(*)(void *,void *),void *);
	void (*Enumerate)(void *,void *,WorldLights *);
} rw{};
void **gScene,**gModels;
void *(*orig_Atomic)(void *);
constexpr uint32_t kNormals=0x10,kPrelit=0x08,kLight=0x20,kNative=0x01000000;
constexpr int kLockPrelight=0x08;

Vec3 Add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 Sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 Mul(Vec3 a,float k){return {a.x*k,a.y*k,a.z*k};}
float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
bool Finite(Vec3 a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
Vec3 Unit(Vec3 a){const float l=std::sqrt(Dot(a,a));return l>1e-7f?Mul(a,1/l):Vec3{};}
Vec3 Row(const float *m,int row){return {m[row*4],m[row*4+1],m[row*4+2]};}
Vec3 Position(const float *m,Vec3 p){return Add(Row(m,3),Add(Mul(Row(m,0),p.x),Add(Mul(Row(m,1),p.y),Mul(Row(m,2),p.z))));}
Vec3 Normal(const float *m,Vec3 n)
{
	// Inverse transpose, including nonuniform or mirrored frame transforms.
	const Vec3 a=Cross(Row(m,1),Row(m,2)),b=Cross(Row(m,2),Row(m,0)),c=Cross(Row(m,0),Row(m,1));
	const float determinant=Dot(Row(m,0),a);
	return std::fabs(determinant)>1e-8f?Unit(Mul(Add(Mul(a,n.x),Add(Mul(b,n.y),Mul(c,n.z))),1/determinant)):Vec3{};
}
bool Structural(const uint8_t *model)
{
	if(!model||!gModels)return false;
	// The visibility plugin is a UNION: vehicle atomics store numeric flags,
	// not model pointers. Compare against trusted model-table entries BEFORE
	// reading a model name; never dereference the union as an arbitrary pointer.
	struct Named { int id; const char *name; };
	constexpr Named meshes[]={{2467,"man_hall_floor"},{2470,"man_hall_wall1"},
	    {2482,"man_scrface_walls"},{2484,"man_scrfce_floor1"},{2499,"man_frntstep"},{2548,"man_frntstepgd"}};
	for(const Named &mesh:meshes)
		if(model==gModels[mesh.id]&&!strncasecmp(reinterpret_cast<const char *>(model+8),mesh.name,21))return true;
	return false;
}

uint64_t Hash(const void *data,size_t bytes,uint64_t h=1469598103934665603ull)
{
	const auto *p=static_cast<const uint8_t *>(data);
	for(size_t i=0;i<bytes;++i){h^=p[i];h*=1099511628211ull;}return h;
}
struct Cached {
	void *geometry=nullptr;
	uint64_t hash=0;
	std::vector<Vec3> normals;
	std::vector<uint16_t> material;
	std::vector<CRGBA> saved,lit;
};
std::array<Cached,24> gCache;
unsigned gNext;
std::array<const void *,6> gDrawLogged{};
std::array<const void *,6> gFallbackLogged{};
struct Rooms {
	std::array<PointLight,kRoomLights> points{};
	std::array<const Effect *,kRoomLights> effects{};
	std::array<Vec3,4> transform{};
	BuildingPool *pool=nullptr;
	uint8_t *objects=nullptr,*flags=nullptr,*entity=nullptr;
	void *world=nullptr,*model=nullptr,*clump=nullptr;
	uint32_t frame=0,time=0;
	int index=-1,count=0,effectBase=0,effectTotal=0;
	float brightness=0;
	uint8_t generation=0;
	bool valid=false;
} gRooms;
bool Interior(const void *model)
{
	if(!model||!gModels)return false;
	struct Named {int id;const char *name;};
	for(const Named entry:{Named{2467,"man_hall_floor"},{2470,"man_hall_wall1"},
	    {2482,"man_scrface_walls"},{2484,"man_scrfce_floor1"}})
		if(model==gModels[entry.id]&&!strncasecmp(static_cast<const char *>(model)+8,entry.name,21))return true;
	return false;
}
int BuildingIndex(BuildingPool *pool,const void *entity)
{
	// CBuilding::operator new/delete (0x46840/0x468c8): stride0x78,
	// pool objects+0, generation bytes+8, count+0x10. Never touch an old
	// entity until its current native pool slot/generation is validated.
	if(!pool||!pool->objects||!pool->flags||pool->size<=0||pool->size>1000000)return -1;
	const auto p=reinterpret_cast<uintptr_t>(entity),base=reinterpret_cast<uintptr_t>(pool->objects);
	if(p<base||p-base>=size_t(pool->size)*0x78||(p-base)%0x78)return -1;
	const int index=int((p-base)/0x78);
	return (pool->flags[index]&0x80)?-1:index;
}
bool ValidateRooms()
{
	if(!gRooms.valid||!gScene||gScene[0]!=gRooms.world||!gArea||*gArea!=2||!gFrame||!gTime||
	   *gFrame!=gRooms.frame||*gTime!=gRooms.time||!gModels||gModels[2467]!=gRooms.model||
	   !gBuildingPool||*gBuildingPool!=gRooms.pool)return false;
	BuildingPool *pool=*gBuildingPool;
	if(pool->objects!=gRooms.objects||pool->flags!=gRooms.flags||BuildingIndex(pool,gRooms.entity)!=gRooms.index||
	   pool->flags[gRooms.index]!=gRooms.generation)return false;
	const uint8_t *entity=gRooms.entity;
	if(*reinterpret_cast<const int16_t *>(entity+0x6a)!=2467||entity[0x6d]!=2||
	   *reinterpret_cast<void *const *>(entity+0x58)!=gRooms.clump||
	   (*reinterpret_cast<const uint32_t *>(entity+0x60)&0x01040007)!=0x00040001)return false;
	const auto *model=static_cast<const uint8_t *>(gRooms.model);
	if(model[0x1d]!=1||strncasecmp(reinterpret_cast<const char *>(model+8),"man_hall_floor",21))return false;
	if(model[0x1e]!=gRooms.effectTotal||*reinterpret_cast<const int16_t *>(model+0x28)!=gRooms.effectBase||
	   !gEffectCount||*gEffectCount<gRooms.effectBase+gRooms.effectTotal||*gEffectCount>1210||
	   !gSpriteBrightness||*gSpriteBrightness!=gRooms.brightness)return false;
	const float *matrix=reinterpret_cast<const float *>(entity+8);
	for(int row=0;row<4;++row){const Vec3 p=Row(matrix,row),saved=gRooms.transform[size_t(row)];
		if(p.x!=saved.x||p.y!=saved.y||p.z!=saved.z)return false;
	}
	return true;
}
bool RoomsCurrent()
{
	const bool valid=ValidateRooms();
	if(!valid)gRooms.valid=false;
	return valid;
}
void Hook_EntityLights(void *pointer)
{
	orig_EntityLights(pointer); // Native coronas/point list and its camera budget are untouched.
	if(!pointer||!gScene||!gScene[0]||!gArea||*gArea!=2||!gFrame||!gTime||!gModels||
	   !gBuildingPool||!gEffect||!gEffectCount||!gSpriteBrightness)return;
	auto *entity=static_cast<uint8_t *>(pointer);
	BuildingPool *pool=*gBuildingPool;const int index=BuildingIndex(pool,entity);
	if(index<0)return;
	if(*reinterpret_cast<const int16_t *>(entity+0x6a)!=2467)return;
	// A failed visit from our source invalidates it immediately, including a
	// same-frame hide/damage/recycle, rather than retaining stale room lamps.
	gRooms={};
	void *model=gModels[2467];const auto *m=static_cast<const uint8_t *>(model);
	if(!m||m[0x1d]!=1||strncasecmp(reinterpret_cast<const char *>(m+8),"man_hall_floor",21)||
	   entity[0x6d]!=2||(*reinterpret_cast<const uint32_t *>(entity+0x60)&0x01040007)!=0x00040001)return;
	void *clump=*reinterpret_cast<void **>(entity+0x58);if(!clump)return;
	const float *matrix=reinterpret_cast<const float *>(entity+8);
	for(int row=0;row<4;++row)if(!Finite(Row(matrix,row)))return;
	if(Row(matrix,2).z<.96f)return; // exact native ProcessLights visibility/damage/upright gates
	const int count=m[0x1e],base=*reinterpret_cast<const int16_t *>(m+0x28),allocated=*gEffectCount;
	if(count<=0||count>64||base<0||allocated<0||allocated>1210||base+count>allocated)return;
	const float brightness=*gSpriteBrightness;if(!std::isfinite(brightness)||brightness<0)return;
	Rooms fresh;fresh.pool=pool;fresh.objects=pool->objects;fresh.flags=pool->flags;fresh.index=index;
	fresh.generation=pool->flags[index];fresh.entity=entity;fresh.clump=clump;fresh.model=model;
	fresh.world=gScene[0];fresh.frame=*gFrame;fresh.time=*gTime;
	fresh.effectBase=base;fresh.effectTotal=count;fresh.brightness=brightness;
	for(int row=0;row<4;++row)fresh.transform[size_t(row)]=Row(matrix,row);
	for(int i=0;i<count;++i){
		const Effect *effect=gEffect(model,i);if(!effect)return;
		// Only authored, always-on POINT effects are estate room emitters.
		// Coronastars with range0 and flickering/night-only effects remain native.
		if(effect->type!=0||effect->lightType!=0||(effect->flags&8)||effect->radius==0)continue;
		if(!Finite(effect->position)||!std::isfinite(effect->radius)||effect->radius<0||fresh.count>=kRoomLights)return;
		PointLight &point=fresh.points[size_t(fresh.count)];point.position=Position(matrix,effect->position);
		point.radius=effect->radius;point.colour={effect->colour.r*brightness/255,effect->colour.g*brightness/255,effect->colour.b*brightness/255};
		if(!Finite(point.position)||!Finite(point.colour))return;
		fresh.effects[size_t(fresh.count++)]=effect;
	}
	if(!fresh.count)return;
	fresh.valid=true;gRooms=fresh;
}
Cached *Prepare(void *geometry,const Vec3 *vertices,int nv,const Triangle *triangles,int nt)
{
	if(!vertices||!triangles||nv<=0||nv>65535||nt<=0||nt>200000)return nullptr;
	const uint64_t hash=Hash(triangles,size_t(nt)*sizeof(Triangle),Hash(vertices,size_t(nv)*sizeof(Vec3)));
	Cached *entry=nullptr;
	for(Cached &c:gCache)if(c.geometry==geometry){entry=&c;break;}
	if(!entry){entry=&gCache[gNext++%gCache.size()];entry->geometry=geometry;entry->hash=0;}
	// Counts and contents, rather than a raw address, identify a streamed mesh.
	// Never dereference an old native pointer from the cache.
	if(entry->hash==hash&&entry->normals.size()==size_t(nv))return entry;
	entry->hash=0; // no prior fingerprint may authorize partially rebuilt normals
	entry->normals.assign(size_t(nv),{});entry->material.assign(size_t(nv),0);
	for(int i=0;i<nv;++i)if(!Finite(vertices[i]))return nullptr;
	for(int i=0;i<nt;++i){
		const Triangle &t=triangles[i];
		if(t.a>=nv||t.b>=nv||t.c>=nv||t.material>=256){entry->hash=0;return nullptr;}
		const Vec3 n=Cross(Sub(vertices[t.b],vertices[t.a]),Sub(vertices[t.c],vertices[t.a]));
		for(uint16_t v:{t.a,t.b,t.c}){entry->normals[v]=Add(entry->normals[v],n);entry->material[v]=t.material;}
	}
	for(Vec3 &n:entry->normals)n=Unit(n);
	entry->saved.resize(size_t(nv));entry->lit.resize(size_t(nv));entry->hash=hash;
	return entry;
}
struct Light {
	Vec3 colour{},direction{},position{};
	float radius=0,cone=0;
	int type=1;
};
struct Lighting {
	Vec3 ambient{};
	std::array<Light,kWorldLights+kRoomLights+kPointLights> lights{};
	int count=0,worldCount=0,roomCount=0,nativeCount=0,registeredCount=0;
};
void NativePoints(Lighting &result,bool roomReceiver=false)
{
	const bool rooms=roomReceiver&&RoomsCurrent();
	if(rooms)for(int i=0;i<gRooms.count;++i){
		const PointLight &source=gRooms.points[size_t(i)];
		Light &light=result.lights[size_t(result.count++)];
		light.colour=source.colour;light.position=source.position;light.radius=source.radius;light.type=kNativePoint;
		++result.roomCount;
	}
	if(!gPointCount||!gPointLights)return;
	result.registeredCount=std::clamp(int(*gPointCount),0,kPointLights);
	for(int i=0;i<result.registeredCount&&result.count<int(result.lights.size());++i){
		const PointLight source=gPointLights[i];
		bool duplicate=false;
		if(rooms&&source.type==0)for(int j=0;j<gRooms.count;++j){
			const PointLight &room=gRooms.points[size_t(j)];const Vec3 delta=Sub(source.position,room.position);
			if(Dot(delta,delta)<1e-6f&&std::fabs(source.radius-room.radius)<.001f){duplicate=true;break;}
		}
		if(duplicate)continue;
		// Fog-only/darken records are not diffuse emitters. Colour already
		// contains the producer's sprite brightness and camera-distance fade.
		if(source.type>1||!Finite(source.position)||!Finite(source.direction)||!Finite(source.colour)||
		   !std::isfinite(source.radius)||source.radius<=0||source.colour.x<0||source.colour.y<0||source.colour.z<0||
		   std::max({source.colour.x,source.colour.y,source.colour.z})<=0)continue;
		Light &light=result.lights[size_t(result.count++)];
		light.colour=source.colour;light.position=source.position;light.direction=source.direction;
		light.radius=source.radius;light.type=kNativePoint+source.type;
		++result.nativeCount;
	}
}
class FlagsScope {
public:
	FlagsScope(void *g,uint32_t flags):geometry(g),saved(rw.Flags(g)){rw.SetFlags(g,flags);}
	~FlagsScope(){rw.SetFlags(geometry,saved);}
private:void *geometry;uint32_t saved;
};
Lighting Current(void *atomic,void *geometry,bool roomReceiver)
{
	Lighting result;
	void *directionals[8]{},*locals[8]{};
	WorldLights lights{};lights.numDirectionals=8;lights.directionals=directionals;lights.numLocals=8;lights.locals=locals;
	{
		// Native enumerateLights omits directionals/locals when NORMALS is absent.
		// We supply geometric normals on CPU; this flag is used only for the
		// enumeration and is restored before any native instancing can read it.
		FlagsScope normals(geometry,rw.Flags(geometry)|kNormals);
		rw.Enumerate(gScene[0],atomic,&lights);
	}
	result.ambient={lights.ambient[0],lights.ambient[1],lights.ambient[2]};
	auto take=[&](void *pointer,bool directional){
		if(!pointer||result.count>=kWorldLights)return;
		// GenerateLightsAffectingObject projects the same native point list into
		// four temporary RpWorld directionals for peds/objects. Do not apply a
		// point producer twice if a nested draw left those active.
		if(directional&&gExtraDirectionals)
			for(int i=0;i<4;++i)if(pointer==gExtraDirectionals[i])return;
		const auto *p=static_cast<const uint8_t *>(pointer);
		void *frame=rw.LightFrame(pointer);const float *matrix=frame?rw.LTM(frame):nullptr;
		if(!matrix)return;
		Light &light=result.lights[size_t(result.count++)];
		light.colour=*reinterpret_cast<const Vec3 *>(p+0x2c);
		light.direction=Unit(Row(matrix,2));light.position=Row(matrix,3);
		light.type=directional?1:p[1];
		light.radius=*reinterpret_cast<const float *>(p+0x28);
		light.cone=-*reinterpret_cast<const float *>(p+0x3c);
	};
	for(int i=0;i<std::clamp(lights.numDirectionals,0,8);++i)take(directionals[i],true);
	for(int i=0;i<std::clamp(lights.numLocals,0,8);++i)take(locals[i],false);
	result.worldCount=result.count;
	NativePoints(result,roomReceiver);
	return result;
}
float Strength(const Light &light,Vec3 point,Vec3 normal)
{
	if(light.type==1)return std::max(0.0f,-Dot(normal,light.direction));
	const Vec3 from=Sub(point,light.position);const float distance=std::sqrt(Dot(from,from));
	if(distance<=1e-6f||light.radius<=0||distance>=light.radius)return 0;
	const Vec3 direction=Mul(from,1/distance);
	if(light.type==kNativePoint||light.type==kNativePoint+1){
		// Exact CPointLights native irradiance envelope: full strength in the
		// inner half-radius, then fade to zero at the radius. This is distinct
		// from an RpLight's falloff and is already camera-faded by AddLight.
		float amount=std::max(0.0f,-Dot(normal,direction))*std::min(1.0f,2*(1-distance/light.radius));
		if(light.type==kNativePoint+1)amount*=std::max(0.0f,(Dot(direction,light.direction)-.5f)*2);
		return amount;
	}
	float amount=std::max(0.0f,-Dot(normal,direction))*(1-distance/light.radius);
	if(light.type==0x81||light.type==0x82){
		const float along=Dot(direction,light.direction);
		if(along<light.cone)return 0;
		if(light.type==0x82)amount*=std::clamp((along-light.cone)/std::max(.001f,1-light.cone),0.0f,1.0f);
	}
	return amount;
}
void *SurfaceAt(void *material,void *data)
{
	auto &surfaces=*static_cast<std::vector<Surface> *>(data);
	if(surfaces.size()<256){const Surface *s=rw.Properties(material);surfaces.push_back(s?*s:Surface{1,0,1});}
	return material;
}
void Relight(Cached &cache,const Vec3 *vertices,const float *matrix,const Lighting &lighting,const std::vector<Surface> &surfaces)
{
	for(size_t i=0;i<cache.normals.size();++i){
		const Surface surface=cache.material[i]<surfaces.size()?surfaces[cache.material[i]]:Surface{1,0,1};
		const Vec3 normal=Normal(matrix,cache.normals[i]),point=Position(matrix,vertices[i]);
		Vec3 colour=Mul(lighting.ambient,surface.ambient);
		for(int j=0;j<lighting.count;++j)colour=Add(colour,Mul(lighting.lights[size_t(j)].colour,
		    surface.diffuse*Strength(lighting.lights[size_t(j)],point,normal)));
		const CRGBA old=cache.saved[i];const float peak=std::max({old.r,old.g,old.b});
		// Keep artist colour ratios, remove only the baked illumination magnitude.
		// At black vertices no chromatic signal remains; the texture/material still
		// supplies the surface albedo. Illumination always comes from actual lights.
		auto channel=[&](float value,uint8_t tint){return uint8_t(std::lround(std::clamp(value*(peak>8?float(tint)/peak:1),0.0f,1.0f)*255));};
		cache.lit[i]={channel(colour.x,old.r),channel(colour.y,old.g),channel(colour.z,old.b),old.a};
	}
}
class DrawScope {
public:
	DrawScope(void *g,Cached &c):geometry(g),cache(c),flags(rw.Flags(g)){
		rw.Lock(geometry,kLockPrelight);std::memcpy(rw.Colours(geometry),cache.lit.data(),cache.lit.size()*sizeof(CRGBA));rw.Unlock(geometry);
		rw.SetFlags(geometry,flags&~kLight); // illumination is already evaluated once
	}
	~DrawScope(){
		rw.SetFlags(geometry,flags);
		rw.Lock(geometry,kLockPrelight);std::memcpy(rw.Colours(geometry),cache.saved.data(),cache.saved.size()*sizeof(CRGBA));rw.Unlock(geometry);
	}
private:void *geometry;Cached &cache;uint32_t flags;
};
void *Hook_Atomic(void *atomic)
{
	const void *model=rw.Model(atomic);
	if(!gScene||!gScene[0]||!Structural(static_cast<const uint8_t *>(model)))return orig_Atomic(atomic);
	const bool room=Interior(model);
	if(room&&!RoomsCurrent()){
		// Unbaking without a validated physical room producer repeats the
		// observed ambient-flat regression. Keep native prelights on failure.
		if(std::find(gFallbackLogged.begin(),gFallbackLogged.end(),model)==gFallbackLogged.end())
			for(auto &logged:gFallbackLogged)if(!logged){logged=model;Log("estate lighting: room producer unavailable for %.21s; native prelights retained",static_cast<const char *>(model)+8);break;}
		return orig_Atomic(atomic);
	}
	void *geometry=rw.Geometry(atomic);const uint32_t flags=geometry?rw.Flags(geometry):0;
	// Native loading clears LIGHT for the mansion's IDE-32 baked interiors.
	// Their PRELIT data is still the signal we replace; requiring LIGHT here
	// accepts the raw DFF but skips the actual streamed interior draw.
	if((flags&(kPrelit|kNormals|kNative))!=kPrelit)return orig_Atomic(atomic);
	void *morph=rw.Morph(geometry,0),*frame=rw.AtomicFrame(atomic);
	const Vec3 *vertices=morph?rw.Vertices(morph):nullptr;const float *matrix=frame?rw.LTM(frame):nullptr;
	CRGBA *colours=rw.Colours(geometry);
	Cached *cache=colours&&matrix?Prepare(geometry,vertices,rw.NumVertices(geometry),rw.Triangles(geometry),rw.NumTriangles(geometry)):nullptr;
	if(!cache)return orig_Atomic(atomic);
	std::memcpy(cache->saved.data(),colours,cache->saved.size()*sizeof(CRGBA));
	std::vector<Surface> surfaces;rw.Materials(geometry,SurfaceAt,&surfaces);
	const Lighting lighting=Current(atomic,geometry,room);
	Relight(*cache,vertices,matrix,lighting,surfaces);
	if(std::find(gDrawLogged.begin(),gDrawLogged.end(),model)==gDrawLogged.end())
		for(auto &logged:gDrawLogged)if(!logged){
			logged=model;
			Log("estate lighting: native draw %.21s flags=0x%x vertices=%zu ambient=%.3f,%.3f,%.3f lights=%d world=%d room_points=%d native_points=%d/%d",
			    static_cast<const char *>(model)+8,flags,cache->normals.size(),lighting.ambient.x,
			    lighting.ambient.y,lighting.ambient.z,lighting.count,lighting.worldCount,lighting.roomCount,
			    lighting.nativeCount,lighting.registeredCount);
			break;
		}
	const DrawScope draw(geometry,*cache);
	return orig_Atomic(atomic);
}
template<class T> bool Bind(T &fn,const char *name){fn=reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY,name));return fn!=nullptr;}
}
bool Install()
{
	gScene=static_cast<void **>(dlsym(RTLD_MAIN_ONLY,"Scene"));
	gModels=static_cast<void **>(dlsym(RTLD_MAIN_ONLY,"_ZN10CModelInfo16ms_modelInfoPtrsE"));
	gPointCount=static_cast<const int16_t *>(dlsym(RTLD_MAIN_ONLY,"_ZN12CPointLights9NumLightsE"));
	gPointLights=static_cast<const PointLight *>(dlsym(RTLD_MAIN_ONLY,"_ZN12CPointLights7aLightsE"));
	gExtraDirectionals=static_cast<void **>(dlsym(RTLD_MAIN_ONLY,"pExtraDirectionals"));
	gBuildingPool=static_cast<BuildingPool **>(dlsym(RTLD_MAIN_ONLY,"_ZN6CPools16ms_pBuildingPoolE"));
	gFrame=static_cast<const uint32_t *>(dlsym(RTLD_MAIN_ONLY,"_ZN6CTimer14m_FrameCounterE"));
	gTime=static_cast<const uint32_t *>(dlsym(RTLD_MAIN_ONLY,"_ZN6CTimer22m_snTimeInMillisecondsE"));
	gArea=static_cast<const int32_t *>(dlsym(RTLD_MAIN_ONLY,"_ZN5CGame8currAreaE"));
	gEffectCount=static_cast<const int32_t *>(dlsym(RTLD_MAIN_ONLY,"_ZN10CModelInfo16ms_2dEffectStoreE"));
	gSpriteBrightness=static_cast<const float *>(dlsym(RTLD_MAIN_ONLY,"_ZN10CTimeCycle26m_fCurrentSpriteBrightnessE"));
	const bool ready=gScene&&gModels&&gPointCount&&gPointLights&&gExtraDirectionals&&gBuildingPool&&gFrame&&gTime&&gArea&&gEffectCount&&gSpriteBrightness&&
	    Bind(gEffect,"_ZN14CBaseModelInfo11Get2dEffectEi")&&
	    Bind(rw.Model,"_ZN18CVisibilityPlugins18GetAtomicModelInfoEPN2rw6AtomicE")&&
	    Bind(rw.Geometry,"_Z19RpAtomicGetGeometryPKN2rw6AtomicE")&&
	    Bind(rw.Morph,"_Z24RpGeometryGetMorphTargetPKN2rw8GeometryEi")&&
	    Bind(rw.NumVertices,"_Z24RpGeometryGetNumVerticesPKN2rw8GeometryE")&&
	    Bind(rw.NumTriangles,"_Z25RpGeometryGetNumTrianglesPKN2rw8GeometryE")&&
	    Bind(rw.Vertices,"_Z24RpMorphTargetGetVerticesPKN2rw11MorphTargetE")&&
	    Bind(rw.Triangles,"_Z22RpGeometryGetTrianglesPKN2rw8GeometryE")&&
	    Bind(rw.Colours,"_Z27RpGeometryGetPreLightColorsPKN2rw8GeometryE")&&
	    Bind(rw.Flags,"_Z18RpGeometryGetFlagsPKN2rw8GeometryE")&&Bind(rw.SetFlags,"_Z18RpGeometrySetFlagsPN2rw8GeometryEj")&&
	    Bind(rw.Lock,"_Z14RpGeometryLockPN2rw8GeometryEi")&&Bind(rw.Unlock,"_Z16RpGeometryUnlockPN2rw8GeometryE")&&
	    Bind(rw.AtomicFrame,"_Z16RpAtomicGetFramePKN2rw6AtomicE")&&Bind(rw.LightFrame,"_Z15RpLightGetFramePKN2rw5LightE")&&
	    Bind(rw.LTM,"_Z13RwFrameGetLTMPN2rw5FrameE")&&
	    Bind(rw.Properties,"_Z30RpMaterialGetSurfacePropertiesPKN2rw8MaterialE")&&
	    Bind(rw.Materials,"_Z25RpGeometryForAllMaterialsPN2rw8GeometryEPFPNS_8MaterialES3_PvES4_")&&
	    Bind(rw.Enumerate,"_ZN2rw5World15enumerateLightsEPNS_6AtomicEPNS_11WorldLightsE");
	if(!ready){Log("estate lighting: native geometry/light bindings missing; original materials retained");return false;}
	orig_EntityLights=reinterpret_cast<decltype(orig_EntityLights)>(hooks::Chain("_ZN7CEntity22ProcessLightsForEntityEv",reinterpret_cast<void *>(Hook_EntityLights)));
	if(!orig_EntityLights)return false;
	orig_Atomic=reinterpret_cast<decltype(orig_Atomic)>(hooks::Chain("_Z27AtomicDefaultRenderCallBackPN2rw6AtomicE",reinterpret_cast<void *>(Hook_Atomic)));
	return orig_Atomic!=nullptr;
}
}

