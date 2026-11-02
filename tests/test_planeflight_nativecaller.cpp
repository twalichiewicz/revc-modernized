// Read the installed native caller, then dispatch its decoded argument through
// the actual shared hook and plane adapter. This checks the external ABI rather
// than choosing a flight enum inside a force-only fixture.
#define main SharedFlightChecks
#include "test_flightmodelhook.cpp"
#undef main
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mach-o/loader.h>
#include <vector>

namespace {
struct NativeImage {
 std::vector<uint8_t> bytes;
 template<class T> T At(size_t at) const {
  T value{};
  Check(at<=bytes.size()&&sizeof value<=bytes.size()-at,"native image read lies in file");
  if(at<=bytes.size()&&sizeof value<=bytes.size()-at)std::memcpy(&value,bytes.data()+at,sizeof value);
  return value;
 }
 uint32_t Instruction(uint64_t address) const {
  const auto header=At<mach_header_64>(0);
  Check(header.magic==MH_MAGIC_64,"native caller image is thin Mach-O64");
  size_t at=sizeof header;
  for(uint32_t i=0;i<header.ncmds&&i<4096;++i){
   const auto command=At<load_command>(at);
   if(command.cmdsize<sizeof command||at>bytes.size()||command.cmdsize>bytes.size()-at)break;
   if(command.cmd==LC_SEGMENT_64){
    const auto segment=At<segment_command_64>(at);
    if(address>=segment.vmaddr&&address-segment.vmaddr+4<=segment.filesize)
     return At<uint32_t>(size_t(segment.fileoff+address-segment.vmaddr));
   }
   at+=command.cmdsize;
  }
  Check(false,"native caller instruction belongs to a file-backed segment");return 0;
 }
};
int64_t Signed(uint32_t value,int bits){return int32_t(value<<(32-bits))>>(32-bits);}
uint64_t ConditionalTarget(const NativeImage &image,uint64_t pc,uint32_t condition)
{
 const auto word=image.Instruction(pc);
 Check((word&0xff00001f)==(0x54000000|condition),"native conditional branch opcode and condition match");
 return uint64_t(int64_t(pc)+Signed((word>>5)&0x7ffff,19)*4);
}
uint64_t BranchTarget(const NativeImage &image,uint64_t pc,bool link)
{
 const auto word=image.Instruction(pc);
 Check((word&0xfc000000)==(link?0x94000000u:0x14000000u),"native branch/link opcode matches");
 return uint64_t(int64_t(pc)+Signed(word&0x3ffffff,26)*4);
}
int Argument(const NativeImage &image,uint64_t pc)
{
 const auto word=image.Instruction(pc);
 Check((word&0xffe0001f)==0x52800001,"native flight argument is MOVZ w1 with no shift");
 return int((word>>5)&0xffff);
}
struct BoatCaller {
 int model=0,playerFlight=-1,cheatFlight=-1;
 bool Read(const NativeImage &image){
  const int before=fails;
  Check(image.Instruction(0x100269d60)==0x7940d668,"CBoat tail reads model uint16 at entity+0x6a");
  const auto compare=image.Instruction(0x100269d64);
  Check((compare&0xffc003ff)==0x7100011f,"CBoat tail compares model w8 with immediate");
  model=int((compare>>10)&0xfff);
  Check(model==190&&ConditionalTarget(image,0x100269d68,1)==0x100269dcc,"model190 stays in Skimmer branch; other boats enter cheat branch");
  Check(image.Instruction(0x100269d6c)==0x90001928&&image.Instruction(0x100269d70)==0x9119d108&&image.Instruction(0x100269d74)==0xbd400100,
        "CBoat tail loads native CTimer timestep at0x10058d674");
  Check(ConditionalTarget(image,0x100269d7c,13)==0x100269dcc,"nonpositive timestep leaves player Skimmer flight branch");
  Check(image.Instruction(0x100269d84)==0x39418268&&image.Instruction(0x100269d88)==0x721d111f,"CBoat reads native status byte+0x60 and tests mask0xf8");
  Check(ConditionalTarget(image,0x100269d8c,0)==0x100269e84,"PLAYER status0 branches to actual Skimmer flight continuation");
  Check(ConditionalTarget(image,0x100269e94,5)==0x100269eac,"player volume-high path reaches flight argument");
  Check(image.Instruction(0x100269ea8)==0xbd035260&&image.Instruction(0x100269eac)==0xaa1303e0,"volume-low path also falls through to same native this pointer");
  playerFlight=Argument(image,0x100269eb0);
  Check(BranchTarget(image,0x100269eb4,false)==0x100269de8,"player argument jumps over cheat-only argument");
  Check(BranchTarget(image,0x100269de8,true)==0x10027a6ec,"native caller invokes CVehicle::FlyingControl ABI");
  Check(image.Instruction(0x100269dcc)==0xd0002a08&&image.Instruction(0x100269dd0)==0x91081508&&image.Instruction(0x100269dd4)==0x39400108,
        "separate branch loads CVehicle::bCheat8 at0x1007ab205");
  Check(ConditionalTarget(image,0x100269ddc,1)==0x100269dec,"inactive boat cheat skips its flight call");
  cheatFlight=Argument(image,0x100269de4);
  return fails==before;
 }
 // Characterized native tail only; full CBoat buoyancy/world collision stays
 // a separate native acceptance check. The enum comes from the loaded image.
 bool Dispatch(uint8_t *v,bool cheat=false) const {
  if(game::EntityModel(v)==model&&step>0){
   if((v[0x60]&0xf8)==0){fly(v,playerFlight);return true;}
   return false;
  }
  if(cheat){fly(v,cheatFlight);return true;}
  return false;
 }
};
void Skimmer(const BoatCaller &caller)
{
 Reset(3000);gConfig.planes=true;
 *reinterpret_cast<int16_t *>(vehicles[0]+0x6a)=int16_t(caller.model);
 *reinterpret_cast<int32_t *>(vehicles[0]+0x348)=1;
 Row(vehicles[0],0x84)={0,1.2f,0};throttle=255;
}
}
int main()
{
 const char *configured=std::getenv("MACVC_NATIVE"),*data=std::getenv("GAMEDATA");
 const auto binary=configured?std::filesystem::path(configured):
     std::filesystem::path(data?data:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData").parent_path().parent_path()/"MacOS/macVC";
 std::ifstream stream(binary,std::ios::binary|std::ios::ate);
 Check(bool(stream),"native caller asset opens (MACVC_NATIVE or GAMEDATA sibling Contents/MacOS/macVC)");
 if(!stream)return 1;
 const auto size=stream.tellg();Check(size>0&&size<128*1024*1024,"native image has bounded positive length");if(fails)return 1;
 NativeImage image;image.bytes.resize(size_t(size));stream.seekg(0);stream.read(reinterpret_cast<char *>(image.bytes.data()),size);
 Check(bool(stream),"native image reads completely");
 BoatCaller caller;if(fails||!caller.Read(image))return 1;
 std::printf("Native CBoat model%d PLAYER argument%d; separate boat-cheat argument%d\n",caller.model,caller.playerFlight,caller.cheatFlight);
 game::api.FindPlayerPed=Player;game::api.GetPad=Pad;game::api.PoolsGetVehicle=GetVehicle;game::api.timeStep=&step;
 game::api.ProcessLineOfSight=Sight;game::api.ignoreEntity=&ignore;game::api.GetWeaponInfo=WeaponInfo;
 *reinterpret_cast<uint8_t **>(pool)=vehicles[0];*reinterpret_cast<uint8_t **>(pool+8)=flags;*reinterpret_cast<int *>(pool+0x10)=2;
 gConfig.heliDamage=true;Check(flightmodel::Install(),"actual shared native FlyingControl chain installs");
 Check(!planeflight::Install(),"missing roster bindings retain real Skimmer force adapter in shared fixture");
 Skimmer(caller);Check(caller.Dispatch(vehicles[0])&&originalCalls==0&&turnCalls>0&&Row(vehicles[0],0x84).z>.008f,
     "actual native player branch/immediate reaches real shared hook and above-cloud plane adapter");
 Skimmer(caller);vehicles[0][0x294]=2;Check(caller.Dispatch(vehicles[0])&&originalCalls==0,"native caller retains free-roam SCM-created Skimmer adapter");
 Skimmer(caller);activeMission=true;Check(caller.Dispatch(vehicles[0])&&originalCalls==1,"native caller returns actual mission Skimmer to original shared owner");
 Skimmer(caller);vehicles[0][0x60]|=4<<3;Check(!caller.Dispatch(vehicles[0])&&originalCalls==0&&turnCalls==0,"nonplayer native Skimmer tail does not dispatch player flight");
 Skimmer(caller);step=0;Check(!caller.Dispatch(vehicles[0])&&turnCalls==0,"nonpositive native timestep suppresses Skimmer flight call");
 Skimmer(caller);*reinterpret_cast<int16_t *>(vehicles[0]+0x6a)=160;
 Check(!caller.Dispatch(vehicles[0])&&originalCalls==0,"ordinary native boat does not enter Skimmer call");
 Check(caller.Dispatch(vehicles[0],true)&&originalCalls==1,"separate native boat-cheat enum retains original controller");
 Skimmer(caller);fly(vehicles[0],caller.cheatFlight);Check(originalCalls==1,"Skimmer rejects unrelated native boat-cheat argument");
 if(!fails)std::puts("planeflight native caller assets: all passed");return fails?1:0;
}
