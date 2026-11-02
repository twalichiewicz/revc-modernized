// Original avatar rig through installed boarding, world travel and render pose.
#define JETBOARDINGPOSE_NO_MAIN
#include "test_jetboardingpose.cpp"
struct WalkSample {
 CVector joints[body::kBones]{};jetboarding::CabinPose cabin{};
 airportjets::Jet::Boarding phase=airportjets::Jet::Boarding::Approach;uint32_t time=0;
};
static const std::vector<boardingassets::Bone> *gBones;
static const std::map<std::string,boardingassets::Animation> *gAnimations;
static CVector gOrigin;
static WalkSample finalSeat;
static bool sawFinalSeat=false;
static uint8_t nestedApplied[sizeof poseKeys]{};
CVector RenderedWorld(int bone){
 const CVector p{renderMatrices[bone][12],renderMatrices[bone][13],renderMatrices[bone][14]};
 return airportjets::Add(V(gPlayer,0x38),airportjets::Add(airportjets::Add(airportjets::Mul(V(gPlayer,8),p.x),airportjets::Mul(V(gPlayer,0x18),p.y)),airportjets::Mul(V(gPlayer,0x28),p.z)));
}
void LoadCurrentRig(){
 auto &jet=airportjets::gJets[0];CHECK(jet.boardingAssoc);if(!jet.boardingAssoc)return;
 const auto name=boardingassets::Name(std::vector<uint8_t>(jet.boardingHierarchy,jet.boardingHierarchy+24),0,24);
 const auto found=gAnimations->find(name);CHECK(found!=gAnimations->end());if(found==gAnimations->end())return;
 LoadRig(*gBones,found->second,*reinterpret_cast<float *>(jet.boardingAssoc+0x30));
 if(jet.boardingAnim==0)body::KeyTranslation(poseKeys[body::kRoot]).y=0;
}
WalkSample Render(){
 LoadCurrentRig();auto &jet=airportjets::gJets[0];WalkSample sample;sample.phase=jet.boarding;sample.time=gameMs;
 const bool cabin=airportjets::CabinPose(gPlayer,sample.cabin);uint8_t original[sizeof poseKeys];std::memcpy(original,poseKeys,sizeof original);
 const bool applied=jetboardingpose::BeforePedRender(gPlayer);if(cabin)CHECK(applied);
 PoseUpdate(gPlayer);for(int b=0;b<body::kBones;++b)sample.joints[b]=airportjets::Sub(RenderedWorld(b),gOrigin);
 jetboardingpose::AfterPedRender(gPlayer);CHECK(!std::memcmp(original,poseKeys,sizeof original));return sample;
}
void SeatWitness(uint8_t *ped,uint8_t *vehicle){
 CHECK(ped==gPlayer&&vehicle==jetVehicles[0]&&!game::PedInVehicle(ped));
 finalSeat=Render();CHECK(finalSeat.phase==airportjets::Jet::Boarding::Sit&&finalSeat.cabin.blend==1);sawFinalSeat=true;
}
uint8_t *BeginGait(float initial,float reuse){
 Setup();body::gFindBone=PoseFindBone;game::api.GetAnimHierarchyFromSkinClump=PoseHierarchy;game::api.HAnimIDGetIndex=PoseIndex;game::api.HAnimMatrixArray=PoseMatrices;game::api.UpdateRpHAnim=PoseUpdate;
 CHECK(planeflight::Install());CHECK(airportjets::Install());gameMs=1000;JetFrame(gameMs);const int model=lastRequest;CHECK(model>=240&&model<=249);if(model<240||model>249)return nullptr;
 *reinterpret_cast<void **>(models[model]+0x30)=&clumpMarker;streamInfo[model*32+0x10]=1;gameMs=1100;JetFrame(gameMs);auto *vehicle=jetVehicles[0];gOrigin=V(vehicle,0x38);
 V(gPlayer,0x38)={gOrigin.x-2.3f,gOrigin.y+3.7f,14.84f};V(gPlayer,8)={1,0,0};V(gPlayer,0x18)={0,1,0};V(gPlayer,0x28)={0,0,1};
 nativeSeatInitial=initial;nativeSeatReuse=reuse;boardingSeatWitness=SeatWitness;sawFinalSeat=false;
 auto enter=JetHook<void (*)(uint8_t *,uint8_t *,uint32_t)>("SetEnterCar");gameMs=1200;enter(gPlayer,vehicle,0);return vehicle;
}
void EndGait(){
 boardingSeatWitness=nullptr;boardingBoundary=nullptr;poseBoundary=nullptr;mission=false;
 auto restart=JetHook<void (*)()>("ClearForRestart"),shutdown=JetHook<void (*)()>("CModelInfo8ShutDown");CHECK(restart&&shutdown);if(restart)restart();if(shutdown)shutdown();
}
alignas(16) uint8_t replacementFrame[0x30]{};
void ProtectOldFrame(bool protect){
#if __has_feature(address_sanitizer)
 if(protect)__asan_poison_memory_region(poseFrames[body::kUpperArmL],sizeof replacementFrame);else __asan_unpoison_memory_region(poseFrames[body::kUpperArmL],sizeof replacementFrame);
#else
 (void)protect;
#endif
}
void ReplaceFrame(){
 std::memcpy(replacementFrame,poseFrames[body::kUpperArmL],sizeof replacementFrame);std::memcpy(replacementKey,originalLeftKey,sizeof replacementKey);body::KeyRotation(replacementKey)=body::X(.5f);
 *reinterpret_cast<uint8_t **>(replacementFrame+0x10)=replacementKey;poseFrameOverride[body::kUpperArmL]=replacementFrame;ProtectOldFrame(true);ProtectLeftKey(true);
}
void ToWalk(){
 auto *vehicle=BeginGait(1,-1);CHECK(vehicle);for(int tick=0;tick<300&&airportjets::gJets[0].boarding<airportjets::Jet::Boarding::Walk;++tick){gameMs+=50;JetFrame(gameMs);}
 CHECK(airportjets::gJets[0].boarding==airportjets::Jet::Boarding::Walk);LoadCurrentRig();
}
void OwnershipCases(){
 for(int cold:{0,1})for(int frame:{0,1}){
  ToWalk();if(cold)body::gSkel={};std::memcpy(originalLeftKey,poseKeys[body::kUpperArmL],sizeof originalLeftKey);
  measurePoint="find";measureOrdinal=cold?70:45;measureSeen=0;measureMutation=frame?ReplaceFrame:ReplaceLeftKey;
  CHECK(!jetboardingpose::BeforePedRender(gPlayer));ProtectLeftKey(false);ProtectOldFrame(false);CHECK(!measurePoint);CHECK(!jetboardingpose::snapshot.ped);CHECK(body::KeyRotation(replacementKey).x==body::X(.5f).x);
  gameMs+=50;JetFrame(gameMs);CHECK(!airportjets::gJets[0].boardingSerial&&!game::PedInVehicle(gPlayer));CHECK(game::EntityFlags(gPlayer)&game::kEntityUsesCollision);EndGait();
 }
 // All22 rotations and the translated root are conditionally restored;
 // later writers and replaced allocations retain their exact own values.
 for(int interruption=0;interruption<10;++interruption){
  ToWalk();uint8_t original[sizeof poseKeys],applied[sizeof poseKeys];std::memcpy(original,poseKeys,sizeof original);CHECK(jetboardingpose::BeforePedRender(gPlayer));std::memcpy(applied,poseKeys,sizeof applied);
  const int generation=pedGeneration;auto *clump=game::EntityClump(gPlayer);const auto lookup=game::api.PoolsGetPed;
  switch(interruption){
   case 0:mission=true;break;case 1:++pedGeneration;ProtectPoseKeys(true);break;case 2:*reinterpret_cast<void **>(gPlayer+0x58)=&policeMarker;ProtectPoseKeys(true);break;
   case 3:body::KeyRotation(poseKeys[body::kCalfR])=body::X(.777f);body::KeyTranslation(poseKeys[body::kRoot])={.123f,.234f,.345f};std::memcpy(original+sizeof poseKeys[0]*body::kCalfR+0x10,&body::KeyRotation(poseKeys[body::kCalfR]),sizeof(body::Quat));std::memcpy(original+0x20,&body::KeyTranslation(poseKeys[body::kRoot]),sizeof(CVector));break;
   case 4:std::memcpy(originalLeftKey,poseKeys[body::kUpperArmL],sizeof originalLeftKey);ReplaceLeftKey();break;
   case 5:game::api.PoolsGetPed=nullptr;ProtectPoseKeys(true);break;
   case 6:boardingAssoc[80]=nullptr;break;
   case 7:poseBoundary=[](){++pedGeneration;ProtectPoseKeys(true);};break;
   case 8:poseBoundary=[](){LoadCurrentRig();body::KeyTranslation(poseKeys[body::kRoot])={.051f,.062f,.073f};CHECK(jetboardingpose::BeforePedRender(gPlayer));std::memcpy(nestedApplied,poseKeys,sizeof nestedApplied);};break;
   case 9:std::memcpy(originalLeftKey,poseKeys[body::kUpperArmL],sizeof originalLeftKey);ReplaceFrame();break;
  }
  jetboardingpose::AfterPedRender(gPlayer);ProtectPoseKeys(false);ProtectLeftKey(false);ProtectOldFrame(false);
  if(interruption==1||interruption==2||interruption==5||interruption==7)CHECK(!std::memcmp(applied,poseKeys,sizeof applied));
  else if(interruption==4||interruption==9){CHECK(!std::memcmp(applied+sizeof poseKeys[0]*body::kUpperArmL,poseKeys[body::kUpperArmL],sizeof poseKeys[0]));for(int b=0;b<body::kBones;++b)if(b!=body::kUpperArmL)CHECK(!std::memcmp(original+sizeof poseKeys[0]*b,poseKeys[b],sizeof poseKeys[0]));}
  else if(interruption==8){CHECK(jetboardingpose::snapshot.ped==gPlayer);CHECK(!std::memcmp(nestedApplied,poseKeys,sizeof nestedApplied));jetboardingpose::AfterPedRender(gPlayer);CHECK(body::KeyTranslation(poseKeys[body::kRoot]).x==.051f);}
  else CHECK(!std::memcmp(original,poseKeys,sizeof original));
  CHECK(!jetboardingpose::snapshot.ped);pedGeneration=generation;*reinterpret_cast<void **>(gPlayer+0x58)=clump;game::api.PoolsGetPed=lookup;EndGait();
 }
}

struct SurfaceTriangle {CVector p[3];};
std::vector<SurfaceTriangle> RoofTriangles(const std::vector<uint8_t> &plane,bool lining){
 using namespace boardingassets;std::vector<SurfaceTriangle> out;
 for(const auto &clump:Chunks(plane,0,plane.size(),12))if(clump.tag==16)for(const auto &list:Chunks(plane,clump.begin,clump.end,12))if(list.tag==26)for(const auto &geometry:Chunks(plane,list.begin,list.end,12))if(geometry.tag==15){
  const auto children=Chunks(plane,geometry.begin,geometry.end,12);const auto st=children.front();const auto flags=Read<uint32_t>(plane,st.begin),nt=Read<uint32_t>(plane,st.begin+4),nv=Read<uint32_t>(plane,st.begin+8);size_t at=st.begin+16;
  const auto version=Read<uint32_t>(plane,st.begin-4);const auto library=version&0xffff0000u?(((version>>14)&0x3ff00u)+0x30000u)|((version>>16)&0x3fu):version<<8;if(library<0x34000)at+=12;
  if(flags&8)at+=nv*4;at+=nv*8;const size_t faces=at;at+=nt*8+24;
  for(unsigned i=0;i<nt;++i){SurfaceTriangle t{};const auto f=faces+i*8;const uint16_t indices[]={Read<uint16_t>(plane,f+2),Read<uint16_t>(plane,f),Read<uint16_t>(plane,f+6)};
   for(int j=0;j<3;++j){auto p=Read<CVector>(plane,at+size_t(indices[j])*12);if(lining)p={p.x*.995f,p.y,p.z*.995f-p.y*.0005f};t.p[j]=p;}out.push_back(t);
  }
 }
 return out;
}
float Roof(const std::vector<SurfaceTriangle> &surface,CVector p){
 double z=-1e9;for(const auto &t:surface){const auto a=t.p[0],b=t.p[1],c=t.p[2];const double ex=b.x-a.x,ey=b.y-a.y,fx=c.x-a.x,fy=c.y-a.y,den=ex*fy-ey*fx;if(std::fabs(den)<1e-12)continue;
  const double dx=p.x-a.x,dy=p.y-a.y,u=(dx*fy-dy*fx)/den,v=(ex*dy-ey*dx)/den;if(u<0||v<0||u+v>1)continue;z=std::max(z,a.z+u*(b.z-a.z)+v*(c.z-a.z));
 }return float(z);
}
float HeadGap(const std::vector<SurfaceTriangle> &roof,CVector head){
 float gap=1e9f;for(float ring:{0.f,.5f,.85f,1.f})for(int i=0;i<24;++i){const float a=i*6.28318530718f/24,r=.12f*ring;const CVector p{head.x+r*std::cos(a),head.y+r*std::sin(a),head.z+.12f*std::sqrt(1-ring*ring)};gap=std::min(gap,Roof(roof,p)-p.z);}return gap;
}
int main(){
 using namespace boardingassets;const char *env=std::getenv("GAMEDATA");const std::string base=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";
 ImgArchive archive;CHECK(archive.Open(base+"/models/gta3.img"));std::vector<uint8_t> player,ifp,plane;CHECK(archive.Read("player.dff",player));CHECK(archive.Read("ap_learjet1_01.dff",plane));CHECK(ReadGameFile(base+"/anim/ped.ifp",ifp));if(failures)return 1;
 const auto bones=Skeleton(player);const auto animations=Animations(ifp);const auto roof=RoofTriangles(plane,false),lining=RoofTriangles(plane,true);gBones=&bones;gAnimations=&animations;CHECK(std::fabs(DoorContact(plane).x+.833174114f)<.000002f);
 // The shipped ped block has static duck poses, but no crouch locomotion.
 int duck=0,crouchLocomotion=0;for(const auto &c:Chunks(ifp,8,8+Read<uint32_t>(ifp,4)))if(c.tag==Four('N','A','M','E')){const auto name=Name(ifp,c.begin,c.end-c.begin);if(name=="duck_down"||name=="duck_low")++duck;if((name.find("duck")!=std::string::npos||name.find("crouch")!=std::string::npos)&&(name.find("walk")!=std::string::npos||name.find("run")!=std::string::npos))++crouchLocomotion;}
 CHECK(duck==2&&crouchLocomotion==0);OwnershipCases();
 for(int step:{16,33,50,100,136})for(int nativeCase:{0,1,2}){
  auto *vehicle=BeginGait(nativeCase==2?-1:float(nativeCase),nativeCase==2?.35f:-1);if(!vehicle)return 1;
  std::vector<WalkSample> samples;for(int tick=0;tick<1500&&!game::PedInVehicle(gPlayer)&&airportjets::gJets[0].boardingSerial;++tick){gameMs+=uint32_t(step);JetFrame(gameMs);if(airportjets::gJets[0].boardingAssoc)samples.push_back(Render());}
  CHECK(game::PedInVehicle(gPlayer)&&game::PedVehicle(gPlayer)==vehicle&&*reinterpret_cast<uint8_t **>(vehicle+0x218)==gPlayer);CHECK(sawFinalSeat);CHECK(gameMs-1200<20000);if(!sawFinalSeat){for(const auto &line:boardingLogs)std::puts(line.c_str());EndGait();continue;}
  CHECK(boardingAssoc[102]&&*reinterpret_cast<uint8_t **>(gPlayer+0x2a0)==boardingAssoc[102]);CHECK(!(*reinterpret_cast<uint16_t *>(boardingAssoc[102]+0x3e)&1));CHECK(*reinterpret_cast<float *>(boardingAssoc[102]+0x2c)>=0);
  int planted[2]{},lifted[2]{},walkFrames=0;float travel=0,maxHead=-100,minSole=100,maxJump=0,maxTargetError=0,minRoofGap=100,minLiningGap=100;
  for(size_t i=0;i<samples.size();++i){const auto &b=samples[i];if(b.phase<airportjets::Jet::Boarding::Crouch)continue;
   minRoofGap=std::min(minRoofGap,HeadGap(roof,b.joints[body::kHead]));minLiningGap=std::min(minLiningGap,HeadGap(lining,b.joints[body::kHead]));maxHead=std::max(maxHead,b.joints[body::kHead].z+.12f);for(int leg=0;leg<2;++leg){const auto foot=b.joints[leg?body::kFootR:body::kFootL];minSole=std::min(minSole,foot.z-jetboarding::kAnkle);}
   if(i){const auto &a=samples[i-1];if(a.phase>=airportjets::Jet::Boarding::Step)for(int bone:{body::kRoot,body::kHead,body::kFootL,body::kFootR})maxJump=std::max(maxJump,airportjets::Length(airportjets::Sub(b.joints[bone],a.joints[bone])));}
   if(b.phase!=airportjets::Jet::Boarding::Walk)continue;++walkFrames;
   if(i&&samples[i-1].phase==b.phase)travel+=airportjets::Length(airportjets::Sub(b.joints[body::kRoot],samples[i-1].joints[body::kRoot]));
   for(int leg=0;leg<2;++leg){const auto expected=jetboarding::WalkFoot(b.cabin.distance,leg);const auto foot=b.joints[leg?body::kFootR:body::kFootL];maxTargetError=std::max(maxTargetError,airportjets::Length(airportjets::Sub(foot,expected.at)));
    if(foot.z-(jetboarding::kFloor+jetboarding::kAnkle)>.04f)++lifted[leg];
    if(i&&samples[i-1].phase==b.phase&&expected.planted&&jetboarding::WalkFoot(samples[i-1].cabin.distance,leg).planted){const auto before=samples[i-1].joints[leg?body::kFootR:body::kFootL];CHECK(airportjets::Length(airportjets::Sub(before,foot))<.001f);++planted[leg];}
   }
  }
  const auto nativeSeat=boardingassets::Pose(bones,animations.at("car_sit"),.033333335f);float seatError=0;for(int b=0;b<body::kBones;++b)seatError=std::max(seatError,airportjets::Length(airportjets::Sub(finalSeat.joints[b],airportjets::Add(jetboarding::kSeatRoot,nativeSeat.at(body::kTags[b])))));
  CHECK(travel>1.0f&&walkFrames>=20);CHECK(planted[0]>=4&&planted[1]>=4&&lifted[0]>=3&&lifted[1]>=3);CHECK(maxTargetError<.002f);CHECK(maxHead<.58f&&minSole>=jetboarding::kFloor-.002f);CHECK(maxJump<.31f);CHECK(seatError<.0003f);CHECK(minRoofGap>.005f&&minLiningGap>.005f);
  std::printf("crouch dt%d seat%d: travel%.3f plant%d/%d lift%d/%d head%.3f sole%.3f jump%.3f target%.5f seat%.6f roof/lining%.4f/%.4f elapsed%u\n",step,nativeCase,travel,planted[0],planted[1],lifted[0],lifted[1],maxHead,minSole,maxJump,maxTargetError,seatError,minRoofGap,minLiningGap,gameMs-1200);
  EndGait();
 }
 if(!failures)std::puts("Learjet crouch walking: original rig foot planting, clearance, continuity and native seat handoff passed");return failures?1:0;
}
