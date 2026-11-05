// Persistent anchors exercise the production Frame roster and native world callbacks.
#define AIRPORTJETS_NO_MAIN
#include "test_airportjets.cpp"

namespace {
int sourceAdds[7]{},sourceRemoves[7]{};
void Expect(bool condition,const char *reason){if(!condition){++failures;std::printf("SPAWNER FAIL: %s\n",reason);}}
int SourceIndex(uint8_t *v){for(int i=0;i<7;++i)if(v==buildings[i])return i;return -1;}
void CountedAdd(uint8_t *v){const int i=SourceIndex(v);if(i>=0){Expect(!sourceInWorld[i],"source world membership is never added twice");++sourceAdds[i];}AddWorld(v);}
void CountedRemove(uint8_t *v){const int i=SourceIndex(v);if(i>=0){Expect(sourceInWorld[i],"source world membership is never removed twice");++sourceRemoves[i];}RemoveWorld(v);}
void AllSourcesHidden(const char *phase){for(int i=0;i<7;++i)if(sourceInWorld[i]){++failures;std::printf("SPAWNER FAIL: source%d is static scenery during %s\n",i,phase);}}
void AllSourcesVisible(){for(bool visible:sourceInWorld)Expect(visible,"explicit cleanup restores original membership");}
void MembershipCounts(int removes,int adds){for(int i=0;i<7;++i){Expect(sourceRemoves[i]==removes,"each source is removed exactly once per enabled lifetime");Expect(sourceAdds[i]==adds,"each source is restored exactly once per explicit cleanup");}}
void Fresh(){buildingPoolPointer=buildingPool;vehiclePoolPointer=vehiclePool;Setup();std::fill(sourceAdds,sourceAdds+7,0);std::fill(sourceRemoves,sourceRemoves+7,0);airportjets::n.add=CountedAdd;airportjets::n.remove=CountedRemove;area=0;mission=cutsceneRunning=blocked=false;airportjetmodel::requireDonor=false;}
int Load(uint32_t now){JetFrame(now);const int id=lastRequest;Expect(id>=240&&id<=249,"nearby anchor requests a private vehicle model");if(id>=240&&id<=249){*reinterpret_cast<void **>(models[id]+0x30)=&clumpMarker;streamInfo[id*32+0x10]=1;JetFrame(now+100);}return id;}
void Empty(uint8_t *v){gPlayer[0x4c0]=0;*reinterpret_cast<uint8_t **>(v+0x218)=nullptr;*reinterpret_cast<int32_t *>(gPlayer+0x2fc)=game::kPedIdle;}
}

int main(){
 Fresh();CHECK(planeflight::Install());CHECK(airportjets::Install());airportjets::n.add=CountedAdd;airportjets::n.remove=CountedRemove;
 auto restart=JetHook<void (*)()>("ClearForRestart");auto shutdown=JetHook<void (*)()>("CModelInfo8ShutDown");CHECK(restart&&shutdown);if(!restart||!shutdown)return 1;
 JetFrame(1000);const int model=lastRequest;CHECK(model>=240&&model<=249);if(model<240||model>249)return 1;
 AllSourcesHidden("initial asynchronous template request");MembershipCounts(1,0);
 *reinterpret_cast<void **>(models[model]+0x30)=&clumpMarker;streamInfo[model*32+0x10]=1;JetFrame(1100);
 Expect(airportjets::Owns(jetVehicles[0]),"usable jet spawns at the nearby anchor");AllSourcesHidden("one nearby usable jet, six distant spawn points");MembershipCounts(1,0);
 const int initialDestroyed=destroyed;V(gPlayer,0x38)={10000,10000,20};JetFrame(2000);
 Expect(destroyed==initialDestroyed+1,"distant unoccupied native actor retires");AllSourcesHidden("departure from the airport");
 blocked=true;V(gPlayer,0x38)={-1767,-1271,14.84f};const int beforeBlocked=allocated;JetFrame(2500);
 Expect(allocated==beforeBlocked,"obstructed spawn waits without placing a jet in an obstacle");AllSourcesHidden("obstructed parking spot");
 blocked=false;JetFrame(3500);Expect(allocated==beforeBlocked+1&&airportjets::Owns(jetVehicles[0]),"return creates a usable replacement after clearance");AllSourcesHidden("return and successful respawn");
 DestroyJet(jetVehicles[0]);const int beforeDeletion=allocated;JetFrame(4500);
 Expect(allocated==beforeDeletion+1&&airportjets::Owns(jetVehicles[0]),"native actor deletion reacquires a usable current generation");
 ObjectiveJet(gPlayer,18,jetVehicles[0]);WarpJet(gPlayer,jetVehicles[0]);const int beforeOccupied=destroyed;V(gPlayer,0x38)=V(jetVehicles[0],0x38)={10000,10000,20};JetFrame(5000);
 Expect(destroyed==beforeOccupied&&airportjets::Owns(jetVehicles[0]),"occupied plane is preserved far from its anchor");AllSourcesHidden("occupied flight away from the airport");
 gConfig.planes=false;JetFrame(5100);Expect(!sourceInWorld[0],"disabling does not restore scenery below an occupied owned aircraft");for(int i=1;i<7;++i)Expect(sourceInWorld[i],"disabling restores unused distant original sources");
 Empty(jetVehicles[0]);JetFrame(5200);AllSourcesVisible();MembershipCounts(1,1);const int disabledDestroyed=destroyed;JetFrame(5300);Expect(destroyed==disabledDestroyed,"repeated disabled frames do not destroy actors twice");MembershipCounts(1,1);
 gConfig.planes=true;V(gPlayer,0x38)={-1767,-1271,14.84f};JetFrame(5400);AllSourcesHidden("re-enabled anchors");MembershipCounts(2,1);
 JetFrame(100);AllSourcesHidden("timer rewind");MembershipCounts(2,1);
 restart();AllSourcesVisible();MembershipCounts(2,2);restart();MembershipCounts(2,2);shutdown();

 // Approaching in a car must request and materialize the same nearby aircraft.
 Fresh();gPlayer[0x4c0]=1;*reinterpret_cast<uint8_t **>(gPlayer+0x4b8)=jetVehicles[7];const int beforeDriving=allocated;lastRequest=-1;JetFrame(5500);const int drivingModel=lastRequest;Expect(drivingModel>=240&&drivingModel<=249,"Learjet requests its model while approaching in another vehicle");if(drivingModel>=240&&drivingModel<=249&&models[drivingModel]){*reinterpret_cast<void **>(models[drivingModel]+0x30)=&clumpMarker;streamInfo[drivingModel*32+0x10]=1;JetFrame(5600);}Expect(allocated==beforeDriving+1&&airportjets::Owns(jetVehicles[0]),"Learjet streams and spawns while player approaches in another vehicle");restart();shutdown();
 // Source hiding is independent of proximity, model slots, player existence,
 // interior, missions and donor readiness, so static RT capture sees none.
 for(int context=0;context<6;++context){Fresh();
  if(context==0)*reinterpret_cast<int32_t *>(modelStore)=110;
  if(context==1)V(gPlayer,0x38)={10000,10000,20};
  if(context==2)area=1;
  if(context==3)mission=true;
  if(context==4)cutsceneRunning=true;
  auto findPlayer=game::api.FindPlayerPed;if(context==5)game::api.FindPlayerPed=[]()->uint8_t *{return nullptr;};
  const int before=allocated;JetFrame(6000+context*100);AllSourcesHidden("inactive native spawning context");MembershipCounts(1,0);Expect(allocated==before,"inactive contexts do not allocate an aircraft");
  game::api.FindPlayerPed=findPlayer;restart();AllSourcesVisible();MembershipCounts(1,1);shutdown();
 }
 Fresh();airportjetmodel::requireDonor=true;streamInfo[227*32+0x10]=0;*reinterpret_cast<void **>(policeInfo+0x30)=nullptr;
 JetFrame(7000);const int donorModel=lastRequest;if(donorModel>=240&&donorModel<=249){*reinterpret_cast<void **>(models[donorModel]+0x30)=&clumpMarker;streamInfo[donorModel*32+0x10]=1;JetFrame(7100);Expect(lastRequest==227,"native donor remains asynchronous");}
 AllSourcesHidden("pending cockpit donor");MembershipCounts(1,0);restart();shutdown();

 // All original placements retain one anchor across complete actor turnover.
 Fresh();for(int i=0;i<7;++i)V(buildings[i],0x38).x=-1765.0f+30*(i-3);const int beforeSeven=allocated;Load(8000);
 Expect(allocated==beforeSeven+7,"all seven clear nearby anchors spawn usable actors");for(int i=0;i<7;++i)Expect(airportjets::Owns(jetVehicles[i]),"every anchor owns its usable actor");
 V(gPlayer,0x38)={10000,10000,20};JetFrame(8200);V(gPlayer,0x38)={-1767,-1271,14.84f};JetFrame(8300);
 Expect(allocated==beforeSeven+14,"all seven anchors respawn on airport return");AllSourcesHidden("seven-anchor return");MembershipCounts(1,0);restart();AllSourcesVisible();MembershipCounts(1,1);shutdown();

 // Native slot recycling must not destroy or remove the foreign generation.
 Fresh();Load(9000);const int beforeRecycle=destroyed;const int beforeForeign=allocated;const int beforeRemove=removed;
 ++vehicleFlags[0];*reinterpret_cast<int16_t *>(jetVehicles[0]+0x6a)=190;jetVehicles[0][0x294]=2;JetFrame(9200);
 Expect(destroyed==beforeRecycle&&removed==beforeRemove,"recycled foreign vehicle receives no owned cleanup");Expect(allocated==beforeForeign+1&&airportjets::Owns(jetVehicles[1]),"recycled actor leaves its anchor retryable");restart();shutdown();

 // A replaced/moved source is foreign to the saved anchor: never restore it.
 alignas(16) uint8_t replacementSources[7][0x78]{},replacementPool[32]{};uint8_t replacementFlags[7]{};
 for(int invalidation=0;invalidation<9;++invalidation){Fresh();Load(10000+invalidation*1000);const int beforeInvalid=destroyed,beforeSourceRemoved=removed;
  std::memcpy(replacementSources,buildings,sizeof buildings);std::memcpy(replacementFlags,buildingFlags,sizeof buildingFlags);std::memcpy(replacementPool,buildingPool,sizeof buildingPool);
  switch(invalidation){case 0:++buildingFlags[0];*reinterpret_cast<int16_t *>(buildings[0]+0x6a)=638;break;case 1:V(buildings[0],0x38).x+=30;break;case 2:V(buildings[0],8).x=-1;break;case 3:*reinterpret_cast<uint32_t *>(buildings[0]+0x60)=2;break;case 4:buildingFlags[0]|=128;break;
   case 5:++buildingFlags[0];break;
   case 6:*reinterpret_cast<uint8_t **>(replacementPool+8)=replacementFlags;buildingPoolPointer=replacementPool;break;
   case 7:*reinterpret_cast<uint8_t **>(replacementPool)=replacementSources[0];buildingPoolPointer=replacementPool;break;
   case 8:buildingPoolPointer=nullptr;break;}
  // Simulate native ownership of moved/basis replacements in its world list.
  if(invalidation==1||invalidation==2)sourceInWorld[0]=true;
  JetFrame(10200+invalidation*1000);Expect(destroyed>=beforeInvalid+1,"invalidated source releases its unused owned aircraft");Expect(sourceAdds[0]==0&&removed==beforeSourceRemoved+1,"invalidated source generation never receives stale world membership changes");
  restart();if(invalidation!=1&&invalidation!=2)Expect(sourceAdds[0]==0,"world cleanup never restores a foreign source");shutdown();
 }

 // Discovery retries missing native placements without rescanning every
 // frame, and a timer rewind makes the next retry immediate.
 Fresh();*reinterpret_cast<int16_t *>(buildings[6]+0x6a)=638;JetFrame(22000);
 Expect(sourceInWorld[6]&&sourceRemoves[6]==0,"unavailable original is not claimed");
 *reinterpret_cast<int16_t *>(buildings[6]+0x6a)=636;JetFrame(22100);Expect(sourceInWorld[6],"missing-source discovery is bounded between retries");
 JetFrame(22750);Expect(!sourceInWorld[6]&&sourceRemoves[6]==1,"missing native placement is discovered on retry");restart();shutdown();
 Fresh();*reinterpret_cast<int16_t *>(buildings[6]+0x6a)=638;JetFrame(23000);*reinterpret_cast<int16_t *>(buildings[6]+0x6a)=636;
 JetFrame(100);Expect(!sourceInWorld[6]&&sourceRemoves[6]==1,"timer rewind resets source discovery retry clock");restart();shutdown();

 // A foreign replacement in the model registry ends actor ownership and
 // blocks claiming any newly appearing source for that replaced model.
 Fresh();*reinterpret_cast<int16_t *>(buildings[6]+0x6a)=638;const int registryModel=Load(24000);uint8_t *registeredInfo=registryModel>=240&&registryModel<=249?models[registryModel]:nullptr;
 if(registeredInfo){models[registryModel]=dodoInfo;*reinterpret_cast<int16_t *>(buildings[6]+0x6a)=636;
  const int beforeForeignDestroyed=destroyed,beforeForeignRemoved=removed,beforeForeignAllocated=allocated;JetFrame(25000);
  Expect(!airportjets::Owns(jetVehicles[0])&&destroyed==beforeForeignDestroyed&&removed==beforeForeignRemoved&&allocated==beforeForeignAllocated,"foreign model registry replacement receives no actor cleanup or recreation");
  Expect(sourceInWorld[6]&&sourceRemoves[6]==0,"foreign model registry replacement blocks newly claimed source anchors");models[registryModel]=registeredInfo;DestroyJet(jetVehicles[0]);
 }restart();shutdown();

 // This hook runs after world destruction. Poison its source pool binding to
 // make an accidental attempt to validate or restore old scenery observable.
 Fresh();Load(21000);const int beforeShutdownAdds=added,beforeShutdownRemoves=removed;
 buildingPoolPointer=reinterpret_cast<uint8_t *>(uintptr_t(1));shutdown();
 Expect(added==beforeShutdownAdds&&removed==beforeShutdownRemoves,"model shutdown never accesses or restores stale world sources");buildingPoolPointer=buildingPool;
 std::printf("persistent airport spawner checks: %d failures\n",failures);return failures?1:0;
}
