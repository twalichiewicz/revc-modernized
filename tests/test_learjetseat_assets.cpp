// Shipped CAR_sit/player hierarchy -> actual first-person camera callback.
// No game process, input, image generation or hard-coded eye-height policy.
#define main AircraftCameraChecks
#include "test_aircraftcamerahook.cpp"
#undef main
#include "../tools/bake/gamefiles.h"
#include <map>

namespace {
struct Chunk {uint32_t kind;size_t begin,end;};
template<class T>T Read(const std::vector<uint8_t> &bytes,size_t at)
{
 T value{};CHECK(at<=bytes.size()&&sizeof value<=bytes.size()-at);
 if(at<=bytes.size()&&sizeof value<=bytes.size()-at)std::memcpy(&value,bytes.data()+at,sizeof value);
 return value;
}
constexpr uint32_t Four(char a,char b,char c,char d){return uint32_t(uint8_t(a))|(uint32_t(uint8_t(b))<<8)|(uint32_t(uint8_t(c))<<16)|(uint32_t(uint8_t(d))<<24);}
std::vector<Chunk> Chunks(const std::vector<uint8_t> &bytes,size_t begin,size_t end,size_t header=12)
{
 std::vector<Chunk> result;
 for(size_t at=begin;at<=end&&header<=end-at;){
  const size_t count=Read<uint32_t>(bytes,at+4),next=at+header+count;
  CHECK(next<=end&&next<=bytes.size());if(next>end||next>bytes.size())break;
  result.push_back({Read<uint32_t>(bytes,at),at+header,next});
  at=header==8?(next+3)&~size_t(3):next;
 }
 return result;
}
struct Basis {double r[3][3]{};CVector p{};};
Basis Compose(const Basis &a,const Basis &b)
{
 Basis result;
 for(int i=0;i<3;++i){
  for(int j=0;j<3;++j)for(int k=0;k<3;++k)result.r[i][j]+=a.r[i][k]*b.r[k][j];
 }
 result.p={float(a.p.x+a.r[0][0]*b.p.x+a.r[0][1]*b.p.y+a.r[0][2]*b.p.z),
           float(a.p.y+a.r[1][0]*b.p.x+a.r[1][1]*b.p.y+a.r[1][2]*b.p.z),
           float(a.p.z+a.r[2][0]*b.p.x+a.r[2][1]*b.p.y+a.r[2][2]*b.p.z)};
 return result;
}
Basis Rotation(const float *q)
{
 // Native CAnimManager::LoadAnimFile inverts the IFP key quaternion.
 const double x=-q[0],y=-q[1],z=-q[2],w=q[3];
 return {{{1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)},
          {2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)},
          {2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)}},{}};
}
struct Key {Basis local{};bool translated=false;};
std::map<int,Key> Animation(const std::vector<uint8_t> &ifp)
{
 CHECK(Read<uint32_t>(ifp,0)==Four('A','N','P','K'));
 bool named=false;std::map<int,Key> result;
 for(const auto &chunk:Chunks(ifp,8,8+Read<uint32_t>(ifp,4),8)){
  if(chunk.kind==Four('N','A','M','E'))named=chunk.end-chunk.begin==8&&!std::memcmp(ifp.data()+chunk.begin,"CAR_sit",8);
  if(chunk.kind!=Four('D','G','A','N')||!named)continue;
  for(const auto &component:Chunks(ifp,chunk.begin,chunk.end,8)){
   if(component.kind!=Four('C','P','A','N'))continue;
   const auto parts=Chunks(ifp,component.begin,component.end,8);
   CHECK(parts.size()==2);if(parts.size()!=2)continue;
   CHECK(parts[0].kind==Four('A','N','I','M')&&parts[0].end-parts[0].begin==44);
   CHECK(Read<uint32_t>(ifp,parts[0].begin+28)==2);
   std::array<float,4> quaternion{};
   for(int i=0;i<4;++i)quaternion[size_t(i)]=Read<float>(ifp,parts[1].begin+size_t(i)*4);
   Key key;key.local=Rotation(quaternion.data());
   key.translated=parts[1].kind==Four('K','R','T','S')||parts[1].kind==Four('K','R','T','0');
   if(key.translated)key.local.p=Read<CVector>(ifp,parts[1].begin+16);
   else CHECK(parts[1].kind==Four('K','R','0','0'));
   result[int(Read<uint32_t>(ifp,parts[0].begin+40))]=key;
  }
  break;
 }
 CHECK(result.size()==22);return result;
}
std::map<int,CVector> Pose(const std::vector<uint8_t> &dff,const std::map<int,Key> &keys)
{
 std::map<int,CVector> result;
 for(const auto &clump:Chunks(dff,0,dff.size()))if(clump.kind==16)
  for(const auto &frames:Chunks(dff,clump.begin,clump.end))if(frames.kind==14){
   const auto parts=Chunks(dff,frames.begin,frames.end);CHECK(!parts.empty());if(parts.empty())return result;
   const size_t count=Read<uint32_t>(dff,parts[0].begin);CHECK(count==25&&parts.size()==count+1);if(parts.size()!=count+1)return result;
   std::vector<Basis> world(count);
   for(size_t index=0;index<count;++index){
    const size_t at=parts[0].begin+4+index*56;Basis local;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)local.r[i][j]=Read<float>(dff,at+size_t(j*3+i)*4);
    local.p=Read<CVector>(dff,at+36);int tag=-1;
    for(const auto &extension:Chunks(dff,parts[index+1].begin,parts[index+1].end))if(extension.kind==0x11e)tag=Read<int32_t>(dff,extension.begin+4);
    const auto key=keys.find(tag);
    if(key!=keys.end()){
     const CVector translation=local.p;local=key->second.local;
     if(!key->second.translated)local.p=translation;
    }
    const int parent=Read<int32_t>(dff,at+48);CHECK(parent<0||size_t(parent)<index);
    world[index]=parent>=0?Compose(world[size_t(parent)],local):local;
    if(tag>=0)result[tag]=world[index].p;
   }
   return result;
  }
 CHECK(false);return result;
}
}
int main()
{
 const char *env=std::getenv("GAMEDATA");
 const std::string base=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";
 ImgArchive archive;CHECK(archive.Open(base+"/models/gta3.img"));
 std::vector<uint8_t> player,ifp;CHECK(archive.Read("player.dff",player));CHECK(ReadGameFile(base+"/anim/ped.ifp",ifp));
 if(gFailures)return 1;
 const auto pose=Pose(player,Animation(ifp));const auto neck=pose.at(4),head=pose.at(5);
 // Native trace flight-presentation-controlled, model248, root(-.32,5,-.65):
 // neck(-.330,4.779,-.171), independent of the derived head/camera values.
 CHECK(std::fabs(neck.x+.009864f)<.00001f&&std::fabs(neck.y+.221146f)<.00001f&&std::fabs(neck.z-.479002f)<.00001f);
 CHECK(std::fabs(head.x+.014334f)<.00001f&&std::fabs(head.y+.251221f)<.00001f&&std::fabs(head.z-.605372f)<.00001f);
 camera::Install();SeatPlacement(head);
 std::printf("native CAR_sit head relative root(%+.6f,%+.6f,%+.6f), Learjet camera local(-.334334,4.748779,+.003372)\n",head.x,head.y,head.z);
 if(!gFailures)std::puts("Learjet native seat assets -> camera consumer: all passed");
 return gFailures?1:0;
}
