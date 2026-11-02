// Installed entry consumer: native-shaped animation records and live player/jet pools.
#define AIRPORTJETS_NO_MAIN
#include "test_airportjets.cpp"
namespace {
#if __has_feature(address_sanitizer)
extern "C" void __asan_poison_memory_region(const void *,size_t);
extern "C" void __asan_unpoison_memory_region(const void *,size_t);
#endif
uint8_t *protectedPlane=nullptr;
void ProtectPlane(uint8_t *v,bool protect){
 if(!v)return;
#if __has_feature(address_sanitizer)
 if(protect)__asan_poison_memory_region(v+0x388,0x6f0-0x388);else __asan_unpoison_memory_region(v+0x388,0x6f0-0x388);
#else
 (void)protect;
#endif
}
void CleanupBoardTest(){if(protectedPlane){ProtectPlane(protectedPlane,false);for(size_t i=0x388;i<0x6f0;++i)CHECK(protectedPlane[i]==0xa5);protectedPlane=nullptr;}boardingBoundary=nullptr;button=false;mission=cutsceneRunning=false;blocked=false;gConfig.planes=true;*reinterpret_cast<uint16_t *>(gPad+0xf0)=0;auto restart=JetHook<void (*)()>("ClearForRestart"),shutdown=JetHook<void (*)()>("CModelInfo8ShutDown");if(restart)restart();if(shutdown)shutdown();}
uint8_t *BeginBoardTest(uint32_t now){Setup();gameMs=now;JetFrame(now);const int model=lastRequest;CHECK(model>=240&&model<=249);if(model<240||model>249)return nullptr;
 *reinterpret_cast<void **>(models[model]+0x30)=&clumpMarker;streamInfo[model*32+0x10]=1;gameMs+=100;JetFrame(gameMs);auto *v=jetVehicles[0];CHECK(airportjets::Owns(v));
 V(gPlayer,0x38)={V(v,0x38).x-2.3f,V(v,0x38).y+3.7f,14.84f};gameMs+=100;std::fill(v+0x388,v+0x6f0,0xa5);protectedPlane=v;ProtectPlane(v,true);auto enter=JetHook<void (*)(uint8_t *,uint8_t *,uint32_t)>("SetEnterCar");CHECK(enter);if(enter)enter(gPlayer,v,0);return v;
}
void AdvanceBoardTest(int ticks=1){for(int i=0;i<ticks;++i){gameMs+=50;JetFrame(gameMs);}}
const char *interruptBoundary=nullptr;int interruptKind=0;
void InterruptBoarding(const char *boundary){if(std::strcmp(boundary,interruptBoundary))return;boardingBoundary=nullptr;switch(interruptKind){case 0:mission=true;break;case 1:++vehicleFlags[0];break;case 2:++pedGeneration;break;case 3:{auto restart=JetHook<void (*)()>("ClearForRestart");if(restart)restart();break;}}}
void ReenterBoarding(const char *boundary){if(std::strcmp(boundary,"association"))return;boardingBoundary=nullptr;airportjets::CancelDoor(airportjets::gJets[0]);auto *v=jetVehicles[0];V(gPlayer,0x38)={V(v,0x38).x-2.3f,V(v,0x38).y+3.7f,14.84f};auto enter=JetHook<void (*)(uint8_t *,uint8_t *,uint32_t)>("SetEnterCar");enter(gPlayer,v,0);}
}
int main(){
 Setup();CHECK(planeflight::Install());CHECK(airportjets::Install());CleanupBoardTest();
 const int firstWarp=warped;
 if(auto *v=BeginBoardTest(1000)){
  for(int i=0;i<21;++i)AdvanceBoardTest();
  CHECK(warped==firstWarp&&!game::PedInVehicle(gPlayer));CHECK(!playedBoardingAnims.empty()&&!boardingPositions.empty());
  for(int i=0;i<210&&!game::PedInVehicle(gPlayer);++i)AdvanceBoardTest();
  CHECK(warped==firstWarp+1&&game::PedVehicle(gPlayer)==v);CHECK(playedBoardingAnims.size()==7);
  CHECK(!boardingPositions.empty()&&boardingPositions.back().y>V(v,0x38).y+4.9f);
  for(size_t i=1;i<boardingPositions.size();++i){const auto a=boardingPositions[i-1],b=boardingPositions[i];CHECK(std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z))<.31f);}
  CHECK(boardingAssoc[102]&&*reinterpret_cast<float *>(boardingAssoc[102]+0x2c)>=0);
  int stages=0,completed=0,aborted=0;for(const auto &line:boardingLogs){stages+=line.find(" phase ")!=std::string::npos;completed+=line.find(" complete ")!=std::string::npos;aborted+=line.find(" abort ")!=std::string::npos;}CHECK(stages==8&&completed==1&&aborted==0);
 }CleanupBoardTest();
 for(int full:{0,1,2}){const int before=warped;if(auto *v=BeginBoardTest(12000+4000*full)){nativeSeatInitial=full==2?-1:float(full);nativeSeatReuse=full==2?.35f:-1;
  for(int tick=0;tick<350&&!game::PedInVehicle(gPlayer);++tick)AdvanceBoardTest();
  CHECK(warped==before+1&&game::PedInVehicle(gPlayer)&&game::PedVehicle(gPlayer)==v);
  CHECK(std::fabs(V(gPlayer,0x38).x-V(v,0x38).x+.32f)<.001f&&std::fabs(V(gPlayer,0x38).y-V(v,0x38).y-5)<.001f);
 }CleanupBoardTest();nativeSeatInitial=nativeSeatReuse=-1;}
 auto control=JetHook<void (*)(uint8_t *)>("CPlayerPed14ProcessControl");CHECK(control);
 for(int cancel=0;cancel<19;++cancel){const int before=warped;if(auto *v=BeginBoardTest(20000+30000*cancel)){
  for(int i=0;i<150&&(!boardingAssoc[80]||*reinterpret_cast<float *>(boardingAssoc[80]+0x30)<.2f);++i)AdvanceBoardTest();
  CHECK(boardingAssoc[80]&&!game::PedInVehicle(gPlayer));if(!boardingAssoc[80]){CleanupBoardTest();continue;}const int beforeControl=nativeControls;control(gPlayer);CHECK(nativeControls==beforeControl);
  CHECK(!(game::EntityFlags(gPlayer)&game::kEntityUsesCollision));
  switch(cancel){case 0:mission=true;break;case 1:cutsceneRunning=true;break;case 2:*reinterpret_cast<uint16_t *>(gPad+0xf0)=1;break;case 3:gConfig.planes=false;break;case 4:*reinterpret_cast<float *>(gPlayer+0x44c)=0;break;case 5:game::SetPedState(gPlayer,50);break;case 6:*reinterpret_cast<int32_t *>(gPlayer+0x19c)=7;break;case 7:*reinterpret_cast<uint8_t **>(v+0x218)=gBoat;break;case 8:++pedGeneration;break;case 9:++vehicleFlags[0];break;case 10:blocked=true;break;case 11:V(gPlayer,0x38).x+=5;break;case 12:*reinterpret_cast<void **>(gPlayer+0x58)=&policeMarker;break;case 13:boardingAssoc[80]=nullptr;break;case 14:*reinterpret_cast<int32_t *>(boardingAssoc[80]+0x40)=1;break;case 15:gameMs=1;break;case 16:{auto restart=JetHook<void (*)()>("ClearForRestart");restart();break;}case 17:freezeBoardingAnimation=true;break;case 18:button=true;break;}
  AdvanceBoardTest();button=false;AdvanceBoardTest(cancel==17?500:30);CHECK(warped==before&&!game::PedInVehicle(gPlayer));if(cancel==18)CHECK(std::fabs(V(gPlayer,0x38).x-V(v,0x38).x+2.25f)<.001f);control(gPlayer);CHECK(nativeControls>beforeControl);
  if(cancel!=8)CHECK(game::EntityFlags(gPlayer)&game::kEntityUsesCollision);else CHECK(!(game::EntityFlags(gPlayer)&game::kEntityUsesCollision));
  for(const auto &jet:airportjets::gJets)CHECK(jet.doorPed!=gPlayer&&jet.boardingSerial==0);
 }CleanupBoardTest();}
 if(BeginBoardTest(900000)){AdvanceBoardTest(30);const auto old=airportjets::gJets[0].boardingSerial;boardingBoundary=ReenterBoarding;AdvanceBoardTest();CHECK(!boardingBoundary);CHECK(airportjets::gJets[0].boardingSerial&&airportjets::gJets[0].boardingSerial!=old);CHECK(!(game::EntityFlags(gPlayer)&game::kEntityUsesCollision));}CleanupBoardTest();
 for(const char *boundary:{"blend","association","time","teleport","objective"})for(int mutation=0;mutation<4;++mutation){const int before=warped;if(BeginBoardTest(700000+30000*mutation)){
  interruptBoundary=boundary;interruptKind=mutation;boardingBoundary=InterruptBoarding;AdvanceBoardTest(350);CHECK(!boardingBoundary);CHECK(warped==before);
  for(const auto &jet:airportjets::gJets)CHECK(jet.doorPed!=gPlayer&&jet.boardingSerial==0);
  if(mutation==0&&std::strcmp(boundary,"objective")==0)CHECK(game::PedObjective(gPlayer)==0);
 }CleanupBoardTest();}
 for(int failure=0;failure<5;++failure){if(BeginBoardTest(2000000+30000*failure)){
  for(int tick=0;tick<200&&(!boardingAssoc[80]||*reinterpret_cast<float *>(boardingAssoc[80]+0x30)<.20f);++tick)AdvanceBoardTest();CHECK(boardingAssoc[80]);
  const char *reason="";switch(failure){
   case 0:*reinterpret_cast<float *>(boardingAssoc[80]+0x30)=std::numeric_limits<float>::quiet_NaN();reason="reasonclock-invalid";break;
   case 1:*reinterpret_cast<float *>(boardingAssoc[80]+0x30)=.01f;reason="reasonclock-reset";break;
   case 2:*reinterpret_cast<float *>(boardingAssoc[80]+0x30)+=.3f;reason="reasonclock-jump";break;
   case 3:gameMs+=21000;reason="reasontimeout";break;
   case 4:*reinterpret_cast<float *>(boardingAssoc[80]+0x30)+=.19f;reason="reasonposition-step";break;
  }
  AdvanceBoardTest(30);int aborts=0;for(const auto &line:boardingLogs)if(line.find(" abort ")!=std::string::npos){++aborts;CHECK(line.find(reason)!=std::string::npos);CHECK(line.find("duration")!=std::string::npos&&line.find("callback")!=std::string::npos);}CHECK(aborts==1&&!game::PedInVehicle(gPlayer));
 }CleanupBoardTest();}
 if(!failures)std::puts("Learjet boarding installed entry, bounded route and cancellation: all passed");return failures?1:0;
}
