#pragma once
// Native command boundaries shared by the sidecar and its offline fixture.
#include "../../src/game.h"
#include "../../src/reactcontact.h"
#include <cmath>
#include <cstring>
#include <string>

namespace probe {
using PedLookup = uint8_t *(*)(int);
using SetWeaponSlot = void (*)(uint8_t *, int);
using LineQuery = bool (*)(const CVector &,const CVector &,uint8_t *,uint8_t *&,bool,bool,bool,bool,bool,bool,bool,bool);
using BulletImpact = void (*)(uint8_t *,uint8_t *,uint8_t *,CVector *,CVector *,uint8_t *,CVector2D);
using CurrentPlayer = uint8_t *(*)();
using DamageEntry = bool (*)(uint8_t *,uint8_t *,int,float,int,uint8_t);
using EntityUpdate = void (*)(uint8_t *);
using MatrixUpdate = void (*)(void *);
struct PlayerHitResult { bool native=false;float healthBefore=0,healthAfter=0,armourBefore=0,armourAfter=0; };
inline bool PlayerDiagnosticContext(const uint8_t *camera,const uint8_t *pad,bool mission) {
    return camera && pad && camera[0x63] && !camera[0x75] && !mission && !game::PadControlsDisabled(pad);
}
inline bool HitPlayer(uint8_t *player,int ref,float amount,CurrentPlayer current,PedLookup lookup,DamageEntry damage,
                      PlayerHitResult &result,std::string &error) {
    if(!player || ref<0 || !current || !lookup || !damage || current()!=player || lookup(ref)!=player) {
        error="refused: current player generation changed";return false;
    }
    const int state=game::PedState(player);const float health=game::Health(player);
    if(!std::isfinite(amount) || amount<=0 || amount>10 || !std::isfinite(health) || health<=50 ||
       game::PedInVehicle(player) || (state!=game::kPedIdle && state!=game::kPedAimGun && state!=game::kPedAttack)) {
        error="refused: bounded hit requires ordinary on-foot player with health above50";return false;
    }
    result.healthBefore=health;result.armourBefore=game::Armour(player);
    result.native=damage(player,nullptr,17,amount,3,0);
    if(current()!=player || lookup(ref)!=player) {error="native hit completed; player generation changed";return false;}
    result.healthAfter=game::Health(player);result.armourAfter=game::Armour(player);return true;
}
inline bool RollPlayerCar(uint8_t *player,int playerRef,uint8_t *car,int carRef,bool inverted,CurrentPlayer current,
                          PedLookup peds,PedLookup vehicles,MatrixUpdate updateMatrix,EntityUpdate updateFrame,std::string &error) {
    if(!player || !car || playerRef<0 || carRef<0 || !current || !peds || !vehicles || !updateMatrix || !updateFrame ||
       current()!=player || peds(playerRef)!=player || vehicles(carRef)!=car) {
        error="refused: current driver/vehicle generation changed";return false;
    }
    const float hp=game::VehicleHealth(car);const CVector speed=game::EntitySpeed(car);
    if(!game::PedInVehicle(player) || game::PedVehicle(player)!=car || game::PedState(player)!=game::kPedDriving ||
       game::Health(player)<=0 || *reinterpret_cast<uint8_t **>(car+0x218)!=player ||
       *reinterpret_cast<const int32_t *>(car+0x348)!=0 || game::IsAircraft(car) || car[0x294]==2 || game::Wrecked(car) ||
       !std::isfinite(hp) || hp<=390 || !std::isfinite(speed.x) || !std::isfinite(speed.y) || !std::isfinite(speed.z) ||
       std::hypot(std::hypot(speed.x,speed.y),speed.z)*50>1 ||
       !*reinterpret_cast<void **>(car+0x48) || !game::EntityClump(car)) {
        error="refused: requires current living driver in a parked healthy ordinary automobile";return false;
    }
    auto &right=*reinterpret_cast<CVector *>(car+8),&forward=*reinterpret_cast<CVector *>(car+0x18),&up=*reinterpret_cast<CVector *>(car+0x28);
    auto dot=[](CVector a,CVector b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    for(CVector axis:{right,forward,up})if(!std::isfinite(dot(axis,axis)) || std::fabs(dot(axis,axis)-1)>.05f){error="refused: non-unit vehicle basis";return false;}
    if(std::fabs(dot(right,forward))>.05f || std::fabs(dot(right,up))>.05f || std::fabs(dot(forward,up))>.05f || std::fabs(up.z)<.25f){error="refused: skewed or mid-roll vehicle basis";return false;}
    if((up.z<0)==inverted){error="requested roll sign already present";return true;}
    right={-right.x,-right.y,-right.z};up={-up.x,-up.y,-up.z};
    // Native RwMatrixUpdate clears cached type/identity bits. Preserve every
    // other matrix flag and all translation/velocity/controller fields.
    *reinterpret_cast<uint32_t *>(car+0x14)&=~uint32_t(0x20003);
    updateMatrix(car+8);updateFrame(car);
    error="native car basis rolled; physics and camera retain subsequent ownership";return true;
}
struct WeaponSnapshot { uint8_t *ped=nullptr; int ref=-1,slot=-1,type=-1; };
struct ShotResult { int weapon=-1,piece=-1; Vec3 point{}; };
struct WindowTransition { void *owner=nullptr; int desired=-1; uint64_t since=0; };
using WindowStyle = uintptr_t (*)(void *);
using WindowToggle = void (*)(void *);
inline bool NativeWindow(bool requested,void *window,int rendererWindowed,uint64_t now,
                         WindowStyle style,WindowToggle toggle,WindowTransition &transition,std::string &result) {
    if(!window || !style || !toggle) { result="native window unavailable";return false; }
    const bool actual=(style(window)&(uintptr_t(1)<<14))!=0;
    if(transition.owner!=window || (transition.desired>=0 && actual==bool(transition.desired)) || now-transition.since>=3000000000ULL)
        transition={};
    if(transition.desired>=0) {
        result="native fullscreen transition pending";
        return transition.desired==int(requested);
    }
    if(actual==requested) { result="native fullscreen already matches requested mode";return true; }
    if(rendererWindowed!=1) { result="refused: native fullscreen requires the windowed renderer";return false; }
    transition={window,int(requested),now};
    toggle(window);result="native fullscreen transition requested; inspect window status after completion";
    return true;
}

inline bool PhysicalRoot(bool active, reactphysics::Mode mode) {
    return active && mode!=reactphysics::Mode::Balanced;
}
inline int OwnedWeaponSlot(uint8_t *ped,int type) {
    if(!ped || type<0 || type>36)return -1;
    for(int slot=0;slot<game::kWeaponSlots;++slot)
        if(game::WeaponType(game::PedWeapon(ped,slot))==type)return slot;
    return -1;
}
inline bool CaptureWeapon(uint8_t *ped,int ref,PedLookup lookup,WeaponSnapshot &snapshot) {
    if(!ped || ref<0 || !lookup || lookup(ref)!=ped)return false;
    if(snapshot.ped==ped && snapshot.ref==ref)return true;
    const int slot=game::PedCurrentWeaponSlot(ped);
    if(slot<0 || slot>=game::kWeaponSlots)return false;
    snapshot={ped,ref,slot,game::WeaponType(game::PedWeapon(ped,slot))};
    return true;
}
inline bool SelectOwnedWeapon(uint8_t *ped,int ref,int type,PedLookup lookup,SetWeaponSlot select,
                              WeaponSnapshot &snapshot,std::string &error) {
    const int slot=OwnedWeaponSlot(ped,type);
    if(slot<0) { error="refused: requested weapon is not already owned";return false; }
    if(!select || !CaptureWeapon(ped,ref,lookup,snapshot)) { error="refused: player generation or current slot changed";return false; }
    if(game::PedCurrentWeaponSlot(ped)!=slot)select(ped,slot);
    if(lookup(ref)!=ped || game::PedCurrentWeaponSlot(ped)!=slot) { error="native weapon selection was not retained";return false; }
    return true;
}
inline bool RestoreWeapon(uint8_t *ped,PedLookup lookup,SetWeaponSlot select,WeaponSnapshot &snapshot,std::string &error) {
    const WeaponSnapshot saved=snapshot;snapshot={};
    if(!ped || !lookup || !select || saved.ped!=ped || saved.ref<0 || lookup(saved.ref)!=ped ||
       saved.slot<0 || saved.slot>=game::kWeaponSlots || game::WeaponType(game::PedWeapon(ped,saved.slot))!=saved.type) {
        error="weapon restore skipped: original generation/owned slot no longer matches";return false;
    }
    if(game::PedCurrentWeaponSlot(ped)!=saved.slot)select(ped,saved.slot);
    return true;
}
inline bool Firearm(int type) { return (type>=17&&type<=29)||type==32||type==33; }
inline bool Finite(CVector v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
inline float Separation(CVector a,CVector b) {
    return std::hypot(std::hypot(a.x-b.x,a.y-b.y),a.z-b.z);
}
inline bool FireNpcShot(uint8_t *player,int playerRef,uint8_t *npc,int npcRef,CVector source,CVector target,
                        PedLookup lookup,LineQuery line,BulletImpact impact,ShotResult &result,std::string &error) {
    if(!player || !npc || player==npc || playerRef<0 || npcRef<0 || !lookup || !line || !impact ||
       lookup(playerRef)!=player || lookup(npcRef)!=npc) { error="refused: player/owned NPC generation changed";return false; }
    if(!Finite(source) || !Finite(target) || Separation(source,game::EntityPos(npc))>30 ||
       Separation(target,game::EntityPos(npc))>3 || Separation(source,target)<.01f) {
        error="refused: finite source must be within 30 metres of owned NPC and target its current skeleton";return false;
    }
    const int slot=game::PedCurrentWeaponSlot(player);
    if(slot<0 || slot>=game::kWeaponSlots) { error="invalid current player weapon slot";return false; }
    uint8_t *weapon=game::PedWeapon(player,slot);const int type=game::WeaponType(weapon);
    if(!Firearm(type) || game::WeaponAmmoTotal(weapon)<=0) { error="refused: hold an already-owned loaded firearm";return false; }
    alignas(16) uint8_t queried[0x30]{};uint8_t *hit=nullptr;
    if(!line(source,target,queried,hit,true,true,true,true,true,false,false,false) || hit!=npc) {
        error="refused: native line of sight did not first hit the owned NPC";return false;
    }
    alignas(16) uint8_t copied[sizeof queried];std::memcpy(copied,queried,sizeof copied);
    reactcontact::NativePoint point;std::memcpy(&point,copied,sizeof point);
    if(!Finite({point.point.x,point.point.y,point.point.z}) || point.pieceB>6 ||
       lookup(playerRef)!=player || lookup(npcRef)!=npc || game::PedCurrentWeaponSlot(player)!=slot ||
       game::WeaponType(weapon)!=type || game::WeaponAmmoTotal(weapon)<=0) {
        error="refused: native contact, weapon or actor generation changed during line query";return false;
    }
    result={type,point.pieceB,point.point};
    impact(weapon,player,npc,&source,&target,copied,{0,0});
    return true;
}
} // namespace probe
