// Test orchestration is enabled only by the existing regular-game smoke env.
// Assets, actors, bullets, native world queries and frame ownership are real.
#include "common.h"
#include "VehicleBodySmoke.h"
#include "AircraftBody.h"
#include "DodoRig.h"
#include "GameplayWing.h"
#include "World.h"
#include "CarCtrl.h"
#include "Pools.h"
#include "Physical.h"
#include "Automobile.h"
#include "Bike.h"
#include "Boat.h"
#include "Plane.h"
#include "PlayerPed.h"
#include "Streaming.h"
#include "ModelIndices.h"
#include "NodeName.h"
#include "VehicleModelInfo.h"
#include "VisibilityPlugins.h"
#include "Weapon.h"
#include "WeaponInfo.h"
#include "Camera.h"
#include "Clock.h"
#include "Timer.h"
#include "main.h"
#include "skeleton.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
void RegularGameSmokeCapture(const char *);
namespace {
struct Sample {CVector point,normal;};
struct Actor {const char *label;int model,ref=-1;Sample hit,retained;AircraftBodySnapshot intact{},damaged{};std::vector<CColLine> lines;float mass=0;Actor(const char *name,int id):label(name),model(id){};};
std::array<Actor,7> actors{{{"car",130},{"bike",191},{"boat",182},{"helicopter",217},{"plane",194},{"dodo",181},{"extras",170}}};
Actor savedActor("saved",130);
struct SelectedAtomic {std::string frame;uint32_t id=0,variant=0;bool visible=false;};
std::vector<SelectedAtomic> savedSelection;float savedTurnMass=0;CVector savedCOM;std::array<float,12> savedDoorPose{},savedWheelPose{};
std::vector<int> stress;unsigned phaseAt=0;int phase=0;bool done=false,all=true;int recycledRef=-1;uint64_t recycledGeneration=0;CVehicle *recycledAddress=nil;double queryMs=0;bool protectedOriginal=false,protectedTargeted=false;float protectedHealth=0;uint64_t protectedRevision=0;CVector animationOrigin;float animationSpin=0;unsigned animationQueries=0;bool animationValid=true;
bool Enabled(){const char *c=std::getenv("VC_REGULAR_WING");return c&&!std::strcmp(c,"body-vehicles");}
CVehicle *Vehicle(const Actor &a){auto v=CPools::GetVehicle(a.ref);return v&&v->GetModelIndex()==a.model?v:nil;}
void Receipt(const char *name,bool success,const Actor *a=nil){all&=success;std::fprintf(stderr,"[vehicle-body-smoke] %s result=%s class=%s model=%d\n",name,success?"PASS":"FAIL",a?a->label:"all",a?a->model:0);}
bool NativeLines(const Actor &a){auto v=Vehicle(a);auto col=v?v->GetColModel():nil;if(!col||col->numLines!=int(a.lines.size()))return false;for(size_t i=0;i<a.lines.size();++i)if((col->lines[i].p0-a.lines[i].p0).Magnitude()>1e-6f||(col->lines[i].p1-a.lines[i].p1).Magnitude()>1e-6f)return false;return true;}
void Position(CVehicle *v,CVector p){CWorld::Remove(v);v->GetMatrix().SetUnity();v->SetPosition(p);v->m_vecMoveSpeed=v->m_vecTurnSpeed=v->m_vecMoveFriction=v->m_vecTurnFriction=CVector(0,0,0);v->SetStatus(STATUS_ABANDONED);v->SetIsStatic(true);v->bUsesCollision=true;v->bIsBIGBuilding=false;v->GetMatrix().UpdateRW();v->UpdateRwFrame();CWorld::Add(v);}
CVehicle *Create(int model,CVector p,int handle=-1){CVehicle *v=nil;if(model==191)v=new CBike(model,MISSION_VEHICLE);else if(model==182)v=new CBoat(model,MISSION_VEHICLE);else if(handle>=0)v=new(handle) CAutomobile(model,MISSION_VEHICLE);else v=new CAutomobile(model,MISSION_VEHICLE);if(v){v->SetPosition(p);v->SetStatus(STATUS_ABANDONED);v->SetIsStatic(true);v->bUsesCollision=true;v->bIsBIGBuilding=false;v->GetMatrix().UpdateRW();v->UpdateRwFrame();CWorld::Add(v);}return v;}
bool SegmentTriangle(CVector from,CVector delta,CVector a,CVector b,CVector c,float &closest){auto e=b-a,f=c-a,h=CrossProduct(delta,f);float det=DotProduct(e,h);if(std::fabs(det)<1e-9f)return false;auto s=from-a;float u=DotProduct(s,h)/det;if(u<0||u>1)return false;auto q=CrossProduct(s,e);float v=DotProduct(delta,q)/det;if(v<0||u+v>1)return false;float t=DotProduct(f,q)/det;if(t<0||t>=closest)return false;closest=t;return true;}
CVector Closest(CVector p,CVector a,CVector b,CVector c){auto ab=b-a,ac=c-a,ap=p-a;float d1=DotProduct(ab,ap),d2=DotProduct(ac,ap);if(d1<=0&&d2<=0)return a;auto bp=p-b;float d3=DotProduct(ab,bp),d4=DotProduct(ac,bp);if(d3>=0&&d4<=d3)return b;float vc=d1*d4-d3*d2;if(vc<=0&&d1>=0&&d3<=0)return a+ab*(d1/(d1-d3));auto cp=p-c;float d5=DotProduct(ab,cp),d6=DotProduct(ac,cp);if(d6>=0&&d5<=d6)return c;float vb=d5*d2-d1*d6;if(vb<=0&&d2>=0&&d6<=0)return a+ac*(d2/(d2-d6));float va=d3*d6-d5*d4;if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0)return b+(c-b)*((d4-d3)/((d4-d3)+(d5-d6)));float inv=1/(va+vb+vc);return a+ab*(vb*inv)+ac*(vc*inv);}
struct RenderQuery {RwFrame *root;CVector from,to,probe;float fraction=1,radius=0,best=0;bool hit=false,touch=false,valid=true,choose=false,wheelsOnly=false;Sample sample{},retained{};unsigned triangles=0;};
rw::Atomic *RenderAtomic(rw::Atomic *atomic,void *data){auto &q=*(RenderQuery*)data;if(!(atomic->getFlags()&4))return atomic;auto frame=atomic->getFrame();const char *name=GetFrameNodeName(frame);if(name&&(std::strstr(name,"_lo")||std::strstr(name,"_vlo")))return atomic;auto g=atomic->geometry;if(!g||!g->morphTargets||!g->morphTargets[0].vertices||g->numVertices<1||g->numVertices>65535||g->numTriangles<0||q.triangles+g->numTriangles>250000){q.valid=false;return nil;}q.triangles+=g->numTriangles;CMatrix local;local.SetUnity();int depth=0;for(;frame&&frame!=q.root&&depth<32;frame=RwFrameGetParent(frame),++depth){CMatrix step(RwFrameGetMatrix(frame),false);local=step*local;}if(frame!=q.root){q.valid=false;return nil;}
 for(int i=0;i<g->numTriangles;++i){const auto&t=g->triangles[i];if(t.v[0]>=g->numVertices||t.v[1]>=g->numVertices||t.v[2]>=g->numVertices||t.matId>=g->matList.numMaterials){q.valid=false;return nil;}auto material=g->matList.materials[t.matId];if(!material||!material->color.alpha)continue;CVector p[3];for(int k=0;k<3;++k){auto v=g->morphTargets[0].vertices[t.v[k]];p[k]=local*CVector(v.x,v.y,v.z);}if(SegmentTriangle(q.from,q.to-q.from,p[0],p[1],p[2],q.fraction))q.hit=true;if(q.radius>0&&(Closest(q.probe,p[0],p[1],p[2])-q.probe).MagnitudeSqr()<q.radius*q.radius)q.touch=true;
  if(q.choose&&material->color.alpha==255&&(q.wheelsOnly?(name&&(std::strstr(name,"wheel")||std::strstr(name,"tire"))):(!name||(!std::strstr(name,"wheel")&&!std::strstr(name,"tire")&&!std::strstr(name,"rotor")&&!std::strstr(name,"door"))))){auto n=CrossProduct(p[1]-p[0],p[2]-p[0]);float area=n.Magnitude();if(area<(q.wheelsOnly?.002f:.02f))continue;n.Normalise();auto centre=(p[0]+p[1]+p[2])/3;float score=n.z>.7f?area:0;if(score>q.best){q.best=score;q.sample={centre,n};int farthest=0;for(int k=1;k<3;++k)if((p[k]-centre).MagnitudeSqr()>(p[farthest]-centre).MagnitudeSqr())farthest=k;q.retained={centre*.6f+p[farthest]*.4f,n};}}
 }return atomic;}
RenderQuery Render(CVehicle *v,CVector from,CVector to,CVector probe=CVector(0,0,0),float radius=0){RenderQuery q;q.root=RpClumpGetFrame(v->GetClump());q.from=from;q.to=to;q.probe=probe;q.radius=radius;RpClumpForAllAtomics(v->GetClump(),RenderAtomic,&q);return q;}
bool Select(CVehicle *v,Sample &sample,Sample *retained=nil,bool wheelsOnly=false){RenderQuery q;q.wheelsOnly=wheelsOnly;q.root=RpClumpGetFrame(v->GetClump());q.choose=true;RpClumpForAllAtomics(v->GetClump(),RenderAtomic,&q);sample=q.sample;if(retained)*retained=q.retained;return q.valid&&q.best>0;}
int NativeContact(CVehicle *v,CVector point,CVector normal,float &radius,CVector &centre){auto info=MI_BEACHBALL>=0?CModelInfo::GetModelInfo(MI_BEACHBALL):nil;auto col=info?info->GetColModel():nil;if(!col||col->numSpheres!=1||!col->spheres)return -1;const auto&sphere=col->spheres[0];radius=sphere.radius;if(!std::isfinite(radius)||radius<.02f||radius>.4f)return -1;const bool collided=v->bHasCollided;auto last=v->m_nLastTimeCollided;auto count=v->m_nCollisionRecords;std::array<CEntity*,PHYSICAL_MAX_COLLISIONRECORDS> records;for(size_t i=0;i<records.size();++i)records[i]=v->m_aCollisionRecords[i];centre=point+normal*(radius-.01f);CPhysical probe;probe.SetType(ENTITY_TYPE_OBJECT);probe.SetModelIndexNoCreate(MI_BEACHBALL);probe.SetIsStatic(true);probe.GetMatrix().SetUnity();probe.SetPosition(centre-sphere.center);CColPoint points[MAX_COLLISION_POINTS];int contacts=probe.ProcessEntityCollision(v,points);v->bHasCollided=collided;v->m_nLastTimeCollided=last;v->m_nCollisionRecords=count;for(size_t i=0;i<records.size();++i)v->m_aCollisionRecords[i]=records[i];return contacts;}
bool Query(const Actor &a,const Sample &sample,const char *stage,int expected=-1){auto v=Vehicle(a);if(!v)return false;auto start=sample.point+CVector(0,0,.035f),end=sample.point-CVector(0,0,.035f),worldStart=v->GetMatrix()*start,worldEnd=v->GetMatrix()*end;AircraftBodySnapshot before{},after{};bool unchanged=VCSourceAircraftBodyRead(v,&before);float health=v->m_fHealth;CColPoint losPoint,verticalPoint;CEntity *losEntity=nil,*verticalEntity=nil;auto stamp=std::chrono::steady_clock::now();bool los=CWorld::ProcessLineOfSight(worldStart,worldEnd,losPoint,losEntity,false,true,false,false,false,false,false,false),vertical=CWorld::ProcessVerticalLine(worldStart,worldEnd.z,verticalPoint,verticalEntity,false,true,false,false,false,false,nil),clear=CWorld::GetIsLineOfSightClear(worldStart,worldEnd,false,true,false,false,false,false,false);auto render=Render(v,start,end);float radius=0;CVector centre;auto normal=Multiply3x3(v->GetMatrix(),sample.normal);normal.Normalise();int contacts=NativeContact(v,v->GetMatrix()*sample.point,normal,radius,centre);auto probeRender=Render(v,start,end,Invert(v->GetMatrix())*centre,radius);queryMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-stamp).count();unchanged&=VCSourceAircraftBodyRead(v,&after)&&before.revision==after.revision&&v->m_fHealth==health;float error=los&&render.hit?(losPoint.point-(worldStart+(worldEnd-worldStart)*render.fraction)).Magnitude():0;bool success=render.valid&&probeRender.valid&&los==render.hit&&vertical==render.hit&&clear!=render.hit&&(!los||losEntity==v)&&(!vertical||verticalEntity==v)&&contacts>=0&&((contacts>0)==probeRender.touch)&&error<=.02f&&unchanged&&(expected<0||render.hit==bool(expected));all&=success;std::fprintf(stderr,"[vehicle-body-smoke] native query result=%s class=%s stage=%s render=%d los=%d vertical=%d contacts=%d sphere_render=%d error=%.6f unchanged=%d\n",success?"PASS":"FAIL",a.label,stage,render.hit,los,vertical,contacts,probeRender.touch,error,unchanged);return success;}
// A saved car can enter CWorld with DAM geometry selected and another part
// already missing. These observations retain native IDs rather than guessing
// variants from node names after model preprocessing merged the frames.
rw::Atomic *SelectedAtomicCB(rw::Atomic *atomic,void *data){
 auto &out=*(std::vector<SelectedAtomic>*)data;auto frame=atomic->getFrame();const char *name=frame?GetFrameNodeName(frame):nil;if(name&&(std::strstr(name,"_lo")||std::strstr(name,"_vlo")))return atomic;
 SelectedAtomic state;state.frame=name?name:"";state.id=CVisibilityPlugins::GetAtomicId(atomic);state.variant=state.id&(ATOMIC_FLAG_OK|ATOMIC_FLAG_DAM);state.visible=(atomic->getFlags()&4)!=0;out.push_back(state);return atomic;
}
std::vector<SelectedAtomic> Selection(CVehicle *v){std::vector<SelectedAtomic> out;if(v&&v->m_rwObject)RpClumpForAllAtomics(v->GetClump(),SelectedAtomicCB,&out);std::sort(out.begin(),out.end(),[](const SelectedAtomic&a,const SelectedAtomic&b){return a.frame!=b.frame?a.frame<b.frame:a.variant<b.variant;});return out;}
void LogSelection(const char *stage,const std::vector<SelectedAtomic>&states){for(const auto&s:states)if(std::strstr(s.frame.c_str(),"door_lf")||std::strstr(s.frame.c_str(),"door_rf")||std::strstr(s.frame.c_str(),"wing_lf"))std::fprintf(stderr,"[vehicle-body-smoke] saved selection stage=%s frame=%s id=%u variant=%u visible=%d\n",stage,s.frame.c_str(),s.id,s.variant,s.visible);}
bool SameSelection(const std::vector<SelectedAtomic>&a,const std::vector<SelectedAtomic>&b){if(a.size()!=b.size())return false;for(size_t i=0;i<a.size();++i)if(a[i].frame!=b[i].frame||a[i].variant!=b[i].variant||a[i].visible!=b[i].visible)return false;return true;}
unsigned Selected(const std::vector<SelectedAtomic>&states,const char *part,uint32_t variant){unsigned count=0;for(const auto &s:states)if(s.visible&&s.variant==variant&&std::strstr(s.frame.c_str(),part))++count;return count;}
bool ExclusiveVariants(const std::vector<SelectedAtomic>&states){for(const auto&a:states)if(a.visible&&a.variant)for(const auto&b:states)if(b.visible&&b.frame==a.frame&&b.variant&&b.variant!=a.variant)return false;return true;}
bool SavedVariants(const std::vector<SelectedAtomic>&s){return ExclusiveVariants(s)&&Selected(s,"door_lf",ATOMIC_FLAG_DAM)==1&&Selected(s,"door_lf",ATOMIC_FLAG_OK)==0&&Selected(s,"door_rf",ATOMIC_FLAG_OK)==0&&Selected(s,"door_rf",ATOMIC_FLAG_DAM)==0&&Selected(s,"wing_lf",ATOMIC_FLAG_DAM)==1&&Selected(s,"wing_lf",ATOMIC_FLAG_OK)==0;}
bool HealthyVariants(const std::vector<SelectedAtomic>&s){return ExclusiveVariants(s)&&Selected(s,"door_lf",ATOMIC_FLAG_OK)==1&&Selected(s,"door_lf",ATOMIC_FLAG_DAM)==0&&Selected(s,"door_rf",ATOMIC_FLAG_OK)==1&&Selected(s,"door_rf",ATOMIC_FLAG_DAM)==0&&Selected(s,"wing_lf",ATOMIC_FLAG_OK)==1&&Selected(s,"wing_lf",ATOMIC_FLAG_DAM)==0;}
std::array<float,12> FramePose(RwFrame *frame){std::array<float,12> result{};if(frame){CMatrix m(RwFrameGetMatrix(frame),false);auto r=m.GetRight(),f=m.GetForward(),u=m.GetUp(),p=m.GetPosition();result={{r.x,r.y,r.z,f.x,f.y,f.z,u.x,u.y,u.z,p.x,p.y,p.z}};}return result;}
bool SamePose(const std::array<float,12>&a,const std::array<float,12>&b){for(size_t i=0;i<a.size();++i)if(std::fabs(a[i]-b[i])>1e-5f)return false;return true;}
std::vector<uint32_t> DamageState(CAutomobile *v){std::vector<uint32_t> out;out.push_back(v->Damage.GetEngineStatus());for(int i=0;i<4;++i)out.push_back(v->Damage.GetWheelStatus(i));for(int i=0;i<6;++i)out.push_back(v->Damage.GetDoorStatus(i));out.push_back(v->Damage.m_lightStatus);out.push_back(v->Damage.m_panelStatus);return out;}
struct PartQuery {RwFrame *root;const char *part;uint32_t variant;bool visible;Sample sample{};float area=0;};
rw::Atomic *PartSampleCB(rw::Atomic *atomic,void *data){
 auto &q=*(PartQuery*)data;auto frame=atomic->getFrame();const char *name=frame?GetFrameNodeName(frame):nil;if(!name||!std::strstr(name,q.part)||(CVisibilityPlugins::GetAtomicId(atomic)&(ATOMIC_FLAG_OK|ATOMIC_FLAG_DAM))!=q.variant||((atomic->getFlags()&4)!=0)!=q.visible)return atomic;auto g=atomic->geometry;if(!g||!g->morphTargets||!g->morphTargets[0].vertices)return atomic;
 CMatrix local;local.SetUnity();int depth=0;for(;frame&&frame!=q.root&&depth<32;frame=RwFrameGetParent(frame),++depth){CMatrix m(RwFrameGetMatrix(frame),false);local=m*local;}if(frame!=q.root)return atomic;
 for(int i=0;i<g->numTriangles;++i){const auto&t=g->triangles[i];if(t.matId>=g->matList.numMaterials||!g->matList.materials[t.matId]||g->matList.materials[t.matId]->color.alpha<255)continue;CVector p[3];bool valid=true;for(int j=0;j<3;++j){if(t.v[j]>=g->numVertices){valid=false;break;}auto v=g->morphTargets[0].vertices[t.v[j]];p[j]=local*CVector(v.x,v.y,v.z);}if(!valid)continue;auto n=CrossProduct(p[1]-p[0],p[2]-p[0]);float area=n.Magnitude();if(area<=q.area||area<.01f)continue;n.Normalise();if(std::fabs(n.x)<.5f)continue;q.area=area;q.sample={(p[0]+p[1]+p[2])/3,n};}return atomic;
}
bool PartSample(CVehicle *v,const char *part,uint32_t variant,bool visible,Sample &sample){if(!v||!v->m_rwObject)return false;PartQuery q;q.root=RpClumpGetFrame(v->GetClump());q.part=part;q.variant=variant;q.visible=visible;RpClumpForAllAtomics(v->GetClump(),PartSampleCB,&q);sample=q.sample;return q.area>0;}
bool SavedQuery(const Sample &sample,const char *stage,bool expected){
 auto v=Vehicle(savedActor);if(!v)return false;auto from=sample.point+sample.normal*.035f,to=sample.point-sample.normal*.035f,worldFrom=v->GetMatrix()*from,worldTo=v->GetMatrix()*to;Sample verticalSample=sample;const bool verticalBody=std::fabs(sample.normal.z)<.25f;if(verticalBody&&!Select(v,verticalSample)){Receipt("saved vertical material sample",false,&savedActor);return false;}auto verticalFrom=verticalSample.point+CVector(0,0,.035f),verticalTo=verticalSample.point-CVector(0,0,.035f);AircraftBodySnapshot before{},after{};bool unchanged=VCSourceAircraftBodyRead(v,&before);float health=v->m_fHealth;CColPoint lp,vp;CEntity *le=nil,*ve=nil;
 bool los=CWorld::ProcessLineOfSight(worldFrom,worldTo,lp,le,false,true,false,false,false,false,false,false),vertical=CWorld::ProcessVerticalLine(v->GetMatrix()*verticalFrom,(v->GetMatrix()*verticalTo).z,vp,ve,false,true,false,false,false,false,nil),clear=CWorld::GetIsLineOfSightClear(worldFrom,worldTo,false,true,false,false,false,false,false);auto render=Render(v,from,to),verticalRender=Render(v,verticalFrom,verticalTo);float radius=0;CVector centre;auto normal=Multiply3x3(v->GetMatrix(),sample.normal);normal.Normalise();int contacts=NativeContact(v,v->GetMatrix()*sample.point,normal,radius,centre);auto probeRender=Render(v,from,to,Invert(v->GetMatrix())*centre,radius);unchanged&=VCSourceAircraftBodyRead(v,&after)&&before.revision==after.revision&&v->m_fHealth==health;float error=los&&render.hit?(lp.point-(worldFrom+(worldTo-worldFrom)*render.fraction)).Magnitude():0;
 bool valid=render.valid&&verticalRender.valid&&probeRender.valid&&render.hit==expected&&los==render.hit&&vertical==verticalRender.hit&&clear!=render.hit&&(!los||le==v)&&(!vertical||ve==v)&&contacts>=0&&((contacts>0)==probeRender.touch)&&error<=.02f&&unchanged;all&=valid;std::fprintf(stderr,"[vehicle-body-smoke] native query result=%s class=saved stage=%s render=%d los=%d vertical=%d vertical_render=%d vertical_body=%d contacts=%d sphere_render=%d error=%.6f unchanged=%d\n",valid?"PASS":"FAIL",stage,render.hit,los,vertical,verticalRender.hit,verticalBody,contacts,probeRender.touch,error,unchanged);return valid;
}
void SavedConditionTick(unsigned frames){
 auto v=Vehicle(savedActor);auto car=v&&v->IsCar()?(CAutomobile*)v:nil;
 if(phase==12){
  car=new CAutomobile(130,MISSION_VEHICLE);if(!car){Receipt("saved native actor creation",false,&savedActor);phase=5;phaseAt=frames;return;}savedActor.ref=CPools::GetVehicleRef(car);car->SetPosition(CVector(-1472,-1190,17));car->SetStatus(STATUS_ABANDONED);car->SetIsStatic(true);car->bUsesCollision=true;car->bIsBIGBuilding=false;car->GetMatrix().UpdateRW();car->UpdateRwFrame();const bool unowned=!VCSourceAircraftBodyOwned(car);
  car->Damage.SetDoorStatus(DOOR_FRONT_LEFT,DOOR_STATUS_SMASHED);car->Damage.SetDoorStatus(DOOR_FRONT_RIGHT,DOOR_STATUS_MISSING);car->Damage.SetPanelStatus(VEHPANEL_FRONT_LEFT,PANEL_STATUS_SMASHED1);car->SetDoorDamage(CAR_DOOR_LF,DOOR_FRONT_LEFT,true);car->SetDoorDamage(CAR_DOOR_RF,DOOR_FRONT_RIGHT,true);car->SetPanelDamage(CAR_WING_LF,VEHPANEL_FRONT_LEFT,true);car->m_aWheelRotation[0]=.6f;car->m_aSuspensionSpringRatioPrev[0]=.72f;car->PreRender();car->OpenDoor(CAR_DOOR_LF,DOOR_FRONT_LEFT,.35f);car->UpdateRwFrame();savedSelection=Selection(car);LogSelection("before-admission",savedSelection);savedActor.mass=car->m_fMass;savedTurnMass=car->m_fTurnMass;savedCOM=car->m_vecCentreOfMass;auto doorPose=FramePose(car->m_aCarNodes[CAR_DOOR_LF]),wheelPose=FramePose(car->m_aCarNodes[CAR_WHEEL_LF]);
  Receipt("saved damage staged before admission",unowned&&SavedVariants(savedSelection)&&std::fabs(doorPose[1])>.1f&&std::fabs(wheelPose[5])>.1f,&savedActor);CWorld::Add(car);phase=13;phaseAt=frames;return;
 }
 if(!car){Receipt("saved native actor lifetime",false,&savedActor);phase=5;phaseAt=frames;return;}
 if(phase==13){
  AircraftBodySnapshot state{};auto selected=Selection(car);LogSelection("admitted",selected);Receipt("saved damaged variants admitted",VCSourceAircraftBodyRead(car,&state)&&state.revision>0&&SameSelection(savedSelection,selected)&&SavedVariants(selected),&savedActor);Sample damaged,missing;bool samples=PartSample(car,"door_lf",ATOMIC_FLAG_DAM,true,damaged)&&PartSample(car,"door_rf",ATOMIC_FLAG_OK,false,missing);Receipt("saved selected material samples",samples,&savedActor);if(samples){SavedQuery(damaged,"saved-damaged-door",true);SavedQuery(missing,"saved-missing-door",false);}
  auto damage=DamageState(car);auto visibility=Selection(car);float health=car->m_fHealth;AircraftBodySnapshot before{},after{};VCSourceAircraftBodyRead(car,&before);car->VehicleDamage(10000,CAR_PIECE_DOOR_LF);bool guard=VCSourceAircraftBodyRead(car,&after)&&before.revision==after.revision&&car->m_fHealth==health&&damage==DamageState(car)&&SameSelection(visibility,Selection(car));Receipt("canonical native damage guard",guard,&savedActor);car->DeleteRwObject();car->CreateRwObject();phase=14;phaseAt=frames;return;
 }
 if(phase==14){
  auto selected=Selection(car);LogSelection("replaced",selected);Receipt("saved condition clump replacement",SameSelection(savedSelection,selected)&&SavedVariants(selected)&&car->Damage.GetDoorStatus(DOOR_FRONT_LEFT)==DOOR_STATUS_SMASHED&&car->Damage.GetDoorStatus(DOOR_FRONT_RIGHT)==DOOR_STATUS_MISSING,&savedActor);Sample damaged,missing;bool samples=PartSample(car,"door_lf",ATOMIC_FLAG_DAM,true,damaged)&&PartSample(car,"door_rf",ATOMIC_FLAG_OK,false,missing);if(samples){SavedQuery(damaged,"saved-replaced-damaged-door",true);SavedQuery(missing,"saved-replaced-missing-door",false);}else Receipt("saved replacement material samples",false,&savedActor);car->OpenDoor(CAR_DOOR_LF,DOOR_FRONT_LEFT,.55f);car->m_aWheelRotation[0]=1.1f;car->PreRender();phase=15;phaseAt=frames;return;
 }
 if(phase==15){savedDoorPose=FramePose(car->m_aCarNodes[CAR_DOOR_LF]);savedWheelPose=FramePose(car->m_aCarNodes[CAR_WHEEL_LF]);Receipt("saved repair starts in live native pose",std::fabs(savedDoorPose[1])>.1f&&std::fabs(savedWheelPose[5])>.1f,&savedActor);VCSourceAircraftBodyRepair(car);phase=16;phaseAt=frames;return;}
 if(phase==16){
  AircraftBodySnapshot state{};auto selected=Selection(car);LogSelection("repaired",selected);bool nativeHealthy=car->Damage.GetDoorStatus(DOOR_FRONT_LEFT)==DOOR_STATUS_OK&&car->Damage.GetDoorStatus(DOOR_FRONT_RIGHT)==DOOR_STATUS_OK&&car->Damage.GetPanelStatus(VEHPANEL_FRONT_LEFT)==PANEL_STATUS_OK;Receipt("explicit saved condition repair",VCSourceAircraftBodyRead(car,&state)&&state.power>.999f&&state.engine==0&&HealthyVariants(selected)&&nativeHealthy,&savedActor);Receipt("saved repair restores native mass and COM",std::fabs(car->m_fMass-savedActor.mass)<.001f&&std::fabs(car->m_fTurnMass-savedTurnMass)<.001f&&(car->m_vecCentreOfMass-savedCOM).Magnitude()<1e-5f,&savedActor);Receipt("saved repair preserves live native poses",SamePose(savedDoorPose,FramePose(car->m_aCarNodes[CAR_DOOR_LF]))&&SamePose(savedWheelPose,FramePose(car->m_aCarNodes[CAR_WHEEL_LF])),&savedActor);Sample left,right;bool samples=PartSample(car,"door_lf",ATOMIC_FLAG_OK,true,left)&&PartSample(car,"door_rf",ATOMIC_FLAG_OK,true,right);Receipt("saved healthy material samples",samples,&savedActor);if(samples){SavedQuery(left,"saved-repaired-left-door",true);SavedQuery(right,"saved-repaired-right-door",true);}phase=5;phaseAt=frames;return;
 }
}
bool Shoot(const Actor &a){auto v=Vehicle(a);if(!v)return false;auto from=v->GetMatrix()*(a.hit.point+a.hit.normal*.03f),to=v->GetMatrix()*(a.hit.point-a.hit.normal*.03f);CColPoint previewPoint;CEntity *preview=nil;bool previewHit=CWorld::ProcessLineOfSight(from,to,previewPoint,preview,true,true,true,true,true,true,false,false);std::fprintf(stderr,"[vehicle-body-smoke] shot world target class=%s hit=%d target=%d victim_model=%d victim_type=%d\n",a.label,previewHit,preview==v,preview?preview->GetModelIndex():-1,preview?preview->GetType():-1);const int budget=CWeaponInfo::GetWeaponInfo(WEAPONTYPE_MINIGUN)->m_nDamage;std::fprintf(stderr,"[vehicle-body-smoke] shot class=%s budget=%d point=%.6f/%.6f/%.6f normal=%.6f/%.6f/%.6f health=%.3f can=%d bulletproof=%d playeronly=%d last_weapon=%d\n",a.label,budget,a.hit.point.x,a.hit.point.y,a.hit.point.z,a.hit.normal.x,a.hit.normal.y,a.hit.normal.z,v->m_fHealth,v->bCanBeDamaged,v->bBulletProof,v->bOnlyDamagedByPlayer,v->m_nLastWeaponDamage);FireOneInstantHitRound(&from,&to,budget);std::fprintf(stderr,"[vehicle-body-smoke] shot native receipt class=%s health=%.3f last_weapon=%d creator=%p\n",a.label,v->m_fHealth,v->m_nLastWeaponDamage,(void*)v->m_pLastDamageEntity);return previewHit&&preview==v;}
void Finish(const char *capture){std::fprintf(stderr,"[vehicle-body-smoke] complete result=%s representatives=%zu stress=%zu query_ms=%.3f\n",all?"PASS":"FAIL",actors.size(),stress.size(),queryMs);RegularGameSmokeCapture(capture);done=true;RsGlobal.quit=TRUE;}
}
void VehicleBodySmokeTick(unsigned frames,const char *capture){
 if(!Enabled()||done)return;
 if(frames==180){for(const auto&a:actors)CStreaming::RequestModel(a.model,STREAMFLAGS_DONT_REMOVE);CStreaming::LoadAllRequestedModels(false);}
 if(!phase&&frames>=200){auto pool=CPools::GetVehiclePool();for(size_t i=0;i<actors.size();++i){auto&a=actors[i];CVehicle *v=nil;if(a.model==181){for(int slot=0;slot<pool->GetSize();++slot){auto candidate=pool->GetSlot(slot);if(candidate&&candidate->IsPlane()&&candidate->GetModelIndex()==181&&DodoRigReady((CPlane*)candidate)){v=candidate;break;}}}else v=Create(a.model,CVector(-1525+float(i%3)*18,-1165+float(i/3)*22,17));if(v){if(a.model==181)Position(v,CVector(-1489,-1143,17));a.ref=CPools::GetVehicleRef(v);a.mass=v->m_fMass;auto col=v->GetColModel();if(col&&col->numLines>0&&col->lines)a.lines.assign(col->lines,col->lines+col->numLines);}Receipt("streamed ordinary actor",v!=nil,&a);}
  auto player=FindPlayerPed();if(player)player->Teleport(CVector(-1540,-1180,15));CClock::SetGameClock(13,0);TheCamera.SetCamPositionForFixedMode(CVector(-1555,-1205,35),CVector(0,0,0));TheCamera.TakeControlNoEntity(CVector(-1505,-1154,17),JUMP_CUT,CAMCONTROL_SCRIPT);phase=1;phaseAt=frames;return;}
 if(!phase||frames<phaseAt+4)return;
 if(phase>=12&&phase<=16){SavedConditionTick(frames);return;}
 if(phase==1){for(auto&a:actors){auto v=Vehicle(a);bool admitted=v&&VCSourceAircraftBodyOwned(v)&&VCSourceAircraftBodyRead(v,&a.intact);Receipt("automatic source admission",admitted,&a);Receipt("geometry before first incident",admitted&&a.intact.revision>0&&v->GetColModel()->numTriangles>100,&a);if(!admitted)continue;float fraction=1;Vec3 normal{};VCSourceAircraftBodyTrace(v,{1000,1000,1000},{1001,1000,1000},0,&fraction,&normal);}
 phase=10;phaseAt=frames;return;}
 if(phase==10){for(auto&a:actors){auto v=Vehicle(a);if(!v)continue;VCSourceAircraftBodyRead(v,&a.intact);Receipt("actual source sample",Select(v,a.hit,&a.retained),&a);Query(a,a.hit,"intact",1);Query(a,a.retained,"intact-retained",1);}auto guarded=Vehicle(actors[0]);if(guarded){protectedOriginal=guarded->bBulletProof;protectedHealth=guarded->m_fHealth;AircraftBodySnapshot s{};VCSourceAircraftBodyRead(guarded,&s);protectedRevision=s.revision;guarded->bBulletProof=true;protectedTargeted=Shoot(actors[0]);}phase=11;phaseAt=frames;return;}
 if(phase==11){auto guarded=Vehicle(actors[0]);AircraftBodySnapshot s{};Receipt("native bulletproof mission guard",protectedTargeted&&guarded&&VCSourceAircraftBodyRead(guarded,&s)&&s.revision==protectedRevision&&guarded->m_fHealth==protectedHealth);if(guarded)guarded->bBulletProof=protectedOriginal;for(const auto&a:actors)Shoot(a);phase=2;phaseAt=frames;return;}
 if(phase==2){for(auto&a:actors){auto v=Vehicle(a);bool changed=v&&VCSourceAircraftBodyRead(v,&a.damaged)&&a.damaged.revision>a.intact.revision;Receipt("localized native bullet damage",changed,&a);if(v){Query(a,a.hit,"damaged",0);Query(a,a.retained,"damaged-retained",1);Receipt("native suspension preserved",NativeLines(a),&a);v->DeleteRwObject();v->CreateRwObject();}}DodoRigFrame();phase=3;phaseAt=frames;return;}
 if(phase==3){for(auto&a:actors){auto v=Vehicle(a);AircraftBodySnapshot s{};Receipt("damaged clump replacement",v&&VCSourceAircraftBodyRead(v,&s)&&s.revision>=a.damaged.revision&&s.engine==a.damaged.engine&&s.breach==a.damaged.breach,&a);if(v){Query(a,a.hit,"replacement",0);if(a.model==191)((CBike*)v)->Fix();else VCSourceAircraftBodyRepair(v);}}phase=4;phaseAt=frames;return;}
 if(phase==4){for(auto&a:actors){auto v=Vehicle(a);AircraftBodySnapshot s{};Receipt("native repair",v&&VCSourceAircraftBodyRead(v,&s)&&s.engine==0&&s.breach==0&&s.power>.999f&&std::fabs(v->m_fMass-a.mass)<.01f&&NativeLines(a),&a);if(v)Query(a,a.hit,"repaired",1);}RegularGameSmokeCapture((std::string(capture)+".repaired.ppm").c_str());auto car=Vehicle(actors[0]);if(car){auto velocity=car->m_vecMoveSpeed;car->ApplyMoveForce(car->m_fMass*.02f,0,0);Receipt("native force remains sole motion owner",!GameplayWingOwns(car)&&std::fabs(car->m_vecMoveSpeed.x-velocity.x-.02f)<1e-6f,&actors[0]);recycledRef=actors[0].ref;AircraftBodySnapshot retiring{};VCSourceAircraftBodyRead(car,&retiring);recycledGeneration=retiring.generation;recycledAddress=car;CWorld::Remove(car);delete car;Receipt("deletion invalidates source identity",!VCSourceAircraftBodyOwned(recycledAddress)&&CPools::GetVehicle(recycledRef)==nil);const int nextHandle=(recycledRef&~255)|((recycledRef+1)&127);auto replacement=Create(130,CVector(-1525,-1165,17),nextHandle);actors[0].ref=replacement?CPools::GetVehicleRef(replacement):-1;Receipt("native pool same address reused",replacement==recycledAddress&&CPools::GetVehicle(recycledRef)==nil);}
  phase=12;phaseAt=frames;return;}
 if(phase==5){AircraftBodySnapshot fresh{};auto replacement=Vehicle(actors[0]);Receipt("reused pool has pristine source",replacement&&VCSourceAircraftBodyRead(replacement,&fresh)&&fresh.power==1&&fresh.engine==0&&fresh.breach==0&&fresh.detachedBodies==0&&fresh.generation!=recycledGeneration);for(int i=0;i<18;++i){auto v=Create(130,CVector(-1540+float(i%6)*10,-1220-float(i/6)*10,17));if(!v)continue;stress.push_back(CPools::GetVehicleRef(v));Sample sample;if(Select(v,sample)){auto from=v->GetMatrix()*(sample.point+sample.normal*.03f),to=v->GetMatrix()*(sample.point-sample.normal*.03f);FireOneInstantHitRound(&from,&to,200);}}phase=6;phaseAt=frames;return;}
 if(phase==6){unsigned active=0;for(auto ref:stress){auto v=CPools::GetVehicle(ref);AircraftBodySnapshot s{};if(v&&VCSourceAircraftBodyRead(v,&s)&&s.revision>0&&v->GetColModel()->numTriangles>100)++active;}Receipt("many actors materialized",stress.size()==18&&active==18);std::fprintf(stderr,"[vehicle-body-smoke] stress active=%u representatives=%zu frame=%u\n",active,actors.size(),frames);auto car=Vehicle(actors[0]);if(car){float fraction=1;Vec3 normal{};VCSourceAircraftBodyTrace(car,{1000,1000,1000},{1001,1000,1000},0,&fraction,&normal);animationOrigin=car->GetPosition();animationSpin=((CAutomobile*)car)->m_aWheelRotation[0];car->SetIsStatic(false);car->AddToMovingList();car->SetStatus(STATUS_PHYSICS);car->m_vecMoveSpeed=CVector(0,.08f,0);car->bEngineOn=true;}phase=7;phaseAt=frames;return;}
 if(phase==7&&frames>phaseAt+8&&frames%8==0){auto car=Vehicle(actors[0]);Sample wheel;bool valid=car&&Select(car,wheel,nil,true);if(valid)valid=Query(actors[0],wheel,"animated-wheel",1);animationValid&=valid;++animationQueries;}
 if(phase==7&&frames>=phaseAt+120){auto car=Vehicle(actors[0]);bool moving=car&&(car->GetPosition()-animationOrigin).Magnitude()>.2f&&std::fabs(((CAutomobile*)car)->m_aWheelRotation[0]-animationSpin)>.01f;Receipt("native controller and rotating wheel queries",moving&&animationValid&&animationQueries>=10);unsigned active=0;for(auto ref:stress){auto v=CPools::GetVehicle(ref);AircraftBodySnapshot s{};if(v&&VCSourceAircraftBodyRead(v,&s)&&std::isfinite(v->m_fMass)&&v->m_fMass>0)++active;}Receipt("many actors remain finite",active==18);Finish(capture);}
 if(frames>900){Receipt("bounded ordinary sequence",false);Finish(capture);}
}
// Kerb acceptance (VC_REGULAR_WING=body-curb). A real kerb is found by native
// vertical-line samples; real cars run at it under native motion only. Every
// frame logs motion, ground, lowest committed material and native contacts.
namespace {
struct Kerb {CVector edge,normal,along;float low=0,high=0;float highRun=0;bool found=false;};
struct CurbRun {int model;float speed,angle;bool drop;int repeats;bool detach;};
const std::vector<CurbRun> curbRuns{
 {211,10,0,false,1,false},{211,20,0,false,1,false},{211,30,0,false,1,false},{211,10,35,false,1,false},{211,20,35,false,1,false},{211,30,35,false,1,false},
 {211,10,0,true,1,false},{211,20,0,true,1,false},{211,30,0,true,1,false},{211,12,0,false,4,false},{211,20,0,false,1,true},
 {130,10,0,false,1,false},{130,20,0,false,1,false},{130,30,0,false,1,false},{130,10,35,false,1,false},{130,20,35,false,1,false},{130,30,35,false,1,false},
 {130,10,0,true,1,false},{130,20,0,true,1,false},{130,30,0,true,1,false},{130,12,0,false,4,false},{130,20,0,false,1,true}};
// Site 0: the first street kerb found (often a narrow pavement against a
// facade or wall). Site 1: the kerb with the longest clear raised side.
// Smoke-only replay filter: VC_REGULAR_CURB_RUNS="site:run,..." runs only those.
bool CurbSelected(int site,int run){static const char *f=std::getenv("VC_REGULAR_CURB_RUNS");if(!f||!*f)return true;char key[24];std::snprintf(key,sizeof key,",%d:%d,",site,run);return (std::string(",")+f+",").find(key)!=std::string::npos;}
Kerb kerb;int curbSite=0,curbPhase=0,curbRun=0,curbRepeat=0,curbRef=-1;unsigned curbAt=0;bool curbAll=true,curbDetached=false;float maxUp=0,maxRise=0,maxBelow=-1e9f,startGround=0;bool leftGrid=false,finiteMotion=true;
bool Ground(CVector p,float top,float &z){CColPoint point;CEntity *entity=nil;if(!CWorld::ProcessVerticalLine(CVector(p.x,p.y,top),top-60,point,entity,true,false,false,false,false,false,nil))return false;z=point.point.z;return true;}
bool Flat(CVector from,CVector dir,const std::vector<float> &offsets,float z){for(float k:offsets){float g;if(!Ground(from+dir*k,z+3,g)||std::fabs(g-z)>.05f)return false;}return true;}
// Edge between a lower and higher flat surface along dir; bisection on height.
bool Edge(CVector p,CVector dir,float low,float high,CVector &edge){float a=-.4f,b=.7f;for(int i=0;i<10;++i){const float m=(a+b)*.5f;float g;if(!Ground(p+dir*m,high+3,g))return false;if(g>(low+high)*.5f)b=m;else a=m;}edge=p+dir*((a+b)*.5f);return true;}
bool FindKerb(CVector centre,bool open){
 for(float dy=-40;dy<=40;dy+=1)for(float dx=-40;dx<=40;dx+=1)for(int d=0;d<4;++d){
  const CVector dir=d==0?CVector(1,0,0):d==1?CVector(-1,0,0):d==2?CVector(0,1,0):CVector(0,-1,0),p(centre.x+dx,centre.y+dy,0);float z0,z1;
  if(!Ground(p,60,z0)||z0<1||z0>40||!Ground(p+dir*.3f,60,z1))continue;const float step=z1-z0;if(step<.08f||step>.4f)continue;
  if(!Flat(p,dir,{-.5f,-1,-2,-4,-6,-8,-10,-12},z0)||!Flat(p+dir*.3f,dir,{.3f,.8f,1.5f},z1))continue;
  // A straight kerb: the same step 3 m either side along it.
  const CVector side(-dir.y,dir.x,0);CVector e0,e1,e2;if(!Edge(p,dir,z0,z1,e0)||!Edge(p+side*3,dir,z0,z1,e1)||!Edge(p-side*3,dir,z0,z1,e2))continue;
  CVector along=e1-e2;along.z=0;if(along.Magnitude()<5)continue;along.Normalise();CVector normal(along.y,-along.x,0);if(DotProduct(normal,dir)<0)normal=normal*-1.f;
  float highRun=0;for(float k:{1.f,2.f,3.f,4.f,5.f,6.f,8.f,10.f,12.f,14.f}){if(!Flat(e0+normal*.3f,normal,{k},z1)||!Flat(e0+normal*.3f+along*2.f,normal,{k},z1)||!Flat(e0+normal*.3f-along*2.f,normal,{k},z1))break;highRun=k;}
  if(!kerb.found||highRun>kerb.highRun){kerb.edge=e0;kerb.normal=normal;kerb.along=along;kerb.low=z0;kerb.high=z1;kerb.highRun=highRun;kerb.found=true;}if(!open||highRun>=14)return true;}
 return kerb.found&&kerb.highRun>=(open?8:1);
}
float LowestMaterial(CVehicle *v){float sweep;if(AircraftBodySmokeLowest(v,sweep))return sweep;auto col=v->GetColModel();float low=1e9f;if(!col||col->numTriangles<=0)return v->GetPosition().z;// Stock COLs share vertices: index through each triangle.
 for(int i=0;i<col->numTriangles;++i){const auto &t=col->triangles[i];for(int k:{int(t.a),int(t.b),int(t.c)})low=Min(low,(v->GetMatrix()*col->vertices[k].Get()).z);}return low;}
bool InGrid(CVector p){return p.x>=-2400&&p.x<1600&&p.y>=-2000&&p.y<2000;}
void CurbStart(const CurbRun &run,CVehicle *v){
 const float a=run.angle*3.14159265f/180;CVector forward=run.drop?kerb.normal*-1.f:kerb.normal;forward=CVector(forward.x*std::cos(a)-forward.y*std::sin(a),forward.x*std::sin(a)+forward.y*std::cos(a),0);
 // Mount: start on the road 9 m before the kerb; drop: rear on the high side.
 const CVector start=run.drop?kerb.edge+kerb.normal*Min(kerb.highRun,3.f)-forward*.5f:kerb.edge-forward*9.f;const float ground=run.drop?kerb.high:kerb.low;
 CWorld::Remove(v);v->GetMatrix().SetUnity();v->GetForward()=forward;v->GetRight()=CrossProduct(forward,CVector(0,0,1));v->GetUp()=CVector(0,0,1);v->SetPosition(start+CVector(0,0,ground-start.z+((CAutomobile*)v)->m_fHeightAboveRoad+.05f));
 v->m_vecMoveSpeed=v->m_vecTurnSpeed=v->m_vecMoveFriction=v->m_vecTurnFriction=CVector(0,0,0);v->SetStatus(STATUS_ABANDONED);v->SetIsStatic(false);v->bUsesCollision=true;v->bIsBIGBuilding=false;v->GetMatrix().UpdateRW();v->UpdateRwFrame();CWorld::Add(v);
 startGround=ground;maxUp=0;maxRise=0;maxBelow=-1e9f;leftGrid=false;finiteMotion=true;
}
void CurbFinish(const char *capture){std::fprintf(stderr,"[curb-smoke] complete result=%s\n",curbAll?"PASS":"FAIL");RegularGameSmokeCapture(capture);RsGlobal.quit=TRUE;curbPhase=99;}
}
void VehicleCurbSmokeTick(unsigned frames,const char *capture){
 if(curbPhase==99)return;
 if(frames>12000){std::fprintf(stderr,"[curb-smoke] bounded sequence result=FAIL\n");curbAll=false;CurbFinish(capture);return;}
 if(curbPhase==0&&frames==60){CCarCtrl::CarDensityMultiplier=0;for(int m:{211,130})CStreaming::RequestModel(m,STREAMFLAGS_DONT_REMOVE);CStreaming::LoadAllRequestedModels(false);curbPhase=1;curbAt=frames;return;}
 if(curbPhase==1&&frames>=curbAt+5){
  // Ocean Beach, Washington Beach and Vice Point street blocks.
  static const CVector centres[]={CVector(150,-1290,11),CVector(250,-1100,11),CVector(330,-960,11),CVector(60,-680,11),CVector(440,170,11),CVector(-860,-600,11),CVector(-500,700,11),CVector(-830,1100,11)};static int next=0,site=-1;if(site!=curbSite){site=curbSite;next=0;kerb.found=false;}
  if(next>=int(sizeof(centres)/sizeof(centres[0]))){std::fprintf(stderr,"[curb-smoke] real kerb found result=FAIL\n");curbAll=false;CurbFinish(capture);return;}
  const CVector c=centres[next++];auto player=FindPlayerPed();CStreaming::LoadScene(c);CStreaming::LoadSceneCollision(c);if(player)player->Teleport(c+CVector(0,0,30));
  if(FindKerb(c,curbSite==1)){std::fprintf(stderr,"[curb-smoke] real kerb found result=PASS site=%d edge=%.2f %.2f %.2f normal=%.3f %.3f low=%.3f high=%.3f step=%.3f high_run=%.1f\n",curbSite,kerb.edge.x,kerb.edge.y,kerb.edge.z,kerb.normal.x,kerb.normal.y,kerb.low,kerb.high,kerb.high-kerb.low,kerb.highRun);
   if(player)player->Teleport(kerb.edge+kerb.along*25+kerb.normal*3+CVector(0,0,kerb.high+1-kerb.edge.z));CStreaming::LoadScene(kerb.edge);CStreaming::LoadSceneCollision(kerb.edge);curbPhase=2;curbAt=frames;}
  else curbAt=frames;return;}
 if(curbPhase==2){
  if(curbRun>=int(curbRuns.size())){if(curbSite==0){curbSite=1;curbRun=0;curbPhase=1;}else curbPhase=4;curbAt=frames;return;}
  if(!CurbSelected(curbSite,curbRun)){++curbRun;return;}
 const auto &run=curbRuns[curbRun];if(run.drop&&kerb.highRun<3){std::fprintf(stderr,"[curb-smoke] run=%d model=%d drop skipped high_run=%.1f\n",curbRun,run.model,kerb.highRun);++curbRun;return;}
  auto v=curbRef>=0?CPools::GetVehicle(curbRef):nil;
  // Parked and passing traffic on the approach would turn a kerb run into a car crash.
  if(!v){const CVector a=kerb.edge-kerb.normal*14.f-kerb.along*8.f,b=kerb.edge+kerb.normal*Max(kerb.highRun,3.f)+kerb.along*8.f;CWorld::ClearCarsFromArea(Min(a.x,b.x),Min(a.y,b.y),kerb.low-3,Max(a.x,b.x),Max(a.y,b.y),kerb.high+6);v=Create(run.model,kerb.edge-kerb.normal*9.f+CVector(0,0,2));if(!v){std::fprintf(stderr,"[curb-smoke] run=%d creation result=FAIL\n",curbRun);curbAll=false;++curbRun;return;}curbRef=CPools::GetVehicleRef(v);AircraftBodyTrace(curbRef);curbRepeat=0;curbDetached=false;}
  CurbStart(run,v);curbPhase=3;curbAt=frames;
  std::fprintf(stderr,"[curb-smoke] run=%d start model=%d ref=%d speed=%.0f angle=%.0f kind=%s repeat=%d mass=%.1f height=%.3f\n",curbRun,run.model,curbRef,run.speed,run.angle,run.drop?"drop":"mount",curbRepeat,v->m_fMass,((CAutomobile*)v)->m_fHeightAboveRoad);return;}
 if(curbPhase==3){
  const auto &run=curbRuns[curbRun];auto v=CPools::GetVehicle(curbRef);const unsigned t=frames-curbAt;
  if(!v){std::fprintf(stderr,"[curb-smoke] run=%d vehicle lost result=FAIL\n",curbRun);curbAll=false;curbRef=-1;++curbRun;curbPhase=2;return;}
  if(t==20&&run.detach&&!curbDetached){const auto b=v->GetColModel()->boundingBox;for(float x:{-.6f,0.f,.6f})AircraftBodyAcceptedWeapon(v,WEAPONTYPE_EXPLOSION,40,v->GetMatrix()*CVector(x*b.max.x,b.max.y+.3f,(b.min.z+b.max.z)*.5f));curbDetached=true;}
  if(t<30){if(run.detach&&t==29){AircraftBodySnapshot s{};VCSourceAircraftBodyRead(v,&s);std::fprintf(stderr,"[curb-smoke] run=%d detached bodies=%u mass=%.1f\n",curbRun,s.detachedBodies,v->m_fMass);}return;}// settle on native suspension
  if(t==30){const float a=run.angle*3.14159265f/180;CVector forward=run.drop?kerb.normal*-1.f:kerb.normal;forward=CVector(forward.x*std::cos(a)-forward.y*std::sin(a),forward.x*std::sin(a)+forward.y*std::cos(a),0);v->m_vecMoveSpeed=forward*(run.speed/50);v->SetIsStatic(false);}
  const CVector p=v->GetPosition(),vel=v->m_vecMoveSpeed*50,w=v->m_vecTurnSpeed*50;float ground=-1e9f;const bool hasGround=Ground(p,p.z+3,ground);const float lowest=LowestMaterial(v);const auto box=v->GetColModel()->boundingBox;
  finiteMotion&=std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::isfinite(vel.x)&&std::isfinite(vel.z);leftGrid|=!InGrid(p);maxUp=Max(maxUp,vel.z);maxRise=Max(maxRise,p.z-((CAutomobile*)v)->m_fHeightAboveRoad-startGround);if(hasGround)maxBelow=Max(maxBelow,ground-lowest);
  std::fprintf(stderr,"[curb-smoke] f=%u run=%d t=%u pos=%.3f %.3f %.3f vel=%.3f %.3f %.3f turn=%.3f %.3f %.3f ground=%.3f lowest=%.3f box_z=%.3f..%.3f mass=%.1f\n",frames,curbRun,t,p.x,p.y,p.z,vel.x,vel.y,vel.z,w.x,w.y,w.z,hasGround?ground:-999.f,lowest,box.min.z,box.max.z,v->m_fMass);
  const float travelled=DotProduct(p-kerb.edge,run.drop?kerb.normal*-1.f:kerb.normal);
  if(t>=140||leftGrid||!finiteMotion||travelled>(run.drop?6.f:kerb.highRun>=8?Min(kerb.highRun,14.f)-1:1e9f)){
   const bool launch=maxUp<8&&maxRise<2.5f,through=maxBelow<.3f;const bool ok=launch&&through&&!leftGrid&&finiteMotion;curbAll&=ok;
   std::fprintf(stderr,"[curb-smoke] run=%d model=%d speed=%.0f angle=%.0f kind=%s repeat=%d result=%s no_launch=%s no_fall_through=%s in_grid=%s max_vz=%.2f max_rise=%.2f max_below_ground=%.3f\n",curbRun,run.model,run.speed,run.angle,run.drop?"drop":"mount",curbRepeat,ok?"PASS":"FAIL",launch?"PASS":"FAIL",through?"PASS":"FAIL",leftGrid?"FAIL":"PASS",maxUp,maxRise,maxBelow);
   if(++curbRepeat<run.repeats&&!leftGrid&&finiteMotion){CurbStart(run,v);curbAt=frames;return;}
   AircraftBodyTrace(-1);CWorld::Remove(v);delete v;curbRef=-1;++curbRun;curbPhase=2;}
  return;}
 if(curbPhase==4){
  // Out-of-grid: a body below y=-2000 for a few frames must stay memory-safe.
  static int ref=-1;static unsigned at=0;auto v=ref>=0?CPools::GetVehicle(ref):nil;
  if(ref<0){v=Create(130,CVector(kerb.edge.x,-2100,kerb.high+2));if(v){ref=CPools::GetVehicleRef(v);v->SetIsStatic(false);v->SetStatus(STATUS_ABANDONED);}at=frames;std::fprintf(stderr,"[curb-smoke] out-of-grid placed=%d\n",v!=nil);return;}
  if(frames<at+8)return;const bool alive=v!=nil;if(v){CWorld::Remove(v);delete v;}std::fprintf(stderr,"[curb-smoke] out-of-grid survived result=%s\n",alive?"PASS":"FAIL");curbAll&=alive;curbPhase=5;}
 if(curbPhase==5){
  // Inverted rest: a car on its roof on flat street must not sink, whatever the
  // frame time. Health is held so native upside-down fire cannot end the probe.
  static const int models[]={130,211};static int index=0,ref=-1;static unsigned at=0;static float worst=-1e9f,z0=0,drift=0;
  if(index>=2){CurbFinish(capture);return;}auto v=ref>=0?CPools::GetVehicle(ref):nil;
  if(ref<0){const CVector p=kerb.edge-kerb.normal*9.f+CVector(0,0,kerb.low+1.6f-kerb.edge.z),a=p-CVector(8,8,3),b=p+CVector(8,8,6);CWorld::ClearCarsFromArea(a.x,a.y,a.z,b.x,b.y,b.z);v=Create(models[index],p);if(!v){std::fprintf(stderr,"[curb-smoke] inverted model=%d creation result=FAIL\n",models[index]);curbAll=false;++index;return;}
   CWorld::Remove(v);v->GetMatrix().GetRight()=CVector(-1,0,0);v->GetMatrix().GetForward()=CVector(0,1,0);v->GetMatrix().GetUp()=CVector(0,0,-1);v->SetPosition(p);v->GetMatrix().UpdateRW();v->UpdateRwFrame();v->SetIsStatic(false);v->m_vecMoveSpeed=v->m_vecTurnSpeed=CVector(0,0,0);CWorld::Add(v);ref=CPools::GetVehicleRef(v);AircraftBodyTrace(ref);at=frames;worst=-1e9f;drift=0;return;}
  if(!v){std::fprintf(stderr,"[curb-smoke] inverted model=%d vehicle lost result=FAIL\n",models[index]);curbAll=false;ref=-1;++index;return;}
  v->m_fHealth=1000;const unsigned t=frames-at;const CVector p=v->GetPosition();float ground=-1e9f;Ground(p,p.z+3,ground);const float lowest=LowestMaterial(v);
  if(t%10==0)std::fprintf(stderr,"[curb-smoke] inverted f=%u model=%d t=%u z=%.3f vz=%.3f up_z=%.3f ground=%.3f lowest=%.3f step=%.3f\n",frames,models[index],t,p.z,v->m_vecMoveSpeed.z*50,v->GetUp().z,ground,lowest,CTimer::GetTimeStep());
  if(t==120)z0=p.z;if(t>=120){worst=Max(worst,ground-lowest);drift=p.z-z0;}if(t<300)return;
  const bool upside=v->GetUp().z<-.5f,ok=upside&&worst<=.10f&&drift>-.05f;
  std::fprintf(stderr,"[curb-smoke] inverted model=%d result=%s upside=%d max_below_ground=%.3f drift=%.3f\n",models[index],ok?"PASS":"FAIL",int(upside),worst,drift);curbAll&=ok;AircraftBodyTrace(-1);CWorld::Remove(v);delete v;ref=-1;++index;
 }
}

// body-bump: light strikes (<= 8 m/s) into a kerb, a wall, a pole and parked or
// moving cars, plus repeated light bumps. Every case runs twice from the same
// start: on a material-owned car and on an unowned stock twin (A/B). After each
// strike a throttle probe on open road measures drivability.
namespace {
struct Bump {const char *name;int model;float speed,angle;int repeats,kind;float targetSpeed;int layout;};
enum {kRestStreet,kRestKerb,kKerb,kWall,kPole,kCar};
const std::vector<Bump> bumps{
 {"rest-street",211,0,0,1,kRestStreet,0,0},{"rest-kerb",211,0,0,1,kRestKerb,0,0},{"rest-kerb-suv",130,0,0,1,kRestKerb,0,0},{"rest-straddle",211,0,0,1,kRestKerb,0,1},{"rest-straddle-suv",130,0,0,1,kRestKerb,0,1},
 {"kerb4",211,4,0,1,kKerb,0,0},{"kerb6",211,6,0,1,kKerb,0,0},{"kerb8",211,8,0,1,kKerb,0,0},{"kerb8-oblique",211,8,35,1,kKerb,0,0},{"kerb8-suv",130,8,0,1,kKerb,0,0},{"kerb4-repeat",211,4,0,3,kKerb,0,0},
 {"wall4",211,4,0,1,kWall,0,0},{"wall8",211,8,0,1,kWall,0,0},{"wall4-repeat",211,4,0,3,kWall,0,0},{"pole8",211,8,0,1,kPole,0,0},
 {"car3-front-rear",211,3,0,1,kCar,0,0},{"car5-front-rear",211,5,0,1,kCar,0,0},{"car8-front-rear",211,8,0,1,kCar,0,0},
 {"car5-rear-front",211,5,0,1,kCar,0,1},{"car8-rear-front",211,8,0,1,kCar,0,1},{"car5-side",211,5,0,1,kCar,0,2},{"car8-side",211,8,0,1,kCar,0,2},
 {"car-moving5",211,10,0,1,kCar,5,0},{"car-moving8",211,12,0,1,kCar,4,0},{"car3-repeat",211,3,0,3,kCar,0,0}};
struct Target {bool found=false;CVector point,dir;};
Target wall,pole;int poleRef=-1;CMatrix poleMatrix;
int bumpPhase=0,bumpCase=0,bumpVariant=0,bumpRepeat=0,bumpRef=-1,bumpTarget=-1;unsigned bumpAt=0,probeMs=0,strikeMs=0;bool cruise=false;bool bumpAll=true,bumpLogged=false;
float preSpeed=0,maxDrop=0,keep10=-1,keepHalf=-1,lastH=0,yieldStart=0;unsigned windowMs=0;int contactAt=-1;CVector probeOrigin,strikeStart,windowVel;bool window=false;
CVector Lane(float s){return kerb.edge-kerb.normal*3.5f+kerb.along*s;}
float GroundAt(CVector p,float fallback){float z;return Ground(CVector(p.x,p.y,0),fallback+4,z)?z:fallback;}
float Heading(CVehicle *v){return std::atan2(v->GetForward().y,v->GetForward().x);}
void Place(CVehicle *v,CVector at,CVector forward){forward.z=0;forward.Normalise();const float ground=GroundAt(at,kerb.low);CWorld::Remove(v);v->GetMatrix().SetUnity();v->GetForward()=forward;v->GetRight()=CrossProduct(forward,CVector(0,0,1));v->GetUp()=CVector(0,0,1);v->SetPosition(CVector(at.x,at.y,ground+((CAutomobile*)v)->m_fHeightAboveRoad+.05f));v->m_vecMoveSpeed=v->m_vecTurnSpeed=v->m_vecMoveFriction=v->m_vecTurnFriction=CVector(0,0,0);v->SetStatus(STATUS_ABANDONED);v->SetIsStatic(false);v->bUsesCollision=true;v->bIsBIGBuilding=false;v->GetMatrix().UpdateRW();v->UpdateRwFrame();CWorld::Add(v);}
CVehicle *Spawn(int model,bool stock){auto v=new CAutomobile(model,MISSION_VEHICLE);if(!v)return nil;if(stock)AircraftBodySmokeUnowned(v,true);v->SetPosition(kerb.edge+CVector(0,0,60));v->SetStatus(STATUS_ABANDONED);v->SetIsStatic(true);v->bUsesCollision=true;v->bIsBIGBuilding=false;v->GetMatrix().UpdateRW();v->UpdateRwFrame();CWorld::Add(v);return v;}
void Despawn(int &ref){auto v=ref>=0?CPools::GetVehicle(ref):nil;if(v){CWorld::Remove(v);AircraftBodySmokeUnowned(v,false);delete v;}ref=-1;}
bool FlatPath(CVector a,CVector b,float z){for(float k=0;k<=1.001f;k+=.1f){float g;const CVector p=a+(b-a)*k;if(!Ground(CVector(p.x,p.y,0),z+3,g)||std::fabs(g-z)>.06f)return false;}return true;}
bool FindFace(CVector from,CVector dir,Target &out){CColPoint point;CEntity *entity=nil;const float z=GroundAt(from,kerb.low);const CVector a(from.x,from.y,z+.7f);if(!CWorld::ProcessLineOfSight(a,a+dir*40,point,entity,true,false,false,false,false,false)||!entity||!entity->IsBuilding())return false;const float d=(point.point-a).Magnitude();
 if(d<8||std::fabs(point.normal.z)>.35f||DotProduct(point.normal,dir)>-.6f||!FlatPath(point.point-dir*8,point.point-dir*1.2f,z))return false;out.found=true;out.point=point.point;out.point.z=z;out.dir=dir;return true;}
bool FindPole(){auto pool=CPools::GetObjectPool();if(!pool)return false;for(int i=0;i<pool->GetSize();++i){auto o=pool->GetSlot(i);if(!o||!o->bUsesCollision)continue;auto col=o->GetColModel();if(!col)continue;const CVector size=col->boundingBox.max-col->boundingBox.min;const CVector p=o->GetPosition();if(Max(size.x,size.y)>1.2f||size.z<2.f||(CVector2D(p)-CVector2D(kerb.edge)).Magnitude()>120)continue;
  const float z=GroundAt(p,kerb.low);for(CVector dir:{kerb.along,kerb.along*-1.f,kerb.normal,kerb.normal*-1.f}){const CVector start=p-dir*8;if(!FlatPath(start,p-dir*1.2f,z))continue;CColPoint point;CEntity *entity=nil;const CVector a(start.x,start.y,z+.7f);if(!CWorld::ProcessLineOfSight(a,CVector(p.x,p.y,z+.7f)+dir,point,entity,true,true,false,true,false,false)||entity!=o)continue;
   pole.found=true;pole.point=CVector(p.x,p.y,z);pole.dir=dir;poleRef=CPools::GetObjectRef(o);poleMatrix=o->GetMatrix();std::fprintf(stderr,"[bump-smoke] pole model=%d name=%s at=%.2f %.2f %.2f size=%.2f %.2f %.2f\n",o->GetModelIndex(),CModelInfo::GetModelInfo(o->GetModelIndex())->GetModelName(),p.x,p.y,p.z,size.x,size.y,size.z);return true;}}return false;}
void RestorePole(){auto o=poleRef>=0?CPools::GetObject(poleRef):nil;if(!o)return;CWorld::Remove(o);o->GetMatrix()=poleMatrix;o->m_vecMoveSpeed=o->m_vecTurnSpeed=CVector(0,0,0);o->SetIsStatic(true);o->GetMatrix().UpdateRW();o->UpdateRwFrame();CWorld::Add(o);}
CVector Rotate(CVector d,float degrees){const float a=degrees*3.14159265f/180;return CVector(d.x*std::cos(a)-d.y*std::sin(a),d.x*std::sin(a)+d.y*std::cos(a),0);}
// Start pose and direction of travel (the striker reverses in rear-into-front).
void StrikeStart(const Bump &b,CVehicle *v){
 if(b.kind==kRestStreet)Place(v,Lane(-10),kerb.along);
 else if(b.kind==kRestKerb)Place(v,b.layout==1?kerb.edge:kerb.edge+kerb.normal*6,kerb.along);
 else if(b.kind==kKerb){const CVector f=Rotate(kerb.normal,b.angle);Place(v,kerb.edge-kerb.along*3-f*7,f);}// clear of the palm 10 m in
 else if(b.kind==kWall)Place(v,wall.point-wall.dir*7.5f,wall.dir);
 else if(b.kind==kPole){RestorePole();Place(v,pole.point-pole.dir*7.5f,pole.dir);}
 else{Despawn(bumpTarget);auto t=Spawn(b.model,bumpVariant==1);if(t){bumpTarget=CPools::GetVehicleRef(t);AircraftBodyTraceAlso(bumpTarget);const CVector at=Lane(15);if(b.layout==2)Place(t,at,kerb.normal);else Place(t,at,b.layout==1?kerb.along*-1.f:kerb.along);}
  Place(v,Lane(15)-kerb.along*(b.targetSpeed>0?14.f:9.f),b.layout==1?kerb.along*-1.f:kerb.along);}
 if(b.kind==kRestStreet||b.kind==kRestKerb){v->GetMatrix().GetPosition().z+=.25f;v->SetStatus(STATUS_PHYSICS);v->AutoPilot.m_nTempAction=TEMPACT_WAIT;v->AutoPilot.m_nTimeTempAction=CTimer::GetTimeInMilliseconds()+100000;}
 preSpeed=0;maxDrop=0;keep10=-1;keepHalf=-1;lastH=0;contactAt=-1;bumpLogged=false;window=false;strikeStart=v->GetPosition();float w;int e;AircraftBodySmokeWork(bumpRef,w,e,true);AircraftBodySmokeWork(bumpTarget,w,e,true);
}
CVector Travel(const Bump &b){if(b.kind==kKerb)return Rotate(kerb.normal,b.angle);if(b.kind==kWall)return wall.dir;if(b.kind==kPole)return pole.dir;return kerb.along;}
// Keep streaming and collision resident around the site, with the player clear of every path.
void Anchor(){auto player=FindPlayerPed();const CVector at=kerb.edge+kerb.normal*Min(kerb.highRun-2,8.f)-kerb.along*20;if(player&&(player->GetPosition()-at).Magnitude()>2)player->Teleport(at+CVector(0,0,kerb.high+.5f-at.z));CStreaming::LoadSceneCollision(kerb.edge);CStreaming::LoadSceneCollision(Lane(-25));}
// Approach under cruise until about a metre before contact: strikes land at the stated speed.
bool Cruising(const Bump &b,CVehicle *v){const CVector d=Travel(b);if(b.kind==kCar){auto t=bumpTarget>=0?CPools::GetVehicle(bumpTarget):nil;if(!t)return false;const float gap=(CVector2D(t->GetPosition())-CVector2D(v->GetPosition())).Magnitude();return gap>(b.layout==2?4.4f:5.8f);}const float run=b.kind==kKerb?7.f:7.5f;return DotProduct(v->GetPosition()-strikeStart,d)<run-3.3f;}float probeHeading=0;
void WheelLine(CVehicle *v,char *out,size_t size){auto car=(CAutomobile*)v;auto col=v->GetColModel();int n=0;for(int i=0;i<4&&size_t(n)<size;++i){float g=-999;CVector top=col&&col->numLines>i?v->GetMatrix()*col->lines[i].p0:v->GetPosition();Ground(CVector(top.x,top.y,0),top.z+1,g);n+=std::snprintf(out+n,size-n," w%d=%.3f/%.3f/%.3f",i,car->m_aSuspensionSpringRatio[i],car->m_aSuspensionSpringRatio[i]<1?car->m_aWheelColPoints[i].point.z:-999.f,g);}}
void BumpFinish(const char *capture){std::fprintf(stderr,"[bump-smoke] complete result=%s\n",bumpAll?"PASS":"FAIL");Despawn(bumpRef);Despawn(bumpTarget);bumpPhase=99;RegularGameSmokeCapture(capture);RsGlobal.quit=TRUE;}
}
void VehicleBumpSmokeTick(unsigned frames,const char *capture){
 if(bumpPhase==99)return;
 if(frames>90000){std::fprintf(stderr,"[bump-smoke] bounded sequence result=FAIL\n");bumpAll=false;BumpFinish(capture);return;}
 if(bumpPhase==0&&frames!=60)return;
 if(bumpPhase==0){CCarCtrl::CarDensityMultiplier=0;for(int m:{211,130})CStreaming::RequestModel(m,STREAMFLAGS_DONT_REMOVE);CStreaming::LoadAllRequestedModels(false);bumpPhase=1;bumpAt=frames;return;}
 if(bumpPhase==1&&frames<bumpAt+5)return;
 if(bumpPhase==1){
  static const CVector centres[]={CVector(250,-1100,11),CVector(150,-1290,11),CVector(330,-960,11),CVector(60,-680,11),CVector(440,170,11),CVector(-860,-600,11)};static int next=0;
  if(next>=int(sizeof(centres)/sizeof(centres[0]))){std::fprintf(stderr,"[bump-smoke] sites found result=FAIL\n");bumpAll=false;BumpFinish(capture);return;}
  const CVector c=centres[next++];auto player=FindPlayerPed();CStreaming::LoadScene(c);CStreaming::LoadSceneCollision(c);if(player)player->Teleport(c+CVector(0,0,30));kerb.found=false;
  if(FindKerb(c,true)){if(player)player->Teleport(kerb.edge+kerb.normal*3+CVector(0,0,kerb.high+1-kerb.edge.z));CStreaming::LoadScene(kerb.edge);CStreaming::LoadSceneCollision(kerb.edge);bumpPhase=2;bumpAt=frames;}else bumpAt=frames;return;}
 if(bumpPhase==2&&frames<bumpAt+30)return;
 if(bumpPhase==2){
  wall.found=false;for(float s:{2.f,4.f,6.f,8.f,10.f,12.f})for(float a=0;a<360&&!wall.found;a+=22.5f)FindFace(kerb.edge+kerb.normal*s+kerb.along*10,Rotate(kerb.along,a),wall);
  for(float s:{-20.f,0.f,20.f,40.f})for(float a=0;a<360&&!wall.found;a+=22.5f)FindFace(Lane(s),Rotate(kerb.along,a),wall);
  FindPole();bool lane=true;for(float s=-34;s<=24;s+=2){float g;if(!Ground(Lane(s),kerb.low+3,g)||std::fabs(g-kerb.low)>.12f)lane=false;}
  std::fprintf(stderr,"[bump-smoke] sites result=%s kerb=%.2f %.2f step=%.3f wall=%d %.2f %.2f dir=%.2f %.2f pole=%d lane_clear=%d\n",kerb.found&&wall.found&&lane?"PASS":"FAIL",kerb.edge.x,kerb.edge.y,kerb.high-kerb.low,wall.found,wall.point.x,wall.point.y,wall.dir.x,wall.dir.y,pole.found,lane);
  if(!kerb.found||!lane){bumpAll=false;BumpFinish(capture);return;}bumpPhase=3;return;}
 if(bumpPhase==3){
  if(bumpCase>=int(bumps.size())){BumpFinish(capture);return;}
  const auto &b=bumps[bumpCase];if((b.kind==kWall&&!wall.found)||(b.kind==kPole&&!pole.found)){std::fprintf(stderr,"[bump-smoke] scenario=%s skipped\n",b.name);++bumpCase;return;}
  auto v=bumpRef>=0?CPools::GetVehicle(bumpRef):nil;
  if(!v){v=Spawn(b.model,bumpVariant==1);if(!v){bumpAll=false;++bumpCase;return;}bumpRef=CPools::GetVehicleRef(v);AircraftBodyTrace(bumpRef);bumpRepeat=0;
   auto col=v->GetColModel(),stock=CModelInfo::GetModelInfo(b.model)->GetColModel();std::fprintf(stderr,"[bump-smoke] scenario=%s variant=%s ref=%d owned=%d lines=%d/%d line0=%.3f %.3f %.3f..%.3f stock_line0=%.3f %.3f %.3f..%.3f box_z=%.3f/%.3f sphere=%.3f/%.3f height=%.3f\n",b.name,bumpVariant?"stock":"material",bumpRef,VCSourceAircraftBodyOwned(v),col?col->numLines:-1,stock?stock->numLines:-1,col&&col->numLines?col->lines[0].p0.x:0,col&&col->numLines?col->lines[0].p0.y:0,col&&col->numLines?col->lines[0].p0.z:0,col&&col->numLines?col->lines[0].p1.z:0,stock&&stock->numLines?stock->lines[0].p0.x:0,stock&&stock->numLines?stock->lines[0].p0.y:0,stock&&stock->numLines?stock->lines[0].p0.z:0,stock&&stock->numLines?stock->lines[0].p1.z:0,col?col->boundingBox.min.z:0,stock?stock->boundingBox.min.z:0,col?col->boundingSphere.radius:0,stock?stock->boundingSphere.radius:0,((CAutomobile*)v)->m_fHeightAboveRoad);}
  Anchor();StrikeStart(b,v);bumpPhase=4;bumpAt=frames;return;}
 const auto &b=bumps[bumpCase];auto v=bumpRef>=0?CPools::GetVehicle(bumpRef):nil;
 if(!v){std::fprintf(stderr,"[bump-smoke] scenario=%s vehicle lost result=FAIL\n",b.name);bumpAll=false;Despawn(bumpTarget);bumpRef=-1;++bumpCase;bumpVariant=0;bumpPhase=3;return;}
 const unsigned t=frames-bumpAt;auto car=(CAutomobile*)v;char wheels[256];WheelLine(v,wheels,sizeof wheels);
 if(bumpPhase==4){
  const bool rest=b.kind==kRestStreet||b.kind==kRestKerb;
  if(t>=30&&!rest&&(t==30||cruise)){cruise=Cruising(b,v);if(t==30)strikeMs=CTimer::GetTimeInMilliseconds();const CVector d=Travel(b)*(b.speed/50);v->m_vecMoveSpeed.x=d.x;v->m_vecMoveSpeed.y=d.y;auto target=bumpTarget>=0?CPools::GetVehicle(bumpTarget):nil;if(target&&b.targetSpeed>0){const CVector d=kerb.along*(b.targetSpeed/50);target->m_vecMoveSpeed.x=d.x;target->m_vecMoveSpeed.y=d.y;}}
  const CVector vel=v->m_vecMoveSpeed*50;const float h=CVector2D(vel).Magnitude();float lowest=LowestMaterial(v),ground=GroundAt(v->GetPosition(),kerb.low);
  if(t>30&&!rest&&!cruise){if(!window){window=true;windowVel=vel;windowMs=CTimer::GetTimeInMilliseconds();float m[7];AircraftBodySmokeMomentumTake(bumpRef,m,true);yieldStart=AircraftBodySmokeYieldWork(v);}if(preSpeed==0)preSpeed=h;if(lastH>.5f){const float drop=(lastH-h)/lastH;maxDrop=Max(maxDrop,drop);if(contactAt<0&&drop>.03f)contactAt=int(t);}if(contactAt>=0&&int(t)==contactAt+10)keep10=preSpeed>0?h/preSpeed:0;if(keepHalf<0&&CTimer::GetTimeInMilliseconds()-windowMs>=500)keepHalf=preSpeed>0?h/preSpeed:0;lastH=h;}
  if(t>=29&&(rest?t%10==0:true))std::fprintf(stderr,"[bump] f=%u case=%s variant=%s repeat=%d t=%u pos=%.3f %.3f %.3f vel=%.3f %.3f %.3f h=%.3f up_z=%.3f lowest=%.3f ground=%.3f%s\n",frames,b.name,bumpVariant?"stock":"material",bumpRepeat,t,v->GetPosition().x,v->GetPosition().y,v->GetPosition().z,vel.x,vel.y,vel.z,h,v->GetUp().z,lowest,ground,wheels);
  if(rest?t<110:(t<31||CTimer::GetTimeInMilliseconds()-strikeMs<3500))return;
  {// Momentum over the strike: applied static responses, shift damping (the only
   // velocity sink without an impulse or force) and the remaining force term.
   float m[7];AircraftBodySmokeMomentumTake(bumpRef,m,true);const CVector d=Travel(b),dv=vel-windowVel;const float p0=std::fabs(DotProduct(windowVel,d)),total=DotProduct(dv,d),response=m[0]*d.x+m[1]*d.y,shift=m[3]*d.x+m[4]*d.y;
   const float side=b.kind==kWall?DotProduct(v->GetPosition()-wall.point,wall.dir):b.kind==kPole?DotProduct(v->GetPosition()-pole.point,pole.dir):0;
   std::fprintf(stderr,"[bump-smoke] momentum case=%s variant=%s repeat=%d v0=%.2f dv=%.2f response=%.2f shift=%.2f forces=%.2f yields=%.0f yield_work=%.0f obstacle_side=%.2f\n",b.name,bumpVariant?"stock":"material",bumpRepeat,p0,total,response,shift,total-response-shift,m[6],AircraftBodySmokeYieldWork(v)-yieldStart,side);}
  char modules[512]="-";AircraftBodySmokeModules(v,modules,sizeof modules);float work=0,targetWork=0;int events=0,targetEvents=0;AircraftBodySmokeWork(bumpRef,work,events,true);AircraftBodySmokeWork(bumpTarget,targetWork,targetEvents,true);
  std::fprintf(stderr,"[bump-smoke] strike case=%s variant=%s repeat=%d pre=%.2f keep10=%.2f keep_half=%.2f max_drop=%.2f contact_t=%d lowest_gap=%.3f work=%.0f events=%d target_work=%.0f target_events=%d ratios=%.3f,%.3f,%.3f,%.3f z=%.3f modules %s\n",b.name,bumpVariant?"stock":"material",bumpRepeat,preSpeed,keep10,keepHalf,maxDrop,contactAt,lowest-ground,work,events,targetWork,targetEvents,car->m_aSuspensionSpringRatio[0],car->m_aSuspensionSpringRatio[1],car->m_aSuspensionSpringRatio[2],car->m_aSuspensionSpringRatio[3],v->GetPosition().z,modules);
  // Throttle probe on open road: same pose and input for both variants.
  bumpPhase=6;bumpAt=frames;probeOrigin=v->GetPosition();probeMs=CTimer::GetTimeInMilliseconds();v->bEngineOn=true;v->SetIsStatic(false);v->SetStatus(STATUS_PHYSICS);return;}
 if(bumpPhase==6){
  // In place: the same throttle from where the strike left the car, obstacle still present.
  const unsigned now=CTimer::GetTimeInMilliseconds();v->SetIsStatic(false);
  v->AutoPilot.m_nTempAction=TEMPACT_GOFORWARD;v->AutoPilot.m_nTimeTempAction=now+500;
  const CVector moved=v->GetPosition()-probeOrigin;const float along=DotProduct(moved,v->GetForward()),h=CVector2D(v->m_vecMoveSpeed*50).Magnitude();
  if(t%6==0)std::fprintf(stderr,"[bump-inplace] case=%s variant=%s repeat=%d ms=%u gas=%.2f moved=%.3f rise=%.3f h=%.3f%s\n",b.name,bumpVariant?"stock":"material",bumpRepeat,now-probeMs,v->m_fGasPedal,along,moved.z,h,wheels);
  // Eight metres shows the car drives away; further it meets whatever lies beyond.
  if(now-probeMs<3000&&along<8)return;
  std::fprintf(stderr,"[bump-smoke] inplace case=%s variant=%s repeat=%d moved=%.2f rise=%.3f speed=%.2f\n",b.name,bumpVariant?"stock":"material",bumpRepeat,along,moved.z,h);
  v->SetStatus(STATUS_ABANDONED);v->AutoPilot.m_nTempAction=TEMPACT_NONE;Despawn(bumpTarget);AircraftBodyTraceAlso(-1);Anchor();{const CVector a=Lane(-36)-kerb.normal*4,c=Lane(4)+kerb.normal*3;CWorld::ClearCarsFromArea(Min(a.x,c.x),Min(a.y,c.y),kerb.low-3,Max(a.x,c.x),Max(a.y,c.y),kerb.high+6);CWorld::ClearPedsFromArea(Min(a.x,c.x),Min(a.y,c.y),kerb.low-3,Max(a.x,c.x),Max(a.y,c.y),kerb.high+6);}Place(v,Lane(-32),kerb.along);bumpPhase=5;bumpAt=frames;return;}
 if(bumpPhase==5){
  const unsigned now=CTimer::GetTimeInMilliseconds();
  if(t==30){probeMs=now;probeOrigin=v->GetPosition();probeHeading=Heading(v);v->bEngineOn=true;v->SetStatus(STATUS_PHYSICS);char part[64]="-";AircraftBodySmokeLowestPart(v,part,sizeof part);std::fprintf(stderr,"[bump-probe-part] case=%s variant=%s repeat=%d lowest_part=%s lowest_gap=%.3f\n",b.name,bumpVariant?"stock":"material",bumpRepeat,part,LowestMaterial(v)-GroundAt(v->GetPosition(),kerb.low));}
  if(t>=30){v->AutoPilot.m_nTempAction=TEMPACT_GOFORWARD;v->AutoPilot.m_nTimeTempAction=now+500;}
  // Emergency services and pedestrians keep re-entering the lane; keep it clear.
  if(t%6==0){const CVector a=Lane(-36)-kerb.normal*4,c=Lane(4)+kerb.normal*3;CWorld::ClearCarsFromArea(Min(a.x,c.x),Min(a.y,c.y),kerb.low-3,Max(a.x,c.x),Max(a.y,c.y),kerb.high+6);CWorld::ClearPedsFromArea(Min(a.x,c.x),Min(a.y,c.y),kerb.low-3,Max(a.x,c.x),Max(a.y,c.y),kerb.high+6);}
  AircraftBodySnapshot s{};const bool owned=VCSourceAircraftBodyRead(v,&s);const CVector vel=v->m_vecMoveSpeed*50;const float fwd=DotProduct(vel,kerb.along),lat=DotProduct(v->GetPosition()-probeOrigin,CrossProduct(kerb.along,CVector(0,0,1)));const float lowest=LowestMaterial(v),ground=GroundAt(v->GetPosition(),kerb.low);
  if(t>=30&&t%3==0)std::fprintf(stderr,"[bump-probe] f=%u case=%s variant=%s repeat=%d ms=%u gas=%.2f brake=%.2f gear=%d engine=%d health=%.0f fire=%d fwd=%.3f lat=%.3f wspeed=%.3f,%.3f,%.3f,%.3f power=%.2f drive=%.2f,%.2f,%.2f,%.2f grip=%.2f,%.2f,%.2f,%.2f present=%d%d%d%d lowest_gap=%.3f%s\n",frames,b.name,bumpVariant?"stock":"material",bumpRepeat,t>=30?now-probeMs:0,v->m_fGasPedal,v->m_fBrakePedal,v->m_nCurrentGear,v->bEngineOn,v->m_fHealth,v->m_pCarFire!=nil,fwd,lat,car->m_aWheelSpeed[0],car->m_aWheelSpeed[1],car->m_aWheelSpeed[2],car->m_aWheelSpeed[3],owned?s.power:1.f,s.wheels[0].drive,s.wheels[1].drive,s.wheels[2].drive,s.wheels[3].drive,s.wheels[0].grip,s.wheels[1].grip,s.wheels[2].grip,s.wheels[3].grip,s.wheelPresent[0],s.wheelPresent[1],s.wheelPresent[2],s.wheelPresent[3],lowest-ground,wheels);
  if(t<30||now-probeMs<3000)return;
  float yaw=Heading(v)-probeHeading;if(yaw>3.14159265f)yaw-=6.2831853f;if(yaw<-3.14159265f)yaw+=6.2831853f;
  std::fprintf(stderr,"[bump-smoke] scenario=%s model=%d variant=%s repeat=%d probe_speed=%.2f probe_dist=%.2f lateral=%.2f yaw=%.3f engine=%d power=%.2f drive=%.2f,%.2f,%.2f,%.2f present=%d%d%d%d health=%.0f keep10=%.2f max_drop=%.2f\n",b.name,b.model,bumpVariant?"stock":"material",bumpRepeat,fwd,DotProduct(v->GetPosition()-probeOrigin,kerb.along),lat,yaw/3.f,v->bEngineOn,owned?s.power:1.f,s.wheels[0].drive,s.wheels[1].drive,s.wheels[2].drive,s.wheels[3].drive,s.wheelPresent[0],s.wheelPresent[1],s.wheelPresent[2],s.wheelPresent[3],v->m_fHealth,keep10,maxDrop);
  v->SetStatus(STATUS_ABANDONED);v->AutoPilot.m_nTempAction=TEMPACT_NONE;
  if(++bumpRepeat<b.repeats){Anchor();StrikeStart(b,v);bumpPhase=4;bumpAt=frames;return;}
  AircraftBodyTrace(-1);Despawn(bumpRef);if(++bumpVariant>1){bumpVariant=0;++bumpCase;}bumpPhase=3;return;}
}
