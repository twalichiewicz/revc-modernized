// Compose the actual readonly RW/mesh producer with the real rotor impact hook.
// The reused fixture provides native-shaped pools, callbacks and installed DFFs.
#include "../src/log.h"
#define main ShotcollisionBaselineMain
#include "test_shotcollision.cpp"
#undef main
#include "../src/flightmodel.cpp"
Config gConfig;
namespace planeflight {bool Apply(uint8_t *,int){return false;}}
static unsigned gPoliceFailures,gNativeImpacts;
static CVector gNativeBladeOrigin;
namespace heli {bool FailRotor(uint8_t *v,bool){if(game::EntityModel(v)!=165)return false;++gPoliceFailures;return true;}}
namespace {
void *SetAtomicFlags(void *a,unsigned f){static_cast<fixture::Atomic *>(a)->flags=f;return a;}
void NativeImpact(uint8_t *,uint8_t *,uint8_t *,CVector *,CVector *,uint8_t *,CVector2D){++gNativeImpacts;}
bool AssetWorld(const CVector &,const CVector &,uint8_t *,uint8_t *&,bool,bool,bool,bool,bool,bool,bool,bool){return false;}
bool NativeBlade(uint8_t *,CVector at,uint8_t *,int16_t,float,float){gNativeBladeOrigin=at;return false;}
void OriginCheck(const char *model,float expected)
{
 using namespace fixture;ImgArchive img;std::vector<uint8_t> b;
 const char *env=std::getenv("GAMEDATA");const std::string base=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";
 Check(img.Open(base+"/models/gta3.img")&&img.Read(std::string(model)+".dff",b),"actual rotor frame hierarchy loads");
 std::vector<Frame> frames;std::vector<int> parents;CVector bonnet{};
 for(const auto &c:Chunks(b,12,12+Get<unsigned>(b,4)))if(c.type==14){
  auto ch=Chunks(b,c.start,c.end);const unsigned n=Get<unsigned>(b,ch[0].start);frames.resize(n);parents.resize(n);
  for(unsigned i=0;i<n;++i){const size_t p=ch[0].start+4+i*56;Identity(frames[i].matrix.data());
   for(int col=0;col<4;++col)for(int row=0;row<3;++row)frames[i].matrix[col*4+row]=Get<float>(b,p+(col*3+row)*4);
   parents[i]=Get<int>(b,p+48);for(const auto &ext:Chunks(b,ch[i+1].start,ch[i+1].end))if(ext.type==0x253f2fe)frames[i].name=std::string(reinterpret_cast<char *>(b.data()+ext.start),ext.end-ext.start).c_str();
   if(frames[i].name=="bonnet_dummy")bonnet={frames[i].matrix[12],frames[i].matrix[13],frames[i].matrix[14]};
  }
 }
 for(size_t i=0;i<frames.size();++i)frames[i].parent=parents[i]<=0?&clump.root:&frames.at(parents[i]);
 for(auto &a:clump.atomics)if(a.frame.name=="toprotor"){
  const auto found=std::find_if(frames.begin(),frames.end(),[](const Frame &f){return f.name=="toprotor";});Check(found!=frames.end(),"actual visible toprotor frame exists");if(found==frames.end())return;
  const auto saved=a.frame;a.frame=*found;flightmodel::Hook_Blade(cars.data(),bonnet,nullptr,3,6.15f,1);a.frame=saved;
  Check(std::fabs(gNativeBladeOrigin.z-expected)<.0001f,"native blade collision receives current actual drawn rotor center");
  printf("rotor origin %s: nativebonnet%.4f -> visible%.4f\n",model,bonnet.z,gNativeBladeOrigin.z);return;
 }
 Check(false,"actual toprotor atomic exists for native collision origin");
}
bool RotorRay(Vec3 &from,Vec3 &to){
 using namespace fixture;
 for(auto &a:clump.atomics){if(!flightmodel::RotorFrame(&a.frame))continue;for(const auto &face:a.geo.tris){
  const V x=a.geo.verts[face.v[0]],y=a.geo.verts[face.v[1]],z=a.geo.verts[face.v[2]];
  const CVector center{(x.x+y.x+z.x)/3,(x.y+y.y+z.y)/3,(x.z+y.z+z.z)/3};
  CVector n=shotcollision::Cross({y.x-x.x,y.y-x.y,y.z-x.z},{z.x-x.x,z.y-x.y,z.z-x.z});const float len=std::sqrt(shotcollision::Dot(n,n));
  if(len<1e-6f||std::hypot(center.x,center.y)<3||flightmodel::RotorFrame(&a.frame)!=1)continue;n=shotcollision::Mul(n,1/len);
  from={center.x+n.x*.2f,center.y+n.y*.2f,center.z+n.z*.2f};to={center.x-n.x*.2f,center.y-n.y*.2f,center.z-n.z*.2f};shotcollision::MeshHit hit;
  if(shotcollision::TraceVehicleMesh(cars.data(),from,to,hit)&&flightmodel::RotorFrame(const_cast<void *>(hit.frame))==1)return true;
 }}return false;
}
}
int main(){
 using namespace fixture;Setup();Check(shotcollision::Install(),"readonly actual mesh bindings installed");
 flightmodel::gPool=reinterpret_cast<uint8_t **>(&cp);flightmodel::gAtomics=Atomics;flightmodel::gAtomicFrame=AFrame;
 flightmodel::gName=Name;flightmodel::gParent=Parent;flightmodel::gFlags=Flags;flightmodel::gSetFlags=SetAtomicFlags;flightmodel::gImpact=NativeImpact;
 flightmodel::gFrameMatrix=FrameMatrix;flightmodel::gRoot=Root;flightmodel::gBlade=NativeBlade;
 uint8_t *scripted[4]{};flightmodel::gScriptedHelis=scripted;game::api.ProcessLineOfSight=AssetWorld;gConfig.heliDamage=true;
 for(const auto &asset:{std::pair<const char *,int>{"hunter",155},{"seaspar",177},{"sparrow",199},{"maverick",217},{"vcnmav",218},{"polmav",227},{"chopper",165}}){
  Setup();Check(LoadAircraftParts(asset.first),"actual installed helicopter split into named native atomics");flightmodel::gRotors.clear();
  auto *vehicle=cars.data();*reinterpret_cast<int16_t *>(vehicle+0x6a)=asset.second;*reinterpret_cast<float *>(vehicle+0x2a0)=1000;scripted[0]=asset.second==165?vehicle:nullptr;gNativeImpacts=0;
  *reinterpret_cast<int *>(vehicle+0x348)=asset.second==165?3:0;
  if(asset.second!=165)OriginCheck(asset.first,asset.second==155?1.971f:(asset.second==177||asset.second==199)?1.608f:2.3179173f);
  game::api.ProcessLineOfSight=AssetWorld;Vec3 a{},b{};Check(RotorRay(a,b),"visible actual rotor triangle supplies captured impact segment");
  CVector from{a.x,a.y,a.z},to{b.x,b.y,b.z};uint32_t weapon=26;alignas(16) uint8_t point[0x30]{};
#if __has_feature(address_sanitizer)
  if(asset.second==165)__asan_poison_memory_region(vehicle+0x430,0x6f0-0x430);
#endif
  for(int i=0;i<3;++i)flightmodel::Hook_Impact(reinterpret_cast<uint8_t *>(&weapon),peds.data()+0x8d0*2,nullptr,&from,&to,point,{});
  Check(flightmodel::RotorFailed(vehicle)&&gNativeImpacts==0&&*reinterpret_cast<float *>(vehicle+0x2a0)==1000,"actual mesh -> real rotor hook destroys main rotor while preserving hull/native impact ownership");
  bool mainVisible=false,bodyVisible=false;for(auto &atomic:clump.atomics){if(flightmodel::RotorFrame(&atomic.frame)==1)mainVisible|=(atomic.flags&4)!=0;else if(flightmodel::RotorFrame(&atomic.frame)==0)bodyVisible|=(atomic.flags&4)!=0;}
  Check(!mainVisible&&bodyVisible,"actual solid and blurred rotor parts disappear; fuselage survives");
#if __has_feature(address_sanitizer)
  if(asset.second==165)__asan_unpoison_memory_region(vehicle+0x430,0x6f0-0x430);
#endif
  printf("rotor asset %s: contact (%.3f,%.3f,%.3f), main hidden\n",asset.first,(a.x+b.x)/2,(a.y+b.y)/2,(a.z+b.z)/2);
 }
 Check(gPoliceFailures==1,"scripted CHeli routes one failure through its own controller API without automobile offsets");
 printf("rotorassets: %d failures\n",fail);return fail?1:0;
}
