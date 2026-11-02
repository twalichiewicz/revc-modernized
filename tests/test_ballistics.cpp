// Actual shot wrapper -> verified native CWeaponInfo layout -> native-shaped
// range-bounded ray, with nearer world cover and native damage/return ownership.
#include <dlfcn.h>
static void *TestBallisticsSymbol(void *,const char *);
#define dlsym TestBallisticsSymbol
#include "../src/ballistics.cpp"
#undef dlsym
#include <cstdio>
#include <limits>

namespace game { Api api; }
void Log(const char *,...) {}
namespace {
alignas(16) uint8_t player[0x900]{},npc[0x900]{},car[0x700]{};
alignas(16) std::array<uint8_t,100> infos[37];
uint8_t weapon[0x18]{};
float farClip=600, targetDistance=75, coverDistance=10000;
bool hit=false,nestNpc=false,nestOther=false,throwShot=false;
float nestedRange=0;
int fail=0;
uint8_t *(*infoEntry)(int32_t);
bool (*fireEntry)(uint8_t *,uint8_t *,CVector *);
bool (*carEntry)(uint8_t *,uint8_t *,bool,bool);
bool (*addEntry)(uint8_t *,int32_t,CVector,CVector);
alignas(8) std::array<uint8_t,nativebullet::kCount*nativebullet::kStride> bulletRecords{};
bool addTwo=false,addResult=true;
void Check(bool ok,const char *name){if(!ok){printf("FAIL: %s\n",name);++fail;}}
uint8_t *Player(){return player;}
uint8_t *Info(int32_t type){return type>=0 && type<37?infos[type].data():nullptr;}
float Range(const uint8_t *p){float f=0;std::memcpy(&f,p+4,4);return f;}
void Set(int type,float distance){auto &i=infos[type];for(size_t n=0;n<i.size();++n)i[n]=uint8_t(n+type);int fire=1;std::memcpy(i.data(),&fire,4);std::memcpy(i.data()+4,&distance,4);}
bool NativeFire(uint8_t *w,uint8_t *shooter,CVector *){
 const int type=game::WeaponType(w);uint8_t *info=infoEntry(type);
 if(shooter!=player){nestedRange=Range(info);return false;}
 auto expected=infos[type];const float observedRange=Range(info);std::memcpy(expected.data()+4,&observedRange,4);
 Check(!std::memcmp(expected.data(),info,expected.size()),"shot copy changes only native range field");
 Check(infoEntry(type)==info,"one shot has stable info pointer");
 if(nestNpc){nestNpc=false;fireEntry(w,npc,nullptr);Check(infoEntry(type)==info,"nested NPC restores outer info scope");}
 if(nestOther){nestOther=false;uint8_t other[0x18]{};int type2=27;std::memcpy(other,&type2,4);fireEntry(other,npc,nullptr);}
 hit=Range(info)>=targetDistance && targetDistance<coverDistance;
 Check(Range(Info(type))==(type==17?30.0f:type==26?90.0f:Range(Info(type))),"native info never modified during shot");
 if(throwShot)throw 7;
 return hit;
}
bool NativeCar(uint8_t *w,uint8_t *,bool,bool){return NativeFire(w,*reinterpret_cast<uint8_t **>(car+0x218),nullptr);}
bool NativeAdd(uint8_t *creator,int32_t type,CVector position,CVector velocity){
 if(!addResult)return false;
 for(size_t i=0;i<nativebullet::kCount;++i)if(!nativebullet::Read(bulletRecords.data(),i).active){
  nativebullet::Record record{};record.creator=creator;record.type=type;record.deadline=11000;record.active=1;
  record.position={position.x,position.y,position.z};record.velocity={velocity.x,velocity.y,velocity.z};record.damage=125;
  std::memcpy(bulletRecords.data()+i*nativebullet::kStride,&record,sizeof record);
  if(addTwo&&i+1<nativebullet::kCount)std::memcpy(bulletRecords.data()+(i+1)*nativebullet::kStride,&record,sizeof record);
  return true;
 }
 return false;
}
}
static void *TestBallisticsSymbol(void *,const char *name){
 if(!std::strcmp(name,"_ZN5CDraw12ms_fFarClipZE"))return &farClip;
 return !std::strcmp(name,"gaBulletInfo")?bulletRecords.data():nullptr;
}
namespace hooks {
void *Chain(const char *name,void *hook){
 if(!std::strcmp(name,"_ZN11CWeaponInfo13GetWeaponInfoE11eWeaponType")){infoEntry=reinterpret_cast<decltype(infoEntry)>(hook);return reinterpret_cast<void *>(Info);}
 if(!std::strcmp(name,"_ZN7CWeapon4FireEP7CEntityP7CVector")){fireEntry=reinterpret_cast<decltype(fireEntry)>(hook);return reinterpret_cast<void *>(NativeFire);}
 if(!std::strcmp(name,"_ZN7CWeapon21FireInstantHitFromCarEP8CVehiclebb")){carEntry=reinterpret_cast<decltype(carEntry)>(hook);return reinterpret_cast<void *>(NativeCar);}
 if(!std::strcmp(name,"_ZN11CBulletInfo9AddBulletEP7CEntity11eWeaponType7CVectorS3_")){addEntry=reinterpret_cast<decltype(addEntry)>(hook);return reinterpret_cast<void *>(NativeAdd);}
 return nullptr;
}
}
int main(){
 game::api.FindPlayerPed=Player;
 // Bind far-clip directly here; Install uses the same exported native global.
 Check(ballistics::Install(),"actual install chains all firing/info seams");
 ballistics::gFarClip=&farClip;
 for(int i=0;i<37;++i)Set(i,30);
 Set(26,90);Set(27,90);
 auto run=[&](int type,float distance){std::memcpy(weapon,&type,4);targetDistance=distance;hit=false;return fireEntry(weapon,player,nullptr);};
 Check(run(17,75)&&hit,"pistol reaches unobstructed visible target beyond stock30m");
 Check(run(26,250)&&hit,"rifle reaches unobstructed visible target beyond stock90m");
 Check(Range(infoEntry(17))==30 && infoEntry(17)==Info(17),"outside fire uses original native info pointer/range");
 coverDistance=20;Check(!run(17,75)&&!hit,"nearer world cover still blocks the native hit");coverDistance=10000;
 auto original=infos[17];nestNpc=true;Check(run(17,75),"outer player still reaches target after nested NPC shot");
 Check(nestedRange==30,"nested NPC receives original engagement range");
 Check(infos[17]==original,"all native weapon fields byte-identical after shot");
 std::memcpy(weapon,"\x1a\0\0\0",4);nestOther=true;run(26,250);Check(nestedRange==90,"nested other-weapon NPC range stays native");
 std::memcpy(weapon,"\x11\0\0\0",4);fireEntry(weapon,npc,nullptr);Check(nestedRange==30,"ordinary NPC receives original engagement range");
 *reinterpret_cast<uint8_t **>(car+0x218)=player;targetDistance=150;Check(carEntry(weapon,car,true,false),"player drive-by reaches visible target");
 *reinterpret_cast<uint8_t **>(car+0x218)=npc;carEntry(weapon,car,true,false);Check(nestedRange==30,"NPC drive-by range stays native");
 Check(!run(30,75),"rocket/projectile info not extended");
 int projectile=2;std::memcpy(infos[17].data(),&projectile,4);Check(!run(17,75),"custom projectile in firearm slot remains native");Set(17,30);
 farClip=20;Check(ballistics::VisibleRange(90)>=90,"short far clip never shortens native range");
 farClip=std::numeric_limits<float>::quiet_NaN();Check(std::isfinite(ballistics::VisibleRange(30))&&ballistics::VisibleRange(30)>=250,"invalid far clip has finite visible combat fallback");
 farClip=600;throwShot=true;try{run(17,75);}catch(int){}throwShot=false;
 Check(infoEntry(17)==Info(17)&&Range(infoEntry(17))==30,"exception restores shot ownership without global writes");
 // Native FireSniper does not use info.range: AddBullet's deadline and
 // Update's 0.5*timeStep*velocity define its actual travel distance.
 const CVector start{10,20,30},speed{0,16,0};
 for(int type:{28,29}){
  bulletRecords.fill(0);Check(addEntry(player,type,start,speed),"native deferred sniper creation succeeds");
  auto record=nativebullet::Read(bulletRecords.data(),0);
  Check((record.deadline-10000)*.001f*400>=farClip,"deferred sniper reaches visible distance beyond native400m");
  auto expected=record;expected.deadline=11000;auto original=nativebullet::Record{};
  original.type=type;original.creator=player;original.deadline=11000;original.active=1;original.position={10,20,30};original.velocity={0,16,0};original.damage=125;
  Check(!std::memcmp(&expected,&original,sizeof original),"deferred range changes only new bullet deadline");
 }
 bulletRecords.fill(0);addEntry(npc,28,start,speed);Check(nativebullet::Read(bulletRecords.data(),0).deadline==11000,"NPC sniper retains native lifetime");
 auto existing=nativebullet::Read(bulletRecords.data(),0);addEntry(player,28,start,speed);auto after=nativebullet::Read(bulletRecords.data(),0);
 Check(!std::memcmp(&existing,&after,sizeof after),"preexisting active bullet untouched");
 bulletRecords.fill(0);addTwo=true;addEntry(player,28,start,speed);addTwo=false;
 Check(nativebullet::Read(bulletRecords.data(),0).deadline==11000&&nativebullet::Read(bulletRecords.data(),1).deadline==11000,"ambiguous new records retain native ownership");
 bulletRecords.fill(0);addEntry(player,28,start,{0,0,0});Check(nativebullet::Read(bulletRecords.data(),0).deadline==11000,"zero bullet velocity stays finite and native");
 bulletRecords.fill(0);for(size_t i=0;i<nativebullet::kCount;++i)bulletRecords[i*nativebullet::kStride+0x14]=1;
 const auto full=bulletRecords;Check(!addEntry(player,28,start,speed)&&bulletRecords==full,"full native bullet pool preserves failure and all records");
 return fail?1:(puts("ballistics: all passed"),0);
}
