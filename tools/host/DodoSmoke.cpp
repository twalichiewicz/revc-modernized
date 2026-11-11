// Ordinary saved-game acceptance. No effect without the explicit smoke env.
#include "common.h"
#include "main.h"
#include "DodoSmoke.h"
#include "DodoRig.h"
#include "AircraftBody.h"
#include "AircraftBodySmoke.h"
#include "GameplayWing.h"
#include "World.h"
#include "Pools.h"
#include "Pad.h"
#include "Timer.h"
#include "PlayerPed.h"
#include "Plane.h"
#include "ModelInfo.h"
#include "Camera.h"
#include "Clock.h"
#include "skeleton.h"
#include <dlfcn.h>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <string>
void RegularGameSmokeCapture(const char *);
namespace {
int ref=-1;double elapsed=0,airborne=0;CVector start;float taxiSpeed=0,maximumSpeed=0,maximumHeight=0;bool parked=false,rigChecked=false,braked=false,finished=false,cabinCaptured=false;
bool Enabled(){auto value=std::getenv("VC_REGULAR_WING");return value&&(!std::strcmp(value,"dodo")||!std::strcmp(value,"body-ground")||!std::strcmp(value,"body-tail"));}
CPlane *Plane(){auto v=ref>=0?CPools::GetVehicle(ref):nil;return v&&v->GetModelIndex()==181&&v->IsPlane()?(CPlane*)v:nil;}
bool GroundStrike(){auto c=std::getenv("VC_REGULAR_WING");return c&&(!std::strcmp(c,"body-ground")||!std::strcmp(c,"body-tail"));}
RwFrame *Frame(RpClump *clump,const char *name){RwObjectNameAssociation search{name,nil};RwFrameForAllChildren(RpClumpGetFrame(clump),CClumpModelInfo::FindFrameFromNameCB,&search);return search.frame;}
bool Wheels(CPlane *plane,float spin[3],float compression[3],int ground[3]){static auto read=reinterpret_cast<bool(*)(const void*,float*,float*,int*)>(dlsym(RTLD_DEFAULT,"VCSourceDodoWheelState"));return read&&read(plane,spin,compression,ground);}
}
void DodoSmokeInput(){
 if(!Enabled()||finished)return;auto plane=Plane();if(!plane)return;
 elapsed+=std::min(.06f,CTimer::GetTimeStepInSeconds());auto pad=CPad::GetPad(0);pad->NewState.Clear();pad->Mode=0;
 if(elapsed>2&&elapsed<3.8)TheCamera.CarZoomIndicator=0;else if(elapsed>=3.8)TheCamera.CarZoomIndicator=2;
 if(elapsed<2){pad->NewState.Square=255;if(elapsed<1.6){pad->NewState.LeftStickX=64;pad->NewState.LeftStickY=64;pad->NewState.RightShoulder2=255;}}
 else if(elapsed<4)pad->NewState.Cross=128;
 else if(elapsed<6)pad->NewState.Square=255;
 else{pad->NewState.Cross=255;float speed=DotProduct(plane->m_vecMoveSpeed,plane->GetForward())*50;if(GroundStrike()&&!std::getenv("VC_TAIL_AIR"))pad->NewState.LeftStickY=-8;else if(speed>32)pad->NewState.LeftStickY=plane->GetForward().z<.18f?80:0;}
}
void DodoSmokeTick(unsigned frames,const char *capture){
 if(!Enabled()||finished)return;
 if(frames==200){auto pool=CPools::GetVehiclePool();for(int i=0;i<pool->GetSize();++i){auto v=pool->GetSlot(i);if(v&&v->GetModelIndex()==181&&v->IsPlane()&&DodoRigReady((CPlane*)v)){if(GroundStrike()){CWorld::Remove(v);v->GetRight()=CVector(-1,0,0);v->GetForward()=CVector(0,-1,0);v->GetUp()=CVector(0,0,1);v->GetMatrix().UpdateRW();v->UpdateRwFrame();CWorld::Add(v);}auto player=FindPlayerPed();player->m_objective=OBJECTIVE_ENTER_CAR_AS_DRIVER;player->WarpPedIntoCar(v);ref=CPools::GetVehicleRef(v);start=v->GetPosition();CClock::SetGameClock(13,0);std::fprintf(stderr,"[dodo-smoke] boarded original parked aircraft ref=%d z=%.5f\n",ref,start.z);break;}}}
 auto plane=Plane();if(!plane){if(frames>300){std::fprintf(stderr,"[dodo-smoke] missing ordinary roster rig result=FAIL\n");RsGlobal.quit=TRUE;}return;}
 float spin[3]{},compression[3]{};int grounded[3]{};bool wheels=Wheels(plane,spin,compression,grounded);float speed=plane->m_vecMoveSpeed.Magnitude()*50,height=plane->GetPosition().z-start.z;maximumSpeed=std::max(maximumSpeed,speed);maximumHeight=std::max(maximumHeight,height);
 if(!rigChecked&&elapsed>.9&&elapsed<1.6){
  bool valid=DodoRigReady(plane);for(auto name:{"dodo_flap_l","dodo_flap_r","dodo_elevator_l","dodo_elevator_r","dodo_rudder","dodo_yoke","dodo_grip_l","dodo_grip_r"})valid&=Frame(plane->GetClump(),name)!=nil;
  auto flap=Frame(plane->GetClump(),"dodo_flap_l"),elevator=Frame(plane->GetClump(),"dodo_elevator_l"),yoke=Frame(plane->GetClump(),"dodo_yoke"),rudder=Frame(plane->GetClump(),"dodo_rudder");
  if(flap&&elevator&&yoke&&rudder){valid&=std::fabs(((float*)RwFrameGetMatrix(flap))[6])>.05f&&std::fabs(((float*)RwFrameGetMatrix(elevator))[6])>.05f&&std::fabs(((float*)RwFrameGetMatrix(yoke))[2])>.1f&&std::fabs(((float*)RwFrameGetMatrix(rudder))[1])>.05f;}
  auto player=FindPlayerPed();auto seat=Invert(plane->GetMatrix())*player->GetPosition();valid&=(seat-CVector(-.32f,.25f,.05f)).Magnitude()<.01f;
  std::fprintf(stderr,"[dodo-smoke] rig surfaces yoke seat result=%s seat=%.5f %.5f %.5f\n",valid?"PASS":"FAIL",seat.x,seat.y,seat.z);rigChecked=true;
 }
 if(!parked&&elapsed>1.8){
  CColPoint ground;CEntity *entity=nil;auto at=plane->GetPosition();bool floor=CWorld::ProcessVerticalLine(at,at.z-10,ground,entity,true,false,false,false,false,false,nil);float support=at.z-(floor?ground.point.z:0);
  bool valid=wheels&&grounded[0]&&grounded[1]&&grounded[2]&&floor&&std::fabs(support-1.50155f)<.08f&&plane->GetUp().z>.99f&&plane->m_fHealth>990;
  std::fprintf(stderr,"[dodo-smoke] parked tire support result=%s height=%.6f contacts=%d/%d/%d health=%.1f\n",valid?"PASS":"FAIL",support,grounded[0],grounded[1],grounded[2],plane->m_fHealth);parked=true;
  // Exercise the real lifecycle notification without relying on allocator
  // address changes (ASAN quarantine can conceal immediate clump reuse).
  auto clump=plane->GetClump();GameplayWingDeleting(plane);bool invalidated=!DodoRigReady(plane);DodoRigFrame();bool same=invalidated&&DodoRigReady(plane)&&plane->GetClump()==clump;
  std::fprintf(stderr,"[dodo-smoke] same address lifecycle invalidation result=%s\n",same?"PASS":"FAIL");
 }
 if(!cabinCaptured&&elapsed>3){RegularGameSmokeCapture((std::string(capture)+".cockpit.ppm").c_str());cabinCaptured=true;}
 if(elapsed>3.8&&elapsed<4)taxiSpeed=speed;
 if(!braked&&elapsed>5.9){bool valid=taxiSpeed>.2f&&speed<taxiSpeed*.8f;std::fprintf(stderr,"[dodo-smoke] taxi brake result=%s before=%.5f after=%.5f spin=%.5f\n",valid?"PASS":"FAIL",taxiSpeed,speed,spin[0]);braked=true;}
 if(elapsed>6&&height>5&&speed>30&&wheels&&!grounded[0]&&!grounded[1]&&!grounded[2])airborne+=CTimer::GetTimeStepInSeconds();else airborne=0;
 if(frames%30==0)std::fprintf(stderr,"[dodo-smoke] t=%.3f z=%.3f speed=%.3f forward=%.3f pitch=%.3f contacts=%d/%d/%d health=%.1f\n",elapsed,plane->GetPosition().z,speed,DotProduct(plane->m_vecMoveSpeed,plane->GetForward())*50,plane->GetForward().z,grounded[0],grounded[1],grounded[2],plane->m_fHealth);
 if(GroundStrike()){static double struckAt=0;static float peak=0;static uint32_t wheelFaces[3]{};AircraftBodySnapshot body;if(VCSourceAircraftBodyRead(plane,&body)){if(!wheelFaces[0])for(int i=0;i<3;++i)wheelFaces[i]=body.wheelTriangles[i];peak=std::max(peak,std::max(body.tipLeft,body.tipRight));if(peak>.05f&&!struckAt)struckAt=elapsed;
   if((struckAt&&elapsed-struckAt>2)||elapsed>25){bool same=true;for(int i=0;i<3;++i)same&=body.wheelTriangles[i]>0&&body.wheelTriangles[i]==wheelFaces[i];bool valid=struckAt&&same&&maximumSpeed>30&&(body.tipLeft>.02f||body.tipRight>.02f||body.detachedBodies>0);std::fprintf(stderr,"[body-smoke] ground wing strike result=%s peak_tip_m=%.6f current=%.6f/%.6f bodies=%u speed=%.3f\n",valid?"PASS":"FAIL",peak,body.tipLeft,body.tipRight,body.detachedBodies,maximumSpeed);RegularGameSmokeCapture(capture);finished=true;CPad::GetPad(0)->NewState.Clear();if(!strcmp(std::getenv("VC_REGULAR_WING"),"body-tail"))AircraftBodySmokeStartTail(plane,frames,capture);else RsGlobal.quit=TRUE;}}return;}
 if(airborne>=2||elapsed>30||frames>1600){bool valid=airborne>=2&&maximumHeight>5&&maximumSpeed>30&&plane->m_fHealth>900&&FindPlayerPed()->m_fHealth>90;std::fprintf(stderr,"[dodo-smoke] force driven takeoff result=%s max_height=%.5f max_speed=%.5f health=%.1f\n",valid?"PASS":"FAIL",maximumHeight,maximumSpeed,plane->m_fHealth);RegularGameSmokeCapture((std::string(capture)+".airborne.ppm").c_str());RegularGameSmokeCapture(capture);CPad::GetPad(0)->NewState.Clear();finished=true;RsGlobal.quit=TRUE;}
}
