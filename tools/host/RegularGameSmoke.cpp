// Automation observes the ordinary game loop; it does not create a separate
// scene or replace the world renderer. Unset VC_REGULAR_SMOKE has no effect.
#include "common.h"
#include "main.h"
#include "World.h"
#include "Pools.h"
#include "PlayerPed.h"
#include "Timer.h"
#include "Streaming.h"
#include "Plane.h"
#include "GameplayWing.h"
#include "DodoSmoke.h"
#include "DodoRig.h"
#include "AircraftBodySmoke.h"
#include "VehicleBodySmoke.h"
#include "NodeName.h"
#include <dlfcn.h>
#include <cmath>
#include "skeleton.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <chrono>
namespace {
std::vector<RwV3d> hullBefore;
RpAtomic *ReadHull(RpAtomic *atomic,void *data){auto frame=RpAtomicGetFrame(atomic);if(frame&&std::strcmp(GetFrameNodeName(frame),"dodo_hull")==0)*static_cast<RpAtomic**>(data)=atomic;return atomic;}
RpGeometry *Hull(CVehicle *vehicle){if(!vehicle||!vehicle->m_rwObject)return nil;RpAtomic *atomic=nil;RpClumpForAllAtomics(vehicle->GetClump(),ReadHull,&atomic);return atomic?RpAtomicGetGeometry(atomic):nil;}
}
namespace { void Capture(const char *path){
  GLint viewport[4];glGetIntegerv(GL_VIEWPORT,viewport);int w=viewport[2],h=viewport[3];std::vector<unsigned char> pixels(size_t(w)*h*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());FILE *out=std::fopen(path,"wb");if(out){std::fprintf(out,"P6\n%d %d\n255\n",w,h);for(int y=h-1;y>=0;--y)std::fwrite(pixels.data()+size_t(y)*w*3,1,size_t(w)*3,out);std::fclose(out);}
} }
void RegularGameSmokeCapture(const char *path){Capture(path);}
void RegularGameSmokeTick(){
 const char *capture=std::getenv("VC_REGULAR_SMOKE");if(!capture)return;static unsigned frames=0;static int controlledRef=-1;static bool intact=false,damaged=false,repaired=false,capturedRepair=false,externalChecked=false;static unsigned repairFrame=0,pristineFrame=0;static CVector initialCOM;static float initialTurnMass=0;static bool pristineChecked=false;++frames;
 const bool wing=std::getenv("VC_REGULAR_WING")!=nil;
 const bool dodo=wing&&(std::string(std::getenv("VC_REGULAR_WING"))=="dodo"||std::string(std::getenv("VC_REGULAR_WING"))=="body-ground"||std::string(std::getenv("VC_REGULAR_WING"))=="body-tail");
 const bool bodyBullets=wing&&(std::string(std::getenv("VC_REGULAR_WING"))=="body-bullets"||std::string(std::getenv("VC_REGULAR_WING"))=="body-tail");
 const bool vehicleBodies=wing&&std::string(std::getenv("VC_REGULAR_WING"))=="body-vehicles";
 const bool bump=wing&&std::string(std::getenv("VC_REGULAR_WING"))=="body-bump";
 const bool curb=bump||(wing&&std::string(std::getenv("VC_REGULAR_WING"))=="body-curb");
 const bool impact=wing&&std::string(std::getenv("VC_REGULAR_WING"))=="impact";
 const bool graze=wing&&std::string(std::getenv("VC_REGULAR_WING"))=="graze";
 auto stamp=std::chrono::steady_clock::now();static auto previous=stamp;if(frames>201)std::fprintf(stderr,"[regular-wing-smoke] frame_interval_ms=%.3f\n",std::chrono::duration<double,std::milli>(stamp-previous).count());previous=stamp;
 if(wing&&!curb&&frames==30){auto p=FindPlayerPed();if(p){CVector airport(-1530,-1130,14);CStreaming::LoadScene(airport);CStreaming::LoadSceneCollision(airport);p->Teleport(airport);std::fprintf(stderr,"[regular-wing-smoke] ordinary save moved to airport\n");}}
 if(wing&&frames==150){
  CEntity *entities[256];int16 count=0;CWorld::FindObjectsInRange(CVector(-1585,-1130,10),250,false,&count,256,entities,true,false,false,true,false);
  for(int e=0;e<count;++e){auto entity=entities[e];auto col=entity->GetColModel();if(!col||!entity->bUsesCollision)continue;if(entity->IsBuilding())for(int t=0;t<col->numTriangles;++t){auto tri=col->triangles[t];auto a=entity->GetMatrix()*col->vertices[tri.a].Get(),b=entity->GetMatrix()*col->vertices[tri.b].Get(),c=entity->GetMatrix()*col->vertices[tri.c].Get();auto n=CrossProduct(b-a,c-a);float area=n.Magnitude();if(area>100&&std::fabs(n.z)<area*.1f){auto center=(a+b+c)/3;std::fprintf(stderr,"[regular-wing-smoke] building model=%d triangle=%d area=%.2f centroid=%.2f %.2f %.2f points %.2f %.2f %.2f / %.2f %.2f %.2f / %.2f %.2f %.2f\n",entity->GetModelIndex(),t,area,center.x,center.y,center.z,a.x,a.y,a.z,b.x,b.y,b.z,c.x,c.y,c.z);break;}}for(int b=0;b<col->numBoxes;++b){auto box=col->boxes[b];if(box.max.z-box.min.z<9)continue;auto center=entity->GetMatrix()*((box.min+box.max)*.5f);std::fprintf(stderr,"[regular-wing-smoke] real box model=%d xyz=%.2f,%.2f,%.2f bounds=%.2f %.2f %.2f / %.2f %.2f %.2f\n",entity->GetModelIndex(),center.x,center.y,center.z,box.min.x,box.min.y,box.min.z,box.max.x,box.max.y,box.max.z);}}
  auto pool=CPools::GetVehiclePool();auto eligible=reinterpret_cast<bool(*)(const void*)>(dlsym(RTLD_DEFAULT,"VCSourceAircraftEligible"));for(int i=0;i<pool->GetSize();++i){auto v=pool->GetSlot(i);if(v&&v->GetModelIndex()==181&&eligible&&eligible(v)){std::fprintf(stderr,"[regular-wing-smoke] playable Dodo ref=%d pos=%.2f %.2f %.2f\n",CPools::GetVehicleRef(v),v->GetPosition().x,v->GetPosition().y,v->GetPosition().z);}}
 }
 if(wing&&!dodo&&!bodyBullets&&!vehicleBodies&&!curb&&frames==200){auto pool=CPools::GetVehiclePool();auto eligible=reinterpret_cast<bool(*)(const void*)>(dlsym(RTLD_DEFAULT,"VCSourceAircraftEligible"));for(int i=0;i<pool->GetSize();++i){auto v=pool->GetSlot(i);if(v&&v->GetModelIndex()==181&&eligible&&eligible(v)){
   CVector position(-1712,(std::string(std::getenv("VC_REGULAR_WING"))=="miss")?-1332.15f:-1335.15f,18.5f);if(impact)position=CVector(-1688.61f,-1329,18.5f);CStreaming::LoadScene(position);CStreaming::LoadSceneCollision(position);auto player=FindPlayerPed();CWorld::Remove(v);v->GetMatrix().SetUnity();v->GetRight()=impact?CVector(-1,0,0):CVector(0,-1,0);v->GetForward()=impact?CVector(0,-1,0):CVector(1,0,0);v->SetPosition(position);v->m_vecMoveSpeed=impact?CVector(0,-.24f,0):CVector(.8f,0,0);v->m_vecTurnSpeed=CVector(0,0,0);v->m_vecMoveFriction=v->m_vecTurnFriction=CVector(0,0,0);v->SetStatus(STATUS_PLAYER);v->bIsBIGBuilding=false;v->bUsesCollision=true;v->bEngineOn=true;v->GetMatrix().UpdateRW();v->UpdateRwFrame();CWorld::Add(v);player->m_objective=OBJECTIVE_ENTER_CAR_AS_DRIVER;player->WarpPedIntoCar(v);controlledRef=CPools::GetVehicleRef(v);initialCOM=v->m_vecCentreOfMass;initialTurnMass=v->m_fTurnMass;v->m_vecMoveFriction=v->m_vecTurnFriction=CVector(0,0,0);if(impact){auto g=Hull(v);if(g)hullBefore.assign(g->morphTargets[0].vertices,g->morphTargets[0].vertices+g->numVertices);}std::fprintf(stderr,"[regular-wing-smoke] approach real hangar model=928 in ordinary Dodo ref=%d\n",CPools::GetVehicleRef(v));break;}}}
 if(impact&&frames==360){auto vehicle=CPools::GetVehicle(controlledRef);auto g=Hull(vehicle);float maximum=0;if(g&&g->numVertices==int(hullBefore.size()))for(int i=0;i<g->numVertices;++i){auto a=g->morphTargets[0].vertices[i],b=hullBefore[i];maximum=Max(maximum,CVector(a.x-b.x,a.y-b.y,a.z-b.z).Magnitude());}
  bool alive=vehicle&&vehicle->m_fHealth>0&&FindPlayerPed()->m_fHealth>0;std::fprintf(stderr,"[regular-wing-smoke] body impact deformation=%s survival=%s max_vertex_m=%.6f hull_health=%.2f pilot_health=%.2f\n",maximum>.002f?"PASS":"FAIL",alive?"PASS":"FAIL",maximum,vehicle?vehicle->m_fHealth:0,FindPlayerPed()->m_fHealth);Capture((std::string(capture)+".impact.ppm").c_str());}
 if(wing){WpState state{};unsigned blocked=0,failed=0;if(GameplayWingSnapshot(&state,&blocked,&failed)){std::fprintf(stderr,"[regular-wing-smoke] accepted-flight t=%.6f plastic=%.12g mounts=%d contacts=%d\n",state.time,state.plastic,state.brokenMounts,state.contacts);if(!intact){Capture((std::string(capture)+".intact.ppm").c_str());intact=true;}if((state.fragments>1||(graze&&state.plastic>0))&&!damaged){Capture((std::string(capture)+".damaged.ppm").c_str());damaged=true;if(graze)std::fprintf(stderr,"[regular-wing-smoke] permanent wing deformation=PASS\n");std::fprintf(stderr,"[regular-wing-smoke] visible fracture mass=%.6f integrity=%.6f native_blocked=%u failed=%u\n",state.mass,VCSourceWingIntegrity(FindPlayerVehicle()),blocked,failed);}}}
 if(wing&&frames>=201){auto vehicle=FindPlayerVehicle();if(bodyBullets){auto pool=CPools::GetVehiclePool();for(int i=0;i<pool->GetSize();++i){auto candidate=pool->GetSlot(i);if(candidate&&candidate->GetModelIndex()==181&&candidate->IsPlane()&&DodoRigReady((CPlane*)candidate)){vehicle=candidate;break;}}}if(vehicle&&vehicle->GetModelIndex()==181){
  static float lastZ=vehicle->GetPosition().z;CVector start=vehicle->GetPosition(),floor;start.z=Max(lastZ,start.z)+.5f;lastZ=vehicle->GetPosition().z;
  bool coverage=GameplayWingGroundCoverage(start,&floor);float gap=coverage?vehicle->GetPosition().z-floor.z:0;
  if(frames%30==0||!coverage||gap<-.3f)std::fprintf(stderr,"[regular-wing-smoke] terrain coverage=%s penetration=%s z=%.3f floor=%.3f\n",coverage?"PASS":"FAIL",coverage&&gap>=-.3f?"PASS":"FAIL",vehicle->GetPosition().z,coverage?floor.z:0);
 }}
 if(wing&&!externalChecked&&std::string(std::getenv("VC_REGULAR_WING"))=="pristine"){WpState state{};if(GameplayWingSnapshot(&state,nil,nil)&&state.plastic==0&&state.fragments==1){auto vehicle=CPools::GetVehicle(controlledRef);if(vehicle){auto sector=CWorld::GetSector(CWorld::GetSectorIndexX(vehicle->GetPosition().x),CWorld::GetSectorIndexY(vehicle->GetPosition().y));for(int list:{ENTITYLIST_VEHICLES,ENTITYLIST_VEHICLES_OVERLAP})for(auto node=sector->m_lists[list].first;node;node=node->next)if(node->item==vehicle&&!externalChecked){auto velocity=vehicle->m_vecMoveSpeed,position=vehicle->GetPosition();auto col=vehicle->GetColModel();vehicle->ApplyMoveForce(vehicle->m_fMass*.1f,0,0);bool valid=!GameplayWingOwns(vehicle)&&VCSourceWingGeometryOwned(vehicle)&&GameplayWingSkipNativeThisPass(vehicle)&&vehicle->GetColModel()==col&&std::fabs(vehicle->m_vecMoveSpeed.x-velocity.x-.1f)<1e-6f;vehicle->ApplyMoveSpeed();valid&=(vehicle->GetPosition()-position).MagnitudeSqr()==0;std::fprintf(stderr,"[regular-wing-smoke] pristine-callback result=%s\n",valid?"PASS":"FAIL");externalChecked=true;pristineFrame=frames;}}}}
 if(wing&&pristineFrame&&frames>pristineFrame&&!pristineChecked){auto vehicle=CPools::GetVehicle(controlledRef);bool valid=vehicle&&!VCSourceWingGeometryOwned(vehicle)&&std::fabs(vehicle->m_fMass-2400)<.01f&&std::fabs(vehicle->m_fTurnMass-initialTurnMass)<.001f&&(vehicle->m_vecCentreOfMass-initialCOM).Magnitude()<1e-6f;std::fprintf(stderr,"[regular-wing-smoke] pristine-properties result=%s\n",valid?"PASS":"FAIL");pristineChecked=true;}
 if(wing&&damaged&&!externalChecked&&std::string(std::getenv("VC_REGULAR_WING"))=="external"){auto vehicle=CPools::GetVehicle(controlledRef);if(vehicle&&GameplayWingOwns(vehicle)){auto velocity=vehicle->m_vecMoveSpeed,position=vehicle->GetPosition();float mass=vehicle->m_fMass;vehicle->ApplyMoveForce(mass*.1f,0,0);bool once=std::fabs(vehicle->m_vecMoveSpeed.x-velocity.x-.1f)<1e-6f&&!GameplayWingOwns(vehicle)&&GameplayWingSkipNativeThisPass(vehicle);vehicle->ApplyMoveSpeed();once&=(vehicle->GetPosition()-position).MagnitudeSqr()==0;std::fprintf(stderr,"[regular-wing-smoke] external-force handoff result=%s\n",once?"PASS":"FAIL");externalChecked=true;}}
 if(wing&&damaged&&!repaired&&!GameplayWingOwns(CPools::GetVehicle(controlledRef))){auto vehicle=CPools::GetVehicle(controlledRef);auto repair=reinterpret_cast<void(*)(void*)>(dlsym(RTLD_DEFAULT,"VCSourceAircraftRepair"));if(vehicle&&repair){int reference=CPools::GetVehicleRef(vehicle);repair(vehicle);repaired=true;repairFrame=frames;std::fprintf(stderr,"[regular-wing-smoke] repair queued generation=%d\n",reference);}}
 if(wing&&repaired&&!capturedRepair&&frames>repairFrame+1){auto vehicle=CPools::GetVehicle(controlledRef);bool restored=vehicle&&!GameplayWingOwns(vehicle)&&VCSourceWingIntegrity(vehicle)==1&&vehicle->GetColModel()==CModelInfo::GetModelInfo(vehicle->GetModelIndex())->GetColModel()&&vehicle->m_rwObject&&std::fabs(vehicle->m_fMass-2400)<.01f;bool rig=restored&&DodoRigReady((CPlane*)vehicle);std::fprintf(stderr,"[regular-wing-smoke] retired-aircraft repair result=%s generation=%d\n",restored?"PASS":"FAIL",controlledRef);std::fprintf(stderr,"[regular-wing-smoke] repaired-rig result=%s\n",rig?"PASS":"FAIL");Capture((std::string(capture)+".repaired.ppm").c_str());capturedRepair=true;}
 if(wing&&frames%60==0){WpState s{};unsigned blocked=0,failed=0;bool present=GameplayWingSnapshot(&s,&blocked,&failed);std::fprintf(stderr,"[regular-wing-smoke] frame=%u island=%d t=%.3f fragments=%d contacts=%d native_blocked=%u failed=%u\n",frames,present,s.time,s.fragments,s.contacts,blocked,failed);}
 if(frames==(wing?600:300)){auto player=FindPlayerPed();std::printf("regular-game-smoke frame=%u player=%p world=normal position=%.3f %.3f %.3f\n",frames,(void*)player,player?player->GetPosition().x:0,player?player->GetPosition().y:0,player?player->GetPosition().z:0);
  Capture(capture);std::fflush(stdout);
 }
 if(dodo)DodoSmokeTick(frames,capture);
 if(bodyBullets)AircraftBodySmokeTick(frames,capture);
 if(vehicleBodies)VehicleBodySmokeTick(frames,capture);
 if(bump)VehicleBumpSmokeTick(frames,capture);else if(curb)VehicleCurbSmokeTick(frames,capture);
 if(!dodo&&!bodyBullets&&!vehicleBodies&&!curb&&frames>=(wing?660:360))RsGlobal.quit=TRUE;
}
