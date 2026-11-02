// Actual model roster -> cached engine spool -> wash Frame -> native physical
// integration. The only doubles are game binary functions; macVC is never run.
#define AIRPORTJETS_NO_MAIN
#include "test_airportjets.cpp"
#include <fstream>
#include <sstream>
static void *WashSymbol(void *,const char *);
#include "../src/reactions.cpp"
namespace hooks {void *Install(const char *,void *){return nullptr;}}
namespace playerreact {bool CanRagdoll(uint8_t *){return true;}bool CanStayDown(uint8_t *){return true;}bool CanKnockdown(uint8_t *){return true;}bool CanResistContact(uint8_t *){return true;}void Contact(uint8_t *,Vec3,Vec3,Vec3){}}
namespace shotimpact {bool Current(uint8_t *,int,Event &){return false;}}
#if !defined(JETWASH_BASELINE) && __has_include("../src/jetwash.cpp")
#define dlsym WashSymbol
#include "../src/jetwash.cpp"
#undef dlsym
#else
namespace jetwash {bool Install(){return true;}void Frame(uint32_t){}}
#endif
namespace {
alignas(16) uint8_t washPeds[5][0x8d0]{},washPool[32]{},washInfo[0x258]{},washCol[0x68]{};
uint8_t *washPoolPtr=washPool,washFlags[5]{};
int particleCalls=0,washQueries=0,promotions=0,movingAdds=0,washMoves=0,windFalls=0;
bool wall=false,solidCar=false,roof=false,targetSkin=false,recycleRecipient=false,recycleEmitter=false,reenter=false;
uint8_t *releasedPoolFlags=nullptr;uint8_t newPoolFlags[14]{};int releaseAt=0;
void ReleasePool(int callback){if(releaseAt!=callback)return;releaseAt=0;std::memcpy(newPoolFlags,releasedPoolFlags,14);*reinterpret_cast<uint8_t **>(vehiclePool+8)=newPoolFlags;delete[] releasedPoolFlags;releasedPoolFlags=nullptr;}
alignas(16) uint8_t particleSystems[83*0xa0]{};
struct NativeFume {CVector at{},velocity{};uint32_t expiry=0;bool alive=false;};
std::array<NativeFume,128> nativeFumes{};CVector particleCamera{};int createdFumes=0;
float observedCreateRange=0;bool particleReentry=false,particleOwnerLoss=false,foreignRangeWrite=false;
float &FumeRange(){return *reinterpret_cast<float *>(particleSystems+67*0xa0+0x18);}
void UpdateFumes(){for(auto &p:nativeFumes)if(p.alive&&gameMs>p.expiry)p.alive=false;}
void *NativeCreateFume(const CVector &at,const CVector &velocity){
 // Actual AddParticle2046f0..4730 compares squared camera distance with
 // system+18 before taking a slot; Update later checks particle expiry.
 UpdateFumes();observedCreateRange=FumeRange();const CVector d{at.x-particleCamera.x,at.y-particleCamera.y,at.z-particleCamera.z};
 if(FumeRange()!=0&&FumeRange()<d.x*d.x+d.y*d.y+d.z*d.z)return nullptr;
 for(auto &p:nativeFumes)if(!p.alive){p={at,velocity,gameMs+1000,true};++createdFumes;return &p;}return nullptr;
}
CVector lastFume{},lastFumeVelocity{};std::vector<CVector> fumeOrigins;
float &W(uint8_t *p,size_t at){return *reinterpret_cast<float *>(p+at);}
uint8_t *WashPed(int ref){const unsigned i=unsigned(ref)>>8;return i<5&&!(washFlags[i]&128)&&washFlags[i]==uint8_t(ref)?washPeds[i]:PedAtReference(ref);}
void WashMove(uint8_t *p,float x,float y,float z){++washMoves;const float m=W(p,0xcc);V(p,0x84).x+=x/m;V(p,0x84).y+=y/m;V(p,0x84).z+=z/m;}
void *Fume(int type,const CVector &at,const CVector &velocity,uint8_t *entity,float size,int a,int b,int c,int d){
 ReleasePool(2);CHECK(type==67&&entity==nullptr&&size>0&&size<.35f&&a==0&&b==0&&c==0&&d==0);++particleCalls;lastFume=at;lastFumeVelocity=velocity;fumeOrigins.push_back(at);
 if(particleReentry){particleReentry=false;jetwash::Frame(gameMs);}
 if(particleOwnerLoss){particleOwnerLoss=false;++vehicleFlags[0];}
 if(foreignRangeWrite){foreignRangeWrite=false;FumeRange()=4096;}
 return NativeCreateFume(at,velocity);
}
void Promote(uint8_t *p){++promotions;CHECK((p[0x60]>>3)==3);p[0x19e]=1;p[0x19f]=0;}
void Moving(uint8_t *p){++movingAdds;CHECK(!(game::EntityFlags(p)&(1u<<10)));}
bool WashLOS(const CVector &from,const CVector &to,uint8_t *,uint8_t *&entity,bool buildingsEnabled,bool cars,bool peds,bool objects,bool dummies,bool see,bool some,bool shoot){
 ++washQueries;ReleasePool(1);CHECK(buildingsEnabled&&cars&&!peds&&objects&&!dummies&&!see&&!some&&!shoot);CHECK(ignored==jetVehicles[0]);entity=nullptr;
 if(recycleRecipient){recycleRecipient=false;++vehicleFlags[1];}
 if(recycleEmitter){recycleEmitter=false;++vehicleFlags[0];}
 if(reenter){reenter=false;jetwash::Frame(gameMs);}
 // Geometric blocker planes cut just the ray segment passing through them.
 if(roof&&from.z>15.8f&&to.z<15.8f){entity=buildings[1];return true;}
 if(wall&&from.y>-8&&to.y<-8){entity=buildings[1];return true;}
 if(solidCar&&from.y>-8&&to.y<-8){entity=jetVehicles[2];return true;}
 if(targetSkin){entity=jetVehicles[1];return true;}
 return false;
}
float washRig[body::kBones*16]{};
int WashRef(uint8_t *p){for(int i=0;i<5;++i)if(p==washPeds[i])return i*256+washFlags[i];return p==gPlayer?pedGeneration:-1;}
uint8_t *WashHierarchy(uint8_t *p){return p==reinterpret_cast<uint8_t *>(&clumpMarker)?p:nullptr;}
float *WashMatrices(uint8_t *){return washRig;}
int WashBone(uint8_t *,int tag){for(int i=0;i<body::kBones;++i)if(reactions::kTags[i]==tag)return i;return -1;}
void WashUpdate(uint8_t *){}
void WashFall(uint8_t *p,int duration,int,uint8_t){CHECK(duration==-1);++windFalls;*reinterpret_cast<int32_t *>(p+0x2fc)=game::kPedFall;}
void WashNativePed(uint8_t *p){V(p,0x84).x=V(p,0x84).y=0;}
void WashCollision(uint8_t *){}
void PrepareWash(){
 Setup();std::memset(particleSystems,0,sizeof particleSystems);*reinterpret_cast<int32_t *>(particleSystems+67*0xa0)=67;std::memcpy(particleSystems+67*0xa0+4,"EXHAUST_FUMES",14);*reinterpret_cast<uint32_t *>(particleSystems+67*0xa0+0x6c)=4;FumeRange()=625;nativeFumes={};particleCamera={};createdFumes=0;reactions::gWindPool=&washPoolPtr;reactions::gWindFrame=&frameNumber;gConfig.npcReactions=false;reactions::gInstalled=true;reactions::gRef=WashRef;reactions::orig_Control=WashNativePed;reactions::gPeople.clear();airportjets::gJets={};airportjets::gPrevious=0;airportjets::gReady=false;airportjets::gModel=-1;
 gExtraSymbol=[](const char *n){return WashSymbol(nullptr,n);};CHECK(planeflight::Install());CHECK(airportjets::Install());
 airportjets::Frame(1000);CHECK(lastRequest>=240);*reinterpret_cast<void **>(models[lastRequest]+0x30)=&clumpMarker;streamInfo[lastRequest*32+0x10]=1;
 airportjets::Frame(1100);CHECK(airportjets::Owns(jetVehicles[0]));ObjectiveJet(gPlayer,18,jetVehicles[0]);WarpJet(gPlayer,jetVehicles[0]);
 V(jetVehicles[0],0x38)={0,0,2};V(jetVehicles[0],0x84)={};W(jetVehicles[0],0xcc)=5000;
 // Native constructor + reference stamp; no assumption about registered ID.
 for(int i=1;i<4;++i){vehicleFlags[i]=uint8_t(12+i);std::memset(jetVehicles[i],0,0x6f0);*reinterpret_cast<int32_t *>(jetVehicles[i]+0x60)=2|0x100|(2<<3)|(i==1?1<<10:0);jetVehicles[i][0x294]=1;W(jetVehicles[i],0xcc)=1000;W(jetVehicles[i],0x2a0)=1000;*reinterpret_cast<int16_t *>(jetVehicles[i]+0x6a)=130;*reinterpret_cast<void **>(jetVehicles[i]+0x58)=&clumpMarker;V(jetVehicles[i],8)={1,0,0};V(jetVehicles[i],0x18)={0,1,0};V(jetVehicles[i],0x28)={0,0,1};V(jetVehicles[i],0x38)={i==1?1.2424f:80.0f,-9.5f,2.2924f};}
 models[130]=washInfo;*reinterpret_cast<uint8_t **>(washInfo+0x20)=washCol;V(washCol,0x10)={-1,-2,-.5f};V(washCol,0x1c)={1,2,1};
 for(int i=0;i<5;++i){washFlags[i]=uint8_t(i+30);std::memset(washPeds[i],0,0x8d0);*reinterpret_cast<int32_t *>(washPeds[i]+0x60)=3|0x100;*reinterpret_cast<int32_t *>(washPeds[i]+0x2fc)=game::kPedIdle;game::PedFlags(washPeds[i])=game::kPedStanding;W(washPeds[i],0xcc)=80;W(washPeds[i],0x44c)=100;*reinterpret_cast<void **>(washPeds[i]+0x58)=&clumpMarker;V(washPeds[i],0x38)={i==0?-1.2424f:80.0f,-9.5f,2.2924f};}
 *reinterpret_cast<uint8_t **>(washPool)=washPeds[0];*reinterpret_cast<uint8_t **>(washPool+8)=washFlags;*reinterpret_cast<int32_t *>(washPool+16)=5;
 game::api.PoolsGetPed=WashPed;game::api.ProcessLineOfSight=WashLOS;game::api.timeMs=&gameMs;game::api.GetIsLineOfSightClear=SightJet;
 wall=solidCar=recycleRecipient=recycleEmitter=reenter=false;particleCalls=washQueries=promotions=movingAdds=washMoves=0;fumeOrigins.clear();gameMs=2000;frameNumber+=10;CHECK(jetwash::Install());jetwash::Frame(gameMs);
}
void TickWash(float ms=20){gameMs+=uint32_t(ms);++frameNumber;gStep=ms/20;CHECK(planeflight::Apply(jetVehicles[0],5));jetwash::Frame(gameMs);}
void Spool(){for(int i=0;i<220;++i){TickWash();V(jetVehicles[0],0x84)={};}}
// CPed control applies gravity/damping, then standing UpdatePosition owns XY.
// The later native world collision consumes the resulting velocity.
void PedNativeTick(uint8_t *p,float seconds){reactions::Hook_Control(p);V(p,0x38).y+=V(p,0x84).y*50*seconds;}
// Real native wheel traction circle: a braked wheel's requested slip is
// clamped by adhesion*step, then converted to impulse by body mass.
// The asset companion pins ProcessWheel's native cap and ApplyMoveForce.
void ParkedCarNativeTick(uint8_t *p,float seconds,float resistance=8.0f){
 const float friction=resistance*seconds/50;const float speed=std::hypot(V(p,0x84).x,V(p,0x84).y);const float remaining=std::max(0.0f,speed-friction);
 if(speed>0){V(p,0x84).x*=remaining/speed;V(p,0x84).y*=remaining/speed;}V(p,0x38).x+=V(p,0x84).x*50*seconds;V(p,0x38).y+=V(p,0x84).y*50*seconds;
}
}
static void *WashSymbol(void *,const char *n){
 if(std::strstr(n,"mod_ParticleSystemManager"))return particleSystems;
 if(std::strstr(n,"ms_pPedPool"))return &washPoolPtr;
 if(std::strstr(n,"ApplyMoveForce"))return reinterpret_cast<void *>(WashMove);
 if(std::strstr(n,"SwitchVehicleToRealPhysics"))return reinterpret_cast<void *>(Promote);
 if(std::strstr(n,"AddToMovingList"))return reinterpret_cast<void *>(Moving);
 if(std::strstr(n,"AddParticle"))return reinterpret_cast<void *>(Fume);
 return JetSymbol(nullptr,n);
}
int main(){
 PrepareWash();gThrottle=0;for(int i=0;i<10;++i)TickWash();CHECK(washMoves==0&&particleCalls==0);gThrottle=255;Spool();
 CHECK(planeflight::EnginePower(jetVehicles[0])>.85f);CHECK(V(jetVehicles[1],0x84).y<-.03f&&V(washPeds[0],0x84).y<-.03f);CHECK(promotions==1&&movingAdds==1&&particleCalls>10&&washQueries>10);CHECK(V(jetVehicles[2],0x84).y==0);
 bool left=false,right=false;for(CVector p:fumeOrigins){left|=std::fabs(p.x+1.2424f)<.01f;right|=std::fabs(p.x-1.2424f)<.01f;CHECK(std::fabs(p.y+5.80f)<.025f&&std::fabs(p.z-2.2924f)<.01f);}CHECK(left&&right&&lastFumeVelocity.y<0);
 // Real captured E9 mode18/zoom3 camera in aircraft local coordinates:
 // the left exhaust outlet is27.036m away, beyond stock CR25 metres.
 const CVector nozzle{-1.2424f,-5.80f,2.2924f};
 const CVector eyes[]={{-.03f,-18,7},{-.030418f,-31.489853f,10.629774f},{0,5,3},{0,-130,10}};
 for(int mode=0;mode<4;++mode){particleCamera=eyes[mode];nativeFumes={};createdFumes=0;
  const CVector remoteCar{particleCamera.x,particleCamera.y+30,particleCamera.z};CHECK(NativeCreateFume(remoteCar,{})==nullptr);
  for(int i=0;i<8;++i)TickWash();const int actualCreated=createdFumes;
  CHECK(mode==3?actualCreated==0:actualCreated>0);CHECK(FumeRange()==625);CHECK(NativeCreateFume(remoteCar,{})==nullptr);
  if(mode==1){UpdateFumes();int alive=0;for(const auto &p:nativeFumes)alive+=p.alive;CHECK(alive>0);const uint32_t liveTime=gameMs;gameMs+=1001;UpdateFumes();int expired=0;for(const auto &p:nativeFumes)expired+=p.alive;CHECK(expired==0);gameMs=liveTime;std::printf("wide native exhaust created%d alive%d at27m; restoredCR2 %.0f\n",actualCreated,alive,FumeRange());}
 }
 particleCamera=eyes[1];nativeFumes={};createdFumes=0;particleReentry=true;for(int i=0;i<8;++i)TickWash();CHECK(!particleReentry&&createdFumes>0&&FumeRange()==625);
 foreignRangeWrite=true;for(int i=0;i<8&&foreignRangeWrite;++i)TickWash();CHECK(!foreignRangeWrite&&FumeRange()==4096);FumeRange()=625;
 particleOwnerLoss=true;for(int i=0;i<8&&particleOwnerLoss;++i)TickWash();CHECK(!particleOwnerLoss&&FumeRange()==625);--vehicleFlags[0];
 // Existing unlimited and larger user ranges retain their native meaning.
 for(float custom:{0.0f,14400.0f}){FumeRange()=custom;for(int i=0;i<8;++i)TickWash();CHECK(FumeRange()==custom&&observedCreateRange==custom);}FumeRange()=625;
 *reinterpret_cast<int32_t *>(particleSystems+67*0xa0)=68;nativeFumes={};createdFumes=0;for(int i=0;i<8;++i)TickWash();CHECK(createdFumes==0&&FumeRange()==625);*reinterpret_cast<int32_t *>(particleSystems+67*0xa0)=67;
 *reinterpret_cast<uint32_t *>(particleSystems+67*0xa0+0x6c)=1;nativeFumes={};createdFumes=0;for(int i=0;i<8;++i)TickWash();CHECK(createdFumes==0&&FumeRange()==625);*reinterpret_cast<uint32_t *>(particleSystems+67*0xa0+0x6c)=4;
 particleCamera={};const CVector captured=eyes[1];const float outletDistance=std::sqrt((captured.x-nozzle.x)*(captured.x-nozzle.x)+(captured.y-nozzle.y)*(captured.y-nozzle.y)+(captured.z-nozzle.z)*(captured.z-nozzle.z));CHECK(outletDistance>27&&outletDistance<27.1f);
 const CVector carSpeed=V(jetVehicles[1],0x84);const int before=particleCalls;jetwash::Frame(gameMs);CHECK(V(jetVehicles[1],0x84).y==carSpeed.y&&particleCalls==before);reenter=true;TickWash();
 V(jetVehicles[1],0x84)={};V(washPeds[0],0x84)={};wall=true;TickWash();CHECK(V(jetVehicles[1],0x84).y==0&&V(washPeds[0],0x84).y==0);wall=false;solidCar=true;TickWash();CHECK(V(washPeds[0],0x84).y==0);solidCar=false;
 recycleRecipient=true;TickWash();CHECK(V(jetVehicles[1],0x84).y==0);TickWash();CHECK(V(jetVehicles[1],0x84).y<0); // next current generation remains an ordinary recipient
 V(jetVehicles[1],0x84)={};jetVehicles[1][0x294]=2;washPeds[0][0x19a]=2;TickWash();CHECK(V(jetVehicles[1],0x84).y==0);jetVehicles[1][0x294]=1;washPeds[0][0x19a]=1;
 for(int at:{0,1,2,3}){V(washPeds[0],0x84)={};if(at==0)washPeds[0][0x4c0]=1;if(at==1)*reinterpret_cast<void **>(washPeds[0]+0x720)=jetVehicles[0];if(at==2)W(washPeds[0],0x44c)=0;if(at==3)V(washPeds[0],0x38).z+=8;TickWash();CHECK(V(washPeds[0],0x84).y==0);washPeds[0][0x4c0]=0;*reinterpret_cast<void **>(washPeds[0]+0x720)=nullptr;W(washPeds[0],0x44c)=100;if(at==3)V(washPeds[0],0x38).z-=8;}
 V(jetVehicles[1],0x84)={};jetVehicles[0][0x295]&=~16;TickWash();CHECK(V(jetVehicles[1],0x84).y==0);jetVehicles[0][0x295]|=16;
 V(jetVehicles[1],0x84)={};gameMs+=500;++frameNumber;jetwash::Frame(gameMs);CHECK(V(jetVehicles[1],0x84).y==0);
 // Rotate the actual source basis ninety degrees; nozzles and push follow it.
 V(jetVehicles[0],8)={0,1,0};V(jetVehicles[0],0x18)={-1,0,0};V(jetVehicles[1],0x38)={9.5f,1.2424f,2.2924f};Spool();CHECK(V(jetVehicles[1],0x84).x>.03f);CHECK(std::fabs(lastFume.x-5.80f)<.025f&&lastFumeVelocity.x>0);
 // Hold recipients at fixed relative range while comparing real dt integration.
 for(int i=0;i<600;++i){TickWash();V(jetVehicles[0],0x84)={};}
 float velocities[2]{};for(int run=0;run<2;++run){const int hz=run?120:30;V(jetVehicles[1],0x84)={};for(int i=0;i<hz;++i){const uint32_t base=gameMs;const uint32_t target=base+(i%3==0?(run?9:34):(run?8:33));TickWash(float(target-base));V(jetVehicles[0],0x84)={};}velocities[run]=V(jetVehicles[1],0x84).x*50;}std::printf("wash 30Hz %.3f,120Hz %.3fm/s\n",velocities[0],velocities[1]);CHECK(std::fabs(velocities[0]-velocities[1])<.6f);
 V(jetVehicles[0],8)={1,0,0};V(jetVehicles[0],0x18)={0,1,0};V(jetVehicles[1],0x38)={1.2424f,-8.3f,2.2924f};V(jetVehicles[1],0x84)={};const float start=V(jetVehicles[1],0x38).y;const float pedStart=V(washPeds[0],0x38).y;for(int i=0;i<50;++i){TickWash();ParkedCarNativeTick(jetVehicles[1],.02f);PedNativeTick(washPeds[0],.02f);}CHECK(V(jetVehicles[1],0x38).y<start-.1f);CHECK(V(washPeds[0],0x38).y<pedStart-.1f);
 // Force scales with real receiving mass below the acceleration cap.
 V(jetVehicles[1],0x38)={1.2424f,-14,2.2924f};V(jetVehicles[1],0x84)={};W(jetVehicles[1],0xcc)=1000;TickWash();const float light=V(jetVehicles[1],0x84).y;
 V(jetVehicles[1],0x84)={};W(jetVehicles[1],0xcc)=5000;TickWash();const float heavy=V(jetVehicles[1],0x84).y;CHECK(light<0&&heavy<0&&std::fabs(light/heavy-5)<.01f);W(jetVehicles[1],0xcc)=1000;
 V(jetVehicles[1],0x38)={1.2424f,-8.3f,2.2924f};
 // Same production Frame now feeds the actual Euphoria body consumer.
 gConfig.npcReactions=true;reactions::gInstalled=true;reactions::gRef=WashRef;
 reactions::gUpdateMatrix=reactions::gUpdateFrame=reactions::gReindex=WashUpdate;
 reactions::gSetFall=WashFall;reactions::orig_Control=WashNativePed;reactions::orig_WorldCollision=WashCollision;
 game::api.GetAnimHierarchyFromSkinClump=WashHierarchy;game::api.HAnimMatrixArray=WashMatrices;game::api.HAnimIDGetIndex=WashBone;game::api.UpdateRpHAnim=WashUpdate;
 V(washPeds[0],0x38)={-1.2424f,-8.3f,2.2924f};V(washPeds[0],0x84)={};
 const reactphysics::Pose rig={{{0,0,.95f},{0,0,1.35f},{0,0,1.7f},{-.22f,0,1.4f},{-.35f,0,1.1f},{-.4f,0,.8f},{.22f,0,1.4f},{.35f,0,1.1f},{.4f,0,.8f},{-.12f,0,.9f},{-.12f,0,.5f},{-.12f,0,.1f},{.12f,0,.9f},{.12f,0,.5f},{.12f,0,.1f}}};
 for(int j=0;j<reactphysics::Count;++j){float *m=washRig+WashBone(nullptr,reactions::kPhysicalTags[j])*16;m[0]=m[5]=m[10]=m[15]=1;m[12]=rig[j].x-1.2424f;m[13]=rig[j].y-8.3f;m[14]=rig[j].z+1.25f;}
 // A real ground-level actor intersects the plume at its torso even
 // while the native entity root sits below the hot exhaust centreline.
 gConfig.npcReactions=false;V(washPeds[0],0x38)={-1.2424f,-9.0f,1.04f};V(washPeds[0],0x84)={};TickWash();CHECK(V(washPeds[0],0x84).y<0);
 V(washPeds[0],0x38)={-1.2424f,-8.3f,2.2924f};gConfig.npcReactions=true;
 const int windBefore=windFalls;for(int i=0;i<70&&windFalls==windBefore;++i){TickWash();reactions::Hook_Control(washPeds[0]);reactions::Hook_WorldCollision(washPeds[0]);}
 CHECK(windFalls==windBefore+1&&game::Health(washPeds[0])==100);
 CHECK(!reactions::gPeople.empty()&&reactions::gPeople[0].physical.State()==reactphysics::Mode::Falling&&!reactions::gPeople[0].physical.Dead());
 // Original636 placement on the same actual runway as the native SUV
 // and pedestrian. Load real COL1 bounds rather than lifting targets into
 // the exhaust axis. Source lowest gear is2.1948m below its entity root.
 const char *dataEnv=std::getenv("GAMEDATA");const std::string dataPath=dataEnv?dataEnv:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";
 std::ifstream carFile(dataPath+"/models/coll/vehicles.col",std::ios::binary);CHECK(bool(carFile));const std::vector<uint8_t> carBytes((std::istreambuf_iterator<char>(carFile)),{});bool foundCar=false;
 for(size_t at=0;at+80<=carBytes.size();){uint32_t bytes=0;std::memcpy(&bytes,carBytes.data()+at+4,4);if(bytes>carBytes.size()-at-8)break;if(!std::strncmp(reinterpret_cast<const char *>(carBytes.data()+at+8),"landstal",22)){std::memcpy(washCol+0x10,carBytes.data()+at+48,24);foundCar=true;break;}at+=8+bytes;}CHECK(foundCar);
 std::ifstream handling(dataPath+"/data/handling.cfg");CHECK(bool(handling));float nativeMass=0;for(std::string line;std::getline(handling,line);){std::istringstream row(line);std::string name;float value=0;if(row>>name>>value)if(name=="LANDSTAL"){nativeMass=value;std::string field;for(int column=2;column<=17&&row>>field;++column)if(column==17)CHECK(std::stof(field)==3.0f);}}CHECK(nativeMass==1700);
 const float runway=13.826f;V(jetVehicles[0],0x38)={0,0,runway+2.1948f};W(jetVehicles[1],0xcc)=nativeMass;reactions::gInstalled=true;gConfig.npcReactions=false;
 for(float behind:{5.0f,10.0f,15.0f}){V(jetVehicles[1],0x38)={0,-5.80f-behind,runway-W(washCol,0x18)};V(washPeds[0],0x38)={-1.2424f,-5.80f-behind,runway+1.04f};V(jetVehicles[1],0x84)=V(washPeds[0],0x84)={};TickWash();const float carAcceleration=-V(jetVehicles[1],0x84).y*2500,pedAcceleration=-V(washPeds[0],0x84).y*2500;std::printf("groundwash %.0fm SUV%.3f ped%.3fm/s2\n",behind,carAcceleration,pedAcceleration);CHECK(carAcceleration>.1f&&pedAcceleration>.1f);if(behind==5)CHECK(carAcceleration>3.0f);}
 // The grounded SUV moves through the real traction-circle consumer
 // against its declared native3m/s² service brake; ped control resets XY,
 // then the real collision consumer integrates the delivered wind once.
 V(jetVehicles[1],0x38)={0,-10.80f,runway-W(washCol,0x18)};V(washPeds[0],0x38)={-1.2424f,-10.80f,runway+1.04f};V(jetVehicles[1],0x84)=V(washPeds[0],0x84)={};
 const float groundCarStart=V(jetVehicles[1],0x38).y,groundPedStart=V(washPeds[0],0x38).y;
 for(int i=0;i<50;++i){TickWash();ParkedCarNativeTick(jetVehicles[1],.02f,3.0f);PedNativeTick(washPeds[0],.02f);}
 CHECK(V(jetVehicles[1],0x38).y<groundCarStart-.25f&&V(washPeds[0],0x38).y<groundPedStart-.1f);
 // A wall or solid vehicle protects bodies on that same runway.
 wall=true;V(jetVehicles[1],0x84)=V(washPeds[0],0x84)={};TickWash();CHECK(V(jetVehicles[1],0x84).y==0&&V(washPeds[0],0x84).y==0);wall=false;roof=true;TickWash();CHECK(V(jetVehicles[1],0x84).y==0&&V(washPeds[0],0x84).y==0);roof=false;
 V(jetVehicles[0],0x38)={0,0,2};V(jetVehicles[1],0x38)={1.2424f,-8.3f,2.2924f};V(washPeds[0],0x38)={-1.2424f,-8.3f,2.2924f};
 // Pool replacement invalidates all carried emission/time state before any push.
 uint8_t replacementFlags[5];std::memcpy(replacementFlags,washFlags,5);*reinterpret_cast<uint8_t **>(washPool+8)=replacementFlags;V(jetVehicles[1],0x84)={};TickWash();CHECK(V(jetVehicles[1],0x84).y==0);*reinterpret_cast<uint8_t **>(washPool+8)=washFlags;TickWash();
 // Invalid capacities never permit an unbounded walk over native memory.
 *reinterpret_cast<int32_t *>(vehiclePool+16)=1025;V(jetVehicles[1],0x84)={};gameMs+=20;++frameNumber;jetwash::Frame(gameMs);CHECK(V(jetVehicles[1],0x84).y==0);*reinterpret_cast<int32_t *>(vehiclePool+16)=14;TickWash();TickWash();
 // Native LOS hitting the recipient's own skin is a valid endpoint.
 targetSkin=true;V(jetVehicles[1],0x84)={};TickWash();CHECK(V(jetVehicles[1],0x84).y<0);targetSkin=false;
 for(CVector outside: {CVector{1.2424f,3,2.2924f},CVector{1.2424f,-35,2.2924f},CVector{9,-10,2.2924f}}){V(jetVehicles[1],0x38)=outside;V(jetVehicles[1],0x84)={};TickWash();CHECK(V(jetVehicles[1],0x84).y==0);}
 // Native callbacks can reconstruct a pool, immediately freeing its
 // former generation storage. No later scan may read that old snapshot.
 for(int callback:{1,2}){releasedPoolFlags=new uint8_t[14];std::memcpy(releasedPoolFlags,vehicleFlags,14);*reinterpret_cast<uint8_t **>(vehiclePool+8)=releasedPoolFlags;Spool();V(jetVehicles[1],0x38)={1.2424f,-8.3f,2.2924f};V(jetVehicles[1],0x84)={};releaseAt=callback;for(int i=0;i<5&&releaseAt;++i){V(jetVehicles[1],0x84)={};TickWash();}CHECK(releaseAt==0&&releasedPoolFlags==nullptr&&V(jetVehicles[1],0x84).y==0);*reinterpret_cast<uint8_t **>(vehiclePool+8)=vehicleFlags;Spool();}
 V(jetVehicles[1],0x38)={1.2424f,-8.3f,2.2924f};V(jetVehicles[1],0x84)={};recycleEmitter=true;TickWash();CHECK(V(jetVehicles[1],0x84).y==0);
 V(jetVehicles[1],0x84)={};gameMs=1;++frameNumber;jetwash::Frame(gameMs);CHECK(V(jetVehicles[1],0x84).y==0);
 if(!failures)puts("jetwash native consumers: all passed");return failures?1:0;
}
