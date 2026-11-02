// Read-only ABI/data evidence. This never launches or attaches to the game.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include "../tools/bake/gamefiles.h"
#include "../tools/bake/visualmodel.h"
namespace {
int failures=0;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);++failures;}}while(0)
struct Image {
 std::vector<uint8_t> bytes;std::map<std::string,uint64_t> symbols;
 template<class T>T At(size_t offset)const{T value{};CHECK(offset<=bytes.size()&&sizeof value<=bytes.size()-offset);if(offset<=bytes.size()&&sizeof value<=bytes.size()-offset)std::memcpy(&value,bytes.data()+offset,sizeof value);return value;}
 uint32_t Instruction(uint64_t address)const{
  auto header=At<mach_header_64>(0);size_t cursor=sizeof header;
  for(uint32_t i=0;i<header.ncmds&&i<4096;++i){auto command=At<load_command>(cursor);if(command.cmdsize<sizeof command)break;
   if(command.cmd==LC_SEGMENT_64){auto segment=At<segment_command_64>(cursor);if(address>=segment.vmaddr&&address-segment.vmaddr+4<=segment.filesize)return At<uint32_t>(size_t(segment.fileoff+address-segment.vmaddr));}
   cursor+=command.cmdsize;
  }CHECK(false);return 0;
 }
 uint64_t Branch(uint64_t address)const{const auto word=Instruction(address);CHECK((word&0xfc000000)==0x94000000);const int32_t delta=int32_t((word&0x3ffffff)<<6)>>4;return uint64_t(int64_t(address)+delta);}
 bool Load(const std::string &path){
  std::ifstream file(path,std::ios::binary|std::ios::ate);CHECK(bool(file));if(!file)return false;
  auto size=file.tellg();CHECK(size>0&&size<128*1024*1024);if(failures)return false;bytes.resize(size_t(size));file.seekg(0);file.read(reinterpret_cast<char *>(bytes.data()),size);
  const auto header=At<mach_header_64>(0);CHECK(header.magic==MH_MAGIC_64&&header.cputype==CPU_TYPE_ARM64);size_t cursor=sizeof header;bool uuid=false;
  for(uint32_t i=0;i<header.ncmds&&i<4096;++i){auto command=At<load_command>(cursor);if(command.cmdsize<sizeof command)break;
   if(command.cmd==LC_UUID){const auto value=At<uuid_command>(cursor);constexpr uint8_t expected[]={0x02,0x98,0x3a,0x69,0x6b,0xd0,0x33,0xf7,0xb7,0x2b,0x2a,0xca,0x8b,0xcb,0x3c,0x33};uuid=!std::memcmp(value.uuid,expected,16);}
   if(command.cmd==LC_SYMTAB){const auto table=At<symtab_command>(cursor);for(uint32_t n=0;n<table.nsyms;++n){const auto symbol=At<nlist_64>(table.symoff+size_t(n)*sizeof(nlist_64));const size_t at=table.stroff+symbol.n_un.n_strx;if(at<bytes.size()){const size_t len=strnlen(reinterpret_cast<const char *>(bytes.data()+at),bytes.size()-at);if(len<bytes.size()-at)symbols[reinterpret_cast<const char *>(bytes.data()+at)]=symbol.n_value;}}}
   cursor+=command.cmdsize;
  }CHECK(uuid);return failures==0;
 }
};
void Contract(const Image &image){
 const std::pair<const char *,uint64_t> functions[]={
 {"__ZN9CRenderer19ConstructRenderListEv",0x10020b9f0},{"__ZN9CRenderer26SetupBigBuildingVisibilityEP7CEntity",0x10020b5b4},
 {"__ZN9CRenderer21SetupEntityVisibilityEP7CEntity",0x10020b02c},
 {"__ZN16CSimpleModelInfo26GetFirstAtomicFromDistanceEf",0x10017ac28},{"__Z23RwCameraSetFarClipPlanePN2rw6CameraEf",0x10016fbc0},
 {"__Z22RwCameraSetFogDistancePN2rw6CameraEf",0x10016fc20},{"__ZN7CClouds13RenderHorizonEv",0x1001e0410},
 {"__ZN9CRenderer17RenderOneBuildingEP7CEntityf",0x10020a678},{"__ZN18CVisibilityPlugins18RenderFadingAtomicEPN2rw6AtomicEf",0x100236cb4},{"__ZN18CVisibilityPlugins18GetAtomicModelInfoEPN2rw6AtomicE",0x100236efc},{"__ZN16CSimpleModelInfo21GetAtomicFromDistanceEf",0x10017abc0},{"__ZN9CRenderer19ScanBigBuildingListER8CPtrList",0x10020cc98},{"__ZN16CSimpleModelInfo21GetLargestLodDistanceEv",0x10017ab68}};
 for(auto [name,address]:functions){auto it=image.symbols.find(name);CHECK(it!=image.symbols.end()&&it->second==address);}
 // Renderer cameraXYZ, entityXYZ, vertical square, then first-atomic consumer.
 CHECK(image.Instruction(0x10020b654)==0x2d400500);CHECK(image.Instruction(0x10020b66c)==0x1e233842);CHECK(image.Instruction(0x10020b678)==0x1f020040);
 CHECK(image.Branch(0x10020b688)==0x10017ab50);CHECK(image.Branch(0x10020b70c)==0x10017ac28);
 // Native stream policy repeats the same 3D vertical leg and largest LOD;
 // this proves merely changing RW's far clip cannot request missing ground.
 CHECK(image.Instruction(0x10020d7ec)==0xbd404002);CHECK(image.Instruction(0x10020d7f0)==0xbd400823);CHECK(image.Instruction(0x10020d800)==0x1f020040);
 CHECK(image.Branch(0x10020d828)==0x10017ab68);
 // Simple-model first LOD +0x48 and camera factor+0xf8; one native atomic.
 CHECK(image.Instruction(0x10017ac28)==0xbd404801);CHECK(image.Instruction(0x10017ac34)==0xbd40f902);CHECK(image.Instruction(0x10017ac38)==0x1e220821);
 // Native request pruning and dependency conversion require keep flags on
 // both the DFF and its separately read TXD. Size is streamInfo+0x18 sectors.
 CHECK(image.Instruction(0x10011e048)==0x39404509);CHECK(image.Instruction(0x10011e04c)==0x72000d3f);
 CHECK(image.Instruction(0x100121b54)==0x7200091f);CHECK(image.Instruction(0x100125c44)==0xb9401b88);
 // Idle's second far/fog reset must receive the same correction; setters on
 // shadow cameras and all other callers retain their own clipping budgets.
 CHECK(image.Branch(0x100141fcc)==0x10016fbc0);CHECK(image.Branch(0x100141fe0)==0x10016fc20);
 CHECK(image.Branch(0x10020bb28)==0x10016fc18); // ScanWorld reads RW far
 CHECK(image.Instruction(0x1000d4de8)==0xbd400104); // IsSphereVisible reads CDraw far
 CHECK(image.Branch(0x10020c324)==0x10020cc98); // shared BIG list
 CHECK(image.Branch(0x10020c318)==0x10020cc98); // selected island list
 CHECK(image.Instruction(0x10020cd2c)==0xf9400a94); // CPtrNode::next+0x10
 CHECK(image.Instruction(0x100228b78)==0x52a89f48); // ocean radius capped2000 above60m
 CHECK(image.Instruction(0x1001e0518)==0x1e18e408); // horizon floor ambient*128
 // Crash2811: BIG enqueues ORIGINAL distance, the later consumer loads
 // AlphaObjectInfo+8, subtracts20, uses a DIFFERENT atomic selector, and
 // dereferences its geometry without checking null. The new fade hook must
 // carry the instance admission through this exact native consumer chain.
 CHECK(image.Branch(0x10020b9a8)==0x10023653c);
 CHECK(image.Instruction(0x10020b9a4)==0x4ea81d00); // s8 queue distance
 CHECK(image.Instruction(0x100236c64)==0xbd400aa0); // queued float+8
 CHECK(image.Branch(0x100236c68)==0x100236cb4);
 CHECK(image.Instruction(0x100236cdc)==0x1e369001); // -20
 CHECK(image.Instruction(0x100236ce0)==0x1e212808); // d-20
 CHECK(image.Branch(0x100236cec)==0x10017abc0);
 CHECK(image.Branch(0x100236d0c)==0x10017ab68); // same distance drives alpha
 CHECK(image.Instruction(0x100236d10)==0x1e283800); // largest-(d-20)
 CHECK(image.Branch(0x100236d58)==0x100170ec0); // nullable result -> geometry
 CHECK(image.Instruction(0x100236f04)==0xf8686800); // real atomic MI plugin
 CHECK(image.Branch(0x10020a734)==0x10017abc0); // WorldRender duplicate selector
 CHECK(image.Branch(0x10020a788)==0x100170ec0); // duplicate nullable geometry
 CHECK(image.Branch(0x10020a954)==0x10020a678); // queued original distance
 CHECK(image.Branch(0x10020aa50)==0x10020a678);
 // Ordinary high-detail originals use a separate selector but enqueue the
 // same physical sort distance into the same later fade consumer.
 CHECK(image.Instruction(0x10020b130)==0x1f020040);
 CHECK(image.Branch(0x10020b168)==0x10017abc0);CHECK(image.Branch(0x10020b324)==0x10017abc0);
 CHECK(image.Branch(0x10020b23c)==0x10023653c);
 // Visual streaming stops above55m; current-player COL streaming does not.
 CHECK(image.Branch(0x10011de9c)==0x10011e32c);
 CHECK(image.Branch(0x10011e000)==0x1000484a8);CHECK(image.Branch(0x10011e010)==0x100048abc);
 // CPlane inherits CPhysical's ordinary COL consumer. It reads both MIs'
 // COLLISION pointers and calls the native narrowphase without an RW test.
 CHECK(image.Instruction(0x100155b1c)==0xf9401101);CHECK(image.Instruction(0x100155b28)==0xf9401103);
 CHECK(image.Branch(0x100155b3c)==0x10004c40c);
 // The broad mission query reads one declared SCM global; it is not an
 // actor/controller/camera ownership query. This same flag is used by CELL.
 CHECK(image.Instruction(0x10008d464)==0xb9495508);
 CHECK(image.Instruction(0x10008d474)==0xb8686928);
 CHECK(image.Instruction(0x10008d478)==0x7100051f);
 CHECK(image.Instruction(0x1000929b8)==0xb900012a);
 std::printf("native ABI: exact vanilla UUID,10callbacks,BIG+original admission→queue→fade,visual55m/COL streaming,Idle clipping verified\n");
}
void Assets(const std::string &data){
 std::ifstream ide(data+"/data/maps/airport/airport.ide");CHECK(bool(ide));std::map<std::string,std::pair<int,float>> lods;
 for(std::string line;std::getline(ide,line);){std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);int id=0,count=0,flags=0;std::string name,txd;float lod=0;if(row>>id>>name>>txd>>count>>lod>>flags)if(name.rfind("LOD",0)==0){CHECK(count==1);lods[name]={id,lod};}}
 CHECK(lods.count("LODrland01")&&lods["LODrland01"].second==1700);CHECK(lods.count("LODsmallradar1")&&lods["LODsmallradar1"].second==700);
 // The actual source archive supplies each native low-detail model; no new
 // terrain assets are invented and no source IMG is modified.
 std::ifstream directory(data+"/models/gta3.dir",std::ios::binary);CHECK(bool(directory));std::map<std::string,uint32_t> archive;
 for(std::array<uint8_t,32> entry{};directory.read(reinterpret_cast<char *>(entry.data()),entry.size());){uint32_t sectors=0;std::memcpy(&sectors,entry.data()+4,4);std::string name(reinterpret_cast<const char *>(entry.data()+8),strnlen(reinterpret_cast<const char *>(entry.data()+8),24));std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});archive[name]=sectors;}
 CHECK(archive.count("lodrland01.dff")&&archive["lodrland01.dff"]>0);
 std::ifstream scm(data+"/data/main.scm",std::ios::binary);CHECK(bool(scm));
 const std::vector<uint8_t> script((std::istreambuf_iterator<char>(scm)),{});
 auto bytes=[&](size_t at,std::initializer_list<uint8_t> expected){return at<=script.size()&&expected.size()<=script.size()-at&&std::equal(expected.begin(),expected.end(),script.begin()+at);};
 CHECK(bytes(0x9ef7,{0x80,0x01,0x02,0xe4,0x04})); // declare mission global0x4e4
 CHECK(bytes(0x28cde,{0x04,0x00,0x02,0xe4,0x04,0x04,0x01})); // CELL ring sets1
 CHECK(bytes(0x290d2,{0x04,0x00,0x02,0xe4,0x04,0x04,0x00})); // CELL cleanup clears0
 std::printf("native airport: %zu single-atomic BIG LOD definitions;ground1700m,structures700m;IMG ground%uKiB\n",lods.size(),archive["lodrland01.dff"]*2);
}
Vec3f Difference(Vec3f a,Vec3f b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
float Dot(Vec3f a,Vec3f b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3f Cross(Vec3f a,Vec3f b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
bool RayTriangle(Vec3f a,Vec3f b,Vec3f x,Vec3f y,Vec3f z){
 const auto d=Difference(b,a),e=Difference(y,x),f=Difference(z,x),h=Cross(d,f);const float det=Dot(e,h);
 if(std::fabs(det)<1e-7f)return false;const auto s=Difference(a,x);const float u=Dot(s,h)/det;if(u<0||u>1)return false;
 const auto q=Cross(s,e);const float v=Dot(d,q)/det;if(v<0||u+v>1)return false;const float t=Dot(f,q)/det;return t>=0&&t<=1;
}
bool CollisionRay(const ColModel &model,const Instance &in,Vec3f a,Vec3f b){
 for(const auto &f:model.faces)if(RayTriangle(a,b,visualmodel::Place(model.verts[f.a],in),visualmodel::Place(model.verts[f.b],in),visualmodel::Place(model.verts[f.c],in)))return true;
 // Both exact fixtures have identity IPL rotations. Test the actual COL
 // boxes too, so this cannot mistake a box facade for absent collision.
 CHECK(in.qx==0&&in.qy==0&&in.qz==0&&in.qw==1);
 for(const auto &box:model.boxes){float near=0,far=1;bool valid=true;const float at[3]={a.x-in.pos.x,a.y-in.pos.y,a.z-in.pos.z},
  delta[3]={b.x-a.x,b.y-a.y,b.z-a.z},lo[3]={box.min.x,box.min.y,box.min.z},hi[3]={box.max.x,box.max.y,box.max.z};
  for(int i=0;i<3;++i){if(std::fabs(delta[i])<1e-7f){if(at[i]<lo[i]||at[i]>hi[i])valid=false;continue;}
   float l=(lo[i]-at[i])/delta[i],h=(hi[i]-at[i])/delta[i];if(l>h)std::swap(l,h);near=std::max(near,l);far=std::min(far,h);
  }if(valid&&near<=far)return true;
 }return false;
}
void SkylineAssets(const std::string &data){
 ImgArchive archive;CHECK(archive.Open(data+"/models/gta3.img"));std::vector<uint8_t> bytes;CHECK(archive.Read("downtown.col",bytes));
 std::map<std::string,ColModel> cols;ParseColArchive(bytes,cols);int count=0;
 for(const auto &in:LoadInstances(data))if(in.model=="doontoon22"||in.model=="doontoon39"){
  ++count;visualmodel::Mesh mesh;std::string why;CHECK(archive.Read(in.model+".dff",bytes)&&visualmodel::Read(bytes,mesh,why));
  const auto &col=cols.at(in.model);CHECK(!col.faces.empty()&&!col.boxes.empty());
  for(float z:{180.0f,250.0f,300.0f}){
   const Vec3f a{in.pos.x-150,in.pos.y,z},b{in.pos.x+150,in.pos.y,z};bool visible=false;
   for(const auto &f:mesh.faces)if(mesh.materials[f.material].alpha>=64&&RayTriangle(a,b,visualmodel::Place(mesh.vertices[f.a],in),visualmodel::Place(mesh.vertices[f.b],in),visualmodel::Place(mesh.vertices[f.c],in))){visible=true;break;}
   CHECK(visible);CHECK(CollisionRay(col,in,a,b)==(z==180));
  }
  if(in.model=="doontoon39"){
   // The original has a real crossed radio-mast surface rising94.85m
   // above the roof. Its LOD proxy omits that entire structure.
   float top=-INFINITY,proxyTop=-INFINITY;int mastFaces=0;
   for(const auto &f:mesh.faces){const auto a=visualmodel::Place(mesh.vertices[f.a],in),b=visualmodel::Place(mesh.vertices[f.b],in),c=visualmodel::Place(mesh.vertices[f.c],in);
    const float max=std::max({a.z,b.z,c.z});top=std::max(top,max);
    if(max>450&&mesh.materials[f.material].texture=="roof06l256")++mastFaces;
   }
   visualmodel::Mesh proxy;CHECK(archive.Read("lodntoon39.dff",bytes)&&visualmodel::Read(bytes,proxy,why));
   for(auto p:proxy.vertices)proxyTop=std::max(proxyTop,visualmodel::Place(p,in).z);
   CHECK(std::fabs(top-467.2043f)<.02f&&mastFaces==4);CHECK(proxyTop<372&&top-proxyTop>95);
   std::printf("actual tower1882: original mast%.2fm,proxy%.2fm; facade rays180mCOLhit,250/300mCOLmiss\n",top,proxyTop);
  }
 }CHECK(count==2);
}
}
int main(){
 const char *env=std::getenv("MACVC_NATIVE"),*home=std::getenv("HOME"),*data=std::getenv("GAMEDATA");
 const std::string binary=env?env:std::string(home?home:"")+"/Library/Application Support/macVC/vcmodernhud/macVC.vanilla";
 Image image;if(!image.Load(binary))return 1;Contract(image);const std::string gameData=data?data:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";Assets(gameData);SkylineAssets(gameData);
 if(!failures)std::puts("airworld native ABI/assets: all passed");return failures?1:0;
}
