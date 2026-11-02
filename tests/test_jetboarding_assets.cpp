// Original ped.ifp/player.dff through the production doorway route.
#define AIRPORTJETS_NO_MAIN
#include "test_airportjets.cpp"
#include "../tools/bake/gamefiles.h"
namespace boardingassets {
struct Chunk {uint32_t tag;size_t begin,end;};
template<class T>T Read(const std::vector<uint8_t> &b,size_t at){T v{};CHECK(at<=b.size()&&sizeof v<=b.size()-at);if(at<=b.size()&&sizeof v<=b.size()-at)std::memcpy(&v,b.data()+at,sizeof v);return v;}
constexpr uint32_t Four(char a,char b,char c,char d){return uint32_t(uint8_t(a))|uint32_t(uint8_t(b))<<8|uint32_t(uint8_t(c))<<16|uint32_t(uint8_t(d))<<24;}
std::vector<Chunk> Chunks(const std::vector<uint8_t> &b,size_t lo,size_t hi,size_t header=8){std::vector<Chunk> out;while(lo+header<=hi){size_t end=lo+header+Read<uint32_t>(b,lo+4);CHECK(end<=hi);if(end>hi)break;out.push_back({Read<uint32_t>(b,lo),lo+header,end});lo=header==8?(end+3)&~size_t(3):end;}return out;}
struct Key {float t;std::array<double,4> q;CVector p;bool translated;};
struct Sequence {int tag;std::string name;std::vector<Key> keys;};
using Animation=std::vector<Sequence>;
std::string Name(const std::vector<uint8_t> &b,size_t at,size_t max){std::string s(reinterpret_cast<const char *>(b.data()+at),strnlen(reinterpret_cast<const char *>(b.data()+at),max));for(char &c:s)c=char(std::tolower(c));return s;}
std::map<std::string,Animation> Animations(const std::vector<uint8_t> &b){std::map<std::string,Animation> out;std::string name;
 CHECK(Read<uint32_t>(b,0)==Four('A','N','P','K'));
 for(const auto &c:Chunks(b,8,8+Read<uint32_t>(b,4))){if(c.tag==Four('N','A','M','E'))name=Name(b,c.begin,c.end-c.begin);if(c.tag!=Four('D','G','A','N'))continue;
  if(name!="walk_player"&&name!="car_open_lhs"&&name!="car_getin_lhs"&&name!="car_sit"&&name!="walk_back"&&name!="idle_stance")continue;
  for(const auto &component:Chunks(b,c.begin,c.end))if(component.tag==Four('C','P','A','N')){
   const auto p=Chunks(b,component.begin,component.end);CHECK(p.size()==2);if(p.size()!=2)continue;const auto &a=p[0],&k=p[1];
   const int count=Read<int32_t>(b,a.begin+28);CHECK(count>0&&count<100);if(count<=0||count>=100)continue;const size_t stride=(k.end-k.begin)/size_t(count);
   Sequence s{a.end-a.begin==44?Read<int32_t>(b,a.begin+40):-1,Name(b,a.begin,28),{}};
   for(int i=0;i<count;++i){const size_t at=k.begin+size_t(i)*stride;Key key{};for(int j=0;j<4;++j)key.q[size_t(j)]=Read<float>(b,at+size_t(j)*4);
    key.translated=k.tag==Four('K','R','T','0')||k.tag==Four('K','R','T','S');if(key.translated)key.p=Read<CVector>(b,at+16);key.t=Read<float>(b,at+stride-4);s.keys.push_back(key);
   }out[name].push_back(s);
  }
 }return out;
}
struct Basis {double r[3][3];CVector p;};
Basis Compose(const Basis &a,const Basis &b){Basis c{};for(int i=0;i<3;++i)for(int j=0;j<3;++j)for(int k=0;k<3;++k)c.r[i][j]+=a.r[i][k]*b.r[k][j];for(int i=0;i<3;++i)reinterpret_cast<float *>(&c.p)[i]=reinterpret_cast<const float *>(&a.p)[i]+float(a.r[i][0]*b.p.x+a.r[i][1]*b.p.y+a.r[i][2]*b.p.z);return c;}
Basis Sample(const Sequence &s,float t,CVector rest){const auto *a=&s.keys.front(),*b=&s.keys.back();for(size_t i=1;i<s.keys.size();++i)if(t<=s.keys[i].t){a=&s.keys[i-1];b=&s.keys[i];break;}
 const double u=a->t==b->t?0:std::clamp(double((t-a->t)/(b->t-a->t)),0.0,1.0);double dot=0;for(int i=0;i<4;++i)dot+=a->q[size_t(i)]*b->q[size_t(i)];double q[4],len=0;
 for(int i=0;i<4;++i){q[i]=a->q[size_t(i)]*(1-u)+b->q[size_t(i)]*(dot<0?-u:u);len+=q[i]*q[i];}len=std::sqrt(len);for(double &v:q)v/=len;
 const double x=-q[0],y=-q[1],z=-q[2],w=q[3];Basis pose{{{1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)},{2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)},{2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)}},rest};
 if(a->translated)pose.p={float(a->p.x*(1-u)+b->p.x*u),float(a->p.y*(1-u)+b->p.y*u),float(a->p.z*(1-u)+b->p.z*u)};return pose;
}
struct Bone {int parent,tag;std::string name;Basis rest;};
std::vector<Bone> Skeleton(const std::vector<uint8_t> &b){std::vector<Bone> out;
 for(const auto &clump:Chunks(b,0,b.size(),12))if(clump.tag==16)for(const auto &frames:Chunks(b,clump.begin,clump.end,12))if(frames.tag==14){const auto p=Chunks(b,frames.begin,frames.end,12);const int count=Read<int>(b,p[0].begin);CHECK(count==25);for(int i=0;i<count;++i){const size_t at=p[0].begin+4+size_t(i)*56;Bone bone{Read<int>(b,at+48),-1,"",{}};for(int a=0;a<3;++a)for(int j=0;j<3;++j)bone.rest.r[a][j]=Read<float>(b,at+size_t(j*3+a)*4);bone.rest.p=Read<CVector>(b,at+36);
  for(const auto &ext:Chunks(b,p[size_t(i)+1].begin,p[size_t(i)+1].end,12)){if(ext.tag==0x11e)bone.tag=Read<int>(b,ext.begin+4);if(ext.tag==0x253f2fe)bone.name=Name(b,ext.begin,ext.end-ext.begin);}out.push_back(bone);
 }
 return out;
 }
 return out;
}
std::map<int,CVector> Pose(const std::vector<Bone> &bones,const Animation &anim,float time){std::vector<Basis> world;std::map<int,CVector> out;
 for(const auto &b:bones){Basis local=b.rest;for(const auto &s:anim)if(b.tag>=0&&((s.tag>=0&&s.tag==b.tag)||(s.tag<0&&s.name==b.name))){local=Sample(s,time,b.rest.p);break;}const Basis current=b.parent<0?local:Compose(world[size_t(b.parent)],local);world.push_back(current);if(b.tag>=0)out[b.tag]=current.p;}return out;
}
}
#ifndef JETBOARDING_ASSETS_NO_MAIN
void AbortRecovery(const std::string &base){
 using namespace boardingassets;
 std::vector<uint8_t> bytes;CHECK(ReadGameFile(base+"/models/coll/generic.col",bytes));
 std::array<uint8_t,56> sourceBoxes{};bool found=false;
 for(size_t at=0;at+116<=bytes.size();){const size_t end=at+8+Read<uint32_t>(bytes,at+4);if(end>bytes.size()||end<=at)break;
  if(Name(bytes,at+8,22)=="ap_learjet1_01"){
   size_t cursor=at+116;cursor+=4+size_t(Read<uint32_t>(bytes,cursor))*16;CHECK(Read<uint32_t>(bytes,cursor)==2);cursor+=4;
   CHECK(cursor+sourceBoxes.size()<=end);if(cursor+sourceBoxes.size()<=end){std::memcpy(sourceBoxes.data(),bytes.data()+cursor,sourceBoxes.size());found=true;}break;
  }at=end;
 }
 CHECK(found);if(!found)return;
 auto intersects=[&](CVector origin){
  const auto local=airportjets::Sub(game::EntityPos(gPlayer),origin);
  const auto lo=airportjets::Vector(airportjets::gBoxes,0),hi=airportjets::Vector(airportjets::gBoxes,12);
  // Exact ms_colModelPed1 sphere offsets/radius are verified by the native
  // boarding test. Test the prepared structural box, not the visual lining.
  for(float z:{-.25f,.15f,.55f}){auto point=local;point.z+=z;
   const CVector nearest{std::clamp(point.x,lo.x,hi.x),std::clamp(point.y,lo.y,hi.y),std::clamp(point.z,lo.z,hi.z)};
   if(airportjets::Length(airportjets::Sub(point,nearest))<.35f)return true;
  }return false;
 };
 for(int interruption=0;interruption<7;++interruption){
  Setup();std::memcpy(boxBytes,sourceBoxes.data(),sourceBoxes.size());CHECK(planeflight::Install());CHECK(airportjets::Install());
  gameMs=1000;JetFrame(gameMs);const int model=lastRequest;CHECK(model>=240&&model<=249);if(model<240||model>249)return;
  *reinterpret_cast<void **>(models[model]+0x30)=&clumpMarker;streamInfo[model*32+0x10]=1;gameMs+=100;JetFrame(gameMs);auto *v=jetVehicles[0];
  V(gPlayer,0x38)={V(v,0x38).x-2.3f,V(v,0x38).y+3.7f,14.84f};
  auto enter=JetHook<void (*)(uint8_t *,uint8_t *,uint32_t)>("SetEnterCar");CHECK(enter);if(!enter)return;gameMs+=100;enter(gPlayer,v,0);
  for(int tick=0;tick<300&&(!boardingAssoc[80]||*reinterpret_cast<float *>(boardingAssoc[80]+0x30)<.5f);++tick){gameMs+=50;JetFrame(gameMs);}
  const auto origin=game::EntityPos(v);
  CHECK(boardingAssoc[80]&&!game::PedInVehicle(gPlayer)&&intersects(origin));const auto before=game::EntityPos(gPlayer);
  auto sphereBefore=game::api.TestSphereAgainstWorld;
  game::api.TestSphereAgainstWorld=[](CVector p,float radius,uint8_t *ignore,bool a,bool b,bool c,bool d,bool e,bool f)->uint8_t *{
   if(c&&(game::EntityFlags(gPlayer)&game::kEntityUsesCollision))for(float z:{-.25f,.15f,.55f}){
    auto centre=game::EntityPos(gPlayer);centre.z+=z;
    if(airportjets::Length(airportjets::Sub(p,centre))<radius+.35f)return gPlayer;
   }
   return SphereJet(p,radius,ignore,a,b,c,d,e,f);
  };
  if(interruption==5){
   static CVector ceilingMin,ceilingMax;const auto outside=airportjets::gJets[0].boardingOutside;
   ceilingMin={outside.x-.4f,outside.y-.4f,outside.z+.5f};ceilingMax={outside.x+.4f,outside.y+.4f,outside.z+1.5f};
   game::api.TestSphereAgainstWorld=[](CVector p,float radius,uint8_t *ignore,bool a,bool b,bool c,bool d,bool e,bool f)->uint8_t *{
    const CVector nearest{std::clamp(p.x,ceilingMin.x,ceilingMax.x),std::clamp(p.y,ceilingMin.y,ceilingMax.y),std::clamp(p.z,ceilingMin.z,ceilingMax.z)};
    return airportjets::Length(airportjets::Sub(p,nearest))<radius?buildings[1]:SphereJet(p,radius,ignore,a,b,c,d,e,f);
   };
  }
  if(interruption==6)game::api.TestSphereAgainstWorld=[](CVector p,float radius,uint8_t *ignore,bool a,bool b,bool c,bool d,bool e,bool f)->uint8_t *{
   if(boardingAssoc[80])*reinterpret_cast<int32_t *>(boardingAssoc[80]+0x40)=1;
   return SphereJet(p,radius,ignore,a,b,c,d,e,f);
  };
  if(interruption==0||interruption==5)boardingAssoc[80]=nullptr;
  if(interruption==1||interruption==6){freezeBoardingAnimation=true;gameMs+=21000;}
  if(interruption==2)mission=true;
  if(interruption==3)*reinterpret_cast<int32_t *>(boardingAssoc[80]+0x40)=1;
  if(interruption==4)gConfig.planes=false;
  gameMs+=50;JetFrame(gameMs);CHECK(game::EntityFlags(gPlayer)&game::kEntityUsesCollision);
  game::api.TestSphereAgainstWorld=sphereBefore;
  if(interruption<2||interruption==4){CHECK(!intersects(origin));CHECK(std::fabs(V(gPlayer,0x38).x-origin.x+2.25f)<.001f);}
  else CHECK(airportjets::Length(airportjets::Sub(game::EntityPos(gPlayer),before))<.00001f);
  mission=false;freezeBoardingAnimation=false;gConfig.planes=true;
  auto restart=JetHook<void (*)()>("ClearForRestart"),shutdown=JetHook<void (*)()>("CModelInfo8ShutDown");CHECK(restart&&shutdown);if(restart)restart();if(shutdown)shutdown();
 }
}
int main(){using namespace boardingassets;const char *env=std::getenv("GAMEDATA");const std::string base=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";
 ImgArchive archive;CHECK(archive.Open(base+"/models/gta3.img"));std::vector<uint8_t> dff,ifp;CHECK(archive.Read("player.dff",dff));CHECK(ReadGameFile(base+"/anim/ped.ifp",ifp));if(failures)return 1;
 const auto animations=Animations(ifp);const auto bones=Skeleton(dff);CHECK(animations.size()==6&&bones.size()==25);if(failures)return 1;
 const auto &getin=animations.at("car_getin_lhs"),&sit=animations.at("car_sit");const auto entry=Pose(bones,getin,jetboarding::kEntryEnd*.6666666865348816f),seated=Pose(bones,sit,0);
 for(const auto &s:getin)if(s.tag==0)for(const auto &key:s.keys){const auto actual=jetboarding::Root(key.t/.6666666865348816f);CHECK(airportjets::Length(airportjets::Sub(actual,key.p))<.000002f);}
 const auto entryActor=airportjets::BoardingRoute(jetboarding::kEntryEnd,{-2.25f,3.7f,-1.234784f});
 CHECK(airportjets::Length(airportjets::Sub(jetboarding::CrouchActor(0),entryActor))<.000002f);
 CHECK(airportjets::Length(airportjets::Sub(jetboarding::CrouchKey(0),entry.at(0)))<.000002f);
 for(int leg=0;leg<2;++leg){const int tag=leg?53:43;
  const auto foot=airportjets::Add(entryActor,jetboarding::Rotate(entry.at(tag),jetboarding::Yaw(jetboarding::kEntryEnd)));
  CHECK(airportjets::Length(airportjets::Sub(jetboarding::EntryFoot(leg),foot))<.000002f);
  CHECK(airportjets::Length(airportjets::Sub(jetboarding::CrouchFoot(0,leg),foot))<.000002f);
  CHECK(airportjets::Length(airportjets::Sub(jetboarding::SeatFoot(leg),airportjets::Add(jetboarding::kSeatRoot,seated.at(tag))))<.000002f);
 }
 int sweepFailures=0;
 for(int sample=0;sample<=1000;++sample){const float u=sample/1000.0f*jetboarding::kEntryEnd;const auto root=airportjets::BoardingRoute(u,{-2.25f,3.7f,-1.234784f});const auto pose=Pose(bones,getin,u*.6666666865348816f);
  auto world=[&](int bone){return airportjets::Add(root,jetboarding::Rotate(pose.at(bone),jetboarding::Yaw(u)));};
  const auto head=world(5);
  if(head.x>-.84f&&head.x<-.40f&&(head.y<3.36f||head.y>4.04f||head.z>.18f)){if(sweepFailures++<12)std::printf("head collision u%.3f (%f,%f,%f)\n",u,head.x,head.y,head.z);}
  for(int bone:{43,53}){const auto foot=world(bone);const float sole=foot.z-.095f;float support=-100;
   if(foot.x>-.56f&&foot.x<.62f&&foot.y>3.15f&&foot.y<6.05f)support=-.93f;
   for(const auto tread:std::array<std::array<float,3>,3>{{{{-1.590019f,-1.381154f,-1.754679f}},{{-1.106269f,-.897404f,-1.6f}},{{-.896851f,-.687986f,-1.227286f}}}})
    if(foot.x>tread[0]-.04f&&foot.x<tread[1]+.04f&&foot.y>3.35f&&foot.y<4.05f)support=std::max(support,tread[2]);
   if(sole<support-.03f){if(sweepFailures++<12)std::printf("foot%d collision u%.3f (%f,%f,%f), support%f\n",bone,u,foot.x,foot.y,sole,support);}
  }
 }
 CHECK(sweepFailures==0);
 // Actual feet contact all three final molded plates before the threshold.
 for(const auto contact:std::array<std::array<float,4>,3>{{{{.15f,53,-1.4855865f,-1.754679f}},{{.21f,43,-1.0018365f,-1.6f}},{{.45f,43,-.7924185f,-1.227286f}}}}){const float u=contact[0];const auto pose=Pose(bones,getin,u*.6666666865348816f);const auto foot=airportjets::Add(airportjets::BoardingRoute(u,{-2.25f,3.7f,-1.234784f}),jetboarding::Rotate(pose.at(int(contact[1])),jetboarding::Yaw(u)));CHECK(std::fabs(foot.x-contact[2])<.03f&&foot.y>=3.39f&&foot.y<=4.01f&&std::fabs(foot.z-.095f-contact[3])<.03f);}
 const auto &open=animations.at("car_open_lhs"),&back=animations.at("walk_back");
 for(int sample=0;sample<=1000;++sample){const float u=sample/1000.0f;const auto reaching=Pose(bones,open,u*.7333333492279053f);const auto reachHead=airportjets::Add({-1.3f,3.58f,-1.234784f},reaching.at(5));CHECK(reachHead.x<-.9f);
  const CVector handle{-1.3f,3.58f,-1.234784f},outside{-2.25f,3.7f,-1.234784f};const auto delta=airportjets::Sub(outside,handle);const float total=airportjets::Length(delta),time=u*total/1.624076f,yaw=std::atan2(-delta.x,-delta.y);const auto walking=Pose(bones,back,time);
  CVector nativeRoot{};for(const auto &sequence:back)if(sequence.tag==0)nativeRoot=Sample(sequence,time,{}).p;
  const float door=0,angle=-2.268928028f*door,c=std::cos(angle),s=std::sin(angle);
  for(int bone:{5,43,53}){auto local=walking.at(bone);local.y-=nativeRoot.y;const auto point=airportjets::Add(airportjets::Add(handle,airportjets::Mul(delta,u)),jetboarding::Rotate(local,yaw));const auto d=airportjets::Sub(point,{-.64f,3.7f,-1.02f});
   const CVector closed{c*d.x-s*d.z,d.y,s*d.x+c*d.z};const CVector nearest{0,std::clamp(closed.y,-.45f,.45f),std::clamp(closed.z,0.0f,1.31f)};if(airportjets::Length(airportjets::Sub(closed,nearest))<=(bone==5?.25f:.1f)){if(sweepFailures++<8)std::printf("retreat u%f bone%d door%f dist%f p(%f,%f,%f)\n",u,bone,door,airportjets::Length(airportjets::Sub(closed,nearest)),point.x,point.y,point.z);}
  }
  const auto standing=Pose(bones,animations.at("idle_stance"),u*.8f);const float lowerAngle=-2.268928028f*u,lc=std::cos(lowerAngle),ls=std::sin(lowerAngle);
  for(int bone:{5,43,53}){const auto point=airportjets::Add(outside,standing.at(bone)),d=airportjets::Sub(point,{-.64f,3.7f,-1.02f});const CVector closed{lc*d.x-ls*d.z,d.y,ls*d.x+lc*d.z},nearest{0,std::clamp(closed.y,-.45f,.45f),std::clamp(closed.z,0.0f,1.31f)};CHECK(airportjets::Length(airportjets::Sub(closed,nearest))>(bone==5?.25f:.1f));}
 }
 CHECK(sweepFailures==0);
 AbortRecovery(base);
 if(!failures)std::puts("Learjet boarding original IFP/player: pose sweep, tread/seat continuity and safe interrupted entry passed");return failures?1:0;
}

#endif
