// Interactive source-host geometry publication test. No flight/solver handoff.
#include "common.h"
#include "Plane.h"
#include "World.h"
#include "Pools.h"
#include "Collision.h"
#include "TxdStore.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace winglab {
struct Face{CVector p[3];bool wing;};
std::vector<Face> mesh;
float cut;CVector tip;
bool damaged=false;unsigned revision=0,failures=0;
float Range(float x,float lo,float hi){return std::max(lo,std::min(hi,x));}
rw::Atomic *aircraft=nil,*controlRender=nil;CPlane *actor=nil,*control=nil;
struct Results{bool los,vertical,contact,control;};
void Require(bool ok,const char *name){if(!ok){++failures;std::fprintf(stderr,"LAB FAIL: %s\n",name);}}
bool Kept(const Face &f){return !damaged||!f.wing||(f.p[0].x<cut&&f.p[1].x<cut&&f.p[2].x<cut);}
CVector Mid(const Face &f){return (f.p[0]+f.p[1]+f.p[2])/3.f;}
rw::Atomic *RenderMesh(const std::vector<Face> &source,int selection,rw::RGBA colour){
 unsigned count=0;for(auto &f:source)if(selection!=1||Kept(f))++count;
 auto g=rw::Geometry::create(count*3,count,rw::Geometry::POSITIONS|rw::Geometry::PRELIT|rw::Geometry::MODULATE);
 if(!g)return nil;auto mat=rw::Material::create();mat->color=rw::makeRGBA(255,255,255,255);g->matList.appendMaterial(mat);mat->destroy();
 unsigned j=0;for(auto &f:source)if(selection!=1||Kept(f)){
  CVector n=CrossProduct(f.p[1]-f.p[0],f.p[2]-f.p[0]);float shade=.55f+.45f*std::fabs(n.z/std::max(.001f,n.Magnitude()));
  rw::RGBA c=selection>=0&&f.wing?rw::makeRGBA(255,191,66,255):colour;
  for(int k=0;k<3;++k){g->morphTargets[0].vertices[3*j+k]=rw::makeV3d(f.p[k].x,f.p[k].y,f.p[k].z);g->colors[3*j+k]=rw::makeRGBA(c.red*shade,c.green*shade,c.blue*shade,c.alpha);g->triangles[j].v[k]=3*j+k;}
  g->triangles[j].matId=0;++j;
 }g->calculateBoundingSphere();g->unlock();auto a=rw::Atomic::create();a->setGeometry(g,0);g->destroy();a->setFrame(rw::Frame::create());return a;
}
void Destroy(rw::Atomic *a){if(a){auto f=a->getFrame();a->destroy();f->destroy();}}
CColModel *Collision(){
 unsigned count=0;for(auto &f:mesh)if(f.wing&&Kept(f))++count;auto col=new CColModel;
 col->numTriangles=count;col->vertices=(CompressedVector*)RwMalloc(count*3*sizeof(CompressedVector));col->triangles=(CColTriangle*)RwMalloc(count*sizeof(CColTriangle));
 CVector lo(1e5,1e5,1e5),hi(-1e5,-1e5,-1e5);unsigned j=0;
 for(auto &f:mesh)if(f.wing&&Kept(f)){for(int k=0;k<3;++k){auto p=f.p[k];col->vertices[3*j+k].Set(p.x,p.y,p.z);lo.x=Min(lo.x,p.x);lo.y=Min(lo.y,p.y);lo.z=Min(lo.z,p.z);hi.x=Max(hi.x,p.x);hi.y=Max(hi.y,p.y);hi.z=Max(hi.z,p.z);}col->triangles[j].Set(j*3,j*3+1,j*3+2,0);++j;}
 col->boundingBox.Set(lo,hi);col->boundingSphere.Set((hi-lo).Magnitude()*.5f,(lo+hi)*.5f);return col;
}
void Publish(bool state){
 damaged=state;auto render=RenderMesh(mesh,1,rw::makeRGBA(190,206,225,255));auto col=Collision();
 if(!render||!actor->ReplaceInstanceCollision(col)){Destroy(render);delete col;Require(false,"prepare/commit");return;}
 Destroy(aircraft);aircraft=render;++revision;
 unsigned renderedWing=0;for(auto &f:mesh)if(f.wing&&Kept(f))++renderedWing;
 Require(renderedWing==unsigned(actor->GetColModel()->numTriangles),"render/COL wing revision");
 std::printf("wing-lab revision=%u damaged=%d wing_faces=%u render_faces=%d\n",revision,damaged,renderedWing,aircraft->geometry->numTriangles);
}
Results Probe(){
 CColPoint point;CEntity *hit=nil;CVector start=tip+CVector(0,0,2),end=tip-CVector(0,0,2);
 Results r{};r.los=CWorld::ProcessLineOfSight(start,end,point,hit,false,true,false,false,false,false,false,false);
 hit=nil;r.vertical=CWorld::ProcessVerticalLine(start,end.z,point,hit,false,true,false,false,false,false,nil);
 CPhysical probe;probe.SetType(ENTITY_TYPE_OBJECT);probe.SetIsStatic(true);probe.SetModelIndexNoCreate(1868);probe.SetPosition(tip+CVector(0,0,.005f));
 CColPoint contacts[MAX_COLLISION_POINTS];r.contact=probe.ProcessEntityCollision(actor,contacts)>0;
 hit=nil;r.control=CWorld::ProcessVerticalLine(start+CVector(0,8,0),end.z,point,hit,false,true,false,false,false,false,nil)&&hit==control;
 static unsigned observed=0;if(observed!=revision){observed=revision;std::printf("wing-lab probes revision=%u los=%d vertical=%d contact=%d control=%d tip=%.4f %.4f %.4f\n",revision,r.los,r.vertical,r.contact,r.control,tip.x,tip.y,tip.z);}
 Require(r.los==!damaged&&r.vertical==!damaged&&r.contact==!damaged&&r.control,"authoritative probes");return r;
}
void Text(rw::Charset *font,const char *text,float x,float y,float scale,rw::RGBA colour){
 using V=rw::RWDEVICE::Im2DVertex;std::vector<V> v;std::vector<uint16> ix;float z=rw::im2d::GetNearZ(),rz=1/rw::engine->currentCamera->nearPlane;
 for(const unsigned char *s=(const unsigned char*)text;*s;++s){unsigned c=*s;if(c>=unsigned(font->desc.count))continue;float u=float(c%font->desc.tileWidth)*font->desc.width_internal/font->raster->width,t=float(c/font->desc.tileWidth)*font->desc.height_internal/font->raster->height;
  float du=float(font->desc.width_internal)/font->raster->width,dv=float(font->desc.height_internal)/font->raster->height,w=font->desc.width_internal*scale,h=font->desc.height_internal*scale;unsigned at=v.size();
  for(int k=0;k<4;++k){V q;q.setScreenX(x+(k&1)*w);q.setScreenY(y+(k/2)*h);q.setScreenZ(z);q.setCameraZ(1/rz);q.setRecipCameraZ(rz);q.setColor(colour.red,colour.green,colour.blue,colour.alpha);q.setU(u+(k&1)*du,rz);q.setV(t+(k/2)*dv,rz);v.push_back(q);}
  for(unsigned k:{0u,1u,2u,2u,1u,3u})ix.push_back(at+k);x+=font->desc.width*scale;
 }rw::SetRenderState(rw::ZTESTENABLE,0);rw::SetRenderState(rw::ZWRITEENABLE,0);rw::SetRenderState(rw::CULLMODE,rw::CULLNONE);rw::SetRenderState(rw::VERTEXALPHA,1);rw::SetRenderStatePtr(rw::TEXTURERASTER,font->raster);rw::SetRenderState(rw::TEXTUREFILTER,rw::Texture::NEAREST);
 if(!v.empty())rw::im2d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST,v.data(),v.size(),ix.data(),ix.size());
}
void Capture(const char *path,int w,int h){
 std::vector<unsigned char> pixels(size_t(w)*h*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());FILE *out=std::fopen(path,"wb");if(!out)return;std::fprintf(out,"P6\n%d %d\n255\n",w,h);for(int y=h-1;y>=0;--y)std::fwrite(pixels.data()+size_t(y)*w*3,1,size_t(w)*3,out);std::fclose(out);
}
}

int WingLab(const char *path,int smokeFrames,const char *capture){
 using namespace winglab;std::setvbuf(stdout,nil,_IONBF,0);FILE *in=std::fopen(path,"r");unsigned count=0;if(!in||std::fscanf(in,"%u",&count)!=1||count<10||count>10000)return 2;
 mesh.resize(count);float lo=1e5,hi=-1e5;for(auto &f:mesh){int wing=0;if(std::fscanf(in,"%d",&wing)!=1)return 2;f.wing=wing!=0;for(auto &p:f.p){if(std::fscanf(in,"%f %f %f",&p.x,&p.y,&p.z)!=3||!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return 2;if(f.wing){lo=Min(lo,p.x);hi=Max(hi,p.x);}}}std::fclose(in);cut=lo+.96f*(hi-lo);float outer=-1e5;
 for(auto &f:mesh)if(f.wing){auto n=CrossProduct(f.p[1]-f.p[0],f.p[2]-f.p[0]);auto c=Mid(f);if(n.z>.05f&&c.x>outer){outer=c.x;tip=c;}}
 if(outer<0)return 2;
 RwEngineInit(nil,0,0);rw::EngineOpenParams native={};GLFWwindow *window=nil;native.window=&window;native.width=1280;native.height=800;native.windowtitle="Vice City - Wing / building test";RwEngineOpenParams open;open.displayID=&native;if(!RwEngineOpen(&open))return 2;
 glfwWindowHint(GLFW_VISIBLE,smokeFrames?GLFW_FALSE:GLFW_TRUE);glfwWindowHint(GLFW_RESIZABLE,GLFW_FALSE);if(!RwEngineStart())return 2;glfwSwapInterval(smokeFrames?0:1);
 CPools::Initialise();CTxdStore::Initialise();CCollision::ms_colModelCache.Init(NUMCOLCACHELINKS);CWorld::Initialise();
 auto info=CModelInfo::AddVehicleModel(181);info->SetTexDictionary("wing-lab");info->m_clump=RpClumpCreate();RpClumpSetFrame(info->m_clump,RwFrameCreate());info->m_handlingId=0;info->m_numDoors=1;auto shared=Collision();info->SetColModel(shared,false);
 auto probeCol=new CColModel;probeCol->numSpheres=1;probeCol->spheres=(CColSphere*)RwMalloc(sizeof(CColSphere));probeCol->spheres[0].Set(.03f,CVector(0,0,0),0,0);probeCol->boundingBox.Set(CVector(-.03f,-.03f,-.03f),CVector(.03f,.03f,.03f));probeCol->boundingSphere.Set(.03f,CVector(0,0,0));CModelInfo::AddSimpleModel(1868)->SetColModel(probeCol,false);
 actor=new CPlane(181,1);control=new CPlane(181,1);for(auto p:{actor,control}){p->bIsBIGBuilding=false;p->bUsesCollision=true;p->SetIsStatic(true);}control->SetPosition(CVector(0,8,0));CWorld::Add(actor);CWorld::Add(control);Publish(false);controlRender=RenderMesh(mesh,0,rw::makeRGBA(95,127,148,255));controlRender->getFrame()->matrix.pos=rw::makeV3d(0,8,0);controlRender->getFrame()->updateObjects();
 // A fixed wall places the native sphere contact probe at the marked tip.
 std::vector<Face> wall;CVector corners[4]={{tip.x,tip.y-.7f,0},{tip.x,tip.y+.7f,0},{tip.x,tip.y-.7f,3},{tip.x,tip.y+.7f,3}};
 for(auto indices:{std::vector<int>{0,1,2},std::vector<int>{2,1,3}}){Face f{};for(int k=0;k<3;++k)f.p[k]=corners[indices[k]];wall.push_back(f);}auto wallRender=RenderMesh(wall,-1,rw::makeRGBA(177,106,81,105));
 auto camera=rw::Camera::create();camera->setFrame(rw::Frame::create());int width,height;glfwGetFramebufferSize(window,&width,&height);camera->frameBuffer=rw::Raster::create(width,height,0,rw::Raster::CAMERA);camera->zBuffer=rw::Raster::create(width,height,0,rw::Raster::ZBUFFER);camera->setNearPlane(.1f);camera->setFarPlane(100);camera->setFOV(55,float(width)/height);auto world=rw::World::create();world->addCamera(camera);
 rw::Charset::open();auto white=rw::makeRGBA(238,243,250,255),transparent=rw::makeRGBA(0,0,0,0);auto font=rw::Charset::create(&white,&transparent);
 float yaw=-.9f,elevation=.6f,distance=18;bool keys[GLFW_KEY_LAST+1]{};unsigned frame=0;std::vector<double> timings,queryTimes;bool sawDamage=false,sawRepair=false;
 while(!glfwWindowShouldClose(window)){
  double start=glfwGetTime();glfwPollEvents();auto pressed=[&](int key){bool down=glfwGetKey(window,key)==GLFW_PRESS;bool edge=down&&!keys[key];keys[key]=down;return edge;};
  bool d=pressed(GLFW_KEY_D),r=pressed(GLFW_KEY_R),reset=pressed(GLFW_KEY_SPACE);if(smokeFrames){d=frame==20||frame==60;r=frame==40;reset=frame==80;}
  if(d){Publish(true);sawDamage=true;}if(r||reset){Publish(false);sawRepair=true;}if(reset){yaw=-.9f;elevation=.6f;distance=18;}
  if(glfwGetKey(window,GLFW_KEY_ESCAPE)==GLFW_PRESS)glfwSetWindowShouldClose(window,1);
  yaw+=(glfwGetKey(window,GLFW_KEY_RIGHT)==GLFW_PRESS?1:0)*.018f-(glfwGetKey(window,GLFW_KEY_LEFT)==GLFW_PRESS?1:0)*.018f;
  elevation=Range(elevation+((glfwGetKey(window,GLFW_KEY_UP)==GLFW_PRESS)-(glfwGetKey(window,GLFW_KEY_DOWN)==GLFW_PRESS))*.015f,.1f,1.3f);
  distance=Range(distance+((glfwGetKey(window,GLFW_KEY_MINUS)==GLFW_PRESS)-(glfwGetKey(window,GLFW_KEY_EQUAL)==GLFW_PRESS))*.12f,3.f,28.f);
  double qt=glfwGetTime();auto result=Probe();queryTimes.push_back((glfwGetTime()-qt)*1000);
  auto target=rw::makeV3d(2.4f,1.7f,.7f);auto pos=rw::makeV3d(target.x+distance*std::cos(yaw)*std::cos(elevation),target.y+distance*std::sin(yaw)*std::cos(elevation),target.z+distance*std::sin(elevation));auto at=rw::normalize(rw::sub(target,pos)),right=rw::normalize(rw::cross(rw::makeV3d(0,0,1),at));auto up=rw::cross(at,right);auto f=camera->getFrame();f->matrix.right=right;f->matrix.up=up;f->matrix.at=at;f->matrix.pos=pos;f->matrix.optimize();f->updateObjects();
  auto bg=rw::makeRGBA(19,26,37,255);camera->clear(&bg,rw::Camera::CLEARIMAGE|rw::Camera::CLEARZ);camera->beginUpdate();rw::SetRenderState(rw::ZTESTENABLE,1);rw::SetRenderState(rw::ZWRITEENABLE,1);rw::SetRenderState(rw::CULLMODE,rw::CULLNONE);rw::SetRenderState(rw::VERTEXALPHA,0);rw::SetRenderStatePtr(rw::TEXTURERASTER,nil);aircraft->render();controlRender->render();rw::SetRenderState(rw::VERTEXALPHA,1);wallRender->render();
  float scale=float(width)/1280*1.6f;Text(font,"VICE CITY / CONTROLLED WING TEST",24*width/1280,22*height/800,scale,white);
  Text(font,"D  damage tip   R  repair   SPACE  reset   ARROWS  orbit   +/-  zoom   ESC  exit",24*width/1280,55*height/800,scale*.8f,white);
  char status[180];std::snprintf(status,sizeof status,"Wing: %s | contact %s | LOS %s | vertical %s | control %s",damaged?"DAMAGED":"INTACT",result.contact?"HIT":"CLEAR",result.los?"HIT":"CLEAR",result.vertical?"HIT":"CLEAR",result.control?"INTACT":"ERROR");Text(font,status,24*width/1280,100*height/800,scale,damaged?rw::makeRGBA(255,195,87,255):rw::makeRGBA(116,234,182,255));
  Text(font,"Yellow: queried wing. Wall marks the contact probe. Rear aircraft is the untouched control.",24*width/1280,(height-72),scale*.7f,white);Text(font,"Geometry publication test / damage preset. Flight dynamics and impact fracture are not enabled.",24*width/1280,(height-38),scale*.7f,white);
  if(capture&&smokeFrames&&frame==30)Capture(capture,width,height);
  if(capture&&smokeFrames&&frame==10)Capture((std::string(capture)+".intact.ppm").c_str(),width,height);
  if(capture&&smokeFrames&&frame==50)Capture((std::string(capture)+".repaired.ppm").c_str(),width,height);
  camera->endUpdate();camera->showRaster(0);timings.push_back((glfwGetTime()-start)*1000);++frame;if(smokeFrames&&frame>=unsigned(smokeFrames))break;
 }
 if(smokeFrames)Require(sawDamage&&sawRepair&&revision>=5,"scripted damage/repair/reset");
 auto percentile=[](std::vector<double> values,double q){std::sort(values.begin(),values.end());return values.empty()?0:values[size_t((values.size()-1)*q)];};std::printf("wing-lab frames=%u failures=%u frame_p50_ms=%.4f frame_p95_ms=%.4f query_p95_ms=%.4f\n",frame,failures,percentile(timings,.5),percentile(timings,.95),percentile(queryTimes,.95));
 font->destroy();rw::Charset::close();Destroy(aircraft);Destroy(controlRender);Destroy(wallRender);world->removeCamera(camera);camera->frameBuffer->destroy();camera->zBuffer->destroy();auto cf=camera->getFrame();camera->destroy();cf->destroy();world->destroy();
 CWorld::Remove(actor);CWorld::Remove(control);delete actor;delete control;RpClumpDestroy(info->m_clump);info->m_clump=nil;delete shared;delete probeCol;CCollision::ms_colModelCache.Shutdown();CPools::ShutDown();CTxdStore::Shutdown();RwEngineStop();RwEngineClose();RwEngineTerm();return failures?1:0;
}
