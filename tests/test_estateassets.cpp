// Actual installed DFF/IDE pairs establish the streamed geometry state and
// drive the same normal/irradiance code used by the draw hook.
#include "../src/estatelighting.cpp"
#include "../tools/bake/gamefiles.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
namespace hooks {void *Chain(const char *,void *){return nullptr;}}
void Log(const char *,...){}
static int failures;
#define CHECK(c) do{if(!(c)){std::printf("FAIL %d: %s\n",__LINE__,#c);++failures;}}while(0)
struct Chunk{uint32_t type,version;size_t start,end;};
template<class T>static T Get(const std::vector<uint8_t> &b,size_t p){T v{};std::memcpy(&v,b.data()+p,sizeof v);return v;}
static std::vector<Chunk> Chunks(const std::vector<uint8_t> &b,size_t a,size_t z)
{
	std::vector<Chunk> out;
	while(a+12<=z){const uint32_t n=Get<uint32_t>(b,a+4);if(n>z-a-12)break;out.push_back({Get<uint32_t>(b,a),Get<uint32_t>(b,a+8),a+12,a+12+n});a+=12+n;}
	return out;
}
static bool Read(const std::vector<uint8_t> &b,uint32_t &flags,std::vector<Vec3> &vertices,std::vector<CRGBA> &colours,std::vector<estatelighting::Triangle> &triangles)
{
	if(b.size()<12||Get<uint32_t>(b,0)!=16)return false;
	for(const auto &list:Chunks(b,12,b.size()))if(list.type==26)
	for(const auto &geometry:Chunks(b,list.start,list.end))if(geometry.type==15)
	for(const auto &s:Chunks(b,geometry.start,geometry.end))if(s.type==1){
		if(s.end-s.start<16)return false;
		flags=Get<uint32_t>(b,s.start);const uint32_t nt=Get<uint32_t>(b,s.start+4),nv=Get<uint32_t>(b,s.start+8);
		const uint32_t version=s.version&0xffff0000u?(((s.version>>14)&0x3ff00u)+0x30000u)|((s.version>>16)&0x3fu):s.version<<8;
		size_t p=s.start+16+(version<0x34000?12:0);
		if(!(flags&8)||nt>200000||nv>65535||size_t(nv)*4>s.end-p)return false;
		colours.resize(nv);std::memcpy(colours.data(),b.data()+p,size_t(nv)*4);p+=size_t(nv)*4;
		const unsigned uv=(flags>>16)&255?((flags>>16)&255):(flags&(4|128)?1:0);p+=size_t(nv)*uv*8;
		if(p>s.end||size_t(nt)*8>s.end-p)return false;
		for(uint32_t i=0;i<nt;++i,p+=8)triangles.push_back({Get<uint16_t>(b,p+2),Get<uint16_t>(b,p),Get<uint16_t>(b,p+6),Get<uint16_t>(b,p+4)});
		if(p>s.end||s.end-p<24||!Get<uint32_t>(b,p+16))return false;p+=24;
		if(size_t(nv)*12>s.end-p)return false;vertices.resize(nv);std::memcpy(vertices.data(),b.data()+p,size_t(nv)*12);return true;
	}
	return false;
}
static std::vector<estatelighting::Surface> Surfaces(const std::vector<uint8_t> &b)
{
	std::vector<estatelighting::Surface> out;
	for(const auto &list:Chunks(b,12,b.size()))if(list.type==26)
	for(const auto &geometry:Chunks(b,list.start,list.end))if(geometry.type==15)
	for(const auto &materials:Chunks(b,geometry.start,geometry.end))if(materials.type==8)
	for(const auto &material:Chunks(b,materials.start,materials.end))if(material.type==7)
	for(const auto &s:Chunks(b,material.start,material.end))if(s.type==1&&s.end-s.start>=28)
		out.push_back({Get<float>(b,s.start+16),Get<float>(b,s.start+20),Get<float>(b,s.start+24)});
	return out;
}
static std::array<float,16> Frame(const std::string &ipl,const char *wanted)
{
	std::array<float,16> out{};
	for(std::istringstream lines(ipl);lines;){
		std::string line;std::getline(lines,line);
		int id=0,area=0;char name[64]{};float x,y,z,sx,sy,sz,qx,qy,qz,qw;
		if(std::sscanf(line.c_str(),"%d, %63[^,], %d, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f",
		    &id,name,&area,&x,&y,&z,&sx,&sy,&sz,&qx,&qy,&qz,&qw)!=13||std::strcmp(name,wanted))continue;
		CHECK(area==2);CHECK(qx==0&&qy==0&&qz==0&&qw==1&&sx==1&&sy==1&&sz==1);
		out={1,0,0,0,0,1,0,0,0,0,1,0,x,y,z,1};break;
	}
	CHECK(out[15]==1);return out;
}
static void NativeRoomAssets(ImgArchive &archive,const std::string &ide,const std::string &ipl)
{
	const auto sourceFrame=Frame(ipl,"man_hall_floor");
	std::array<estatelighting::PointLight,64> points{};int16_t count=0;
	std::vector<estatelighting::PointLight> authoredPoints;
	std::vector<estatelighting::Effect> authoredEffects;
	int authored=0,coronasOnly=0;bool effects=false;
	// Reproduce the exact native IDE -> entity transform -> AddLight producer,
	// with its real fixed estate camera. SpriteBrightness1 is a native-unit
	// fixture input, not an invented RpWorld directional or room exposure lift.
	const Vec3 camera{-378.5f,-592.8f,26.5f};
	for(std::istringstream lines(ide);lines;){
		std::string line;std::getline(lines,line);
		if(line.find("2dfx")==0){effects=true;continue;}if(line.find("end")==0){effects=false;continue;}
		if(!effects)continue;std::replace(line.begin(),line.end(),',',' ');
		int id,r,g,b,a,type,shadowIntensity,lightType,road,flare,flags;
		Vec3 position{};float distance,radius,size,shadowSize;std::string corona,shadow;
		std::istringstream row(line);
		if(!(row>>id>>position.x>>position.y>>position.z>>r>>g>>b>>a>>type>>corona>>shadow>>distance>>radius>>size>>shadowSize>>shadowIntensity>>lightType>>road>>flare>>flags))continue;
		if((id==2466||id==2478)&&type==0&&radius==0)++coronasOnly;
		if(id!=2467||type!=0||radius<=0)continue;
		++authored;CHECK(r==255&&g==255&&b==255&&lightType==0&&(radius==7||radius==13));
		estatelighting::Effect effect;effect.position=position;effect.radius=radius;
		effect.colour={uint8_t(r),uint8_t(g),uint8_t(b),uint8_t(a)};effect.lightType=uint8_t(lightType);effect.flags=uint8_t(flags);
		authoredEffects.push_back(effect);
		position=estatelighting::Position(sourceFrame.data(),position);
		estatelighting::PointLight authoredPoint;authoredPoint.position=position;authoredPoint.radius=radius;
		authoredPoint.colour={1,1,1};authoredPoints.push_back(authoredPoint);
		const Vec3 delta=estatelighting::Sub(position,camera);const float d=std::sqrt(estatelighting::Dot(delta,delta));
		if(d>=22)continue;
		const float fade=d<16.5f?1:1-(d/22-.75f)*4;
		auto &point=points[size_t(count++)];point.position=position;point.radius=radius;point.colour={fade,fade,fade};
	}
	CHECK(authored==9&&coronasOnly==27&&count==2);
	// Encode the actual asset metadata at its verified native boundaries, then
	// visit the same ProcessLightsForEntity hook that registers the producer.
	alignas(8) uint8_t model[0x60]{},entity[0x78]{};uint8_t poolFlags[1]{5};
	std::array<void *,6500> models{};models[2467]=model;
	std::strcpy(reinterpret_cast<char *>(model+8),"man_hall_floor");model[0x1d]=1;model[0x1e]=uint8_t(authoredEffects.size());
	std::memcpy(entity+8,sourceFrame.data(),sizeof(float)*16);
	*reinterpret_cast<void **>(entity+0x58)=model;*reinterpret_cast<uint32_t *>(entity+0x60)=0x40001;
	*reinterpret_cast<int16_t *>(entity+0x6a)=2467;entity[0x6d]=2;
	estatelighting::BuildingPool pool{entity,poolFlags,1,0},*poolPointer=&pool;
	uint32_t frameNumber=1,time=1000;int32_t area=2,effectCount=int32_t(authoredEffects.size());float spriteBrightness=1;
	void *scene[2]{model,nullptr};
	static const std::vector<estatelighting::Effect> *activeEffects;activeEffects=&authoredEffects;
	estatelighting::gScene=scene;estatelighting::gModels=models.data();estatelighting::gBuildingPool=&poolPointer;
	estatelighting::gFrame=&frameNumber;estatelighting::gTime=&time;estatelighting::gArea=&area;
	estatelighting::gEffectCount=&effectCount;estatelighting::gSpriteBrightness=&spriteBrightness;
	estatelighting::gEffect=+[](void *,int index)->const estatelighting::Effect *{
		CHECK(index>=0&&index<int(activeEffects->size()));return &(*activeEffects)[size_t(index)];
	};
	estatelighting::orig_EntityLights=+[](void *){};
	estatelighting::Hook_EntityLights(entity);CHECK(estatelighting::RoomsCurrent()&&estatelighting::gRooms.count==9);
	auto cameraPoints=[&](Vec3 view){
		count=0;std::fill(points.begin(),points.end(),estatelighting::PointLight{});
		for(auto point:authoredPoints){
			const auto delta=estatelighting::Sub(point.position,view);const float d=std::sqrt(estatelighting::Dot(delta,delta));
			if(d>=22)continue;const float fade=d<16.5f?1:1-(d/22-.75f)*4;
			point.colour={fade,fade,fade};points[size_t(count++)]=point;
		}
	};
	estatelighting::gPointCount=&count;estatelighting::gPointLights=points.data();
	for(const char *name:{"man_hall_floor","man_hall_wall1","man_scrface_walls","man_scrfce_floor1"}){
		std::vector<uint8_t> bytes;CHECK(archive.Read(std::string(name)+".dff",bytes));
		uint32_t flags=0;std::vector<Vec3> vertices;std::vector<CRGBA> colours;std::vector<estatelighting::Triangle> triangles;
		CHECK(Read(bytes,flags,vertices,colours,triangles));if(vertices.empty())continue;
		const auto frame=Frame(ipl,name);const auto surfaces=Surfaces(bytes);CHECK(!surfaces.empty());
		for(const auto &surface:surfaces)CHECK(surface.ambient==1&&surface.diffuse==1);
		auto *cache=estatelighting::Prepare(const_cast<char *>(name),vertices.data(),int(vertices.size()),triangles.data(),int(triangles.size()));
		CHECK(cache);if(!cache)continue;cache->saved=colours;
		estatelighting::Lighting lights;lights.ambient={55.0f/255,55.0f/255,55.0f/255};
		estatelighting::Relight(*cache,vertices.data(),frame.data(),lights,surfaces);const auto baseline=cache->lit;
		estatelighting::NativePoints(lights,true);CHECK(lights.worldCount==0&&lights.roomCount==9&&lights.nativeCount==0&&lights.registeredCount==2);
		estatelighting::Relight(*cache,vertices.data(),frame.data(),lights,surfaces);
		unsigned affected=0,ambient=0;int maxIncrease=0;
		for(size_t i=0;i<colours.size();++i){
			const auto lit=cache->lit[i],before=baseline[i];CHECK(lit.a==colours[i].a);
			CHECK(lit.r>=before.r&&lit.g>=before.g&&lit.b>=before.b);
			const int increase=std::max({int(lit.r)-before.r,int(lit.g)-before.g,int(lit.b)-before.b});
			affected+=increase>0;ambient+=increase==0;maxIncrease=std::max(maxIncrease,increase);
		}
		CHECK(affected>0&&ambient>0&&maxIncrease>4);
		std::printf("native room asset %s: ambient55/255, 9 authored emitters/2 native ped-car records deduplicated, %u affected/%u ambient vertices, max increase%d\n",name,affected,ambient,maxIncrease);
		// Diffuse room illumination must depend on the physical receiver/light,
		// not the camera's distance to an otherwise stationary lamp. The hall
		// point fades at cameraA and is culled at cameraB, while these visible
		// vertices remain physically inside its native13m influence radius.
		const auto cameraA=cache->lit;
		cameraPoints({camera.x,camera.y-4,camera.z});
		estatelighting::Lighting alternate;alternate.ambient=lights.ambient;
		estatelighting::NativePoints(alternate,true);
		estatelighting::Relight(*cache,vertices.data(),frame.data(),alternate,surfaces);
		int cameraDelta=0;
		for(size_t i=0;i<colours.size();++i)cameraDelta=std::max({cameraDelta,
		    std::abs(int(cache->lit[i].r)-cameraA[i].r),std::abs(int(cache->lit[i].g)-cameraA[i].g),std::abs(int(cache->lit[i].b)-cameraA[i].b)});
		std::printf("native room camera invariance %s: fixed receiver, camera moves4m, max diffuse delta%d\n",name,cameraDelta);
		CHECK(cameraDelta==0);
		cameraPoints(camera);
	}
	estatelighting::gRooms={};estatelighting::gPointCount=nullptr;estatelighting::gPointLights=nullptr;
	estatelighting::gScene=nullptr;estatelighting::gModels=nullptr;estatelighting::gBuildingPool=nullptr;
	estatelighting::gFrame=nullptr;estatelighting::gTime=nullptr;estatelighting::gArea=nullptr;
	estatelighting::gEffectCount=nullptr;estatelighting::gSpriteBrightness=nullptr;
}
int main()
{
	const char *env=std::getenv("GAMEDATA");const std::string data=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";
	ImgArchive archive;CHECK(archive.Open(data+"/models/gta3.img"));if(failures)return 1;
	// Preserve the loader input that distinguishes a real streamed draw from
	// a raw-DFF fixture. Native SetModelInfoFlags maps IDE bit5 to model bit9,
	// and SetAtomic clears geometry LIGHT for that bit before the first draw.
	std::vector<uint8_t> ideBytes;CHECK(ReadGameFile(data+"/data/maps/mansion/mansion.ide",ideBytes));
	std::istringstream ide(std::string(ideBytes.begin(),ideBytes.end()));
	std::map<std::string,uint32_t> ideFlags;
	for(std::string line;std::getline(ide,line);){
		int id=0,count=0;char name[64]{},txd[64]{};float distance=0;unsigned flags=0;
		if(std::sscanf(line.c_str(),"%d, %63[^,], %63[^,], %d, %f, %u",&id,name,txd,&count,&distance,&flags)==6)
			ideFlags[Lower(name)]=flags;
	}
	for(const char *name:{"man_hall_floor","man_hall_wall1","man_scrface_walls","man_scrfce_floor1","man_frntstep","man_frntstepgd"}){
		std::vector<uint8_t> bytes;CHECK(archive.Read(std::string(name)+".dff",bytes));
		uint32_t flags=0;std::vector<Vec3> vertices;std::vector<CRGBA> colours;std::vector<estatelighting::Triangle> triangles;
		CHECK(Read(bytes,flags,vertices,colours,triangles));if(vertices.empty())continue;
		CHECK((flags&0x38)==0x28); // prelit/light, with no normals
		const auto row=ideFlags.find(name);CHECK(row!=ideFlags.end());if(row==ideFlags.end())continue;
		const bool interior=std::strncmp(name,"man_frntstep",12)!=0;
		CHECK(row->second==(interior?32u:128u));
		const uint32_t loadedFlags=(row->second&32)?flags&~0x20u:flags;
		CHECK((loadedFlags&0x38)==(interior?0x08u:0x28u));
		auto *cache=estatelighting::Prepare(const_cast<char *>(name),vertices.data(),int(vertices.size()),triangles.data(),int(triangles.size()));
		CHECK(cache);if(!cache)continue;cache->saved=colours;
		estatelighting::Lighting lights;lights.ambient={.2f,.2f,.2f};lights.count=1;
		lights.lights[0].colour={.6f,.6f,.6f};lights.lights[0].direction={0,0,-1};
		const float world[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0};
		estatelighting::Relight(*cache,vertices.data(),world,lights,{});
		unsigned dark=0,zero=0,top=0;
		for(size_t i=0;i<colours.size();++i){
			const auto old=colours[i],lit=cache->lit[i];const int before=std::max({old.r,old.g,old.b}),after=std::max({lit.r,lit.g,lit.b});
			dark+=before<64;zero+=before==0;
			CHECK(lit.a==old.a);CHECK(after>=51&&after<=204);
			// Equal upward normals receive equal synthetic irradiance regardless
			// of the original vertex shade. This does not attribute a live shadow.
			if(cache->normals[i].z>.999f){CHECK(after==204);++top;}
		}
		CHECK(dark>0&&zero>0);
		std::printf("estate asset %s: flags 0x%x -> 0x%x after native load, %zu vertices, %u baked dark/%u black, %u upward vertices relit from real normal\n",name,flags,loadedFlags,vertices.size(),dark,zero,top);
	}
	std::vector<uint8_t> iplBytes;CHECK(ReadGameFile(data+"/data/maps/mansion/mansion.ipl",iplBytes));
	NativeRoomAssets(archive,std::string(ideBytes.begin(),ideBytes.end()),std::string(iplBytes.begin(),iplBytes.end()));
	if(!failures)std::puts("estateassets: all passed");return failures?1:0;
}
