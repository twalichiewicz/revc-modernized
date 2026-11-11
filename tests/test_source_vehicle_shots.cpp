#define VC_SOURCE_UUID_BYTES 1
#define main LegacyShotChecks
#include "test_shotcollision.cpp"
#undef main

namespace sourcefixture {
bool owned=false,trace=false,repair=false;uint64_t generation=1;int bullets=0;float limit=0,budget=0;
bool Owns(const void *v){return owned&&v==fixture::cars.data();}
bool Read(const void *v,AircraftBodySnapshot *out){if(!Owns(v)||!out)return false;*out={};out->generation=generation;return true;}
bool Bullet(void *v,Vec3,Vec3,float b,float l){if(!Owns(v))return false;++bullets;budget=b;limit=l;return true;}
bool Trace(const void *v,Vec3,Vec3,float,float *fraction,Vec3 *normal){if(!Owns(v)||!trace||*fraction<=.4f)return false;*fraction=.4f;*normal={-1,0,0};return true;}
void NativeDamage(uint8_t *v,uint8_t *by,int weapon,float damage,CVector at){fixture::NativeVehicleDamage(v,by,weapon,damage,at);if(repair)++generation;}
void *Symbol(const char *s){
 if(!strcmp(s,"VCSourceAircraftBodyOwned"))return reinterpret_cast<void *>(Owns);
 if(!strcmp(s,"VCSourceAircraftBodyRead"))return reinterpret_cast<void *>(Read);
 if(!strcmp(s,"VCSourceAircraftBodyBullet"))return reinterpret_cast<void *>(Bullet);
 if(!strcmp(s,"VCSourceAircraftBodyTrace"))return reinterpret_cast<void *>(Trace);
 return nullptr;
}
void Setup(int model=150,int kind=0){
 owned=false;fixture::Setup();owned=true;trace=repair=false;bullets=0;++generation;
 fixture::Panel(-.8f);fixture::Panel(.8f);fixture::noPeds=true;fixture::doDamage=true;
 *reinterpret_cast<int16_t *>(fixture::cars.data()+0x6a)=int16_t(model);
 *reinterpret_cast<int *>(fixture::cars.data()+0x348)=kind;
 *reinterpret_cast<void **>(fixture::cars.data()+0x218)=nullptr;
}
}
int main(){using namespace fixture;extraLookup=sourcefixture::Symbol;
 if(LegacyShotChecks(0,nullptr))return 1;
 shotcollision::gVehicleDamage=sourcefixture::NativeDamage;
 for(auto actor:{std::pair<int,int>{150,0},{152,1},{166,5},{180,4},{155,0},{155,3}}){
  sourcefixture::Setup(actor.first,actor.second);carmechanics::enabled=carparts::enabled=airframedamage::enabled=true;doubleDamage=true;FireShot();
  Check(sourcefixture::bullets==1,"each native vehicle class delivers one canonical material round despite duplicate native callbacks");
  Check(carmechanics::deliveries==0&&carparts::hits==0&&airframedamage::hits==0&&carglass::perforations==0&&carglass::calls==0,"source material delivery yields every legacy damage/mesh writer");
  Check(*reinterpret_cast<float *>(cars.data()+0x2a0)==1000,"source material work leaves native scalar hull health unchanged");
 }
 sourcefixture::Setup();acceptDamage=false;FireShot();Check(sourcefixture::bullets==0,"native mission/aggressor rejection vetoes source damage");
 sourcefixture::Setup();cars[0x63]|=2;FireShot();Check(sourcefixture::bullets==0,"scripted bullet proof vetoes source damage");
 sourcefixture::Setup();cars[0x63]|=8;FireShot();Check(sourcefixture::bullets==1,"collision proof does not become bullet immunity");
 sourcefixture::Setup();damageMutation=1;FireShot();Check(sourcefixture::bullets==0,"recycled native identity retires pending source round");
 sourcefixture::Setup();damageMutation=2;FireShot();Check(sourcefixture::bullets==0,"changed clump retires pending source round");
 sourcefixture::Setup();damageMutation=3;FireShot();Check(sourcefixture::bullets==0,"native movement retires pending source ray");
 sourcefixture::Setup();sourcefixture::repair=true;FireShot();Check(sourcefixture::bullets==0,"repair during accepted native callback retires the source material lease");
 sourcefixture::Setup();{
  uint8_t gun[32]{};int type=17;memcpy(gun,&type,4);Shot query(gun,peds.data()+0x8d0*2);WorldEntry()(from,to,result,victim,true,true,true,true,true,false,false,true);
  ++sourcefixture::generation;Hook_VehicleDamage(victim,peds.data()+0x8d0*2,type,20,result.p);
 }Check(sourcefixture::bullets==0&&*reinterpret_cast<float *>(cars.data()+0x2a0)==1000,"repair before the impact boundary cannot fall back to scalar hull damage");
 sourcefixture::Setup();coverWall=true;FireShot();Check(sourcefixture::bullets==1&&std::fabs(sourcefixture::limit-.5f)<.001f,"real world cover limits canonical internal work");
 sourcefixture::Setup(166,5);*reinterpret_cast<void **>(cars.data()+0x460)=&clump.atomics[0].frame;FireShot();Check(sourcefixture::bullets==1&&result.surfaceB==6,"bike mesh never reads automobile wheel-node identity");
 sourcefixture::Setup();sourcefixture::trace=true;clump.atomics[0].flags=0;FireShot();Check(sourcefixture::bullets==1&&std::fabs(result.p.x+.6f)<.001f,"open source skin still intercepts its canonical internal component");
 sourcefixture::Setup();doDamage=false;FireShot();Check(sourcefixture::bullets==0,"read-only obstruction queries never spend source material work");
 sourcefixture::Setup();for(int i=0;i<65;i++)Panel(-.7f+i*.02f);FireShot();Check(sourcefixture::bullets==1,"canonical body accounting does not inherit the legacy material-layer cap");
 sourcefixture::Setup();clump.atomics[0].geo.mats[0].colour[3]=89;noPeds=false;*reinterpret_cast<void **>(cars.data()+0x218)=peds.data();
 shotcollision::gPedDamage=+[](uint8_t *,uint8_t *,int,float,int,uint8_t){return true;};FireShot();
 Check(victim==peds.data()&&sourcefixture::bullets==1&&sourcefixture::limit<.5f&&carglass::calls==0,"accepted seated-occupant hit spends source glass work once and clips at the occupant");
 printf("source vehicle shot routing: %d failures\n",fail);return fail?1:0;
}
