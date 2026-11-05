// Linked into the complete source host. Invoked before window/game startup.
// Actual CPlane instances, engine sectors and compiled query/contact routines;
// no copied machine code, solver, flight step or render publication.
#include "common.h"
#include "Plane.h"
#include "Physical.h"
#include "World.h"
#include "Pools.h"
#include "Collision.h"
#include "TxdStore.h"
#include <cstdio>
#include <vector>
#include <cmath>

namespace {
struct Face { CVector p[3]; };
std::vector<Face> faces;
float cut;
unsigned checks,failures;
void Check(bool ok,const char *name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
void Publish(CEntity *e,CColModel *col) {
#ifdef HOST_WING_QUERY_BASELINE
 // The original model-only API is the deliberate isolation-failure control.
 CModelInfo::GetModelInfo(e->GetModelIndex())->SetColModel(col,false);
#else
 e->ReplaceInstanceCollision(col);
#endif
}
bool Keep(const Face &f){return f.p[0].x<cut&&f.p[1].x<cut&&f.p[2].x<cut;}
CVector Centre(const Face &f){return (f.p[0]+f.p[1]+f.p[2])/3.f;}
CColModel *Geometry(bool damaged){
 CColModel *col=new CColModel;unsigned n=0;for(unsigned i=0;i<faces.size();++i)if(!damaged||Keep(faces[i]))++n;
 col->numTriangles=n;col->vertices=(CompressedVector*)RwMalloc(n*3*sizeof(CompressedVector));
 col->triangles=(CColTriangle*)RwMalloc(n*sizeof(CColTriangle));
 CVector lo(1e6f,1e6f,1e6f),hi(-1e6f,-1e6f,-1e6f);unsigned j=0;
 for(unsigned i=0;i<faces.size();++i)if(!damaged||Keep(faces[i])){
  for(int k=0;k<3;++k){CVector p=faces[i].p[k];col->vertices[j*3+k].Set(p.x,p.y,p.z);p=col->vertices[j*3+k].Get();
   lo.x=Min(lo.x,p.x);lo.y=Min(lo.y,p.y);lo.z=Min(lo.z,p.z);hi.x=Max(hi.x,p.x);hi.y=Max(hi.y,p.y);hi.z=Max(hi.z,p.z);}
  col->triangles[j].Set(j*3,j*3+1,j*3+2,0);++j;
 }
 col->boundingBox.Set(lo,hi);col->boundingSphere.Set((hi-lo).Magnitude()*.5f,(lo+hi)*.5f);
 return col;
}
void Query(CPlane *plane,CVector local,bool expected,const char *stage){
 const CVector point=plane->GetMatrix()*local;
 const CVector start=point+CVector(0,0,2),end=point-CVector(0,0,2);
 CColPoint hit;CEntity *entity=nil;
 const bool los=CWorld::ProcessLineOfSight(start,end,hit,entity,false,true,false,false,false,false,false,false);
 Check(los==expected&&(!los||entity==plane),stage);
 entity=nil;const bool vertical=CWorld::ProcessVerticalLine(start,end.z,hit,entity,false,true,false,false,false,false,nil);
 Check(vertical==expected&&(!vertical||entity==plane),stage);
 const bool clear=CWorld::GetIsLineOfSightClear(start,end,false,true,false,false,false,false,false);
 Check(clear!=expected,stage);
 CPhysical probe;probe.SetType(ENTITY_TYPE_OBJECT);probe.SetIsStatic(true);probe.SetModelIndexNoCreate(1868);
 probe.GetMatrix().SetUnity();probe.SetPosition(point+CVector(0,0,.005f));
 CColPoint contacts[MAX_COLLISION_POINTS];const int count=probe.ProcessEntityCollision(plane,contacts);
 Check((count>0)==expected,stage);
 std::printf("wing-query stage=%s expected=%d los=%d vertical=%d clear=%d contacts=%d\n",stage,expected,los,vertical,clear,count);
}
}
int HostWingQueryTest(const char *file){
 std::setvbuf(stdout,nil,_IONBF,0);
 FILE *in=std::fopen(file,"r");if(!in)return 2;unsigned count=0;
 if(std::fscanf(in,"%u",&count)!=1||count<2||count>500){std::fclose(in);return 2;}
 faces.resize(count);float lo=1e6f,hi=-1e6f;
 for(unsigned i=0;i<count;++i)for(int k=0;k<3;++k){CVector &p=faces[i].p[k];if(std::fscanf(in,"%f %f %f",&p.x,&p.y,&p.z)!=3||!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)){std::fclose(in);return 2;}lo=Min(lo,p.x);hi=Max(hi,p.x);}
 std::fclose(in);cut=lo+.96f*(hi-lo);int outer=-1,inner=-1;
 for(unsigned i=0;i<count;++i){CVector normal=CrossProduct(faces[i].p[1]-faces[i].p[0],faces[i].p[2]-faces[i].p[0]);
  if(normal.z<=.05f)continue;
  if(Keep(faces[i])&&(inner<0||Centre(faces[i]).x<Centre(faces[inner]).x))inner=i;
  if(outer<0||Centre(faces[i]).x>Centre(faces[outer]).x)outer=i;
 }
 if(inner<0||outer<0||Keep(faces[outer]))return 2;
 RwEngineInit(nil,0,0);
 rw::EngineOpenParams nativeOpen={};GLFWwindow *window=nil;nativeOpen.window=&window;nativeOpen.width=64;nativeOpen.height=64;nativeOpen.windowtitle="Wing query host";
 RwEngineOpenParams open;open.displayID=&nativeOpen;if(!RwEngineOpen(&open))return 2;
 glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);if(!RwEngineStart())return 2;
 CPools::Initialise();CTxdStore::Initialise();CCollision::ms_colModelCache.Init(NUMCOLCACHELINKS);CWorld::Initialise();
 CColModel *shared=Geometry(false);CVehicleModelInfo *info=CModelInfo::AddVehicleModel(181);
 info->SetTexDictionary("host-wing");
 info->m_clump=RpClumpCreate();RpClumpSetFrame(info->m_clump,RwFrameCreate());
 info->m_handlingId=0;info->m_numDoors=1;info->SetColModel(shared,false);
 CColModel *probeCol=new CColModel;probeCol->numSpheres=1;probeCol->spheres=(CColSphere*)RwMalloc(sizeof(CColSphere));
 probeCol->spheres[0].Set(.03f,CVector(0,0,0),0,0);probeCol->boundingBox.Set(CVector(-.03f,-.03f,-.03f),CVector(.03f,.03f,.03f));probeCol->boundingSphere.Set(.03f,CVector(0,0,0));
 CModelInfo::AddSimpleModel(1868)->SetColModel(probeCol,false);
 const CVector tip=Centre(faces[outer]),root=Centre(faces[inner]);
 for(int pose=0;pose<2;++pose){
  CPlane *plane=new CPlane(181,1),*control=new CPlane(181,1);
  plane->bIsBIGBuilding=control->bIsBIGBuilding=false;plane->bUsesCollision=control->bUsesCollision=true;
  plane->SetIsStatic(true);control->SetIsStatic(true);plane->GetMatrix().SetRotateZ(pose*.6f);control->GetMatrix().SetRotateZ(pose*.6f);
  plane->SetPosition(CVector(pose?49.f:0.f,0,10));control->SetPosition(plane->GetPosition()+CVector(30,0,0));
  info->SetColModel(shared,false);CWorld::Add(plane);CWorld::Add(control);
  Query(plane,tip,true,"intact-tip");Query(control,tip,true,"control-intact");
  CColModel *damaged=Geometry(true);Publish(plane,damaged);
  Check(plane->GetColModel()==damaged,"instance geometry pointer published");
  Check(control->GetColModel()==shared,"same-model pointer isolation");
  Query(plane,tip,false,"damaged-tip");Query(plane,root,true,"surviving-root");Query(control,tip,true,"untouched-control");
  // Repair is another complete immutable geometry revision, not a shared edit.
  CColModel *repaired=Geometry(false);Publish(plane,repaired);Query(plane,tip,true,"repaired-tip");
  CWorld::Remove(plane);CWorld::Remove(control);delete plane;delete control;
#ifdef HOST_WING_QUERY_BASELINE
  delete damaged;delete repaired;
#endif
  info->SetColModel(shared,false);
 }
 Check(CPools::GetVehiclePool()->GetNoOfUsedSpaces()==0,"vehicle teardown releases both pool slots");
 RpClumpDestroy(info->m_clump);info->m_clump=nil;
 delete shared;delete probeCol;CCollision::ms_colModelCache.Shutdown();CPools::ShutDown();RwEngineStop();RwEngineClose();RwEngineTerm();
 std::printf("host-wing-query checks=%u failures=%u faces=%u scope=compiled-source-host-queries-no-flight-or-render\n",checks,failures,count);
 return failures?1:0;
}
