#define main FireChecks
#include "test_carfire.cpp"
#undef main
#include "nativejetshadowarm.h"
namespace audiofixture {using namespace fixture;using namespace nativejetshadowtest;
int fireSounds=0,steamSounds=0;unsigned Freq(void *,unsigned){return 20000;}unsigned Loop(void *,unsigned){return 0;}uint8_t Volume(void *,uint8_t,float,float){return 40;}
void Queue(uint8_t *a){const auto sample=*reinterpret_cast<uint32_t *>(a+0x1c);if(sample==153)++fireSounds;if(sample==37)++steamSounds;}
}
int main(){if(FireChecks())return 1;using namespace audiofixture;Code code;code.Add(0x100011bc0,0x100011d6c);code.bindings={{0x100044ed8,reinterpret_cast<void *>(Freq)},{0x100044f30,reinterpret_cast<void *>(Loop)},{0x100044f88,reinterpret_cast<void *>(Loop)},{0x1000329e8,reinterpret_cast<void *>(Volume)},{0x100032c68,reinterpret_cast<void *>(Queue)}};code.Finish();using Audio=bool (*)(uint8_t *,uint8_t *);auto original=reinterpret_cast<Audio>(code.Address(0x100011bc0));auto entry=entries.find("_ZN13cAudioManager19ProcessEngineDamageER14cVehicleParams");auto call=entry==entries.end()?original:reinterpret_cast<Audio>(entry->second);

 carfire::gEngineAudio=original;
 alignas(16) uint8_t audio[0x100]{},params[0x30]{};*reinterpret_cast<void **>(params+0x10)=cars[0];F(params,8)=16;
 for(int mode=0;mode<4;mode++){
  Reset();*reinterpret_cast<int16_t *>(cars[0]+0x6a)=199;game::api.AirframeEffects=+[](const uint8_t *){airframemodel::Effects e;e.managed=true;return e;};F(cars[0],0x2a0)=mode==3?300:200;fireSounds=steamSounds=0;
  if(mode==1){auto *f=Start(manager,cars[0],nullptr,.8f,0);Process()(f);}if(mode==2)gConfig.carFire=false;
  Check(call(audio,params),"native engine audio acceptance preserved");Check(fireSounds==((mode==1||mode==2)?1:0),"actual native fire loop only follows managed registered fire, while disabled behavior remains native");if(mode==3)Check(steamSounds==1,"native non-fire engine damage sound is preserved");gConfig.carFire=true;
 }
 Reset();*reinterpret_cast<int16_t *>(cars[0]+0x6a)=187;F(cars[0],0x2a0)=200;Check(call(audio,params)==original(audio,params),"native model187 audio exclusion keeps its original return");
 F(params,8)=1700;Check(!call(audio,params),"native far-audio return remains unchanged");printf("native fire audio: %s\n",failures?"FAILED":"all passed");return failures?1:0;
}
