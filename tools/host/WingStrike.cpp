// Explicit experimental island: native controllers/World::Process are not run.
#include "LabDraw.h"
#include "WingPhysics.h"
#include "Plane.h"
#include "Object.h"
#include "Building.h"
#include "World.h"
#include "Pools.h"
#include "Collision.h"
#include "TxdStore.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <map>
#include <set>
#include <string>
#include <cstdio>
#include <cmath>
#include <limits.h>
namespace strike {
unsigned failures=0,nativeIntegration=0;
void Check(bool okay,const char *what){if(!okay){++failures;std::fprintf(stderr,"STRIKE FAIL: %s\n",what);}}
struct OwnedPlane:CPlane {
 OwnedPlane():CPlane(181,1){}
 void ProcessControl() override{++nativeIntegration;Check(false,"native control entered owned island");}
 void ProcessCollision() override{++nativeIntegration;Check(false,"native collision integration entered owned island");}
 void ProcessShift() override{++nativeIntegration;Check(false,"native shift entered owned island");}
};
// Only the worker accesses the coordinator. It waits for the host to publish
// each accepted snapshot before beginning another interval. Rendering never
// borrows mutable solver arrays and remains responsive during expensive solves.
class Worker {
 std::string mesh;std::thread thread;std::mutex mutex;std::condition_variable changed;
 bool quit=false,running=false,pending=false;unsigned command=1,publishedCommand=0;double speed=6;bool miss=false,oblique=false;WpState state{};std::string error;
 void Run(){uint64_t session=0;unsigned active=0;
  for(;;){unsigned wanted;double velocity;bool clear,angled,advance;
   {std::unique_lock<std::mutex> lock(mutex);changed.wait(lock,[&]{return quit||active!=command||(!pending&&running);});if(quit)break;wanted=command;velocity=speed;clear=miss;angled=oblique;advance=running;}
   char message[512]{};int result=0;
   if(active!=wanted){if(session)wp_destroy(session);session=0;result=wp_create(mesh.c_str(),velocity,clear,angled,&session,message,sizeof message);active=wanted;}
   else if(advance)result=wp_advance(session,.005,message,sizeof message);
   WpState snapshot{};if(!result&&!wp_snapshot(session,&snapshot)){result=1;std::snprintf(message,sizeof message,"missing accepted snapshot");}
   {std::lock_guard<std::mutex> lock(mutex);if(wanted!=command)continue;if(result){error=message;running=false;pending=false;}else{state=snapshot;publishedCommand=active;pending=true;if(state.time>=.8)running=false;}}
   changed.notify_all();
   // No catch-up integrates a second owner or skips a physical interval.
   if(!result&&advance){std::unique_lock<std::mutex> lock(mutex);changed.wait_for(lock,std::chrono::milliseconds(12),[&]{return quit||command!=active;});}
  }
  if(session)wp_destroy(session);
 }
public:
 explicit Worker(const char *path):mesh(path){thread=std::thread(&Worker::Run,this);}
 ~Worker(){{std::lock_guard<std::mutex> lock(mutex);quit=true;}changed.notify_all();thread.join();Check(wp_live_sessions()==0,"worker teardown");}
 void Configure(double velocity,bool clear,bool angled,bool start){std::lock_guard<std::mutex> lock(mutex);speed=velocity;miss=clear;oblique=angled;running=start;pending=false;error.clear();++command;changed.notify_all();}
 void Toggle(){std::lock_guard<std::mutex> lock(mutex);running=!running;changed.notify_all();}
 bool Take(WpState &snapshot){std::lock_guard<std::mutex> lock(mutex);if(!pending||publishedCommand!=command)return false;snapshot=state;return true;}
 void Ack(uint64_t id,uint64_t revision){std::lock_guard<std::mutex> lock(mutex);if(pending&&state.session==id&&state.revision==revision){pending=false;changed.notify_all();}}
 std::string Failure(){std::lock_guard<std::mutex> lock(mutex);return error;}
 bool Running(){std::lock_guard<std::mutex> lock(mutex);return running;}
};
CVector Vector(const double *p){return CVector(p[0],p[1],p[2]);}
struct Part {CEntity *entity=nil;rw::Atomic *render=nil;};
CColModel *Collider(const std::vector<labdraw::Face> &faces){
 if(faces.empty())return nil;auto col=new CColModel;col->numTriangles=faces.size();col->vertices=(CompressedVector*)RwMalloc(faces.size()*3*sizeof(CompressedVector));col->triangles=(CColTriangle*)RwMalloc(faces.size()*sizeof(CColTriangle));CVector lo(1e10,1e10,1e10),hi(-1e10,-1e10,-1e10);
 for(unsigned i=0;i<faces.size();++i){for(int j=0;j<3;++j){auto p=faces[i].p[j];col->vertices[3*i+j].Set(p.x,p.y,p.z);lo.x=Min(lo.x,p.x);lo.y=Min(lo.y,p.y);lo.z=Min(lo.z,p.z);hi.x=Max(hi.x,p.x);hi.y=Max(hi.y,p.y);hi.z=Max(hi.z,p.z);}col->triangles[i].Set(3*i,3*i+1,3*i+2,0);}col->boundingBox.Set(lo,hi);col->boundingSphere.Set((hi-lo).Magnitude()*.5f,(hi+lo)*.5f);return col;
}
class Host {
 std::map<int,Part> parts;Part wall;rw::Atomic *body=nil;uint64_t session=0,revision=0;
 static void Retire(Part &p){if(p.entity){CWorld::Remove(p.entity);delete p.entity;}labdraw::Destroy(p.render);p={};}
 static CMatrix Pose(const WpState &s){CMatrix pose;pose.SetUnity();pose.GetPosition()=Vector(s.bodyCenter);pose.GetRight()=CVector(s.bodyRotation[0],s.bodyRotation[3],s.bodyRotation[6]);pose.GetForward()=CVector(s.bodyRotation[1],s.bodyRotation[4],s.bodyRotation[7]);pose.GetUp()=CVector(s.bodyRotation[2],s.bodyRotation[5],s.bodyRotation[8]);return pose;}
public:
 explicit Host(rw::Atomic *visual):body(visual){}
 ~Host(){Clear();labdraw::Destroy(body);}
 void Clear(){for(auto &p:parts)Retire(p.second);parts.clear();Retire(wall);session=revision=0;}
 bool Publish(const WpState &s){
  if(!s.session||s.nodes<1||s.nodes>WP_MAX_NODES||s.faces<1||s.faces>WP_MAX_FACES)return false;
  if(s.session<session)return false;
  if(s.session==session&&s.revision<=revision)return false;
  for(double value:s.bodyCenter)if(!std::isfinite(value)||std::fabs(value)>100)return false;
  for(double value:s.bodyRotation)if(!std::isfinite(value))return false;
  bool reset=session!=s.session;
  std::map<int,std::vector<labdraw::Face>> geometry;for(int i=0;i<s.faces;++i){auto &f=s.face[i];labdraw::Face face;face.colour=f.wing?(f.fragment?rw::makeRGBA(255,126,56,255):rw::makeRGBA(255,195,65,255)):rw::makeRGBA(58,89,109,255);for(int k=0;k<3;++k){if(f.node[k]<0||f.node[k]>=s.nodes||s.node[f.node[k]].fragment!=f.fragment)return false;face.p[k]=Vector(s.node[f.node[k]].x);if(!std::isfinite(face.p[k].x+face.p[k].y+face.p[k].z))return false;}geometry[f.fragment].push_back(face);}
  struct Prepared{int id;CMatrix pose;CColModel *col;rw::Atomic *render;};std::vector<Prepared> ready;
  for(auto &g:geometry){CMatrix pose;pose.SetUnity();if(g.first==0)pose=Pose(s);else{CVector center(0,0,0);int n=0;for(int i=0;i<s.nodes;++i)if(s.node[i].fragment==g.first){center+=Vector(s.node[i].x);++n;}pose.GetPosition()=center/float(n);}
   auto local=g.second;for(auto &f:local)for(auto &p:f.p){auto d=p-pose.GetPosition();p=CVector(DotProduct(d,pose.GetRight()),DotProduct(d,pose.GetForward()),DotProduct(d,pose.GetUp()));}
   std::vector<labdraw::Face> visible;for(auto &f:g.second)if(f.colour.red!=58)visible.push_back(f);
   auto col=Collider(local);auto render=labdraw::Mesh(visible);if(!col||(!visible.empty()&&!render)){delete col;labdraw::Destroy(render);for(auto &r:ready){delete r.col;labdraw::Destroy(r.render);}return false;}ready.push_back({g.first,pose,col,render});
  }
  // Main-thread commit between native queries. Every fragment uses its own COL.
  if(reset)Clear();
  for(auto &r:ready){auto &p=parts[r.id];if(!p.entity){if(r.id==0)p.entity=new OwnedPlane;else p.entity=new CObject(1868,false);p.entity->bUsesCollision=true;p.entity->bIsBIGBuilding=false;p.entity->SetIsStatic(true);}else CWorld::Remove(p.entity);
   p.entity->GetMatrix()=r.pose;if(!p.entity->ReplaceInstanceCollision(r.col)){Check(false,"prepared collision publication");return false;}CWorld::Add(p.entity);labdraw::Destroy(p.render);p.render=r.render;
   // Check actual float COL round-trip against the rendered world vertices.
   auto col=p.entity->GetColModel();for(int i=0;i<col->numTriangles;++i)for(int j=0;j<3;++j){auto q=p.entity->GetMatrix()*col->vertices[3*i+j].Get();Check((q-geometry[r.id][i].p[j]).Magnitude()<3e-5f,"render/native vertex agreement");}
  }
  for(auto it=parts.begin();it!=parts.end();)if(!geometry.count(it->first)){Retire(it->second);it=parts.erase(it);}else ++it;
  if(!wall.entity){std::vector<labdraw::Face> triangles;const int indices[12][3]={{0,2,1},{1,2,3},{4,5,6},{5,7,6},{0,1,4},{1,5,4},{2,6,3},{3,6,7},{0,4,2},{2,4,6},{1,3,5},{3,7,5}};for(auto &ids:indices){labdraw::Face f;f.colour=rw::makeRGBA(160,106,82,255);for(int k=0;k<3;++k)f.p[k]=Vector(s.building[ids[k]]);triangles.push_back(f);}wall.entity=new CBuilding;wall.entity->SetModelIndexNoCreate(1869);wall.entity->ReplaceInstanceCollision(Collider(triangles));CWorld::Add(wall.entity);wall.render=labdraw::Mesh(triangles);}
  auto pose=Pose(s);auto frame=body->getFrame();frame->matrix.right=rw::makeV3d(pose.GetRight().x,pose.GetRight().y,pose.GetRight().z);frame->matrix.up=rw::makeV3d(pose.GetForward().x,pose.GetForward().y,pose.GetForward().z);frame->matrix.at=rw::makeV3d(pose.GetUp().x,pose.GetUp().y,pose.GetUp().z);frame->matrix.pos=rw::makeV3d(pose.GetPosition().x,pose.GetPosition().y,pose.GetPosition().z);frame->matrix.optimize();frame->updateObjects();session=s.session;revision=s.revision;return true;
 }
 void Probe(const WpState &s){
  Check(s.session==session&&s.revision==revision,"host snapshot revision");
  for(int i=0;i<s.faces;++i)if(s.face[i].wing){auto &f=s.face[i];CVector a=Vector(s.node[f.node[0]].x),b=Vector(s.node[f.node[1]].x),c=Vector(s.node[f.node[2]].x),center=(a+b+c)/3.f,n=CrossProduct(b-a,c-a);float length=n.Magnitude();if(length<.00001f)continue;n/=length;CColPoint point;CEntity *hit=nil;
   bool los=CWorld::ProcessLineOfSight(center+n*.01f,center-n*.01f,point,hit,false,true,false,true,false,false,false,false);Check(los,"world LOS sees current wing");
   if(std::fabs(n.z)>.2f){hit=nil;bool vertical=CWorld::ProcessVerticalLine(center+CVector(0,0,.01f),center.z-.01f,point,hit,false,true,false,true,false,false,nil);Check(vertical,"vertical sees current wing");}
   CPhysical probe;probe.SetType(ENTITY_TYPE_OBJECT);probe.SetIsStatic(true);probe.SetModelIndexNoCreate(1870);probe.SetPosition(center+n*.003f);CColPoint points[MAX_COLLISION_POINTS];Check(probe.ProcessEntityCollision(parts[f.fragment].entity,points)>0,"native contact sees current wing");
  }
 }
 void Draw(){body->render();for(auto &p:parts)if(p.second.render)p.second.render->render();if(wall.render)wall.render->render();}
};
double Percentile(std::vector<double> v,double q){if(v.empty())return 0;std::sort(v.begin(),v.end());return v[size_t((v.size()-1)*q)];}
}

int WingStrike(const char *path,int smoke,const char *capture){
 using namespace strike;char resolved[PATH_MAX];if(!realpath(path,resolved))return 2;path=resolved;std::setvbuf(stdout,nil,_IONBF,0);FILE *in=std::fopen(path,"r");unsigned count=0;if(!in||std::fscanf(in,"%u",&count)!=1||count>10000)return 2;std::vector<labdraw::Face> mesh;for(unsigned i=0;i<count;++i){int wing;labdraw::Face f;f.colour=rw::makeRGBA(191,210,228,255);if(std::fscanf(in,"%d",&wing)!=1)return 2;for(auto &p:f.p)if(std::fscanf(in,"%f %f %f",&p.x,&p.y,&p.z)!=3)return 2;if(!wing){
   // Other exported LOD atomics contain full-span triangles too. Clip every
   // visual attachment at the physical wing root so no intact overlay remains.
   std::vector<CVector> clipped;for(int k=0;k<3;++k){auto a=f.p[k],b=f.p[(k+1)%3];bool inside=a.x<=2.625f,next=b.x<=2.625f;if(inside)clipped.push_back(a);if(inside!=next)clipped.push_back(a+(b-a)*((2.625f-a.x)/(b.x-a.x)));}
   for(unsigned k=1;k+1<clipped.size();++k){labdraw::Face triangle;triangle.colour=f.colour;triangle.p[0]=clipped[0];triangle.p[1]=clipped[k];triangle.p[2]=clipped[k+1];mesh.push_back(triangle);}
  }}std::fclose(in);
 RwEngineInit(nil,0,0);rw::EngineOpenParams native={};GLFWwindow *window=nil;native.window=&window;native.width=1280;native.height=800;native.windowtitle="Vice City - Aircraft / building strike";RwEngineOpenParams open;open.displayID=&native;if(!RwEngineOpen(&open))return 2;glfwWindowHint(GLFW_VISIBLE,smoke?GLFW_FALSE:GLFW_TRUE);glfwWindowHint(GLFW_RESIZABLE,GLFW_FALSE);if(!RwEngineStart())return 2;glfwSwapInterval(smoke?0:1);
 static_assert(sizeof(CompressedVector)==12,"Moving source host requires float COL vertices");CPools::Initialise();CTxdStore::Initialise();CCollision::ms_colModelCache.Init(NUMCOLCACHELINKS);CWorld::Initialise();
 auto info=CModelInfo::AddVehicleModel(181);info->SetTexDictionary("wing-strike");info->m_clump=RpClumpCreate();RpClumpSetFrame(info->m_clump,RwFrameCreate());info->m_handlingId=0;info->m_numDoors=1;
 auto empty=new CColModel;empty->boundingBox.Set(CVector(-1,-1,-1),CVector(1,1,1));empty->boundingSphere.Set(2,CVector(0,0,0));info->SetColModel(empty,false);CModelInfo::AddSimpleModel(1868)->SetColModel(empty,false);CModelInfo::AddSimpleModel(1869)->SetColModel(empty,false);
 auto sphere=new CColModel;sphere->numSpheres=1;sphere->spheres=(CColSphere*)RwMalloc(sizeof(CColSphere));sphere->spheres[0].Set(.01f,CVector(0,0,0),0,0);sphere->boundingBox.Set(CVector(-.01,-.01,-.01),CVector(.01,.01,.01));sphere->boundingSphere.Set(.01f,CVector(0,0,0));CModelInfo::AddSimpleModel(1870)->SetColModel(sphere,false);
 auto camera=rw::Camera::create();camera->setFrame(rw::Frame::create());int width,height;glfwGetFramebufferSize(window,&width,&height);camera->frameBuffer=rw::Raster::create(width,height,0,rw::Raster::CAMERA);camera->zBuffer=rw::Raster::create(width,height,0,rw::Raster::ZBUFFER);camera->setNearPlane(.1f);camera->setFarPlane(100);camera->setFOV(55,float(width)/height);auto world=rw::World::create();world->addCamera(camera);rw::Charset::open();auto white=rw::makeRGBA(237,243,249,255),clear=rw::makeRGBA(0,0,0,0);auto font=rw::Charset::create(&white,&clear);
 std::vector<double> frames,steps,publications,queries;double start=glfwGetTime();int accepted=0,phase=0,hitFragments=0,hitMounts=0,missMounts=-1,hitContacts=0;double hitSpeed=0,missSpeed=0;bool savedInitial=false,savedDamage=false,finished=false;unsigned drawFrames=0;WpState initialSnapshot{};
 {
 Host host(labdraw::Mesh(mesh));Worker worker(path);WpState state{};bool has=false,miss=false,oblique=false;double speed=6;float yaw=-.9f,elevation=.55f,distance=19;bool keys[GLFW_KEY_LAST+1]{};
 while(!glfwWindowShouldClose(window)&&!finished){double began=glfwGetTime();glfwPollEvents();auto pressed=[&](int key){bool d=glfwGetKey(window,key)==GLFW_PRESS,e=d&&!keys[key];keys[key]=d;return e;};
  if(pressed(GLFW_KEY_ESCAPE))glfwSetWindowShouldClose(window,1);if(pressed(GLFW_KEY_SPACE))worker.Toggle();if(pressed(GLFW_KEY_R))worker.Configure(speed,miss,oblique,false);if(pressed(GLFW_KEY_M)){miss=!miss;worker.Configure(speed,miss,oblique,false);}if(pressed(GLFW_KEY_O)){oblique=!oblique;worker.Configure(speed,miss,oblique,false);}for(int i=0;i<3;++i)if(pressed(GLFW_KEY_1+i)){speed=i==0?3:i==1?6:12;worker.Configure(speed,miss,oblique,false);}
  yaw+=((glfwGetKey(window,GLFW_KEY_RIGHT)==GLFW_PRESS)-(glfwGetKey(window,GLFW_KEY_LEFT)==GLFW_PRESS))*.018f;elevation=std::max(.1f,std::min(1.3f,elevation+((glfwGetKey(window,GLFW_KEY_UP)==GLFW_PRESS)-(glfwGetKey(window,GLFW_KEY_DOWN)==GLFW_PRESS))*.015f));distance=std::max(6.f,std::min(35.f,distance+((glfwGetKey(window,GLFW_KEY_MINUS)==GLFW_PRESS)-(glfwGetKey(window,GLFW_KEY_EQUAL)==GLFW_PRESS))*.15f));
  bool fresh=false;WpState next{};if(worker.Take(next)){fresh=true;double t=glfwGetTime();Check(host.Publish(next),"publish accepted state");publications.push_back((glfwGetTime()-t)*1000);state=next;has=true;t=glfwGetTime();host.Probe(state);queries.push_back((glfwGetTime()-t)*1000);worker.Ack(state.session,state.revision);++accepted;if(state.time>0)steps.push_back(state.stepMs);Check(nativeIntegration==0,"exclusive motion owner");
   if(drawFrames%30==0||state.brokenMounts)std::printf("strike-host session=%llu revision=%llu t=%.4f fragments=%d mounts=%d faces=%d owner=ipc native_steps=%u step_ms=%.3f\n",(unsigned long long)state.session,(unsigned long long)state.revision,state.time,state.fragments,state.brokenMounts,state.faces,nativeIntegration,state.stepMs);
  }
  auto error=worker.Failure();if(!error.empty()&&smoke){Check(false,error.c_str());finished=true;}
  auto target=rw::makeV3d(2.3f,-.3f,3.5f);auto pos=rw::makeV3d(target.x+distance*std::cos(yaw)*std::cos(elevation),target.y+distance*std::sin(yaw)*std::cos(elevation),target.z+distance*std::sin(elevation));auto at=rw::normalize(rw::sub(target,pos)),right=rw::normalize(rw::cross(rw::makeV3d(0,0,1),at)),up=rw::cross(at,right);auto f=camera->getFrame();f->matrix.right=right;f->matrix.up=up;f->matrix.at=at;f->matrix.pos=pos;f->matrix.optimize();f->updateObjects();auto bg=rw::makeRGBA(19,26,37,255);camera->clear(&bg,rw::Camera::CLEARIMAGE|rw::Camera::CLEARZ);camera->beginUpdate();rw::SetRenderState(rw::ZTESTENABLE,1);rw::SetRenderState(rw::ZWRITEENABLE,1);rw::SetRenderState(rw::CULLMODE,rw::CULLNONE);rw::SetRenderState(rw::VERTEXALPHA,0);rw::SetRenderStatePtr(rw::TEXTURERASTER,nil);if(has)host.Draw();
  float scale=float(width)/1280*1.5f;labdraw::Text(font,"AIRCRAFT / BUILDING STRIKE",24,22,scale,white);labdraw::Text(font,"SPACE launch/pause   R reset   M hit/miss   O angle   1/2/3 speed   ARROWS orbit   +/- zoom   ESC exit",24,55,scale*.7f,white);
  char status[256];std::snprintf(status,sizeof status,"%s | %s | %.0f m/s | t %.3fs | speed %.2f | fragments %d | broken mounts %d",worker.Running()?"RUNNING":"PAUSED",miss?"MISS":"WING CLIP",speed,state.time,state.forwardSpeed,state.fragments,state.brokenMounts);labdraw::Text(font,status,24,94,scale*.8f,white);
  std::snprintf(status,sizeof status,"Physics step %.2f ms | contacts %d | native geometry rev %llu | checks %s",state.stepMs,state.contacts,(unsigned long long)state.revision,failures?"FAILED":"PASS");labdraw::Text(font,status,24,127,scale*.75f,rw::makeRGBA(255,196,90,255));
  labdraw::Text(font,"Ballistic test with gravity. Yellow skin and its mounts are solved; orange pieces are independent debris.",24,height-70,scale*.65f,white);labdraw::Text(font,"Synthetic structure, frictionless contact. Slow simulation exposes step cost; engine/lift/free-roam flight remain open.",24,height-38,scale*.65f,white);
  if(!error.empty())labdraw::Text(font,error.c_str(),24,175,scale*.7f,rw::makeRGBA(255,100,100,255));
  if(smoke&&has&&fresh){
   if(phase==0&&!savedInitial&&state.time==0){initialSnapshot=state;if(capture)Check(labdraw::Capture((std::string(capture)+".intact.ppm").c_str(),width,height),"initial capture");savedInitial=true;phase=1;worker.Toggle();}
   else if(phase==1){hitContacts=std::max(hitContacts,state.contacts);hitMounts=std::max(hitMounts,state.brokenMounts);hitFragments=std::max(hitFragments,state.fragments);if(state.fragments>1&&!savedDamage){if(capture)Check(labdraw::Capture((std::string(capture)+".impact.ppm").c_str(),width,height),"strike capture");savedDamage=true;}
    if(state.time>=.8){if(capture)Check(labdraw::Capture(capture,width,height),"separated wing capture");hitSpeed=state.forwardSpeed;phase=2;miss=true;worker.Configure(6,true,false,true);}}
   else if(phase==2&&state.time>=.8){missSpeed=state.forwardSpeed;missMounts=state.brokenMounts;phase=3;miss=false;worker.Configure(6,false,false,false);}
   else if(phase==3&&state.time==0){Check(!host.Publish(initialSnapshot),"revoked generation rejected");auto invalid=state;++invalid.revision;invalid.face[0].node[0]=-1;Check(!host.Publish(invalid),"failed preparation leaves resident geometry intact");host.Probe(state);if(capture)Check(labdraw::Capture((std::string(capture)+".repaired.ppm").c_str(),width,height),"reset capture");finished=true;}
  }
  camera->endUpdate();camera->showRaster(0);frames.push_back((glfwGetTime()-began)*1000);++drawFrames;if(smoke)std::this_thread::sleep_for(std::chrono::milliseconds(2));
 }
 }
 if(smoke){Check(phase==3&&savedDamage&&hitContacts>0&&hitMounts>0&&hitFragments>=2,"force-driven strike smoke");Check(missMounts==0&&std::fabs(missSpeed-6)<1e-6,"clear-miss smoke");}
 std::printf("strike-summary accepted=%d failures=%u fragments=%d mounts=%d hit_speed=%.6f miss_speed=%.6f step_p50_ms=%.3f step_p95_ms=%.3f step_max_ms=%.3f frame_p95_ms=%.3f pub_p95_ms=%.3f query_p95_ms=%.3f wall_s=%.3f native_steps=%u\n",accepted,failures,hitFragments,hitMounts,hitSpeed,missSpeed,Percentile(steps,.5),Percentile(steps,.95),Percentile(steps,1),Percentile(frames,.95),Percentile(publications,.95),Percentile(queries,.95),glfwGetTime()-start,nativeIntegration);
 if(capture&&smoke){FILE *report=std::fopen((std::string(capture)+".json").c_str(),"w");if(report){std::fprintf(report,"{\"kind\":\"wing-strike\",\"failures\":%u,\"accepted\":%d,\"fragments\":%d,\"mounts\":%d,\"contacts\":%d,\"hit_speed\":%.9g,\"miss_speed\":%.9g,\"miss_mounts\":%d,\"step_p95_ms\":%.9g,\"native_steps\":%u}\n",failures,accepted,hitFragments,hitMounts,hitContacts,hitSpeed,missSpeed,missMounts,Percentile(steps,.95),nativeIntegration);std::fclose(report);}else Check(false,"write smoke report");}
 font->destroy();rw::Charset::close();world->removeCamera(camera);camera->frameBuffer->destroy();camera->zBuffer->destroy();auto cf=camera->getFrame();camera->destroy();cf->destroy();world->destroy();RpClumpDestroy(info->m_clump);info->m_clump=nil;delete empty;delete sphere;CCollision::ms_colModelCache.Shutdown();CPools::ShutDown();CTxdStore::Shutdown();RwEngineStop();RwEngineClose();RwEngineTerm();return failures?1:0;
}
