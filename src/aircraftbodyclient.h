#pragma once
#include "aircraftbodybridge.h"
#include <dlfcn.h>
namespace aircraftbodyclient {
template<class F> F Bind(const char *name){
#ifdef VC_SOURCE_UUID_BYTES
 return reinterpret_cast<F>(dlsym(RTLD_MAIN_ONLY,name));
#else
 (void)name;return nullptr;
#endif
}
inline bool Owned(const void *v){static auto f=Bind<decltype(&VCSourceAircraftBodyOwned)>("VCSourceAircraftBodyOwned");return f&&f(v);}
inline bool Read(const void *v,AircraftBodySnapshot &s){static auto f=Bind<decltype(&VCSourceAircraftBodyRead)>("VCSourceAircraftBodyRead");return f&&f(v,&s);}
inline bool Bullet(void *v,Vec3 a,Vec3 b,float budget,float limit){static auto f=Bind<decltype(&VCSourceAircraftBodyBullet)>("VCSourceAircraftBodyBullet");return f&&f(v,a,b,budget,limit);}
inline bool Contact(void *v,Vec3 p,Vec3 n,float dv,uint32_t now){static auto f=Bind<decltype(&VCSourceAircraftBodyContact)>("VCSourceAircraftBodyContact");return f&&f(v,p,n,dv,now);}
inline bool Trace(const void *v,Vec3 a,Vec3 b,float entry,float &f,Vec3 &n){static auto p=Bind<decltype(&VCSourceAircraftBodyTrace)>("VCSourceAircraftBodyTrace");return p&&p(v,a,b,entry,&f,&n);}
inline void Repair(void *v){static auto f=Bind<decltype(&VCSourceAircraftBodyRepair)>("VCSourceAircraftBodyRepair");if(f)f(v);}
inline void Burn(void *v,float dt){static auto f=Bind<decltype(&VCSourceAircraftBodyBurn)>("VCSourceAircraftBodyBurn");if(f)f(v,dt);}
inline void Controls(void *v,float l,float r,float e,float y){static auto f=Bind<decltype(&VCSourceAircraftBodyControls)>("VCSourceAircraftBodyControls");if(f)f(v,l,r,e,y);}
inline bool BladeRadius(const void *v,float &r){static auto f=Bind<decltype(&VCSourceAircraftBodyBladeRadius)>("VCSourceAircraftBodyBladeRadius");return f&&f(v,&r);}
inline bool BladeStrike(void *v,float spin,uint32_t now){static auto f=Bind<decltype(&VCSourceAircraftBodyBladeStrike)>("VCSourceAircraftBodyBladeStrike");return f&&f(v,spin,now);}
}
