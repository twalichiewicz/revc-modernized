#define main LearjetWheelFixtures
#include "test_jetwheels.cpp"
#undef main
void DodoReset(){Reset();dodoOwner=true;*reinterpret_cast<int16_t *>(v+0x6a)=181;V(v,0x38).z=1.50155f;F(v,0xcc)=2400;F(v,0xd0)=8000;}
int main(){
 LearjetWheelFixtures();
 DodoReset();Step();std::array<jetwheels::WheelPose,3> poses{};
 Check(wheelCalls==3&&jetwheels::Grounded(v)&&jetwheels::Snapshot(v,poses),"Dodo three actual tire contacts own runway support");
 DodoReset();for(int i=0;i<500;++i)Step();
 Check(std::fabs(V(v,0x38).z-1.50155f)<.04f&&std::fabs(V(v,0x84).z)<.01f&&V(v,0x28).z>.998f,"Dodo settles at visible tire plane instead of clipping or tipping");
 DodoReset();gas=1;for(int i=0;i<200;++i)Step();jetwheels::Snapshot(v,poses);
 Check(V(v,0x84).y*50>25&&std::fabs(poses[0].spin)>.1f&&poses[1].grounded,"Dodo tires roll from contact velocity while accelerating");
 const float speed=V(v,0x84).y;gas=0;brake=255;for(int i=0;i<100;++i)Step();
 Check(V(v,0x84).y<speed*.85f&&V(v,0x84).y>=-.001f,"Dodo wheel brakes slow without reversing");
 DodoReset();V(v,0x84).y=.2f;steer=128;for(int i=0;i<10;++i)Step();jetwheels::Snapshot(v,poses);
 Check(poses[0].steer<0&&poses[1].steer==0&&V(v,0x90).z<0,"Dodo nose contact steers while main gear stays straight");
 DodoReset();dodoOwner=false;Step();Check(!queries&&!wheelCalls,"ambient Dodo keeps native path");
 if(!fails)puts("Dodo rolling gear: all passed");return fails?1:0;
}
