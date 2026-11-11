#include "GameplayWing.h"
#include "LiveWingGeometry.h"
#include "DodoRig.h"
#include "AircraftBody.h"
#include "WingPhysics.h"
#include "Plane.h"
#include "Object.h"
#include "PlayerPed.h"
#include "Pools.h"
#include "World.h"
#include "Timer.h"
#include "CutsceneMgr.h"
#include "Replay.h"
#include "main.h"
#include <mach-o/dyld.h>
#include <memory>
#include <string>
#include <array>
#include <map>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <dlfcn.h>
#include <limits.h>
#include <chrono>
namespace {
const double kHz=50;
CVector Vec(const double *v){return CVector(v[0],v[1],v[2]);}
void Store(double *v,CVector p){v[0]=p.x;v[1]=p.y;v[2]=p.z;}
std::string MeshPath(){char executable[PATH_MAX];uint32_t size=sizeof(executable);if(_NSGetExecutablePath(executable,&size))return {};auto slash=std::strrchr(executable,'/');if(!slash)return {};*slash=0;return std::string(executable)+"/../Resources/PhysicsData/aircraft.txt";}
struct Island {
 CPlane *plane=nil;int reference=-1;uint64_t session=0;CVector anchor;WpState state{};WpStaticMesh environment{};std::unique_ptr<LiveWingGeometry> geometry;
 float originalMass=0,originalTurnMass=0;CVector originalCOM;
 bool failed=false,capturing=false;double frameDt=0;WpLoad load{};unsigned published=0;int peakContacts=0;double totalMs=0,maximumMs=0;
 ~Island(){geometry.reset();if(session)wp_destroy(session);}
 bool Current()const{return plane&&CPools::GetVehicle(reference)==plane&&plane->GetModelIndex()==181&&geometry&&geometry->Current();}
};
std::ofstream replay;
void Trace(char tag,const void *data,size_t size){if(replay){replay.write(&tag,1);replay.write((const char*)data,size);replay.flush();}}
struct AdvancedActor{const CEntity *entity;int reference;bool vehicle;};std::vector<AdvancedActor> advanced;
struct DeferredObject{CObject *object;int reference;};std::vector<DeferredObject> deferred;
void FlushDeferred(){auto objects=std::move(deferred);deferred.clear();for(auto item:objects)if(CPools::GetObject(item.reference)==item.object){CWorld::Remove(item.object);delete item.object;}}
struct Restoration{std::unique_ptr<LiveWingGeometry> geometry;CPlane *plane;int reference;float mass,turnMass;CVector com;};std::vector<Restoration> restorations;
struct Rebuild{CVehicle *plane;int reference;float mass,turnMass;CVector com;};std::vector<Rebuild> rebuilds;
void FlushRestorations(){auto pending=std::move(restorations);restorations.clear();for(auto &item:pending)if(CPools::GetVehicle(item.reference)==item.plane&&item.geometry->Current()){item.plane->m_fMass=item.mass;item.plane->m_fTurnMass=item.turnMass;item.plane->m_vecCentreOfMass=item.com;}pending.clear();
 auto models=std::move(rebuilds);rebuilds.clear();for(auto item:models)if(CPools::GetVehicle(item.reference)==item.plane&&item.plane->GetModelIndex()==181){auto plane=item.plane;CWorld::Remove(plane);int model=plane->GetModelIndex();plane->DeleteRwObject();plane->ClearInstanceCollision();plane->SetModelIndex(model);plane->m_fMass=item.mass;plane->m_fTurnMass=item.turnMass;plane->m_vecCentreOfMass=item.com;plane->GetMatrix().UpdateRW();plane->UpdateRwFrame();CWorld::Add(plane);}}
std::unique_ptr<Island> active;struct Retirement{double integrity;float mass,turnMass;CVector com;};std::map<int,Retirement> retainedIntegrity;unsigned blockedNative=0;bool closing=false;uint32_t retryAt=0;int retryRef=-1;
// Get the existing static COL through the same entity resolver as native world
// queries. No synthetic wall or fitted obstacle is introduced.
bool Neighborhood(const WpState &state,CVector anchor,WpStaticMesh &mesh,std::string &error,const CEntity *required=nil){
 mesh={};
 CVector lo(1e10f,1e10f,1e10f),hi(-1e10f,-1e10f,-1e10f);
 for(int i=0;i<state.nodes;++i){auto p=Vec(state.node[i].x)+anchor;auto end=p+Vec(state.node[i].v)*.12f;for(auto q:{p,end}){lo.x=Min(lo.x,q.x-2);lo.y=Min(lo.y,q.y-2);lo.z=Min(lo.z,q.z-2);hi.x=Max(hi.x,q.x+2);hi.y=Max(hi.y,q.y+2);hi.z=Max(hi.z,q.z+2);}}
 // FindObjectsInRange tests entity origins, not their COL bounds. A runway
 // can extend hundreds of metres from its origin. Walk the same sectors and
 // overlap lists as native contacts/LOS/vertical queries, then cull geometry.
 std::vector<CEntity*> entities;
 const int lists[]={ENTITYLIST_BUILDINGS,ENTITYLIST_BUILDINGS_OVERLAP,ENTITYLIST_OBJECTS,ENTITYLIST_OBJECTS_OVERLAP,ENTITYLIST_DUMMIES,ENTITYLIST_DUMMIES_OVERLAP};
 for(int y=Clamp(CWorld::GetSectorIndexY(lo.y),0,NUMSECTORS_Y-1);y<=Clamp(CWorld::GetSectorIndexY(hi.y),0,NUMSECTORS_Y-1);++y)
  for(int x=Clamp(CWorld::GetSectorIndexX(lo.x),0,NUMSECTORS_X-1);x<=Clamp(CWorld::GetSectorIndexX(hi.x),0,NUMSECTORS_X-1);++x)
   for(int list:lists)for(auto node=CWorld::GetSector(x,y)->m_lists[list].first;node;node=node->next){auto entity=(CEntity*)node->item;
    if(std::find(entities.begin(),entities.end(),entity)!=entities.end())continue;
    if(entities.size()==512){error="static sector neighborhood exceeds island capacity";return false;}entities.push_back(entity);
   }
 auto overlaps=[&](CVector a,CVector b){return a.x<=hi.x&&a.y<=hi.y&&a.z<=hi.z&&b.x>=lo.x&&b.y>=lo.y&&b.z>=lo.z;};
 auto triangle=[&](CVector p,CVector q,CVector r){
  CVector a(Min(p.x,Min(q.x,r.x)),Min(p.y,Min(q.y,r.y)),Min(p.z,Min(q.z,r.z))),b(Max(p.x,Max(q.x,r.x)),Max(p.y,Max(q.y,r.y)),Max(p.z,Max(q.z,r.z)));
  if(!overlaps(a,b)||CrossProduct(q-p,r-p).MagnitudeSqr()<1e-12f)return true;
  if(mesh.faces==WP_MAX_WORLD_FACES){error="static triangle neighborhood exceeds island capacity";return false;}
  CVector points[3]={p,q,r};int ids[3];for(int j=0;j<3;++j){auto local=points[j]-anchor;int n=0;for(;n<mesh.vertices;++n)if((Vec(mesh.point[n])-local).MagnitudeSqr()<1e-12f)break;if(n==mesh.vertices){if(n==WP_MAX_WORLD_POINTS){error="static vertex neighborhood exceeds island capacity";return false;}Store(mesh.point[n],local);++mesh.vertices;}ids[j]=n;}
  auto key=std::array<int,3>{{ids[0],ids[1],ids[2]}};std::sort(key.begin(),key.end());for(int f=0;f<mesh.faces;++f){auto old=std::array<int,3>{{mesh.face[f][0],mesh.face[f][1],mesh.face[f][2]}};std::sort(old.begin(),old.end());if(old==key)return true;}
  for(int j=0;j<3;++j)mesh.face[mesh.faces][j]=ids[j];++mesh.faces;return true;
 };
 const int boxFaces[12][3]={{0,2,1},{1,2,3},{4,5,6},{5,7,6},{0,1,4},{1,5,4},{2,6,3},{3,6,7},{0,4,2},{2,4,6},{1,3,5},{3,7,5}};
 for(auto entity:entities){if(!entity||(required&&entity!=required)||!entity->bUsesCollision||GameplayWingOwns(entity))continue;auto col=entity->GetColModel();if(!col)continue;
  CVector minimum(1e10f,1e10f,1e10f),maximum(-1e10f,-1e10f,-1e10f);for(int i=0;i<8;++i){auto p=entity->GetMatrix()*CVector(i&1?col->boundingBox.max.x:col->boundingBox.min.x,i&2?col->boundingBox.max.y:col->boundingBox.min.y,i&4?col->boundingBox.max.z:col->boundingBox.min.z);minimum.x=Min(minimum.x,p.x);minimum.y=Min(minimum.y,p.y);minimum.z=Min(minimum.z,p.z);maximum.x=Max(maximum.x,p.x);maximum.y=Max(maximum.y,p.y);maximum.z=Max(maximum.z,p.z);}if(!overlaps(minimum,maximum))continue;
  if(entity->IsObject()&&!entity->GetIsStatic()){error="dynamic world object entered the static island neighborhood";return false;}
  for(int i=0;i<col->numSpheres;++i){auto sphere=col->spheres[i];auto p=entity->GetMatrix()*sphere.center,extent=CVector(sphere.radius,sphere.radius,sphere.radius);if(overlaps(p-extent,p+extent)){error="static curved primitive requires an exact contact adapter";return false;}}
  for(int i=0;i<col->numBoxes;++i){auto box=col->boxes[i];CVector corners[8];for(int k=0;k<8;++k)corners[k]=entity->GetMatrix()*CVector(k&1?box.max.x:box.min.x,k&2?box.max.y:box.min.y,k&4?box.max.z:box.min.z);
   for(int n=0;n<state.nodes;++n){auto local=Invert(entity->GetMatrix())*(Vec(state.node[n].x)+anchor);if(local.x>box.min.x+.0001f&&local.x<box.max.x-.0001f&&local.y>box.min.y+.0001f&&local.y<box.max.y-.0001f&&local.z>box.min.z+.0001f&&local.z<box.max.z-.0001f){error="initial state intersects a native solid box";return false;}}
   for(auto &f:boxFaces)if(!triangle(corners[f[0]],corners[f[1]],corners[f[2]]))return false;
  }
  for(int t=0;t<col->numTriangles;++t){auto tri=col->triangles[t];if(!triangle(entity->GetMatrix()*col->vertices[tri.a].Get(),entity->GetMatrix()*col->vertices[tri.b].Get(),entity->GetMatrix()*col->vertices[tri.c].Get()))return false;}
 }
 if(!mesh.faces)mesh.vertices=0;return true;
}
// This slice owns one outer wing, with a chassis motion proxy. Other native
// aircraft surfaces remain outside its contact contract. A conservative swept
// body envelope hands off before those surfaces could reach a static feature.
// The bounded flight envelope uses 500m/s² translation and 20rad/s² rotation;
// it is an admission guard, not an additional contact solver.
bool BodyClear(CPlane *plane,const WpStaticMesh &world,CVector anchor,double dt,const WpState *accepted=nil){
 CMatrix pose=plane->GetMatrix();CVector velocity=plane->m_vecMoveSpeed*50;if(accepted){pose.GetPosition()=Vec(accepted->bodyCenter)+anchor;pose.GetRight()=CVector(accepted->bodyRotation[0],accepted->bodyRotation[3],accepted->bodyRotation[6]);pose.GetForward()=CVector(accepted->bodyRotation[1],accepted->bodyRotation[4],accepted->bodyRotation[7]);pose.GetUp()=CVector(accepted->bodyRotation[2],accepted->bodyRotation[5],accepted->bodyRotation[8]);velocity=CVector(0,0,0);for(int i=0;i<8;++i)velocity+=Vec(accepted->node[i].v)/8;}
 auto box=plane->GetColModel()->boundingBox;box.max.x=Min(box.max.x,2.625f);CVector lo(1e10f,1e10f,1e10f),hi(-1e10f,-1e10f,-1e10f);float radius=0;
 for(int i=0;i<8;++i){CVector local(i&1?box.max.x:box.min.x,i&2?box.max.y:box.min.y,i&4?box.max.z:box.min.z);radius=Max(radius,local.Magnitude());auto p=pose*local-anchor;auto end=p+velocity*float(dt);for(auto point:{p,end}){lo.x=Min(lo.x,point.x);lo.y=Min(lo.y,point.y);lo.z=Min(lo.z,point.z);hi.x=Max(hi.x,point.x);hi.y=Max(hi.y,point.y);hi.z=Max(hi.z,point.z);}}
 float pad=.05f+plane->m_vecTurnSpeed.Magnitude()*50*radius*dt+.5f*(500+20*radius)*dt*dt;lo-=CVector(pad,pad,pad);hi+=CVector(pad,pad,pad);
 for(int f=0;f<world.faces;++f){auto a=Vec(world.point[world.face[f][0]]),b=Vec(world.point[world.face[f][1]]),c=Vec(world.point[world.face[f][2]]);CVector lower(Min(a.x,Min(b.x,c.x)),Min(a.y,Min(b.y,c.y)),Min(a.z,Min(b.z,c.z))),upper(Max(a.x,Max(b.x,c.x)),Max(a.y,Max(b.y,c.y)),Max(a.z,Max(b.z,c.z)));if(lower.x<=hi.x&&lower.y<=hi.y&&lower.z<=hi.z&&upper.x>=lo.x&&upper.y>=lo.y&&upper.z>=lo.z){if(std::getenv("VC_REGULAR_WING")){static int reports=0;if(reports++<6)std::fprintf(stderr,"[gameplay-wing] body admission face=%d lo=%.3f %.3f %.3f hi=%.3f %.3f %.3f triangle=%.3f %.3f %.3f / %.3f %.3f %.3f\n",f,lo.x,lo.y,lo.z,hi.x,hi.y,hi.z,lower.x,lower.y,lower.z,upper.x,upper.y,upper.z);}return false;}}return true;
}
// Only admit a possible outer-wing contact during the coming native interval.
// Empty flight must not pay for an implicit material solve at every substep.
bool WingNear(const WpState &state,const WpStaticMesh &world,double dt){
 CVector lo(1e10f,1e10f,1e10f),hi(-1e10f,-1e10f,-1e10f);
 const float pad=.08f+250.f*dt*dt;
 for(int n=8;n<state.nodes;++n)for(auto p:{Vec(state.node[n].x),Vec(state.node[n].x)+Vec(state.node[n].v)*float(dt)}){
  lo.x=Min(lo.x,p.x-pad);lo.y=Min(lo.y,p.y-pad);lo.z=Min(lo.z,p.z-pad);hi.x=Max(hi.x,p.x+pad);hi.y=Max(hi.y,p.y+pad);hi.z=Max(hi.z,p.z+pad);
 }
 for(int f=0;f<world.faces;++f){auto a=Vec(world.point[world.face[f][0]]),b=Vec(world.point[world.face[f][1]]),c=Vec(world.point[world.face[f][2]]);
  if(Min(a.x,Min(b.x,c.x))<=hi.x&&Min(a.y,Min(b.y,c.y))<=hi.y&&Min(a.z,Min(b.z,c.z))<=hi.z&&Max(a.x,Max(b.x,c.x))>=lo.x&&Max(a.y,Max(b.y,c.y))>=lo.y&&Max(a.z,Max(b.z,c.z))>=lo.z)return true;
 }return false;
}
void Fail(Island &island,const char *reason){if(!island.failed)std::fprintf(stderr,"[gameplay-wing] paused ref=%d revision=%llu t=%.6f reason=%s\n",island.reference,(unsigned long long)island.state.revision,island.state.time,reason);island.failed=true;}
void Retire(bool repair=false){
 if(!active)return;closing=true;
 const bool pristine=active->Current()&&active->state.fragments==1&&active->state.plastic==0&&active->state.brokenMounts==0;
 for(auto entity:active->geometry->DetachDebris()){auto object=(CObject*)entity;deferred.push_back({object,CPools::GetObjectRef(object)});}
 if(repair||pristine){retainedIntegrity.erase(active->reference);restorations.push_back({std::move(active->geometry),active->plane,active->reference,active->originalMass,active->originalTurnMass,active->originalCOM});}
 else{retainedIntegrity[active->reference]={VCSourceWingIntegrity(active->plane),active->originalMass,active->originalTurnMass,active->originalCOM};active->geometry->Abandon(active->plane);}
 std::fprintf(stderr,"[gameplay-wing] retire ref=%d snapshots=%u step_max_ms=%.3f solver_total_ms=%.3f simulated_s=%.6f retired_fragments=%d\n",active->reference,active->published,active->maximumMs,active->totalMs,active->state.time,active->state.fragments-1);
 active.reset();closing=false;
}
}
bool GameplayWingOwns(const CEntity *e){return active&&active->geometry&&active->geometry->Owns(e);}
bool GameplayWingGroundCoverage(CVector point,CVector *ground){
 CColPoint hit;CEntity *entity=nil;
 if(!CWorld::ProcessVerticalLine(point,point.z-500,hit,entity,true,false,false,false,false,false,nil))return false;
 if(ground)*ground=hit.point;
 WpState probe{};probe.nodes=1;WpStaticMesh world{};std::string error;
 // This observer verifies the queried floor entity. Nearby dynamic debris
 // remains a separate native contact; it is not an implicit-island admission.
 if(!Neighborhood(probe,hit.point,world,error,entity))return false;
 for(int f=0;f<world.faces;++f){auto a=Vec(world.point[world.face[f][0]]),b=Vec(world.point[world.face[f][1]]),c=Vec(world.point[world.face[f][2]]);
  auto u=b-a,v=c-a,w=-a;double uu=DotProduct(u,u),uv=DotProduct(u,v),vv=DotProduct(v,v),wu=DotProduct(w,u),wv=DotProduct(w,v),det=uu*vv-uv*uv;if(det<1e-12)continue;
  double s=(vv*wu-uv*wv)/det,t=(uu*wv-uv*wu)/det;auto closest=a+u*s+v*t;
  if(s>=-1e-5&&t>=-1e-5&&s+t<=1.00001&&closest.Magnitude()<.001f)return true;
 }return false;
}
bool GameplayWingSkipNativeThisPass(const CEntity *e){if(GameplayWingOwns(e))return true;for(auto &a:advanced)if(a.entity==e){if(a.vehicle&&const_cast<CEntity*>(e)->IsVehicle()&&CPools::GetVehicle(a.reference)==e)return true;if(!a.vehicle&&const_cast<CEntity*>(e)->IsObject()&&CPools::GetObject(a.reference)==e)return true;}return false;}
void GameplayWingReleaseForExternal(const CEntity *e){if(GameplayWingOwns(e)){std::fprintf(stderr,"[gameplay-wing] external interaction returns motion ownership to native game\n");Retire();}}
extern "C" bool VCSourceWingOwns(const void *e){return GameplayWingOwns((const CEntity*)e);}
extern "C" bool VCSourceWingGeometryOwned(const void *entity){
 auto e=(const CEntity*)entity;if(!e)return false;if(GameplayWingOwns(e))return true;
 for(auto &item:restorations)if(item.plane==e&&CPools::GetVehicle(item.reference)==e)return true;
 for(auto &item:rebuilds)if(item.plane==e&&CPools::GetVehicle(item.reference)==e)return true;
 if(!const_cast<CEntity*>(e)->IsVehicle())return false;return retainedIntegrity.count(CPools::GetVehicleRef((CVehicle*)e))!=0;
}
extern "C" double VCSourceWingIntegrity(const void *v){if(!active||active->plane!=v){auto vehicle=(CVehicle*)v;if(!vehicle)return 1;auto it=retainedIntegrity.find(CPools::GetVehicleRef(vehicle));return it==retainedIntegrity.end()?1:it->second.integrity;}int total=0,attached=0;for(int i=0;i<active->state.faces;++i)if(active->state.face[i].wing){++total;attached+=active->state.face[i].fragment==0;}return total?double(attached)/total:0;}
bool GameplayWingCaptureMove(CPhysical *p,CVector impulse){
 if(!GameplayWingOwns(p))return false;
 if(active->capturing&&p==active->plane){auto force=impulse*float(kHz/active->frameDt);for(int k=0;k<3;++k)active->load.force[k]+=(&force.x)[k];}
 else{GameplayWingReleaseForExternal(p);return false;}
 return true;
}
bool GameplayWingCaptureTurn(CPhysical *p,CVector impulse,CVector lever){
 if(!GameplayWingOwns(p))return false;
 if(active->capturing&&p==active->plane){auto com=Multiply3x3(p->GetMatrix(),p->m_vecCentreOfMass);auto torque=CrossProduct(lever-com,impulse)*float(kHz/active->frameDt);for(int k=0;k<3;++k)active->load.torque[k]+=(&torque.x)[k];}
 else{GameplayWingReleaseForExternal(p);return false;}return true;
}
void GameplayWingDeleting(CEntity *entity){AircraftBodyDeleting(entity);DodoRigDeleting(entity);for(auto &item:restorations)item.geometry->Abandon(entity);rebuilds.erase(std::remove_if(rebuilds.begin(),rebuilds.end(),[&](const Rebuild &item){return item.plane==entity;}),rebuilds.end());deferred.erase(std::remove_if(deferred.begin(),deferred.end(),[&](const DeferredObject &item){return item.object==entity;}),deferred.end());int ref=entity&&entity->IsVehicle()?CPools::GetVehicleRef((CVehicle*)entity):-1;if(ref>=0)retainedIntegrity.erase(ref);if(closing||!active||!active->geometry||!active->geometry->Owns(entity))return;active->geometry->Abandon(entity);Retire();if(ref>=0)retainedIntegrity.erase(ref);}
void GameplayWingShutdown(){Retire();FlushRestorations();FlushDeferred();advanced.clear();retainedIntegrity.clear();retryAt=0;retryRef=-1;}
extern "C" void VCSourceRepairWing(void *vehicle){if(!vehicle)return;auto plane=(CVehicle*)vehicle;const int ref=CPools::GetVehicleRef(plane);if(active&&active->plane==vehicle){Retire(true);return;}auto saved=retainedIntegrity.find(ref);if(saved==retainedIntegrity.end())return;auto original=saved->second;retainedIntegrity.erase(saved);
 // Ownership may already have returned to the native game. Recreate this
 // instance from untouched model data; no original actor pointer is retained.
 rebuilds.push_back({plane,ref,original.mass,original.turnMass,original.com});
}
bool GameplayWingRejectNative(const CEntity *entity,const char *method){if(!GameplayWingSkipNativeThisPass(entity))return false;if(!GameplayWingOwns(entity))return true;++blockedNative;Fail(*active,method);return true;}
bool GameplayWingSnapshot(WpState *state,unsigned *blocked,unsigned *failed){if(blocked)*blocked=blockedNative;if(failed)*failed=active&&active->failed;if(!active||!state)return false;*state=active->state;return true;}
namespace {
bool RestPoints(std::vector<CVector> &points){
 static std::vector<CVector> cached;static std::string file;
 if(file.empty()){file=MeshPath();std::ifstream in(file);int count=0;if(!(in>>count)||count<1||count>10000)return false;for(int i=0;i<count;++i){int wing;double p[3][3];if(!(in>>wing))return false;for(auto &v:p)for(double &q:v)if(!(in>>q)||!std::isfinite(q))return false;if(wing)for(auto &v:p){CVector point(v[0],v[1],v[2]);if(std::none_of(cached.begin(),cached.end(),[&](CVector old){return (old-point).MagnitudeSqr()<1e-10f;}))cached.push_back(point);}}}
 points=cached;return !points.empty()&&points.size()+8<=WP_MAX_NODES;
}
WpInitial Initial(CPlane *plane){
 WpInitial result{};Store(result.velocity,plane->m_vecMoveSpeed*float(kHz));Store(result.spin,plane->m_vecTurnSpeed*float(kHz));result.bodyMass=plane->m_fMass*.88;result.wingMass=plane->m_fMass*.12;result.gravity[2]=-GRAVITY*kHz*kHz;
 const CVector axes[3]={plane->GetRight(),plane->GetForward(),plane->GetUp()};for(int i=0;i<3;++i)for(int j=0;j<3;++j)result.rotation[3*i+j]=(&axes[j].x)[i];
 auto box=plane->GetColModel()->boundingBox;CVector center=(box.min+box.max)*.5f,half=(box.max-box.min)*.5f;center.x=0;half.x=Min(2.f,half.x);half.x=Max(.4f,half.x);half.y=Max(.6f,half.y);half.z=Max(.2f,half.z);Store(result.chassisCenter,center);Store(result.chassisHalf,half);return result;
}
WpState AdmissionPoints(CPlane *plane,const WpInitial &initial,const std::vector<CVector> &wing,CVector anchor){
 WpState state{};auto add=[&](CVector point){auto &n=state.node[state.nodes++];auto arm=Multiply3x3(plane->GetMatrix(),point);Store(n.x,plane->GetPosition()+arm-anchor);Store(n.v,plane->m_vecMoveSpeed*float(kHz)+CrossProduct(plane->m_vecTurnSpeed*float(kHz),arm));};
 auto half=Vec(initial.chassisHalf),center=Vec(initial.chassisCenter);for(int i=0;i<8;++i)add(center+CVector(i&1?half.x:-half.x,i&2?half.y:-half.y,i&4?half.z:-half.z));for(auto p:wing)add(p);return state;
}
void PublishVelocities(Island &island,const WpState &before,double dt){
 auto participants=island.geometry->Participants();for(auto entity:participants){if(!entity->IsVehicle()&&!entity->IsObject())continue;auto physical=(CPhysical*)entity;int owner=island.geometry->Fragment(entity);
  CVector velocity(0,0,0);double mass=0;for(int n=0;n<island.state.nodes;++n)if(island.state.node[n].fragment==owner){mass+=island.state.node[n].mass;velocity+=Vec(island.state.node[n].v)*island.state.node[n].mass;}if(mass>0){physical->m_vecMoveSpeed=velocity/float(mass*kHz);physical->m_fMass=mass;CVector center(0,0,0);for(int n=0;n<island.state.nodes;++n)if(island.state.node[n].fragment==owner)center+=(Vec(island.state.node[n].x)+island.anchor)*island.state.node[n].mass;center/=float(mass);physical->m_vecCentreOfMass=Invert(physical->GetMatrix())*center;}
  physical->bIsInSafePosition=true;physical->bWasPostponed=false;physical->m_vecMoveFriction=physical->m_vecTurnFriction=CVector(0,0,0);
 }
 if(dt>0){CVector rotation(0,0,0);for(int column=0;column<3;++column){CVector a(before.bodyRotation[column],before.bodyRotation[3+column],before.bodyRotation[6+column]),b(island.state.bodyRotation[column],island.state.bodyRotation[3+column],island.state.bodyRotation[6+column]);rotation+=CrossProduct(a,b);}island.plane->m_vecTurnSpeed=rotation/float(2*dt*kHz);}
}
}
void GameplayWingFrame(){
 auto frameStart=std::chrono::steady_clock::now();
 FlushRestorations();FlushDeferred();advanced.clear();
 // The dense implicit solve does not meet an interactive frame budget. Keep
 // it available in the ordinary world for measured experiments; the shipped
 // default retains native motion/collision plus localized airframe materials.
 const char *experiment=std::getenv("VC_EXPERIMENTAL_WING");
 if(!experiment||std::strcmp(experiment,"1")){if(active)Retire();return;}
 if(std::getenv("VC_REGULAR_WING")){static unsigned calls=0;if(++calls%60==0){auto v=FindPlayerVehicle();auto eligible=reinterpret_cast<bool(*)(const void*)>(dlsym(RTLD_DEFAULT,"VCSourceAircraftEligible"));std::fprintf(stderr,"[gameplay-wing] entry %u vehicle=%p model=%d plane=%d eligible=%d driver=%d dt=%.5f z=%.3f speed=%.3f\n",calls,(void*)v,v?v->GetModelIndex():-1,v?v->IsPlane():0,eligible&&v?eligible(v):0,v?v->pDriver==FindPlayerPed():0,CTimer::GetTimeStepInSeconds(),v?v->GetPosition().z:0,v?v->m_vecMoveSpeed.Magnitude()*50:0);}}
 if(closing)return;
 if(CWorld::bProcessCutsceneOnly||CReplay::IsPlayingBack())return;
 const double dt=CTimer::GetTimeStepInSeconds();if(!std::isfinite(dt)||dt<=0)return;
 if(active&&!active->Current()){Retire();return;}
 if(active){auto p=active->plane;bool changed=(p->GetPosition()-(Vec(active->state.bodyCenter)+active->anchor)).MagnitudeSqr()>1e-8f;const CVector axes[]={p->GetRight(),p->GetForward(),p->GetUp()};for(int j=0;j<3;++j){CVector expected(active->state.bodyRotation[j],active->state.bodyRotation[3+j],active->state.bodyRotation[6+j]);changed|=(axes[j]-expected).MagnitudeSqr()>1e-10f;}if(changed){std::fprintf(stderr,"[gameplay-wing] external pose handoff ref=%d\n",active->reference);Retire();return;}}
 if(!active){
  static auto eligible=reinterpret_cast<bool(*)(const void*)>(dlsym(RTLD_DEFAULT,"VCSourceAircraftEligible"));
  auto vehicle=FindPlayerVehicle();if(!eligible||!vehicle||vehicle->GetModelIndex()!=181||!vehicle->IsPlane()||!eligible(vehicle)||vehicle->pDriver!=FindPlayerPed())return;
  auto plane=(CPlane*)vehicle;if(!DodoRigReady(plane))return;float speed=plane->m_vecMoveSpeed.Magnitude()*kHz;if(!std::isfinite(speed)||speed<3||speed>120||plane->m_fMass<100||plane->m_fMass>100000)return;
  const int reference=CPools::GetVehicleRef(plane);if(retainedIntegrity.count(reference))return;const uint32_t now=CTimer::GetTimeInMilliseconds();if(reference==retryRef&&now<retryAt)return;
  CColPoint ground;CEntity *groundEntity=nil;auto at=plane->GetPosition();if(CWorld::ProcessVerticalLine(at,at.z-50,ground,groundEntity,true,false,false,false,false,false,nil)&&at.z-ground.point.z<3){if(std::getenv("VC_REGULAR_WING"))std::fprintf(stderr,"[gameplay-wing] ground admission z=%.3f ground=%.3f model=%d\n",at.z,ground.point.z,groundEntity?groundEntity->GetModelIndex():-1);return;}
  std::vector<CVector> rest;if(!RestPoints(rest)){std::fprintf(stderr,"[gameplay-wing] material asset unavailable\n");return;}
  DodoRigNeutralWing(plane);
  auto candidate=std::unique_ptr<Island>(new Island);candidate->plane=plane;candidate->reference=reference;candidate->originalMass=plane->m_fMass;candidate->originalTurnMass=plane->m_fTurnMass;candidate->originalCOM=plane->m_vecCentreOfMass;candidate->anchor=plane->GetPosition();auto initial=Initial(plane);auto points=AdmissionPoints(plane,initial,rest,candidate->anchor);std::string error;
  if(!Neighborhood(points,candidate->anchor,candidate->environment,error)||!candidate->environment.faces){if(!error.empty())std::fprintf(stderr,"[gameplay-wing] admission ref=%d rejected: %s\n",reference,error.c_str());return;}
  if(!BodyClear(plane,candidate->environment,candidate->anchor,dt)||!WingNear(points,candidate->environment,dt))return;
  retryRef=reference;retryAt=now+1000;
  if(const char *path=std::getenv("VC_REGULAR_REPLAY")){replay.close();replay.open(path,std::ios::binary|std::ios::trunc);Trace('I',&initial,sizeof initial);Trace('W',&candidate->environment,sizeof candidate->environment);}
  char message[512]{};if(wp_create_world(MeshPath().c_str(),&initial,&candidate->environment,&candidate->session,message,sizeof message)){std::fprintf(stderr,"[gameplay-wing] admission ref=%d rejected: %s\n",reference,message);return;}
  if(!wp_snapshot(candidate->session,&candidate->state))return;candidate->geometry.reset(new LiveWingGeometry);
  if(!candidate->geometry->Capture(plane,reference,candidate->state,candidate->anchor,error)){std::fprintf(stderr,"[gameplay-wing] capture ref=%d rejected: %s\n",reference,error.c_str());return;}
  if(!candidate->geometry->Publish(candidate->state,candidate->anchor,error)){std::fprintf(stderr,"[gameplay-wing] publication ref=%d rejected: %s\n",reference,error.c_str());return;}
  std::fprintf(stderr,"[gameplay-wing] acquired ref=%d nodes=%d faces=%d world_faces=%d owner=coordinator\n",reference,candidate->state.nodes,candidate->state.faces,candidate->environment.faces);active=std::move(candidate);
 }
 Island &island=*active;if(island.failed)return;
 std::string error;char message[512]{};WpStaticMesh environment{};
 if(!Neighborhood(island.state,island.anchor,environment,error)){std::fprintf(stderr,"[gameplay-wing] unsupported neighborhood handoff: %s\n",error.c_str());Retire();return;}
 if(island.state.contacts==0&&!WingNear(island.state,environment,dt)){Retire();return;}
 if(std::memcmp(&environment,&island.environment,sizeof environment)){
  Trace('W',&environment,sizeof environment);
  if(wp_replace_world(island.session,&environment,message,sizeof message)){std::fprintf(stderr,"[gameplay-wing] world replacement handoff: %s\n",message);Retire();return;}island.environment=environment;
 }
 const WpState before=island.state;island.load={};island.frameDt=dt;
 // Existing flight controllers supply force impulses only. The engine's
 // ApplyMove/TurnForce seam captures these without changing native state.
 if(island.plane->pDriver==FindPlayerPed()&&island.plane->GetStatus()!=STATUS_WRECKED){
  CMatrix pose=island.plane->GetMatrix();auto velocity=island.plane->m_vecMoveSpeed,spin=island.plane->m_vecTurnSpeed;const float mass=island.plane->m_fMass;
  static auto forceProvider=reinterpret_cast<bool(*)(void*)>(dlsym(RTLD_DEFAULT,"VCSourceAircraftForces"));
  island.capturing=true;bool evaluated=forceProvider&&forceProvider(island.plane);island.capturing=false;if(!evaluated){Retire();return;}
  bool changed=(island.plane->GetPosition()-pose.GetPosition()).MagnitudeSqr()>0||(island.plane->m_vecMoveSpeed-velocity).MagnitudeSqr()>0||(island.plane->m_vecTurnSpeed-spin).MagnitudeSqr()>0;
  changed|=(island.plane->GetRight()-pose.GetRight()).MagnitudeSqr()>0||(island.plane->GetForward()-pose.GetForward()).MagnitudeSqr()>0||(island.plane->GetUp()-pose.GetUp()).MagnitudeSqr()>0;
  island.plane->m_fMass=mass;if(changed){island.plane->GetMatrix()=pose;island.plane->m_vecMoveSpeed=velocity;island.plane->m_vecTurnSpeed=spin;Fail(island,"flight evaluator changed owned motion state");return;}
 }
 if(island.failed)return;
 int steps=int(std::ceil(dt/.005));if(steps<1||steps>24){Fail(island,"native interval exceeds supported step budget");return;}
 for(int i=0;i<steps;++i){double h=dt/steps;if(!BodyClear(island.plane,environment,island.anchor,h,&island.state)){island.state=before;std::fprintf(stderr,"[gameplay-wing] native body boundary handoff before publication\n");Retire();return;}Trace('T',&h,sizeof h);Trace('F',&island.load,sizeof island.load);if(wp_advance_forces(island.session,dt/steps,&island.load,message,sizeof message)){island.state=before;std::fprintf(stderr,"[gameplay-wing] numerical boundary handoff; unpublished interval discarded: %s\n",message);Retire();return;}wp_snapshot(island.session,&island.state);if(!BodyClear(island.plane,environment,island.anchor,0,&island.state)){island.state=before;std::fprintf(stderr,"[gameplay-wing] discard unpublished body boundary interval\n");Retire();return;}island.peakContacts=std::max(island.peakContacts,island.state.contacts);island.totalMs+=island.state.stepMs;island.maximumMs=std::max(island.maximumMs,island.state.stepMs);}
 if(island.state.revision==before.revision)return;
 auto publicationStart=std::chrono::steady_clock::now();
 if(!island.geometry->Publish(island.state,island.anchor,error)){Fail(island,error.c_str());return;}PublishVelocities(island,before,island.state.time-before.time);++island.published;for(auto participant:island.geometry->Participants()){bool vehicle=participant->IsVehicle();int ref=vehicle?CPools::GetVehicleRef((CVehicle*)participant):CPools::GetObjectRef((CObject*)participant);advanced.push_back({participant,ref,vehicle});}if(std::getenv("VC_REGULAR_WING"))std::fprintf(stderr,"[gameplay-wing] publication revision=%llu ms=%.3f\n",(unsigned long long)island.state.revision,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-publicationStart).count());
 if(std::getenv("VC_REGULAR_WING")&&(island.published==1||island.state.fragments>before.fragments||island.state.plastic>before.plastic)){int checks=0;bool okay=island.geometry->VerifyQueries(island.state,island.anchor,checks,error);std::fprintf(stderr,"[gameplay-wing] native-query-proof revision=%llu fragments=%d checks=%d result=%s plastic=%.9g detail=%s\n",(unsigned long long)island.state.revision,island.state.fragments,checks,okay?"PASS":"FAIL",island.state.plastic,error.c_str());/* Query failures are recorded for verification; they do not introduce a second motion owner. */}
 if(island.state.fragments>before.fragments||island.published%120==0)std::fprintf(stderr,"[gameplay-wing] ref=%d t=%.3f revision=%llu fragments=%d mounts=%d contacts_peak=%d step_ms=%.3f owner=coordinator\n",island.reference,island.state.time,(unsigned long long)island.state.revision,island.state.fragments,island.state.brokenMounts,island.peakContacts,island.state.stepMs);
 if(std::getenv("VC_REGULAR_WING"))std::fprintf(stderr,"[gameplay-wing] coordinator_frame_ms=%.3f\n",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frameStart).count());
}
