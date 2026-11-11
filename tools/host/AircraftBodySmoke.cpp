// Acceptance runs in the ordinary saved-game world. Every shot below goes
// through the game's instant-round/world-query/InflictDamage hook chain.
#include "common.h"
#include "AircraftBody.h"
#include "AircraftBodySmoke.h"
#include "DodoRig.h"
#include "GameplayWing.h"
#include "World.h"
#include "Pools.h"
#include "Physical.h"
#include "ModelIndices.h"
#include "Plane.h"
#include "PlayerPed.h"
#include "Weapon.h"
#include "WeaponInfo.h"
#include "Camera.h"
#include "Clock.h"
#include "Timer.h"
#include "main.h"
#include "skeleton.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
void RegularGameSmokeCapture(const char *);
namespace {
struct WingSample {CVector point,normal;};
WingSample tipSample,rootSample;bool initialQueriesDone=false,repairedQueriesDone=false;
int reference=-1,phase=0,shots=0,phaseShots=0;unsigned phaseAt=0;bool done=false;AircraftBodySnapshot baseline{},beforeReplacement{};float damagedMass=0;
bool TailCase(){auto c=std::getenv("VC_REGULAR_WING");return c&&!strcmp(c,"body-tail");}
bool Enabled(){auto c=std::getenv("VC_REGULAR_WING");return c&&(!strcmp(c,"body-bullets")||TailCase());}
unsigned tailStart=0;CVector tailOrigin;bool tailFailed=false;
CPlane *Plane(){auto v=CPools::GetVehicle(reference);return v&&v->GetModelIndex()==181&&v->IsPlane()?(CPlane*)v:nil;}
bool Wheels(const AircraftBodySnapshot &s){for(int i=0;i<3;++i)if(s.wheelTriangles[i]!=baseline.wheelTriangles[i]||s.wheelTriangles[i]==0)return false;return true;}
void Receipt(const char *name,bool success,const AircraftBodySnapshot &s){std::fprintf(stderr,"[body-smoke] %s result=%s shots=%d phase_shots=%d lift=%.4f/%.4f controls=%.4f/%.4f wheels=%u/%u/%u bodies=%u\n",name,success?"PASS":"FAIL",shots,phaseShots,s.leftLift,s.rightLift,s.pitch,s.yaw,s.wheelTriangles[0],s.wheelTriangles[1],s.wheelTriangles[2],s.detachedBodies);}
// Locate the real upper skin in the committed native model. These samples
// are cached before shooting, so a removed tip cannot silently choose a root.
bool SampleWing(CPlane *plane,float x,WingSample &sample,float y=.3f){
 auto col=plane->GetColModel();if(!col||!col->vertices||!col->triangles)return false;float height=-1000;bool found=false;
 for(int i=0;i<col->numTriangles;++i){const auto &t=col->triangles[i];if(t.surface==7)continue;CVector a=col->vertices[t.a].Get(),b=col->vertices[t.b].Get(),c=col->vertices[t.c].Get(),e=b-a,f=c-a;float det=e.x*f.y-e.y*f.x;if(fabs(det)<1e-8f)continue;float u=((x-a.x)*f.y-(y-a.y)*f.x)/det,v=(e.x*(y-a.y)-e.y*(x-a.x))/det;if(u<0||v<0||u+v>1)continue;float z=a.z+u*e.z+v*f.z;if(z<=height)continue;auto n=CrossProduct(e,f);if(n.MagnitudeSqr()<1e-10f)continue;n.Normalise();if(n.z<0)n=-n;if(n.z<.7f)continue;sample.point=CVector(x,y,z);sample.normal=n;height=z;found=true;
 }return found;
}
bool SegmentTriangle(CVector from,CVector delta,CVector a,CVector b,CVector c,float &closest){
 auto e=b-a,f=c-a,h=CrossProduct(delta,f);float det=DotProduct(e,h);if(fabs(det)<1e-9f)return false;auto s=from-a;float u=DotProduct(s,h)/det;if(u<0||u>1)return false;auto q=CrossProduct(s,e);float v=DotProduct(delta,q)/det;if(v<0||u+v>1)return false;float t=DotProduct(f,q)/det;if(t<0||t>=closest)return false;closest=t;return true;
}
struct RenderQuery {
 RwFrame *root;CVector from,to;float fraction=1;bool hit=false,valid=true;unsigned atomics=0,triangles=0;
 RenderQuery(RwFrame *r,CVector a,CVector b):root(r),from(a),to(b){}
};
rw::Atomic *RenderAtomic(rw::Atomic *atomic,void *data){
 auto &q=*(RenderQuery*)data;if(!(atomic->getFlags()&4))return atomic;if(++q.atomics>512){q.valid=false;return nil;}auto g=atomic->geometry;if(!g||!g->morphTargets||!g->morphTargets[0].vertices||g->numVertices<1||g->numVertices>65535||g->numTriangles<0||q.triangles+g->numTriangles>250000){q.valid=false;return nil;}q.triangles+=g->numTriangles;
 CMatrix local;local.SetUnity();auto frame=atomic->getFrame();int depth=0;for(;frame&&frame!=q.root&&depth<32;frame=RwFrameGetParent(frame),++depth){CMatrix step(RwFrameGetMatrix(frame),false);local=step*local;}if(frame!=q.root){q.valid=false;return nil;}
 for(int i=0;i<g->numTriangles;++i){const auto &t=g->triangles[i];if(t.v[0]>=g->numVertices||t.v[1]>=g->numVertices||t.v[2]>=g->numVertices||t.matId>=g->matList.numMaterials){q.valid=false;return nil;}auto material=g->matList.materials[t.matId];if(!material||!material->color.alpha)continue;CVector p[3];for(int k=0;k<3;++k){auto v=g->morphTargets[0].vertices[t.v[k]];p[k]=local*CVector(v.x,v.y,v.z);}if(SegmentTriangle(q.from,q.to-q.from,p[0],p[1],p[2],q.fraction))q.hit=true;
 }return atomic;
}
bool RenderTrace(CPlane *plane,CVector start,CVector end,bool &valid,CVector &point){
 auto inverse=Invert(plane->GetMatrix());RenderQuery q(RpClumpGetFrame(plane->GetClump()),inverse*start,inverse*end);RpClumpForAllAtomics(plane->GetClump(),RenderAtomic,&q);valid=q.valid;if(q.hit)point=start+(end-start)*q.fraction;return q.hit;
}
// The unresident probe borrows the real game's beachball COL. It never enters
// CWorld, requests a model, replaces collision, or consumes an object-pool slot.
// ProcessEntityCollision does update collision records, so restore the parent's
// records and flags before returning from this observational check.
int NativeContact(CPlane *plane,CVector point,CVector normal,float &radius){
 auto info=MI_BEACHBALL>=0?CModelInfo::GetModelInfo(MI_BEACHBALL):nil;auto col=info?info->GetColModel():nil;if(!col||col->numSpheres!=1||!col->spheres)return -1;const auto &sphere=col->spheres[0];radius=sphere.radius;if(!std::isfinite(radius)||radius<.02f||radius>.4f)return -1;
 const bool collided=plane->bHasCollided;const auto last=plane->m_nLastTimeCollided;const auto count=plane->m_nCollisionRecords;std::array<CEntity*,PHYSICAL_MAX_COLLISIONRECORDS> records;for(size_t i=0;i<records.size();++i)records[i]=plane->m_aCollisionRecords[i];
 CPhysical probe;probe.SetType(ENTITY_TYPE_OBJECT);probe.SetModelIndexNoCreate(MI_BEACHBALL);probe.SetIsStatic(true);probe.GetMatrix().SetUnity();probe.SetPosition(point+normal*(radius-.01f)-sphere.center);CColPoint points[MAX_COLLISION_POINTS];int contacts=probe.ProcessEntityCollision(plane,points);
 plane->bHasCollided=collided;plane->m_nLastTimeCollided=last;plane->m_nCollisionRecords=count;for(size_t i=0;i<records.size();++i)plane->m_aCollisionRecords[i]=records[i];return contacts;
}
bool NativeQuery(CPlane *plane,const WingSample &sample,bool expected,const char *stage){
 AircraftBodySnapshot before,after;if(!VCSourceAircraftBodyRead(plane,&before))return false;const float health=plane->m_fHealth;auto point=plane->GetMatrix()*sample.point,normal=Multiply3x3(plane->GetMatrix(),sample.normal);normal.Normalise();auto start=point+CVector(0,0,.18f),end=point-CVector(0,0,.18f);CColPoint losPoint,verticalPoint;CEntity *losEntity=nil,*verticalEntity=nil;
 // Vehicles only: the short segment cannot reach the ground, and actual
 // detached CObjects are excluded rather than mistaken for the parent skin.
 bool los=CWorld::ProcessLineOfSight(start,end,losPoint,losEntity,false,true,false,false,false,false,false,false),vertical=CWorld::ProcessVerticalLine(start,end.z,verticalPoint,verticalEntity,false,true,false,false,false,false,nil),clear=CWorld::GetIsLineOfSightClear(start,end,false,true,false,false,false,false,false);
 bool renderValid=false;CVector renderPoint;bool render=RenderTrace(plane,start,end,renderValid,renderPoint);float radius=0;int contacts=NativeContact(plane,point,normal,radius);bool unchanged=VCSourceAircraftBodyRead(plane,&after)&&before.revision==after.revision&&before.power==after.power&&before.engine==after.engine&&before.breach==after.breach&&before.detachedBodies==after.detachedBodies&&plane->m_fHealth==health;for(int i=0;i<3;++i)unchanged&=before.wheelTriangles[i]==after.wheelTriangles[i];
 float error=los&&render?(losPoint.point-renderPoint).Magnitude():0;bool success=los==expected&&(!los||losEntity==plane)&&vertical==expected&&(!vertical||verticalEntity==plane)&&clear!=expected&&contacts>=0&&((contacts>0)==expected)&&renderValid&&render==expected&&(!expected||error<=.02f)&&unchanged;
 std::fprintf(stderr,"[body-smoke] native query %s result=%s expected=%d los=%d parent=%d vertical=%d parent=%d clear=%d contacts=%d render=%d render_error=%.6f probe_model=%d radius=%.3f unchanged=%d revision=%llu\n",stage,success?"PASS":"FAIL",expected,los,losEntity==plane,vertical,verticalEntity==plane,clear,contacts,render,error,MI_BEACHBALL,radius,unchanged,(unsigned long long)before.revision);return success;
}
// Select a currently surviving COL interval on the desired material band.
// Aiming at the remaining geometry avoids wasting rounds in existing holes.
bool Aim(CPlane *plane,int axis,float coordinate,bool preserveBridge,CVector &point,CVector &normal){
 auto col=plane->GetColModel();float best=0;bool found=false;
 for(int t=0;t<col->numTriangles;++t){auto tri=col->triangles[t];if(tri.surface==7)continue;CVector p[3]={col->vertices[tri.a].Get(),col->vertices[tri.b].Get(),col->vertices[tri.c].Get()};std::array<CVector,6> intersections;int count=0;
  for(int k=0;k<3;++k){auto a=p[k],b=p[(k+1)%3];float ca=(&a.x)[axis]-coordinate,cb=(&b.x)[axis]-coordinate;if(fabs(ca)<.00001f)intersections[count++]=a;if(ca*cb<0)intersections[count++]=a+(b-a)*(ca/(ca-cb));}
  if(count<2)continue;auto n=CrossProduct(p[1]-p[0],p[2]-p[0]);if(n.MagnitudeSqr()<1e-12f)continue;n.Normalise();for(int a=0;a<count;++a)for(int b=0;b<a;++b){float span=(intersections[a]-intersections[b]).Magnitude();if(span<=best)continue;auto middle=(intersections[a]+intersections[b])*.5f;if(preserveBridge&&((axis==0&&middle.y>.10f&&middle.y<.50f)||(axis==1&&middle.x>-.1f&&middle.x<.1f)))continue;point=middle*.98f+(p[0]+p[1]+p[2])*(.02f/3);normal=n;best=span;found=true;}
 }return found;
}
void Shoot(CPlane *plane,CVector localPoint,CVector localNormal){auto from=plane->GetMatrix()*(localPoint+localNormal*.3f),to=plane->GetMatrix()*(localPoint-localNormal*.3f);int damage=CWeaponInfo::GetWeaponInfo(WEAPONTYPE_MINIGUN)->m_nDamage;FireOneInstantHitRound(&from,&to,damage);++shots;++phaseShots;}
rw::Atomic *CountVisible(rw::Atomic *atomic,void *data){if((atomic->getFlags()&4)&&atomic->geometry)*static_cast<int*>(data)+=atomic->geometry->numTriangles;return atomic;}
void TailTick(unsigned frames,const char *capture){
 if(!tailStart||done)return;auto plane=Plane();AircraftBodySnapshot state;int triangles=0;if(plane&&plane->m_rwObject)RpClumpForAllAtomics(plane->GetClump(),CountVisible,&triangles);
 bool valid=plane&&VCSourceAircraftBodyRead(plane,&state)&&plane->bIsVisible&&triangles>200&&plane->GetColModel()->numTriangles>200;
 if(plane){auto p=plane->GetPosition(),v=plane->m_vecMoveSpeed;valid&=std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&p.z>tailOrigin.z-4&&(p-tailOrigin).Magnitude()<35;if(frames%30==0||!valid)std::fprintf(stderr,"[body-smoke] tail lifetime ref=%d xyz=%.4f %.4f %.4f speed=%.4f visible=%d triangles=%d col=%d mass=%.3f\n",reference,p.x,p.y,p.z,v.Magnitude()*50,plane->bIsVisible,triangles,plane->GetColModel()->numTriangles,plane->m_fMass);}
 if(!valid){std::fprintf(stderr,"[body-smoke] crash then tail shooting retains aircraft result=FAIL shots=%d live=%d\n",shots,plane!=nil);RegularGameSmokeCapture(capture);tailFailed=done=true;RsGlobal.quit=TRUE;return;}
 unsigned elapsed=frames-tailStart;
 if(elapsed>20&&elapsed<=160){CVector point,normal;float band=-3.8f-.3f*float((elapsed-21)%6);if(Aim(plane,1,band,false,point,normal))Shoot(plane,point,normal);}
 if(elapsed==180)RegularGameSmokeCapture((std::string(capture)+".after-tail-shots.ppm").c_str());
 if(elapsed>850){Receipt("crash then tail shooting retains aircraft",!tailFailed&&shots>20,state);RegularGameSmokeCapture(capture);done=true;RsGlobal.quit=TRUE;}
}
}
void AircraftBodySmokeStartTail(CPlane *plane,unsigned frames,const char *capture){
 if(!TailCase()||tailStart)return;reference=CPools::GetVehicleRef(plane);tailStart=frames;tailOrigin=plane->GetPosition();VCSourceAircraftBodyRead(plane,&baseline);auto player=FindPlayerPed();
 if(player&&player->bInVehicle){player->SetObjective(OBJECTIVE_LEAVE_CAR,plane);CPed::PedSetOutCarCB(nil,player);player->Teleport(plane->GetMatrix()*CVector(-9,0,.4f));player->SetObjective(OBJECTIVE_NONE);}
 CClock::SetGameClock(13,0);TheCamera.SetCamPositionForFixedMode(plane->GetMatrix()*CVector(9,-10,4),CVector(0,0,0));TheCamera.TakeControlNoEntity(plane->GetMatrix()*CVector(0,-2,0),JUMP_CUT,CAMCONTROL_SCRIPT);
 std::fprintf(stderr,"[body-smoke] after real crash exit result=%s ref=%d\n",player&&!player->bInVehicle?"PASS":"FAIL",reference);RegularGameSmokeCapture((std::string(capture)+".crash.ppm").c_str());
}
void AircraftBodySmokeTick(unsigned frames,const char *capture){
 if(!Enabled()||done)return;
 if(TailCase()){TailTick(frames,capture);return;}
 if(reference<0&&frames>=200){auto pool=CPools::GetVehiclePool();for(int i=0;i<pool->GetSize();++i){auto v=pool->GetSlot(i);if(v&&v->IsPlane()&&v->GetModelIndex()==181&&VCSourceAircraftBodyRead(v,&baseline)){reference=CPools::GetVehicleRef(v);phaseAt=frames;auto player=FindPlayerPed();if(player){CVector point=v->GetMatrix()*CVector(-9,0,0);player->Teleport(point);}CClock::SetGameClock(13,0);TheCamera.SetCamPositionForFixedMode(v->GetMatrix()*CVector(9,-10,4),CVector(0,0,0));TheCamera.TakeControlNoEntity(v->GetMatrix()*CVector(0,-2,0),JUMP_CUT,CAMCONTROL_SCRIPT);std::fprintf(stderr,"[body-smoke] original parked Dodo; real instant rounds budget=%d\n",CWeaponInfo::GetWeaponInfo(WEAPONTYPE_MINIGUN)->m_nDamage);break;}}}
 auto plane=Plane();AircraftBodySnapshot state;if(!plane||!VCSourceAircraftBodyRead(plane,&state)){if(frames>300){std::fprintf(stderr,"[body-smoke] owner result=FAIL\n");done=true;RsGlobal.quit=TRUE;}return;}
 if(frames<=phaseAt+2)return;
 if(!initialQueriesDone){bool samples=SampleWing(plane,4.4f,tipSample)&&SampleWing(plane,2.0f,rootSample);std::fprintf(stderr,"[body-smoke] native query samples result=%s\n",samples?"PASS":"FAIL");if(!samples){done=true;RsGlobal.quit=TRUE;return;}NativeQuery(plane,tipSample,true,"intact-tip");NativeQuery(plane,rootSample,true,"intact-root");initialQueriesDone=true;}
 if(phase==0){
  // This accepted body-glass path was inside the old generic engine box.
  CVector a=plane->GetMatrix()*CVector(-3,.5f,.5f),b=plane->GetMatrix()*CVector(3,.5f,.5f);FireOneInstantHitRound(&a,&b,CWeaponInfo::GetWeaponInfo(WEAPONTYPE_MINIGUN)->m_nDamage);++shots;++phaseShots;if(phaseShots>=12){phase=1;phaseAt=frames;phaseShots=0;}
 }else if(phase==1){Receipt("window preserves engine",state.power>.99f&&state.engine==0&&state.breach==0,state);VCSourceAircraftBodyRepair(plane);phase=2;phaseAt=frames;phaseShots=0;
 }else if(phase==2||phase==3){CVector point,normal;bool target=Aim(plane,0,3,phase==2,point,normal);if(target&&phaseShots<300){Shoot(plane,point,normal);return;}if(phase==2){NativeQuery(plane,tipSample,true,"partial-tip");Receipt("partial wing remains connected",state.rightLift>.80f&&state.leftLift>.99f&&Wheels(state),state);phase=6;phaseAt=frames;phaseShots=0;}else{NativeQuery(plane,tipSample,false,"removed-tip");NativeQuery(plane,rootSample,true,"retained-root");Receipt("cut wing releases material",state.rightLift<.80f&&state.leftLift>.99f&&Wheels(state)&&state.detachedBodies>0,state);RegularGameSmokeCapture((std::string(capture)+".cut-wing.ppm").c_str());beforeReplacement=state;damagedMass=plane->m_fMass;plane->DeleteRwObject();plane->CreateRwObject();DodoRigFrame();phase=5;phaseAt=frames;phaseShots=0;}
 }else if(phase==6){static int upperAttempt=0;if(upperAttempt<8){WingSample skin;const float y=.12f+.05f*float(upperAttempt++);if(SampleWing(plane,3,skin,y)){const auto from=plane->GetMatrix()*(skin.point+skin.normal*.025f),to=plane->GetMatrix()*(skin.point-skin.normal*.025f);CVector a=from,b=to;FireOneInstantHitRound(&a,&b,CWeaponInfo::GetWeaponInfo(WEAPONTYPE_MINIGUN)->m_nDamage);++shots;++phaseShots;}return;}phase=7;phaseAt=frames;phaseShots=0;
 }else if(phase==7){if(frames<phaseAt+90)return;WingSample bent;bool found=SampleWing(plane,4.4f,bent);float sag=found?tipSample.point.z-bent.point.z:0;std::fprintf(stderr,"[body-smoke] own weight flex result=%s sag_m=%.6f\n",found&&sag>.2f&&Wheels(state)?"PASS":"FAIL",sag);if(found){NativeQuery(plane,bent,true,"loaded-tip");NativeQuery(plane,tipSample,false,"vacated-tip");}RegularGameSmokeCapture((std::string(capture)+".loaded-wing.ppm").c_str());phase=3;phaseAt=frames;phaseShots=0;
 }else if(phase==5){Receipt("damaged clump replacement preserves body",std::fabs(state.rightLift-beforeReplacement.rightLift)<1e-5f&&state.rightLift<.8f&&Wheels(state)&&std::fabs(plane->m_fMass-damagedMass)<1e-3f,state);NativeQuery(plane,tipSample,false,"replacement-removed-tip");NativeQuery(plane,rootSample,true,"replacement-root");VCSourceAircraftBodyRepair(plane);phase=4;phaseAt=frames;phaseShots=0;
 }else if(phase==4){if(!repairedQueriesDone){NativeQuery(plane,tipSample,true,"repaired-tip");NativeQuery(plane,rootSample,true,"repaired-root");Receipt("repair restores carrier mass",std::fabs(plane->m_fMass-2400)<1e-3f,state);repairedQueriesDone=true;}CVector point,normal;bool target=Aim(plane,1,-4,false,point,normal);if(target&&phaseShots<400){Shoot(plane,point,normal);return;}Receipt("tail cut preserves wings wheels",state.pitch<.5f&&state.leftLift>.99f&&state.rightLift>.99f&&Wheels(state)&&state.engine==0&&state.breach==0,state);RegularGameSmokeCapture((std::string(capture)+".cut-tail.ppm").c_str());RegularGameSmokeCapture(capture);done=true;RsGlobal.quit=TRUE;}
 if(frames>1900){std::fprintf(stderr,"[body-smoke] bounded sequence result=FAIL\n");done=true;RsGlobal.quit=TRUE;}
}
