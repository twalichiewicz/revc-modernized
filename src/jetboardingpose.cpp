#include "jetboardingpose.h"
#include "airportjets.h"
#include "jetboarding.h"
#include "body.h"
#include "game.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
namespace jetboardingpose {
namespace {
using namespace game;
struct Key {uint8_t *frame=nullptr,*key=nullptr;uint32_t tag=0;body::Quat original{},applied{};};
struct Snapshot {uint8_t *ped=nullptr,*clump=nullptr;int ref=-1,count=0;Key keys[body::kBones];bool root=false;CVector originalRoot{},appliedRoot{};} snapshot;
struct Measurement {uint8_t *clump;int ref;uint64_t serial;int mode;};
uint64_t poseEpoch=0;
uint8_t *(*findBone)(uint8_t *,uint32_t)=nullptr;
constexpr uint32_t tags[body::kBones]={0,1,2,3,4,5,31,32,33,34,35,21,22,23,24,25,41,42,43,51,52,53};
bool Live(const Snapshot &s){return s.ped&&s.ref>=0&&api.PoolsGetPed&&api.PoolsGetPed(s.ref)==s.ped&&EntityClump(s.ped)==s.clump;}
bool Active(uint8_t *ped,uint8_t *clump,int ref,uint64_t serial,int mode){
 if(!api.PoolsGetPed||!api.FindPlayerPed||api.PoolsGetPed(ref)!=ped||EntityClump(ped)!=clump)return false;
 if(mode==0){CVector target{};float weight=0;int current=-1;uint64_t token=0;return airportjets::ReachDoor(ped,target,weight,current,token)&&current==ref&&token==serial;}
 jetboarding::CabinPose pose;return airportjets::CabinPose(ped,pose)&&pose.pedRef==ref&&pose.serial==serial&&1+int(pose.mode)==mode;
}
bool Limb(uint8_t *ped,body::Pose &pose,body::Bone upper,body::Bone lower,body::Bone end,CVector target,CVector pole){
 using namespace body;const auto shoulder=JointOver(ped,pose,upper),elbow=JointOver(ped,pose,lower);const float a=Length(upper),b=Length(lower);const auto d=Sub(target,shoulder);const float distance=std::sqrt(Dot(d,d));
 if(!std::isfinite(distance)||distance<.001f||a<=0||b<=0)return false;
 const auto along=Normalised(d);const float reach=std::clamp(distance,std::fabs(a-b)+.002f,a+b-.002f),x=(a*a-b*b+reach*reach)/(2*reach),y=std::sqrt(std::max(0.0f,a*a-x*x));
 pole=Normalised(Sub(pole,Scale(along,Dot(pole,along))));if(Dot(pole,pole)<.5f)pole=Normalised(Sub(CVector{0,1,0},Scale(along,along.y)));
 const auto wantedElbow=Add(shoulder,Add(Scale(along,x),Scale(pole,y))),wantedEnd=Add(shoulder,Scale(along,reach));
 TurnOver(ped,pose,upper,Arc(Sub(elbow,shoulder),Sub(wantedElbow,shoulder)));
 const auto movedElbow=JointOver(ped,pose,lower),hand=JointOver(ped,pose,end);TurnOver(ped,pose,lower,Arc(Sub(hand,movedElbow),Sub(wantedEnd,movedElbow)));return true;
}
body::Pose NativePose(uint8_t *ped){using namespace body;Pose pose;for(int b=0;b<kBones;++b)TurnOver(ped,pose,Bone(b),{});pose.rootMove=Sub(JointOver(ped,{},kRoot),Joint({},kRoot));return pose;}
body::Pose Crouch(float distance){
 using namespace body;Pose pose;for(int b=0;b<kBones;++b)pose.Turn(Bone(b),{});
 for(Bone b:{kPelvis,kSpine,kSpine1,kNeck,kHead,kClavicleL,kClavicleR})pose.Turn(b,X(-.10f));
 const float swing=std::sin(distance/(2*jetboarding::kStep)*6.28318530718f);
 pose.Turn(kUpperArmL,X(.18f*swing));pose.Turn(kUpperArmR,X(-.18f*swing));pose.Turn(kForearmL,X(-.25f+.10f*swing));pose.Turn(kForearmR,X(-.25f-.10f*swing));
 pose.rootMove=Sub(jetboarding::CrouchRoot(),Joint({},kRoot));return pose;
}
}
void AfterPedRender(uint8_t *ped){
 if(snapshot.ped!=ped)return;const Snapshot saved=snapshot;const auto epoch=poseEpoch;snapshot={};if(!findBone||!Live(saved))return;
 for(int i=0;i<saved.count;++i){const auto &key=saved.keys[i];auto *frame=findBone(saved.clump,key.tag);if(epoch!=poseEpoch||!Live(saved))return;
  if(frame!=key.frame||!frame||*reinterpret_cast<uint8_t **>(frame+0x10)!=key.key)continue;
  if(!std::memcmp(key.key+0x10,&key.applied,sizeof key.applied))std::memcpy(key.key+0x10,&key.original,sizeof key.original);
 }
 if(saved.root){const auto &key=saved.keys[0];auto *frame=findBone(saved.clump,0);if(epoch!=poseEpoch||!Live(saved))return;
  if(frame==key.frame&&frame&&*reinterpret_cast<uint8_t **>(frame+0x10)==key.key&&!std::memcmp(key.key+0x20,&saved.appliedRoot,sizeof saved.appliedRoot))std::memcpy(key.key+0x20,&saved.originalRoot,sizeof saved.originalRoot);
 }
}
bool BeforePedRender(uint8_t *ped){
 if(snapshot.ped)AfterPedRender(snapshot.ped);
 const auto epoch=++poseEpoch;
 if(!api.PoolsGetPed||!api.FindPlayerPed||!api.GetAnimHierarchyFromSkinClump||!api.HAnimIDGetIndex||!api.HAnimMatrixArray||!api.UpdateRpHAnim)return false;
 CVector target{};float weight=0;int ref=-1;uint64_t serial=0;jetboarding::CabinPose cabin;
 const bool reaching=airportjets::ReachDoor(ped,target,weight,ref,serial);if(!reaching){if(!airportjets::CabinPose(ped,cabin))return false;ref=cabin.pedRef;serial=cabin.serial;weight=1;}
 const int mode=reaching?0:1+int(cabin.mode);auto *clump=EntityClump(ped);
 if(!findBone)findBone=reinterpret_cast<decltype(findBone)>(dlsym(RTLD_MAIN_ONLY,"_Z24RpAnimBlendClumpFindBonePN2rw5ClumpEj"));
 Measurement measurement{clump,ref,serial,mode};const body::PreparationOwner owner{
  [](uint8_t *actor,void *context){const auto &m=*static_cast<Measurement *>(context);return api.PoolsGetPed&&api.PoolsGetPed(m.ref)==actor&&EntityClump(actor)==m.clump;},
  [](uint8_t *actor,void *context){const auto &m=*static_cast<Measurement *>(context);return Active(actor,m.clump,m.ref,m.serial,m.mode);},&measurement};
 if(!findBone)return false;
 if(!body::Prepare(ped,owner)){if(!reaching&&Active(ped,clump,ref,serial,mode))airportjets::RejectBoardingPose(ped,ref,serial);return false;}
 if(epoch!=poseEpoch||!Active(ped,clump,ref,serial,mode))return false;
 Snapshot saved;saved.ped=ped;saved.clump=clump;saved.ref=ref;saved.count=reaching?2:body::kBones;saved.root=!reaching;
 uint8_t *frames[body::kBones]{};
 for(int i=0;i<body::kBones;++i){
  frames[i]=findBone(clump,tags[i]);if(epoch!=poseEpoch||!Active(ped,clump,ref,serial,mode)||!frames[i])return false;
 }
 // Original FindBone/GetAssociation only inspect their native lists; neither
 // invokes an external callback or retires earlier frames. After those pure
 // lookups, no native call may intervene between identity checks and key math.
 auto current=[&](){return epoch==poseEpoch&&body::CurrentKeys(ped,frames);};
 if(!current()){airportjets::RejectBoardingPose(ped,ref,serial);return false;}
 for(int i=0;i<saved.count;++i){const int bone=reaching?(i==0?body::kUpperArmL:body::kForearmL):i;auto *frame=frames[bone],*key=*reinterpret_cast<uint8_t **>(frame+0x10);
  saved.keys[i].frame=frame;saved.keys[i].key=key;saved.keys[i].tag=tags[bone];std::memcpy(&saved.keys[i].original,key+0x10,sizeof(body::Quat));
 }
 if(saved.root)std::memcpy(&saved.originalRoot,saved.keys[0].key+0x20,sizeof saved.originalRoot);
 using namespace body;Pose pose;
 if(reaching){if(!Limb(ped,pose,kUpperArmL,kForearmL,kHandL,target,{-1,0,0}))return false;}
 else{
  const auto crouched=Crouch(cabin.distance);
  if(cabin.mode==jetboarding::CabinPose::Mode::Crouch)pose=Mix(NativePose(ped),crouched,cabin.blend);
  else if(cabin.mode==jetboarding::CabinPose::Mode::Walk)pose=crouched;
  else pose=Mix(crouched,NativePose(ped),cabin.blend);
  pose.rootMove=Sub(cabin.root,Joint({},kRoot));
  if(cabin.mode!=jetboarding::CabinPose::Mode::Sit||cabin.blend<1){
   if(!Limb(ped,pose,kThighL,kCalfL,kFootL,cabin.feet[0],{-.2f,1,0})||!Limb(ped,pose,kThighR,kCalfR,kFootR,cabin.feet[1],{.2f,1,0}))return false;
  }
 }
 if(epoch!=poseEpoch||!Active(ped,clump,ref,serial,mode))return false;
 if(!current()){airportjets::RejectBoardingPose(ped,ref,serial);return false;}ApplyOver(ped,pose,weight);
 for(int i=0;i<saved.count;++i)std::memcpy(&saved.keys[i].applied,saved.keys[i].key+0x10,sizeof(body::Quat));
 if(saved.root)std::memcpy(&saved.appliedRoot,saved.keys[0].key+0x20,sizeof saved.appliedRoot);snapshot=saved;return true;
}
}
