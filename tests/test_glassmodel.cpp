#include "glassmodel.h"
#include <cstdio>
#include <limits>
using namespace glassmodel;
int main()
{
 int fail=0;
 auto check=[&](bool ok,const char *s){if(!ok){std::printf("FAIL %s\n",s);++fail;}};
 check(Classify("door_lf_hi_ok",{-.8f,.5f,.5f},{1,0,0},160)==Pane::LeftFront,"left front glass");
 check(Classify("door_rf_hi_dam",{.8f,.5f,.5f},{-1,0,0},160)==Pane::RightFront,"right front damaged variant");
 check(Classify("door_lr_hi_ok",{-.8f,-.5f,.5f},{1,0,0},160)==Pane::LeftRear,"rear door");
 check(Classify("windscreen_hi_ok",{0,1,.5f},{0,-1,1},160)==Pane::Front,"native windscreen");
 check(Classify("chassis_hi",{0,-1,.5f},{0,-1,1},160)==Pane::Rear,"rear window in chassis");
 check(Classify("chassis_hi",{-.8f,-.5f,.5f},{1,0,0},160)==Pane::LeftRear,"fixed rear quarter glass");
 check(Classify("door_lf_hi_ok",{-.8f,.5f,.5f},{1,0,0},255)==Pane::None,"opaque door stays");
 check(Classify("chassis_hi",{0,2,-.2f},{0,1,0},160)==Pane::None,"low translucent lamp stays");
 check(Classify("chassis_vlo",{0,-1,.5f},{0,1,0},160)==Pane::None,"LOD untouched");
 check(Classify("bump_rear_hi_ok",{0,-1,.5f},{0,1,0},160)==Pane::None,"translucent bumper stays");
 check(Classify("chassis_hi",{0,0,.5f},{0,0,1},160)==Pane::None,"horizontal sunroof stays");
 check(Classify("chassis_hi",{-.375f,-1.515f,.621f},{-.007f,-.046f,.240f},102)==Pane::Rear,"actual shallow Deluxo rear glass");
 check(Classify("chassis_hi",{-.193f,-2.207f,.474f},{0,-.162f,.769f},102)==Pane::Rear,"actual lower Deluxo rear glass");
 check(Classify("chassis_hi",{0,-1,.5f},{0,.05f,1},160)==Pane::None,"nearly horizontal roof remains excluded");
 check(Classify("chassis_hi",{0,-1,.5f},{0,0,0},160)==Pane::None,"degenerate triangle");
 check(Classify(nullptr,{0,-1,.5f},{0,1,0},160)==Pane::None,"missing frame");
 check(Classify("chassis_hi",{0,-1,.5f},{0,1,0},0)==Pane::None,"invisible material");
 check(Classify("chassis_hi",{0,std::numeric_limits<float>::quiet_NaN(),.5f},{0,1,0},160)==Pane::None,"nonfinite geometry");
 check(Classify("chassis_hi",{1.211637f,-2,1.306666f},{.504f,0,0},252,"ambudecals128")==Pane::None,"actual ambulance side decal");
 check(Classify("chassis_hi",{-1.170454f,-.535169f,.244398f},{-.318635f,0,0},247,"vcpoldecals128")==Pane::None,"actual Enforcer police decal");
 check(Classify("chassis_hi",{-.620671f,2.460652f,.109806f},{.013817f,.005679f,.007714f},152)==Pane::None,"actual untextured Infernus headlamp");
 check(Classify("door_rf_hi_ok",{.788204f,.102335f,.441131f},{.042002f,0,.023485f},242,"white128a")==Pane::RightFront,"actual tinted Stallion window");
 check(Classify("boot_hi_ok",{-.557994f,-2.879514f,.698888f},{-.006295f,-.315600f,.122418f},232)==Pane::Rear,"actual tinted FBI Rancher rear glass");
 check(DoorPane(2)==Pane::LeftFront&&DoorPane(3)==Pane::RightFront&&DoorPane(4)==Pane::LeftRear&&DoorPane(5)==Pane::RightRear,"native door ids");
 check(DoorPane(0)==Pane::None&&DoorPane(1)==Pane::Rear&&DoorPane(6)==Pane::None,"bonnet/boot and invalid door ids");
 std::printf("glassmodel: %s\n",fail?"FAILED":"all passed");return fail?1:0;
}
