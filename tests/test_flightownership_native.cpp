// Native SCM CREATE_CAR -> player's native Automobile caller -> raw keyboard
// CPad getters -> real shared flight controller -> exact CPhysical force ABI.
#define main ExistingFlightChecks
#include "test_flightmodelhook.cpp"
#undef main
namespace {
uint8_t controllerAffected=0,controlSettings[0x1500]{};bool heldKeys[1056]{};
int16_t PadField(uint8_t *p,int offset){return *reinterpret_cast<const int16_t *>(p+offset);}
int NativeMode(uint8_t *p){return controllerAffected==1?PadField(p,0xec):0;}
int16_t KeyboardGas(uint8_t *p){const int mode=NativeMode(p);return PadField(p,0xf0)||mode>3?0:mode==3?std::max(0,-2*PadField(p,6)):PadField(p,0x20);}
int16_t KeyboardBrake(uint8_t *p){const int mode=NativeMode(p);return PadField(p,0xf0)||mode>3?0:mode==3?std::max(0,2*PadField(p,6)):PadField(p,0x1c);}
int16_t KeyboardPitch(uint8_t *p){const int mode=NativeMode(p);if(PadField(p,0xf0)||mode>3)return 0;const int stick=PadField(p,2),dpad=(PadField(p,0x12)-PadField(p,0x10))/2;return mode==1||mode==3||std::abs(stick)>std::abs(dpad)?stick:dpad;}
int16_t KeyboardRoll(uint8_t *p){const int mode=NativeMode(p);if(PadField(p,0xf0)||mode>3)return 0;const int stick=PadField(p,0),dpad=(PadField(p,0x16)-PadField(p,0x14))/2;return mode==1||mode==3||std::abs(stick)>std::abs(dpad)?stick:dpad;}
int16_t KeyboardGunPitch(uint8_t *p){const int mode=NativeMode(p);return PadField(p,0xf0)||mode>3?0:mode==3?(PadField(p,0x10)-PadField(p,0x12))/2:PadField(p,6);}
int16_t KeyboardGunYaw(uint8_t *p){const int mode=NativeMode(p);return PadField(p,0xf0)||mode>3?0:mode==3?(PadField(p,0x16)-PadField(p,0x14))/2:PadField(p,4);}
bool NativeLookLeft(uint8_t *p){return !PadField(p,0xf0)&&PadField(p,0xa)&&!PadField(p,0xe);}
bool NativeLookRight(uint8_t *p){return !PadField(p,0xf0)&&PadField(p,0xe)&&!PadField(p,0xa);}
bool ConfiguredKeyDown(uint8_t *,int key){return key>0&&key<1056&&heldKeys[key];}
void BindFlightKey(int action,int slot,int key){*reinterpret_cast<int32_t *>(controlSettings+0xec0+32*action+8*slot)=key;if(key>0&&key<1056)heldKeys[key]=true;}
void NativeTurn(uint8_t *v,float x,float y,float z,float px,float py,float pz){
 const CVector com=Row(v,0xe4),r=Row(v,8),f=Row(v,0x18),u=Row(v,0x28);
 const float lx=px-r.x*com.x-f.x*com.y-u.x*com.z,ly=py-r.y*com.x-f.y*com.y-u.y*com.z,lz=pz-r.z*com.x-f.z*com.y-u.z*com.z;
 auto &w=Row(v,0x90);const float mass=Scalar(v,0xd0);w.x+=(ly*z-lz*y)/mass;w.y+=(lz*x-lx*z)/mass;w.z+=(lx*y-ly*x)/mass;
}
void NativePlayerCaller(uint8_t *v){
 // CPhysical0x155e70 applies gravity before native FlyingControl. Full heli
 // call0x24c6bc requires status0/3/10, rotor>.15 and an airborne/active craft.
 Row(v,0x84).z-=.008f*step;
 const int status=v[0x60]>>3;
 if(status==0||status==3||status==10){if(Scalar(v,0x5f0)<.22f)Scalar(v,0x5f0)+=.001f;if(Scalar(v,0x5f0)>.15f)fly(v,6);}
 else Scalar(v,0x5f0)=std::max(0.0f,Scalar(v,0x5f0)-.0005f);
 Row(v,0x38)=flightmodel::Add(Row(v,0x38),flightmodel::Mul(Row(v,0x84),step));
}
void Prepare(){
 Reset();vehicles[0][0x294]=2;Row(vehicles[0],0xe4)={0,0,-.1f};
 *reinterpret_cast<int16_t *>(pad+0x20)=255;throttle=255;
 flightmodel::gThrottle=KeyboardGas;flightmodel::gBrake=KeyboardBrake;flightmodel::gPitch=KeyboardPitch;flightmodel::gRoll=KeyboardRoll;
 flightmodel::gGunPitch=KeyboardGunPitch;flightmodel::gGunYaw=KeyboardGunYaw;flightmodel::gTurn=NativeTurn;flightmodel::gControl=NativePlayerCaller;
 std::memset(controlSettings,0,sizeof controlSettings);std::fill(std::begin(heldKeys),std::end(heldKeys),false);controllerAffected=0;
 game::api.controlsManager=controlSettings;game::api.GetIsKeyboardKeyDown=ConfiguredKeyDown;game::api.padAffectedByController=&controllerAffected;
}
}
int main(){
 if(ExistingFlightChecks())return 1;
 Prepare();for(int i=0;i<30*50;++i)automobile(vehicles[0]);
 Check(originalCalls==0&&Row(vehicles[0],0x38).z>2750,"free-roam player SCM-origin helicopter reaches real above-cloud forces through native keyboard/caller boundary");
 Prepare();*reinterpret_cast<int16_t *>(pad)=128;*reinterpret_cast<int16_t *>(pad+2)=-128;automobile(vehicles[0]);
 Check(Row(vehicles[0],0x90).x<0&&Row(vehicles[0],0x90).y>0,"raw native keyboard cyclic survives COM subtraction and genuine force ABI");
 // Replay the user's actual simultaneous pilot/camera axes: merged pad's
 // leftY126 is pull-up, rightY42/rightX43 are camera look, Cross255 collective.
 Prepare();*reinterpret_cast<int16_t *>(pad)=21;*reinterpret_cast<int16_t *>(pad+2)=126;automobile(vehicles[0]);const CVector pilotOnly=Row(vehicles[0],0x90);
 Prepare();*reinterpret_cast<int16_t *>(pad)=21;*reinterpret_cast<int16_t *>(pad+2)=126;*reinterpret_cast<int16_t *>(pad+4)=43;*reinterpret_cast<int16_t *>(pad+6)=42;automobile(vehicles[0]);
 Check(pilotOnly.x>0&&std::fabs(Row(vehicles[0],0x90).x-pilotOnly.x)<1e-6f&&std::fabs(Row(vehicles[0],0x90).z-pilotOnly.z)<1e-6f,"captured right-stick camera look cannot reverse pilot cyclic or add helicopter yaw");
 Prepare();*reinterpret_cast<int16_t *>(pad+4)=100;*reinterpret_cast<int16_t *>(pad+6)=-90;automobile(vehicles[0]);
 Check(std::fabs(Row(vehicles[0],0x90).x)<1e-6f&&std::fabs(Row(vehicles[0],0x90).z)<1e-6f,"camera-only right-stick motion cannot pitch/yaw helicopter with primary flight axes released");
 Prepare();flightmodel::gLeft=NativeLookLeft;flightmodel::gRight=NativeLookRight;*reinterpret_cast<int16_t *>(pad+0xa)=255;*reinterpret_cast<int16_t *>(pad+4)=100;automobile(vehicles[0]);
 Check(Row(vehicles[0],0x90).z>0,"modern bumper yaw remains pilot-owned while right stick looks in opposite direction");
 Prepare();BindFlightKey(30,0,'E');*reinterpret_cast<int16_t *>(pad+6)=128;automobile(vehicles[0]);const float keyPitch=Row(vehicles[0],0x90).x;
 Prepare();BindFlightKey(30,0,'E');*reinterpret_cast<int16_t *>(pad+6)=-70;automobile(vehicles[0]);
 Check(keyPitch<0&&std::fabs(Row(vehicles[0],0x90).x-keyPitch)<1e-6f,"configured keyboard turret pitch survives the native temp-state clear and independent merged right-stick look");
 Prepare();BindFlightKey(31,1,'K');BindFlightKey(29,1,'L');automobile(vehicles[0]);
 Check(Row(vehicles[0],0x90).x>0&&Row(vehicles[0],0x90).z<0,"optional keyboard pitch/yaw remaps retain native fallback without relying on merged camera axes");
 Prepare();BindFlightKey(30,0,'E');*reinterpret_cast<int16_t *>(pad+2)=1;automobile(vehicles[0]);
 Check(Row(vehicles[0],0x90).x>0,"every nonzero native primary cyclic input owns pitch over opposite keyboard fallback");
 Prepare();BindFlightKey(30,0,'E');BindFlightKey(31,1,'K');*reinterpret_cast<int16_t *>(pad+6)=42;automobile(vehicles[0]);
 Check(std::fabs(Row(vehicles[0],0x90).x)<1e-6f,"opposing configured keyboard pitch keys cancel while camera look remains independent");
 Prepare();BindFlightKey(30,0,'E');BindFlightKey(29,0,'L');*reinterpret_cast<int16_t *>(pad+0xf0)=1;automobile(vehicles[0]);
 Check(std::fabs(Row(vehicles[0],0x90).x)<1e-6f&&std::fabs(Row(vehicles[0],0x90).z)<1e-6f,"disabled native pad gates configured keyboard fallback");
 Prepare();controllerAffected=1;*reinterpret_cast<int16_t *>(pad+0xec)=3;*reinterpret_cast<int16_t *>(pad+0x10)=255;*reinterpret_cast<int16_t *>(pad+0x16)=255;*reinterpret_cast<int16_t *>(pad+4)=-100;*reinterpret_cast<int16_t *>(pad+6)=-60;automobile(vehicles[0]);
 Check(Row(vehicles[0],0x90).x<0&&Row(vehicles[0],0x90).z<0,"actual controller Mode3 retains native Dpad turret flight axes while right stick keeps its native collective role");
 Prepare();controllerAffected=1;*reinterpret_cast<int16_t *>(pad+0xec)=4;BindFlightKey(30,0,'E');BindFlightKey(29,0,'L');automobile(vehicles[0]);
 Check(std::fabs(Row(vehicles[0],0x90).x)<1e-6f&&std::fabs(Row(vehicles[0],0x90).z)<1e-6f,"invalid native controller mode cannot admit keyboard fallback");
 for(int mode:{0,1,2}){
  Prepare();controllerAffected=1;*reinterpret_cast<int16_t *>(pad+0xec)=mode;*reinterpret_cast<int16_t *>(pad+4)=100;*reinterpret_cast<int16_t *>(pad+6)=-90;automobile(vehicles[0]);
  Check(std::fabs(Row(vehicles[0],0x90).x)<1e-6f&&std::fabs(Row(vehicles[0],0x90).z)<1e-6f,"actual controller modes0-2 keep camera-only stick independent of flight");
 }
 Prepare();*reinterpret_cast<int16_t *>(pad+0xec)=3;*reinterpret_cast<int16_t *>(pad+4)=100;*reinterpret_cast<int16_t *>(pad+6)=-90;automobile(vehicles[0]);
 Check(std::fabs(Row(vehicles[0],0x90).x)<1e-6f&&std::fabs(Row(vehicles[0],0x90).z)<1e-6f,"stored Mode3 is ignored while native IsAffectedByController is clear");
 Prepare();BindFlightKey(30,2,1);BindFlightKey(29,3,2);automobile(vehicles[0]);
 Check(std::fabs(Row(vehicles[0],0x90).x)<1e-6f&&std::fabs(Row(vehicles[0],0x90).z)<1e-6f,"mouse and joystick binding slots cannot masquerade as keyboard flight fallback");
 for(int gate:{1,2,3}){
  Prepare();if(gate==1)activeMission=true;if(gate==2)cutscene=true;if(gate==3){player[0x4c0]=0;*reinterpret_cast<uint8_t **>(vehicles[0]+0x218)=vehicles[1];vehicles[0][0x60]=2|(3<<3);}
  automobile(vehicles[0]);Check(originalCalls==1,"active mission/cutscene/foreign driver keeps native script aircraft control");
 }
 Prepare();meshFrames[0]=&mainFrame;CVector from{20,0,2502},to{-20,0,2502};uint32_t weapon=26;alignas(16) uint8_t point[0x30]{};
 for(int i=0;i<3;++i)impact(reinterpret_cast<uint8_t *>(&weapon),player,nullptr,&from,&to,point,{});
 Check(flightmodel::RotorFailed(vehicles[0]),"admitted free-roam script aircraft receives actual rotor damage");
 player[0x4c0]=0;*reinterpret_cast<uint8_t **>(vehicles[0]+0x218)=nullptr;vehicles[0][0x60]=2|(4<<3);automobile(vehicles[0]);
 Check(flightmodel::RotorFailed(vehicles[0])&&Scalar(vehicles[0],0x5f0)<=0,"exiting same abandoned free-roam aircraft cannot repair its failed rotor");
 fix(vehicles[0]);for(int i=0;i<3;++i)impact(reinterpret_cast<uint8_t *>(&weapon),player,nullptr,&from,&to,point,{});
 Check(flightmodel::RotorFailed(vehicles[0]),"native repair preserves confirmed free-roam ownership so parked rotors remain damageable");
 activeMission=true;const float rotor=Scalar(vehicles[0],0x5f0);automobile(vehicles[0]);Check(Scalar(vehicles[0],0x5f0)==std::max(0.0f,rotor-.0005f),"new mission ownership suspends mod control of a previously admitted aircraft");
 // Native Render only changes component material alpha; it cannot recover
 // AtomicRENDER flags suppressed by our old player ownership period.
 Prepare();meshFrames[0]=&mainFrame;atomics[0].flags=0x14;atomics[1].flags=0; // native hidden solid/damaged part
 for(int i=0;i<3;++i)impact(reinterpret_cast<uint8_t *>(&weapon),player,nullptr,&from,&to,point,{});
 Check(!(atomics[0].flags&4)&&!atomics[1].flags,"handoff precondition: owned blur suppressed while intrinsic hidden solid remains hidden");
 atomics[0].flags=0x20; // another renderer changes a non-RENDER bit while suppressed
 activeMission=true;player[0x4c0]=0;*reinterpret_cast<uint8_t **>(vehicles[0]+0x218)=vehicles[1];vehicles[0][0x60]=2|(3<<3);vehicles[0][0x19e]=20;Scalar(vehicles[0],0x5f0)=.22f;
 automobile(vehicles[0]);flightmodel::Frame(1000);
 Check(originalCalls==1&&!flightmodel::RotorFailed(vehicles[0])&&Scalar(vehicles[0],0x5f0)>.15f,"foreign mission takeover retains native flight and damage ownership");
 Check(atomics[0].flags==0x24&&!atomics[1].flags&&(atomics[2].flags&4)&&(atomics[3].flags&4),"handoff restores only owned RENDER suppression and preserves current nonrender bits and intrinsically hidden parts");
 activeMission=false;player[0x4c0]=1;*reinterpret_cast<uint8_t **>(vehicles[0]+0x218)=player;vehicles[0][0x60]=2;vehicles[0][0x19e]=0;
 automobile(vehicles[0]);Check(!flightmodel::RotorFailed(vehicles[0])&&Scalar(vehicles[0],0x5f0)>.15f&&(atomics[0].flags&4),"fresh player acquisition cannot revive a relinquished ownership period's rotor failure");
 Prepare();meshFrames[0]=&mainFrame;for(int i=0;i<3;++i)impact(reinterpret_cast<uint8_t *>(&weapon),player,nullptr,&from,&to,point,{});
 activeMission=true;player[0x4c0]=0;*reinterpret_cast<uint8_t **>(vehicles[0]+0x218)=vehicles[1];vehicles[0][0x60]=2|(3<<3);
 fly(vehicles[0],6);Check(originalCalls==1&&(atomics[0].flags&4)&&(atomics[1].flags&4),"direct native FlyingControl releases suppression before foreign ownership early returns");
 for(int replacement:{0,1,2,3}){
  Prepare();meshFrames[0]=&mainFrame;for(int i=0;i<3;++i)impact(reinterpret_cast<uint8_t *>(&weapon),player,nullptr,&from,&to,point,{});
  Atomic fresh[4];std::copy(atomics,atomics+4,fresh); // all current rotor flags intentionally native-hidden
  currentAtomics=fresh;
  if(replacement==0)*reinterpret_cast<void **>(vehicles[0]+0x58)=fresh; // whole clump
  if(replacement==1)++flags[0]; // recycled entity, even if its clump address is reused
  if(replacement==2)*reinterpret_cast<int16_t *>(vehicles[0]+0x6a)=155; // same slot/generation, new airframe
  // replacement3: same clump, newly loaded atomics; old pointers are only identity tokens
  activeMission=true;player[0x4c0]=0;*reinterpret_cast<uint8_t **>(vehicles[0]+0x218)=vehicles[1];vehicles[0][0x60]=2|(3<<3);
  automobile(vehicles[0]);flightmodel::Frame(1000);
  Check(!(atomics[0].flags&4)&&!(atomics[1].flags&4)&&!(fresh[0].flags&4)&&!(fresh[1].flags&4),"handoff never follows old atomics or restores unowned geometry after clump/generation/model/atomic replacement");
  currentAtomics=atomics;
 }
 Prepare();player[0x4c0]=0;*reinterpret_cast<uint8_t **>(vehicles[0]+0x218)=nullptr;vehicles[0][0x60]=2|(4<<3);meshFrames[0]=&mainFrame;
 for(int i=0;i<3;++i)impact(reinterpret_cast<uint8_t *>(&weapon),player,nullptr,&from,&to,point,{});
 Check(!flightmodel::RotorFailed(vehicles[0])&&impactCalls==3,"unclaimed foreign script-created parked aircraft remains native");
 // A new world may temporarily reject State while a mission/foreign driver
 // owns the same native slot. Rewind must discard every old admission anyway.
 Prepare();meshFrames[0]=&mainFrame;for(int i=0;i<3;++i)impact(reinterpret_cast<uint8_t *>(&weapon),player,nullptr,&from,&to,point,{});
 flightmodel::Frame(10000);activeMission=true;player[0x4c0]=0;*reinterpret_cast<uint8_t **>(vehicles[0]+0x218)=vehicles[1];vehicles[0][0x60]=2|(3<<3);
 flightmodel::Frame(100);activeMission=false;*reinterpret_cast<uint8_t **>(vehicles[0]+0x218)=nullptr;vehicles[0][0x60]=2|(4<<3);Scalar(vehicles[0],0x5f0)=.22f;for(auto &a:atomics)a.flags=4;
 const bool inherited=flightmodel::RotorFailed(vehicles[0]);automobile(vehicles[0]);
 Check(!inherited&&Scalar(vehicles[0],0x5f0)>.15f&&(atomics[0].flags&4),"rewind rejects stale admission/damage even when foreign ownership excluded the slot during reset");
 // Controls run before the later Frame callback, so use native timer at all
 // state entrypoints instead of waiting for the after-world frame observer.
 Prepare();meshFrames[0]=&mainFrame;for(int i=0;i<3;++i)impact(reinterpret_cast<uint8_t *>(&weapon),player,nullptr,&from,&to,point,{});
 flightmodel::Frame(20000);uint32_t nativeTime=200;game::api.timeMs=&nativeTime;Scalar(vehicles[0],0x5f0)=.22f;for(auto &a:atomics)a.flags=4;
 automobile(vehicles[0]);Check(!flightmodel::RotorFailed(vehicles[0])&&Scalar(vehicles[0],0x5f0)>.15f&&(atomics[0].flags&4),"native controller before Frame discards old-world rotor failure on timer rewind");game::api.timeMs=nullptr;
 if(!fails)puts("flight ownership native: all passed");return fails?1:0;
}
