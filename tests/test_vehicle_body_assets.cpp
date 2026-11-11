// Complete installed vehicle catalog -> actual DFF frame hierarchy -> body.
// Catches admission limits, aircraft-only dimensions and corrupted source IDs.
#define main LearjetModelFixtures
#include "test_airportjetmodel.cpp"
#undef main
#include "../src/aircraftbodymodel.h"
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace vehicleasset {
using namespace aircraftbodymodel;
using BodySurface=aircraftbodymodel::Surface;
struct Catalog {int id;std::string name,type;Kind kind;};
const char *KindName(Kind kind){switch(kind){case Kind::Automobile:return "automobile";case Kind::Bike:return "bike";case Kind::Boat:return "boat";case Kind::Plane:return "plane";case Kind::Helicopter:return "helicopter";}return "unknown";}
std::vector<Catalog> Catalogs(const std::string &data){
 std::ifstream ide(data+"/data/default.ide");std::vector<Catalog> result;bool cars=false;
 for(std::string line;std::getline(ide,line);){if(line=="cars"||line=="cars\r"){cars=true;continue;}if(!cars)continue;if(line=="end"||line=="end\r")break;std::replace(line.begin(),line.end(),',',' ');std::istringstream in(line);Catalog c;std::string txd;if(!(in>>c.id>>c.name>>txd>>c.type))continue;
  c.kind=c.type=="bike"?Kind::Bike:c.type=="boat"?Kind::Boat:c.type=="plane"?Kind::Plane:c.type=="heli"?Kind::Helicopter:Kind::Automobile;
  for(int id:{155,177,195,199,217,218,227,231})if(c.id==id)c.kind=Kind::Helicopter;if(c.id==190||c.id==194)c.kind=Kind::Plane;result.push_back(c);
 }return result;
}
// The general vehicle catalog contains DFF material-list references. The
// Dodo/Learjet fixture reader assumes unique materials, so decode the complete
// native list here rather than dropping Flatbed's four referenced faces.
Geo *Geometry(const std::vector<uint8_t>&b,int wanted){
 Chunk geometry{};int index=0;for(auto c:Chunks(b,12,12+Get<uint32_t>(b,4)))if(c.id==26)for(auto g:Chunks(b,c.a,c.z))if(g.id==15&&index++==wanted)geometry=g;
 auto kids=Chunks(b,geometry.a,geometry.z);CHECK(!kids.empty());if(kids.empty())return nullptr;auto st=kids.front();size_t p=st.a;const auto flags=Get<uint32_t>(b,p),nt=Get<uint32_t>(b,p+4),nv=Get<uint32_t>(b,p+8);p+=16;const uint32_t lib=st.version&0xffff0000u?(((st.version>>14)&0x3ff00u)+0x30000u)|((st.version>>16)&0x3fu):st.version<<8;if(lib<0x34000)p+=12;
 auto g=static_cast<Geo*>(GeometryCreate(nv,nt,flags));if(flags&8){std::memcpy(g->colors.data(),b.data()+p,nv*4);p+=nv*4;}const unsigned uv=(flags>>16)&255?((flags>>16)&255):(flags&(4|128)?1:0);if(uv)std::memcpy(g->uv.data(),b.data()+p,nv*8);p+=size_t(nv)*uv*8;
 for(unsigned i=0;i<nt;++i,p+=8)g->triangles[i]={{Get<uint16_t>(b,p+2),Get<uint16_t>(b,p),Get<uint16_t>(b,p+6)},Get<uint16_t>(b,p+4)};
 p+=16;const auto hasVertices=Get<uint32_t>(b,p),hasNormals=Get<uint32_t>(b,p+4);p+=8;CHECK(hasVertices);std::memcpy(g->vertices.data(),b.data()+p,nv*12);p+=nv*12;if(hasNormals)std::memcpy(g->normals.data(),b.data()+p,nv*12);
 static std::map<std::string,Tex> textures;for(auto list:kids)if(list.id==8){auto mats=Chunks(b,list.a,list.z);CHECK(!mats.empty()&&mats.front().id==1);if(mats.empty())continue;const auto count=Get<uint32_t>(b,mats.front().a);size_t next=1;for(unsigned i=0;i<count;++i){const int32_t reference=Get<int32_t>(b,mats.front().a+4+i*4);if(reference>=0){CHECK(size_t(reference)<g->materials.size());if(size_t(reference)>=g->materials.size())continue;auto material=g->materials[reference];++material->refs;g->materials.push_back(material);continue;}while(next<mats.size()&&mats[next].id!=7)++next;CHECK(next<mats.size());if(next==mats.size())break;auto material=static_cast<Mat*>(MaterialCreate());for(auto c:Chunks(b,mats[next].a,mats[next].z)){if(c.id==1)material->color=Get<Color>(b,c.a+4);if(c.id==6)for(auto name:Chunks(b,c.a,c.z))if(name.id==2){std::string key(reinterpret_cast<const char*>(b.data()+name.a),strnlen(reinterpret_cast<const char*>(b.data()+name.a),name.z-name.a));auto &tex=textures[key];tex.name=key;material->texture=&tex;break;}}g->materials.push_back(material);++next;}}
 return g;
}
Clump Read(const std::string &data,const Catalog &catalog){
 ImgArchive img;CHECK(img.Open(data+"/models/gta3.img"));std::vector<uint8_t> b;CHECK(img.Read(catalog.name+".dff",b));Clump clump{};std::vector<Frame*> frames;
 for(auto c:Chunks(b,12,12+Get<uint32_t>(b,4)))if(c.id==14){unsigned ext=0;for(auto s:Chunks(b,c.a,c.z))if(s.id==1){const auto n=Get<uint32_t>(b,s.a);for(unsigned i=0;i<n;++i){auto f=static_cast<Frame*>(FrameCreate());const auto at=s.a+4+i*56;for(int column=0;column<4;++column)std::memcpy(f->matrix+column*4,b.data()+at+column*12,12);frames.push_back(f);}for(unsigned i=0;i<n;++i){const auto parent=Get<int32_t>(b,s.a+4+i*56+48);if(parent>=0&&size_t(parent)<frames.size())FrameChild(frames[parent],frames[i]);else{CHECK(parent==-1);clump.root=frames[i];}}}
  else if(s.id==3){CHECK(ext<frames.size());if(ext<frames.size())for(auto n:Chunks(b,s.a,s.z))if(n.id==0x253f2fe){const size_t length=std::min<size_t>(23,n.z-n.a);std::memcpy(frames[ext]->name,b.data()+n.a,length);frames[ext]->name[length]=0;}++ext;}}
 for(auto c:Chunks(b,12,12+Get<uint32_t>(b,4)))if(c.id==20)for(auto s:Chunks(b,c.a,c.z))if(s.id==1){const int frame=Get<int>(b,s.a);CHECK(frame>=0&&size_t(frame)<frames.size());if(frame<0||size_t(frame)>=frames.size())continue;auto a=static_cast<Atomic*>(AtomicCreate());a->frame=frames[frame];a->geometry=Geometry(b,Get<int>(b,s.a+4));a->flags=Get<unsigned>(b,s.a+8);clump.atomics.push_back(a);}CHECK(clump.root);return clump;
}
bool Visible(const Atomic &a){const char *n=a.frame->name;return (a.flags&4)&&!std::strstr(n,"_dam")&&!std::strstr(n,"_lo")&&!std::strstr(n,"_vlo");}
Role Classify(const char *n){if(std::strstr(n,"wheel")||std::strstr(n,"tire"))return Role::Wheel;if(std::strstr(n,"back_rotor")||std::strstr(n,"rearrotor")||std::strstr(n,"tail_rotor"))return Role::TailRotor;if(std::strstr(n,"rotor"))return Role::MainRotor;if(std::strstr(n,"door")||std::strstr(n,"bonnet")||std::strstr(n,"boot"))return Role::Door;return Role::Body;}
Vec3 V(CVector p){return {p.x,p.y,p.z};}Vec3 SubV(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}Vec3 AddV(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}Vec3 MulV(Vec3 a,float s){return {a.x*s,a.y*s,a.z*s};}
float DotV(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}Vec3 CrossV(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
void Grow(mechanicalmodel::Box &b,Vec3 p){b.lo={std::min(b.lo.x,p.x),std::min(b.lo.y,p.y),std::min(b.lo.z,p.z)};b.hi={std::max(b.hi.x,p.x),std::max(b.hi.y,p.y),std::max(b.hi.z,p.z)};}
std::vector<BodySurface> Surfaces(Clump &clump,Definition &definition,size_t &source,std::ostream *report,const Catalog &catalog){
 std::vector<BodySurface> surfaces;definition.bounds={{1e9f,1e9f,1e9f},{-1e9f,-1e9f,-1e9f}};source=0;
 for(size_t group=0;group<clump.atomics.size();++group){const auto a=clump.atomics[group];auto g=a->geometry;if(!g)continue;source+=g->triangles.size();mechanicalmodel::Box bounds{{1e9f,1e9f,1e9f},{-1e9f,-1e9f,-1e9f}};for(auto p:g->vertices)Grow(bounds,V(ModelPoint(a->frame,p)));
  if(report)*report<<catalog.id<<','<<catalog.name<<','<<KindName(catalog.kind)<<','<<a->frame->name<<','<<g->vertices.size()<<','<<g->triangles.size()<<','<<Visible(*a)<<','<<bounds.lo.x<<','<<bounds.lo.y<<','<<bounds.lo.z<<','<<bounds.hi.x<<','<<bounds.hi.y<<','<<bounds.hi.z<<'\n';
  if(!Visible(*a))continue;for(const auto &t:g->triangles){BodySurface s;s.group=uint32_t(group);s.role=Classify(a->frame->name);CHECK(t.material<g->materials.size());if(t.material>=g->materials.size())continue;s.glass=g->materials[t.material]->color.a<255;s.structural=!s.glass&&s.role!=Role::Wheel&&s.role!=Role::Door;
   for(int i=0;i<3;++i){CHECK(t.v[i]<g->vertices.size());s.points[i]=V(ModelPoint(a->frame,g->vertices[t.v[i]]));Grow(definition.bounds,s.points[i]);}surfaces.push_back(s);}
 }
 const char *wheels[]={"wheel_lf_dummy","wheel_lb_dummy","wheel_rf_dummy","wheel_rb_dummy"};for(int w=0;w<4;++w)if(auto f=Find(clump.root,wheels[w])){definition.wheels[w]=V(ModelPoint(f,{0,0,0}));definition.hasWheels=true;}
 return surfaces;
}
void FrameReport(const Catalog &catalog,const Clump &clump,Frame *frame,std::ostream &out){
 const float *m=LTM(frame);size_t atomics=0,triangles=0;for(auto a:clump.atomics)if(a->frame==frame){++atomics;if(a->geometry)triangles+=a->geometry->triangles.size();}
 out<<catalog.id<<','<<catalog.name<<','<<KindName(catalog.kind)<<','<<frame->name<<','<<(frame->parent?frame->parent->name:"")<<','<<atomics<<','<<triangles;for(int column=0;column<4;++column)for(int row=0;row<3;++row)out<<','<<m[column*4+row];out<<'\n';for(auto child:frame->children)FrameReport(catalog,clump,child,out);
}
float Area(const std::vector<aircraftconnectivity::Fragment> &fragments,uint32_t group){float area=0;for(const auto &f:fragments)if(f.group==group&&f.attached)for(size_t i=1;i+1<f.polygon.size();++i){const auto cross=CrossV(SubV(f.polygon[i].position,f.polygon[0].position),SubV(f.polygon[i+1].position,f.polygon[0].position));area+=.5f*std::sqrt(DotV(cross,cross));}return area;}
void Exercise(const Catalog &catalog,const std::vector<BodySurface>&surfaces,const Definition &definition,double &maxInit){
 Model model;auto start=std::chrono::steady_clock::now();const bool initialized=model.Initialize(surfaces,1500,definition);maxInit=std::max(maxInit,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());if(!initialized){std::printf("FAIL: body admission model=%d name=%s kind=%s surfaces=%zu\n",catalog.id,catalog.name.c_str(),KindName(catalog.kind),surfaces.size());++failures;return;}
 CHECK(model.Surfaces().size()==surfaces.size());CHECK(model.Fragments().size()<=16384);for(const auto &f:model.Fragments()){CHECK(f.source<surfaces.size());CHECK(f.group==surfaces[f.source].group);CHECK(f.attached);}
 std::map<uint32_t,float> wheels;for(const auto&s:surfaces)if(s.role==Role::Wheel)wheels.emplace(s.group,Area(model.Fragments(),s.group));
 // A short ray through an actual opaque upper/body sheet avoids arbitrary
 // aircraft coordinates and cannot accidentally target the engine box.
 const BodySurface *target=nullptr;float largest=0;for(const auto&s:surfaces)if(s.structural&&s.role==Role::Body){const auto cross=CrossV(SubV(s.points[1],s.points[0]),SubV(s.points[2],s.points[0]));const float area=DotV(cross,cross);if(area<=largest||area<1e-10f)continue;const auto centre=MulV(AddV(AddV(s.points[0],s.points[1]),s.points[2]),1.f/3),normal=MulV(cross,1/std::sqrt(area));bool tire=false;for(const auto&wheel:surfaces)if(wheel.role==Role::Wheel){const auto from=AddV(centre,MulV(normal,.025f)),delta=MulV(normal,-.05f),e=SubV(wheel.points[1],wheel.points[0]),f=SubV(wheel.points[2],wheel.points[0]),h=CrossV(delta,f);const float det=DotV(e,h);if(std::fabs(det)<1e-9f)continue;const auto v=SubV(from,wheel.points[0]),q=CrossV(v,e);const float u=DotV(v,h)/det,w=DotV(delta,q)/det,t=DotV(f,q)/det;tire|=u>=0&&w>=0&&u+w<=1&&t>=0&&t<=1;}if(!tire){target=&s;largest=area;}}
 CHECK(target&&largest>1e-10f);if(target){auto centre=MulV(AddV(AddV(target->points[0],target->points[1]),target->points[2]),1.f/3);auto normal=CrossV(SubV(target->points[1],target->points[0]),SubV(target->points[2],target->points[0]));normal=MulV(normal,1/std::sqrt(DotV(normal,normal)));const auto revision=model.Revision();const auto hit=model.Bullet(AddV(centre,MulV(normal,.025f)),AddV(centre,MulV(normal,-.025f)),200);const bool expected=definition.armor>1?hit.valid&&hit.deposited>0&&hit.holes==0:hit.valid&&hit.changed&&hit.holes>0&&model.Revision()>revision;if(!expected){std::printf("bullet outcome model=%d valid=%d changed=%d holes=%zu deposited=%.3f\n",catalog.id,hit.valid,hit.changed,hit.holes,hit.deposited);++failures;}}
 for(auto [group,area]:wheels){if(std::fabs(Area(model.Fragments(),group)-area)>=.00001f){std::printf("wheel area model=%d group=%u before=%.8f after=%.8f target_group=%u\n",catalog.id,group,area,Area(model.Fragments(),group),target?target->group:999);++failures;}for(const auto&s:surfaces)if(s.group==group)for(auto p:s.points){const auto q=model.Deform(p,group);CHECK(DotV(SubV(q,p),SubV(q,p))<1e-10f);}}
 const auto fire=model.Fire(false);CHECK(std::isfinite(fire.engine)&&std::isfinite(fire.breach));for(int i=0;i<3;++i)model.Tick(1.f/60,false,{0,0,0});
 for(const auto&f:model.Fragments()){CHECK(f.source<surfaces.size());for(const auto&p:f.polygon)CHECK(std::isfinite(p.position.x)&&std::isfinite(p.position.y)&&std::isfinite(p.position.z));}
}
}
#ifndef VEHICLE_BODY_ASSETS_NO_MAIN
int main(int argc,char **argv){
 using namespace vehicleasset;const char *env=std::getenv("GAMEDATA");const std::string data=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";auto catalogs=Catalogs(data);CHECK(catalogs.size()==107);std::ofstream csv;if(argc>1){csv.open(argv[1]);CHECK(csv.good());csv<<"model,name,class,frame,vertices,source_triangles,admitted_hi_detail,min_x,min_y,min_z,max_x,max_y,max_z\n";csv<<std::fixed<<std::setprecision(6);}
 std::ofstream frames;if(argc>2){frames.open(argv[2]);CHECK(frames.good());frames<<"model,name,class,frame,parent_frame,atomics,source_triangles,right_x,right_y,right_z,forward_x,forward_y,forward_z,up_x,up_y,up_z,position_x,position_y,position_z\n";frames<<std::fixed<<std::setprecision(6);}
 size_t source=0,visible=0,maximum=0;int largest=0;double maxInit=0;std::map<Kind,int> counts;
 for(const auto&c:catalogs){const int beforeFailures=failures;auto clump=Read(data,c);if(frames.is_open()&&clump.root)FrameReport(c,clump,clump.root,frames);Definition definition;definition.kind=c.kind;definition.model=c.id;definition.armor=(c.id==157||c.id==158||c.id==162)?8.f:1.f;size_t all=0;auto surfaces=Surfaces(clump,definition,all,csv.is_open()?&csv:nullptr,c);source+=all;visible+=surfaces.size();if(surfaces.size()>maximum){maximum=surfaces.size();largest=c.id;}++counts[c.kind];Exercise(c,surfaces,definition,maxInit);Destroy(clump);if(failures!=beforeFailures)std::printf("MODEL FAIL id=%d name=%s failures=%d\n",c.id,c.name.c_str(),failures-beforeFailures);}
 CHECK(liveGeometry==0&&liveMaterial==0&&liveFrame==0&&liveAtomic==0);std::printf("vehicle body actual catalog: %zu models, %d automobiles, %d bikes, %d boats, %d helicopters, %d planes; source=%zu admitted=%zu max=%zu(model=%d); max_init_ms=%.3f; failures=%d\n",catalogs.size(),counts[Kind::Automobile],counts[Kind::Bike],counts[Kind::Boat],counts[Kind::Helicopter],counts[Kind::Plane],source,visible,maximum,largest,maxInit,failures);return failures?1:0;
}

#endif
