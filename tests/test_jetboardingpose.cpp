// Original player.dff/ped.ifp keyframes through the owned hand render adapter.
#define JETBOARDING_ASSETS_NO_MAIN
#define Cross PlaneBoardingCross
#include "test_jetboarding_assets.cpp"
#undef Cross
#include "../src/body.cpp"
namespace {
#if __has_feature(address_sanitizer)
extern "C" void __asan_poison_memory_region(const void *,size_t);
extern "C" void __asan_unpoison_memory_region(const void *,size_t);
#endif
static int rigParents[body::kBones]{};
static int nativeUpdates=0;
static uint8_t *poseFrameOverride[body::kBones]{};
alignas(16) uint8_t poseFrames[body::kBones][0x30]{},poseKeys[body::kBones][0x40]{};
alignas(16) uint8_t replacementKey[0x40]{},originalLeftKey[0x40]{};
void ProtectLeftKey(bool protect){
#if __has_feature(address_sanitizer)
 if(protect)__asan_poison_memory_region(poseKeys[body::kUpperArmL],sizeof replacementKey);else __asan_unpoison_memory_region(poseKeys[body::kUpperArmL],sizeof replacementKey);
#else
 (void)protect;
#endif
}
[[maybe_unused]] void ReplaceLeftKey(){std::memcpy(replacementKey,originalLeftKey,sizeof replacementKey);body::KeyRotation(replacementKey)=body::X(.5f);*reinterpret_cast<uint8_t **>(poseFrames[body::kUpperArmL]+0x10)=replacementKey;ProtectLeftKey(true);}
[[maybe_unused]] void ProtectPoseKeys(bool protect){
#if __has_feature(address_sanitizer)
 if(protect)__asan_poison_memory_region(poseKeys,sizeof poseKeys);else __asan_unpoison_memory_region(poseKeys,sizeof poseKeys);
#else
 (void)protect;
#endif
}
void LoadRig(const std::vector<boardingassets::Bone> &bones,const boardingassets::Animation &animation,float time){
 using namespace body;std::fill(poseFrameOverride,poseFrameOverride+kBones,nullptr);gSkel={};gSkel.clump=game::EntityClump(gPlayer);gSkel.hierarchy=gSkel.clump;gSkel.ok=true;
 std::map<int,int> indices;for(int b=0;b<kBones;++b)indices[kTags[b]]=b;
 for(int b=0;b<kBones;++b){const auto found=std::find_if(bones.begin(),bones.end(),[&](const auto &bone){return bone.tag==kTags[b];});CHECK(found!=bones.end());if(found==bones.end())return;
  int parent=found->parent;while(parent>=0&&bones[size_t(parent)].tag<0)parent=bones[size_t(parent)].parent;gSkel.parent[b]=parent>=0?indices.at(bones[size_t(parent)].tag):0;rigParents[b]=gSkel.parent[b];
  const Quat local{-kStanding[b][0],-kStanding[b][1],-kStanding[b][2],kStanding[b][3]};const auto offset=b==kRoot?kStandingRoot:found->rest.p;
  gSkel.rest[b]=b==kRoot?local:gSkel.rest[gSkel.parent[b]]*local;gSkel.restPos[b]=b==kRoot?offset:Add(gSkel.restPos[gSkel.parent[b]],Rotate(gSkel.rest[gSkel.parent[b]],offset));
  gSkel.frame[b]=poseFrames[b];gSkel.key[b]=poseKeys[b];*reinterpret_cast<uint8_t **>(poseFrames[b]+0x10)=poseKeys[b];
  auto pose=found->rest;for(const auto &sequence:animation)if(sequence.tag==kTags[b]||(sequence.tag<0&&sequence.name==found->name)){pose=boardingassets::Sample(sequence,time,found->rest.p);break;}
  Mat3 matrix{};for(int i=0;i<3;++i)for(int j=0;j<3;++j)matrix.m[i][j]=float(pose.r[i][j]);KeyRotation(poseKeys[b])=ToQuat(matrix);KeyTranslation(poseKeys[b])=pose.p;
 }
}
CVector DoorContact(const std::vector<uint8_t> &bytes){using namespace boardingassets;
 for(const auto &clump:Chunks(bytes,0,bytes.size(),12))if(clump.tag==16)
  for(const auto &list:Chunks(bytes,clump.begin,clump.end,12))if(list.tag==26)
   for(const auto &geometry:Chunks(bytes,list.begin,list.end,12))if(geometry.tag==15){
    const auto children=Chunks(bytes,geometry.begin,geometry.end,12);const auto st=children.front();const auto flags=Read<uint32_t>(bytes,st.begin),triangles=Read<uint32_t>(bytes,st.begin+4),vertices=Read<uint32_t>(bytes,st.begin+8);CHECK(triangles==1212&&vertices==1303);
    size_t at=st.begin+16;const auto version=Read<uint32_t>(bytes,st.begin-4);const auto library=version&0xffff0000u?(((version>>14)&0x3ff00u)+0x30000u)|((version>>16)&0x3fu):version<<8;if(library<0x34000)at+=12;
    if(flags&8)at+=vertices*4;at+=vertices*8;const size_t face=at+319*8;
    const uint16_t a=Read<uint16_t>(bytes,face+2),b=Read<uint16_t>(bytes,face),c=Read<uint16_t>(bytes,face+6);at+=triangles*8+24;
    const auto p=Read<CVector>(bytes,at+size_t(a)*12),q=Read<CVector>(bytes,at+size_t(b)*12),r=Read<CVector>(bytes,at+size_t(c)*12);
    return airportjets::Add(p,airportjets::Add(airportjets::Mul(airportjets::Sub(q,p),.125651357f),airportjets::Mul(airportjets::Sub(r,p),.663221276f)));
   }
 CHECK(false);return {};
}
}
static const char *measurePoint=nullptr;
static int measureOrdinal=0,measureSeen=0;
static void (*measureMutation)()=nullptr;
static void MeasureBoundary(const char *point){if(measurePoint&&!std::strcmp(measurePoint,point)&&++measureSeen==measureOrdinal){auto callback=measureMutation;measurePoint=nullptr;measureMutation=nullptr;if(callback)callback();}}
static void (*poseBoundary)()=nullptr;
static bool poseFindAvailable=true;
static float renderMatrices[body::kBones][16]{};
static uint8_t *PoseHierarchy(uint8_t *clump){MeasureBoundary("hierarchy");return clump;}
static int PoseIndex(uint8_t *,int tag){MeasureBoundary("index");for(int i=0;i<body::kBones;++i)if(body::kTags[i]==tag)return i;return -1;}
static float *PoseMatrices(uint8_t *){MeasureBoundary("matrices");return renderMatrices[0];}
static void PoseUpdate(uint8_t *){++nativeUpdates;body::Quat world[body::kBones];CVector points[body::kBones];for(int i=0;i<body::kBones;++i){auto *frame=poseFrameOverride[i]?poseFrameOverride[i]:poseFrames[i];auto *key=*reinterpret_cast<uint8_t **>(frame+0x10);const auto local=body::KeyRotation(key);const auto translation=body::KeyTranslation(key);world[i]=i?world[rigParents[i]]*local:local;points[i]=i?body::Add(points[rigParents[i]],body::Rotate(world[rigParents[i]],translation)):translation;const auto matrix=body::ToMat(world[i]);for(int row=0;row<3;++row)for(int col=0;col<3;++col)renderMatrices[i][col*4+row]=matrix.m[row][col];renderMatrices[i][12]=points[i].x;renderMatrices[i][13]=points[i].y;renderMatrices[i][14]=points[i].z;renderMatrices[i][15]=1;}MeasureBoundary("update");}
static uint8_t *PoseFindBone(uint8_t *clump,uint32_t tag){MeasureBoundary("find");if(poseBoundary){auto callback=poseBoundary;poseBoundary=nullptr;callback();}if(clump!=game::EntityClump(gPlayer))return nullptr;for(int i=0;i<body::kBones;++i)if(body::kTags[i]==int(tag))return poseFrameOverride[i]?poseFrameOverride[i]:poseFrames[i];return nullptr;}
static void *PoseSymbol(void *,const char *name){return (poseFindAvailable&&std::strstr(name,"RpAnimBlendClumpFindBone"))?reinterpret_cast<void *>(PoseFindBone):nullptr;}
#define dlsym PoseSymbol
#include "../src/jetboardingpose.cpp"
#undef dlsym
#ifndef JETBOARDINGPOSE_NO_MAIN
int main(){using namespace boardingassets;const char *env=std::getenv("GAMEDATA");const std::string base=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";ImgArchive archive;CHECK(archive.Open(base+"/models/gta3.img"));std::vector<uint8_t> player,ifp,jet;CHECK(archive.Read("player.dff",player));CHECK(archive.Read("ap_learjet1_01.dff",jet));CHECK(ReadGameFile(base+"/anim/ped.ifp",ifp));if(failures)return 1;const auto bones=Skeleton(player);const auto animations=Animations(ifp);const auto contact=DoorContact(jet);CHECK(std::fabs(contact.x+.833174114f)<.000002f&&std::fabs(contact.y-3.7f)<.000002f&&std::fabs(contact.z+.78f)<.000002f);
 Setup();body::gFindBone=PoseFindBone;game::api.GetAnimHierarchyFromSkinClump=PoseHierarchy;game::api.HAnimIDGetIndex=PoseIndex;game::api.HAnimMatrixArray=PoseMatrices;game::api.UpdateRpHAnim=PoseUpdate;CHECK(planeflight::Install());CHECK(airportjets::Install());gameMs=1000;JetFrame(gameMs);const int model=lastRequest;CHECK(model>=240&&model<=249);if(model<240||model>249)return 1;
 *reinterpret_cast<void **>(models[model]+0x30)=&clumpMarker;streamInfo[model*32+0x10]=1;gameMs=1100;JetFrame(gameMs);auto *v=jetVehicles[0];
 V(gPlayer,0x38)={V(v,0x38).x-1.3f,V(v,0x38).y+3.58f,14.84f};V(gPlayer,8)={1,0,0};V(gPlayer,0x18)={0,1,0};V(gPlayer,0x28)={0,0,1};
 auto enter=JetHook<void (*)(uint8_t *,uint8_t *,uint32_t)>("SetEnterCar");gameMs=1200;enter(gPlayer,v,0);gameMs=1250;JetFrame(gameMs);CHECK(boardingAssoc[76]);if(!boardingAssoc[76])return 1;*reinterpret_cast<float *>(boardingAssoc[76]+0x30)=.3f*.73333335f;
 LoadRig(bones,animations.at("car_open_lhs"),.3f*.73333335f);const CVector target{.4668259f,.12f,.454784f};
 const auto originalHand=body::JointOver(gPlayer,{},body::kHandL);const auto head=body::JointOver(gPlayer,{},body::kHead),leftFoot=body::JointOver(gPlayer,{},body::kFootL),rightFoot=body::JointOver(gPlayer,{},body::kFootR);
 uint8_t saved[sizeof poseKeys];std::memcpy(saved,poseKeys,sizeof saved);const auto measuredExpected=body::gSkel;body::gSkel={};body::gFindBone=PoseFindBone;nativeUpdates=0;CHECK(jetboardingpose::BeforePedRender(gPlayer));CHECK(nativeUpdates==2&&body::gSkel.ok);for(int b=0;b<body::kBones;++b){CHECK(body::gSkel.parent[b]==measuredExpected.parent[b]);CHECK(airportjets::Length(airportjets::Sub(body::gSkel.restPos[b],measuredExpected.restPos[b]))<.00001f);}const auto hand=body::JointOver(gPlayer,{},body::kHandL);
 std::printf("Original native reach gap %.6fm -> corrected %.6fm\n",airportjets::Length(airportjets::Sub(originalHand,target)),airportjets::Length(airportjets::Sub(hand,target)));
 CHECK(airportjets::Length(airportjets::Sub(hand,target))<.0002f);
 CHECK(airportjets::Length(airportjets::Sub(body::JointOver(gPlayer,{},body::kHead),head))<.00001f);CHECK(airportjets::Length(airportjets::Sub(body::JointOver(gPlayer,{},body::kFootL),leftFoot))<.00001f);CHECK(airportjets::Length(airportjets::Sub(body::JointOver(gPlayer,{},body::kFootR),rightFoot))<.00001f);

 PoseUpdate(gPlayer);const CVector renderedHand{renderMatrices[body::kHandL][12],renderMatrices[body::kHandL][13],renderMatrices[body::kHandL][14]};CHECK(airportjets::Length(airportjets::Sub(renderedHand,target))<.0002f);
 jetboardingpose::AfterPedRender(gPlayer);CHECK(!std::memcmp(saved,poseKeys,sizeof saved));CHECK(!std::memcmp(&renderedHand,renderMatrices[body::kHandL]+12,sizeof renderedHand));
 for(int sample=15;sample<=60;++sample){const float u=sample/100.0f;LoadRig(bones,animations.at("car_open_lhs"),u*.73333335f);*reinterpret_cast<float *>(boardingAssoc[76]+0x30)=u*.73333335f;std::memcpy(saved,poseKeys,sizeof saved);
  const auto h=body::JointOver(gPlayer,{},body::kHead),l=body::JointOver(gPlayer,{},body::kFootL),r=body::JointOver(gPlayer,{},body::kFootR);CHECK(jetboardingpose::BeforePedRender(gPlayer));const auto corrected=body::JointOver(gPlayer,{},body::kHandL);CHECK(airportjets::Length(airportjets::Sub(corrected,target))<.00025f);
  CHECK(airportjets::Length(airportjets::Sub(body::JointOver(gPlayer,{},body::kHead),h))<.00001f);CHECK(airportjets::Length(airportjets::Sub(body::JointOver(gPlayer,{},body::kFootL),l))<.00001f);CHECK(airportjets::Length(airportjets::Sub(body::JointOver(gPlayer,{},body::kFootR),r))<.00001f);jetboardingpose::AfterPedRender(gPlayer);CHECK(!std::memcmp(saved,poseKeys,sizeof saved));
 }
 *reinterpret_cast<float *>(boardingAssoc[76]+0x30)=.3f*.73333335f;
 for(int interruption=0;interruption<7;++interruption){LoadRig(bones,animations.at("car_open_lhs"),.3f*.73333335f);std::memcpy(saved,poseKeys,sizeof saved);CHECK(jetboardingpose::BeforePedRender(gPlayer));uint8_t applied[sizeof poseKeys];std::memcpy(applied,poseKeys,sizeof applied);auto *clump=game::EntityClump(gPlayer);const int generation=pedGeneration;auto lookup=game::api.PoolsGetPed;
  switch(interruption){case 0:mission=true;break;case 1:++pedGeneration;break;case 2:*reinterpret_cast<void **>(gPlayer+0x58)=&policeMarker;break;case 3:body::KeyRotation(poseKeys[body::kForearmL])=body::X(.5f);break;case 4:*reinterpret_cast<uint8_t **>(poseFrames[body::kUpperArmL]+0x10)=poseKeys[body::kUpperArmR];break;case 5:game::api.PoolsGetPed=nullptr;break;case 6:boardingAssoc[76]=nullptr;break;}
  if(interruption==1||interruption==2||interruption==5)ProtectPoseKeys(true);jetboardingpose::AfterPedRender(gPlayer);ProtectPoseKeys(false);CHECK(!jetboardingpose::snapshot.ped);
  if(interruption==0||interruption==6)CHECK(!std::memcmp(saved,poseKeys,sizeof saved));
  if(interruption==1||interruption==2||interruption==5)CHECK(!std::memcmp(applied,poseKeys,sizeof applied));
  if(interruption==3){CHECK(!std::memcmp(poseKeys[body::kUpperArmL],saved+sizeof poseKeys[0]*body::kUpperArmL,sizeof poseKeys[0]));CHECK(body::KeyRotation(poseKeys[body::kForearmL]).x==body::X(.5f).x);}
  if(interruption==4)CHECK(!std::memcmp(poseKeys[body::kUpperArmL],applied+sizeof poseKeys[0]*body::kUpperArmL,sizeof poseKeys[0]));
  mission=false;pedGeneration=generation;game::api.PoolsGetPed=lookup;*reinterpret_cast<void **>(gPlayer+0x58)=clump;boardingAssoc[76]=boardingAnims[1];
 }
 for(int change=0;change<3;++change){LoadRig(bones,animations.at("car_open_lhs"),.3f*.73333335f);std::memcpy(saved,poseKeys,sizeof saved);const int generation=pedGeneration;auto *clump=game::EntityClump(gPlayer);
  if(change==0){body::gSkel={};body::gFindBone=PoseFindBone;poseBoundary=[](){++pedGeneration;ProtectPoseKeys(true);};}else if(change==1)poseBoundary=[](){*reinterpret_cast<void **>(gPlayer+0x58)=&policeMarker;ProtectPoseKeys(true);};else poseBoundary=[](){boardingAssoc[76]=nullptr;};
  CHECK(!jetboardingpose::BeforePedRender(gPlayer));ProtectPoseKeys(false);CHECK(!std::memcmp(saved,poseKeys,sizeof saved));CHECK(!jetboardingpose::snapshot.ped);pedGeneration=generation;*reinterpret_cast<void **>(gPlayer+0x58)=clump;boardingAssoc[76]=boardingAnims[1];
 }

 // Each measurement starts with an empty body cache. The native matrix
 // builder reads the actual bound keys, independently of body::gSkel.
 std::vector<std::pair<const char *,int>> boundaries{{"hierarchy",1},{"matrices",1},{"update",1},{"update",2}};
 for(int n=1;n<=22;++n)boundaries.push_back({"index",n});
 for(int n=1;n<=134;++n)boundaries.push_back({"find",n});
 for(const auto boundary:boundaries)for(int mutation=0;mutation<4;++mutation){
  LoadRig(bones,animations.at("car_open_lhs"),.3f*.73333335f);body::gSkel={};body::gFindBone=PoseFindBone;std::memcpy(saved,poseKeys,sizeof saved);const int generation=pedGeneration;auto *clump=game::EntityClump(gPlayer);
  measurePoint=boundary.first;measureOrdinal=boundary.second;measureSeen=0;
  switch(mutation){case 0:measureMutation=[](){++pedGeneration;ProtectPoseKeys(true);};break;case 1:measureMutation=[](){*reinterpret_cast<void **>(gPlayer+0x58)=&policeMarker;ProtectPoseKeys(true);};break;case 2:measureMutation=[](){boardingAssoc[76]=nullptr;};break;case 3:measureMutation=[](){mission=true;};break;}
  CHECK(!jetboardingpose::BeforePedRender(gPlayer));ProtectPoseKeys(false);CHECK(!measurePoint);CHECK(!jetboardingpose::snapshot.ped);CHECK(!body::gSkel.clump&&!body::gSkel.ok);
  if(mutation>=2)CHECK(!std::memcmp(saved,poseKeys,sizeof saved));
  pedGeneration=generation;*reinterpret_cast<void **>(gPlayer+0x58)=clump;boardingAssoc[76]=boardingAnims[1];mission=false;
 }
 // Foreign native writes survive rollback, and prevent the hand adapter
 // from taking its own pose lease after measurement.
 for(const auto boundary:std::array<std::pair<const char *,int>,3>{{{"update",1},{"find",70},{"update",2}}}){
  LoadRig(bones,animations.at("car_open_lhs"),.3f*.73333335f);body::gSkel={};body::gFindBone=PoseFindBone;std::memcpy(saved,poseKeys,sizeof saved);
  measurePoint=boundary.first;measureOrdinal=boundary.second;measureSeen=0;measureMutation=[](){body::KeyRotation(poseKeys[body::kUpperArmL])=body::X(.5f);body::KeyTranslation(poseKeys[body::kRoot])={.123f,.234f,.345f};};
  CHECK(!jetboardingpose::BeforePedRender(gPlayer));CHECK(!body::gSkel.ok&&!jetboardingpose::snapshot.ped);CHECK(body::KeyRotation(poseKeys[body::kUpperArmL]).x==body::X(.5f).x);CHECK(body::KeyTranslation(poseKeys[body::kRoot]).x==.123f);
  std::memcpy(saved+sizeof poseKeys[0]*body::kUpperArmL+0x10,&body::KeyRotation(poseKeys[body::kUpperArmL]),sizeof(body::Quat));std::memcpy(saved+0x20,&body::KeyTranslation(poseKeys[body::kRoot]),sizeof(CVector));CHECK(!std::memcmp(saved,poseKeys,sizeof saved));
 }
 // A live clump can replace a key allocation during the native rebuild.
 // The old detached allocation is poisoned while rollback handles others.
 for(const auto boundary:std::array<std::pair<const char *,int>,2>{{{"update",1},{"find",70}}}){
  LoadRig(bones,animations.at("car_open_lhs"),.3f*.73333335f);body::gSkel={};body::gFindBone=PoseFindBone;std::memcpy(saved,poseKeys,sizeof saved);std::memcpy(originalLeftKey,poseKeys[body::kUpperArmL],sizeof originalLeftKey);
  measurePoint=boundary.first;measureOrdinal=boundary.second;measureSeen=0;measureMutation=ReplaceLeftKey;
  CHECK(!jetboardingpose::BeforePedRender(gPlayer));ProtectLeftKey(false);CHECK(!body::gSkel.clump&&!body::gSkel.ok&&!jetboardingpose::snapshot.ped);CHECK(*reinterpret_cast<uint8_t **>(poseFrames[body::kUpperArmL]+0x10)==replacementKey);CHECK(body::KeyRotation(replacementKey).x==body::X(.5f).x);
  for(int bone=0;bone<body::kBones;++bone)if(bone!=body::kUpperArmL)CHECK(!std::memcmp(poseKeys[bone],saved+sizeof poseKeys[0]*bone,sizeof poseKeys[0]));
 }
 // A reentrant legacy preparation can publish its own complete cache;
 // guarded measurement must release keys without overwriting that cache.
 LoadRig(bones,animations.at("car_open_lhs"),.3f*.73333335f);body::gSkel={};body::gFindBone=PoseFindBone;std::memcpy(saved,poseKeys,sizeof saved);const auto oldVersion=body::gSkeletonVersion;
 measurePoint="update";measureOrdinal=1;measureSeen=0;measureMutation=[](){CHECK(body::Prepare(gPlayer));};
 CHECK(!jetboardingpose::BeforePedRender(gPlayer));CHECK(body::gSkeletonVersion>oldVersion&&body::gSkel.ok);CHECK(!jetboardingpose::snapshot.ped);CHECK(!std::memcmp(saved,poseKeys,sizeof saved));
 // The legacy overload has the same successful measured pose and retains
 // a negative cache for an unsupported clump.
 LoadRig(bones,animations.at("car_open_lhs"),.3f*.73333335f);const auto defaultExpected=body::gSkel;std::memcpy(saved,poseKeys,sizeof saved);body::gSkel={};body::gFindBone=PoseFindBone;nativeUpdates=0;CHECK(body::Prepare(gPlayer));CHECK(nativeUpdates==2);CHECK(!std::memcmp(saved,poseKeys,sizeof saved));
 for(int bone=0;bone<body::kBones;++bone){CHECK(body::gSkel.parent[bone]==defaultExpected.parent[bone]);CHECK(airportjets::Length(airportjets::Sub(body::gSkel.restPos[bone],defaultExpected.restPos[bone]))<.00001f);CHECK(body::Alike(body::gSkel.rest[bone],body::Normalise(defaultExpected.rest[bone]))>.999999f);}
 // First use during a transient animation handoff must also be retried.
 body::gSkel={};*reinterpret_cast<uint8_t **>(poseFrames[body::kFootR]+0x10)=nullptr;CHECK(!body::Prepare(gPlayer));
 *reinterpret_cast<uint8_t **>(poseFrames[body::kFootR]+0x10)=poseKeys[body::kFootR];CHECK(body::Prepare(gPlayer));CHECK(!std::memcmp(saved,poseKeys,sizeof saved));
 // Ordinary aiming/swimming preparation needs the same binding ownership
 // during native rebuilds as a temporary boarding pose.
 LoadRig(bones,animations.at("car_open_lhs"),.3f*.73333335f);body::gSkel={};std::memcpy(saved,poseKeys,sizeof saved);std::memcpy(originalLeftKey,poseKeys[body::kUpperArmL],sizeof originalLeftKey);
 measurePoint="update";measureOrdinal=1;measureSeen=0;measureMutation=ReplaceLeftKey;
 CHECK(!body::Prepare(gPlayer));ProtectLeftKey(false);CHECK(!body::gSkel.ok);CHECK(body::KeyRotation(replacementKey).x==body::X(.5f).x);
 for(int bone=0;bone<body::kBones;++bone)if(bone!=body::kUpperArmL)CHECK(!std::memcmp(poseKeys[bone],saved+sizeof poseKeys[0]*bone,sizeof poseKeys[0]));
 body::gSkel={};int unsupportedCalls=0;auto oldFind=body::gFindBone;body::gFindBone=[](uint8_t *,uint32_t)->uint8_t *{return nullptr;};measurePoint="hierarchy";measureOrdinal=1000;measureSeen=0;CHECK(!body::Prepare(gPlayer));unsupportedCalls=measureSeen;CHECK(!body::Prepare(gPlayer));CHECK(measureSeen==unsupportedCalls&&unsupportedCalls==1);measurePoint=nullptr;body::gFindBone=oldFind;
 auto hierarchy=game::api.GetAnimHierarchyFromSkinClump;game::api.GetAnimHierarchyFromSkinClump=nullptr;CHECK(!jetboardingpose::BeforePedRender(gPlayer));game::api.GetAnimHierarchyFromSkinClump=hierarchy;
 jetboardingpose::findBone=nullptr;poseFindAvailable=false;CHECK(!jetboardingpose::BeforePedRender(gPlayer));poseFindAvailable=true;

 if(!failures)std::puts("Learjet native door reach: real rig wrist contact and exact key restore passed");return failures?1:0;
}

#endif
