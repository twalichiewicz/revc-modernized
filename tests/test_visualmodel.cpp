#include "../tools/bake/visualmodel.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <unistd.h>

namespace {
using Bytes = std::vector<uint8_t>;
template<class T> void Put(Bytes &b,T value) { const size_t n=b.size();b.resize(n+sizeof value);std::memcpy(b.data()+n,&value,sizeof value); }
void Append(Bytes &b,const Bytes &v) { b.insert(b.end(),v.begin(),v.end()); }
Bytes Chunk(uint32_t type,const Bytes &body) { Bytes b;Put(b,type);Put(b,uint32_t(body.size()));Put(b,0x1803ffffu);Append(b,body);return b; }
Bytes Geometry(float height)
{
	Bytes s;Put(s,0x1000eu);Put(s,1u);Put(s,3u);Put(s,1u);
	for(int i=0;i<3;i++)Put(s,0xffffffffu); // prelight
	for(int i=0;i<6;i++)Put(s,0.f); // one UV set
	for(uint16_t n:{1,0,0,2})Put(s,n); // native RW triangle order
	for(int i=0;i<4;i++)Put(s,0.f);Put(s,1u);Put(s,0u);
	for(Vec3f p:std::array<Vec3f,3>{{{0,0,0},{2,0,height},{0,2,0}}}) {Put(s,p.x);Put(s,p.y);Put(s,p.z);}
	Bytes mat;Put(mat,0u);Put(mat,0xffffffffu);Put(mat,0u);Put(mat,1u);Put(mat,1.f);Put(mat,1.f);Put(mat,1.f);
	Bytes texture=Chunk(1,Bytes(4));Append(texture,Chunk(2,Bytes{'r','o','o','f',0}));Append(texture,Chunk(2,Bytes{0}));
	Bytes material=Chunk(1,mat);Append(material,Chunk(6,texture));
	Bytes ml;Put(ml,1u);Put(ml,uint32_t(-1));ml=Chunk(1,ml);Append(ml,Chunk(7,material));
	Bytes g=Chunk(1,s);Append(g,Chunk(8,ml));return Chunk(15,g);
}
Bytes Model(bool cyclic=false,bool hideSecond=false)
{
	Bytes fs;Put(fs,2u);
	for(int f=0;f<2;f++) {
		const float m[12]={1,0,0,0,1,0,0,0,1,float(f?10:3),float(f?2:4),float(f?1:5)};
		for(float n:m)Put(fs,n);Put(fs,int32_t(f?0:cyclic?1:-1));Put(fs,0u);
	}
	Bytes list;Put(list,2u);list=Chunk(1,list);Append(list,Geometry(3));Append(list,Geometry(7));
	Bytes c=Chunk(1,Bytes(12));Append(c,Chunk(14,Chunk(1,fs)));Append(c,Chunk(26,list));
	for(int a=0;a<2;a++) {Bytes s;Put(s,int32_t(a));Put(s,int32_t(a));Put(s,uint32_t(a&&hideSecond?0:5));Put(s,0u);Append(c,Chunk(20,Chunk(1,s)));}
	return Chunk(16,c);
}
}
int main()
{
	using namespace visualmodel;int fail=0;auto check=[&](bool yes,const char *what){if(!yes){std::printf("FAIL: %s\n",what);++fail;}};
	Mesh mesh;std::string why;
	check(Read(Model(),mesh,why),"visual model parses multiple atomics instead of collision rectangles");
	check(mesh.vertices.size()==6&&mesh.faces.size()==2,"all visible geometries survive the bake input");
	if(mesh.vertices.size()==6) {
		check(mesh.vertices[0].x==3&&mesh.vertices[0].y==4&&mesh.vertices[0].z==5,"root frame transform is retained");
		check(mesh.vertices[3].x==13&&mesh.vertices[3].y==6&&mesh.vertices[3].z==6,"child and parent frame transforms compose");
		check(mesh.vertices[4].z==13,"pitched roof/eave elevation is retained");
		check(mesh.faces[1].a>=3&&mesh.faces[1].material==1,"flattened geometry and material indices remain distinct");
	}
	check(Read(Model(false,true),mesh,why)&&mesh.faces.size()==1,"invisible atomics are not resurrected on the map");
	check(!Read(Model(true),mesh,why)&&mesh.vertices.empty(),"cyclic frames fail without partial model geometry");
	auto broken=Model();broken.resize(broken.size()-8);check(!Read(broken,mesh,why),"truncated chunks fail safely");
	Instance instance{};instance.pos={10,20,30};instance.qw=std::sqrt(.5f);instance.qz=std::sqrt(.5f);
	const auto p=Place({1,0,2},instance);check(std::fabs(p.x-10)<.0001f&&std::fabs(p.y-19)<.0001f&&std::fabs(p.z-32)<.0001f,"IPL inverse quaternion placement matches the native world");
	instance.scale={2,3,4};const auto scaled=Place({1,0,2},instance);
	check(std::fabs(scaled.y-19)<.0001f&&std::fabs(scaled.z-32)<.0001f,"native object placement ignores exported IPL scale fields");
	Instance origin{};origin.qw=1;Mesh roof;
	roof.materials.push_back({"tiles",255});roof.vertices={{-3,-2,7},{3,-2,7},{-3,0,10},{3,0,10},{-3,2,7},{3,2,7}};
	roof.faces={{0,1,2,0},{1,3,2,0},{2,3,4,0},{3,5,4,0}};
	mapdata::Building house;
	check(Build(roof,origin,3,house)&&house.flags==mapdata::kVisualMesh,"visual roofs replace rectangular collision blocks");
	check(house.tris.size()==12&&house.edges.empty(),"eaves keep actual mesh faces without invented walls to ground");
	check(house.top==7&&house.lo.x==-3&&house.hi.x==3,"ridge and eave heights remain distinct at the placed street reference");
	Mesh crossing;crossing.materials=roof.materials;crossing.vertices={{-2,0,-1},{2,0,1},{0,2,1}};crossing.faces={{0,1,2,0}};
	mapdata::Building clipped;check(Build(crossing,origin,0,clipped)&&clipped.tris.size()==6,"bridge geometry crossing the map ground is clipped, not flattened into a false face");
	for(const Vec3 &v:clipped.verts)check(v.z>=0,"bridge piers cannot create inverted underwater walls");
	Mesh support;support.materials=roof.materials;support.vertices={{-2,0,0},{2,0,0},{0,0,12}};support.faces={{0,1,2,0}};
	check(Build(support,origin,10,clipped,true)&&clipped.flags==(mapdata::kVisualMesh|mapdata::kBridge),"bridges retain native above-water supports below their road deck");
	bool below=false;for(const Vec3 &v:clipped.verts)below|=v.z<-9.99f;check(below,"bridge origin is independent from the sea-level clipping floor");
	check(Build(support,origin,10,clipped,true,6),"bridge clipping uses the game's actual water level");
	for(const Vec3 &v:clipped.verts)check(v.z>=-4.001f,"geometry under the actual water surface is hidden");
	std::vector<BridgePart> parts{{{0,0},{30,20},10,true},{{20,0},{50,20},8.75f,false},{{200,0},{230,20},25,false}};
	AlignBridges(parts);check(parts[0].ground==10&&parts[1].ground==10,"connected bridge pieces share one native vertical reference");
	check(parts[2].ground==25,"separate hill bridges keep their own terrain reference");
	roof.materials[0]={"blueshade2_64",255};check(Build(roof,origin,3,house)&&house.tris.size()==12,"real shaded awnings retain their geometry despite a shade texture name");
	roof.materials[0].alpha=0;check(!Build(roof,origin,3,house),"invisible material geometry does not produce opaque map blocks");
	Mesh decal;decal.materials={{"blackshadow1",255}};decal.vertices={{0,0,5},{2,0,5},{0,2,5}};decal.faces={{0,1,2,0}};
	check(!Build(decal,origin,0,house),"verified horizontal baked shadow decals do not become building geometry");
	Instance timed{};timed.timeOn=5;timed.timeOff=23;check(ActiveAt(timed,12)&&!ActiveAt(timed,0),"day bridge geometry is selected at its native hours");
	timed.timeOn=23;timed.timeOff=5;check(!ActiveAt(timed,12)&&ActiveAt(timed,0),"mutually exclusive night bridge geometry is not baked over its day variant");
	roof.materials[0]={"tiles",255};Build(roof,origin,3,house);
	char path[]="/tmp/vcmm-visual-XXXXXX";const int fd=mkstemp(path);if(fd>=0)close(fd);
	mapdata::Map map;map.size=1;map.classes={mapdata::kLand};house.flags|=mapdata::kBridge;map.buildings={house};mapdata::Map loaded;
	check(map.Save(path)&&loaded.Load(path)&&loaded.buildings.size()==1&&loaded.buildings[0].flags==house.flags,"bake-to-runtime file retains visual mesh and bridge semantics");
	if(!loaded.buildings.empty())check(loaded.buildings[0].edges.empty()&&loaded.buildings[0].tris.size()==12,"runtime does not turn an eave mesh back into roof extrusion");
	map.buildings[0].tris[0]=65530;map.Save(path);check(!loaded.Load(path),"invalid triangle indices are rejected before runtime mesh drawing");
	Bytes legacy{'V','C','M','M','M','A','P','3'};Put(legacy,1u);Put(legacy,uint8_t(mapdata::kLand));Put(legacy,1u);
	Put(legacy,uint16_t(3));Put(legacy,uint16_t(1));Put(legacy,uint16_t(0));
	for(Vec3 v:std::array<Vec3,3>{{{0,0,5},{2,0,5},{0,2,5}}}){Put(legacy,v.x);Put(legacy,v.y);Put(legacy,v.z);}
	for(uint16_t i:{0,1,2})Put(legacy,i);Put(legacy,0u);
	if(FILE *f=std::fopen(path,"wb")){std::fwrite(legacy.data(),1,legacy.size(),f);std::fclose(f);}
	check(loaded.Load(path)&&loaded.buildings.size()==1&&!loaded.buildings[0].flags,"existing version3 map files remain readable before a rebake");
	unlink(path);
	return fail?1:(std::puts("visualmodel: all passed"),0);
}
