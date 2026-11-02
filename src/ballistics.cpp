#include "ballistics.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "nativebullet.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <dlfcn.h>

namespace ballistics {
namespace {
using Fire = bool (*)(uint8_t *, uint8_t *, CVector *);
using FireCar = bool (*)(uint8_t *, uint8_t *, bool, bool);
Fire gFire;
FireCar gFireCar;
uint8_t *(*gInfo)(int32_t);
float *gFarClip;
uint8_t *gBullets;
bool (*gAddBullet)(uint8_t *,int32_t,CVector,CVector);
bool gInstalled;
// GetWeaponInfo's native stride is 0x64; FireInstantHit reads range at+4.
// Loan a shot-local copy instead of mutating the shared weapon/AI table.
constexpr size_t kInfoBytes=0x64;
struct Shot;
thread_local Shot *gShot;
bool Bullet(int type) { return (type>=17 && type<=27) || type==32 || type==33; }
struct Shot {
	Shot *parent=gShot;
	alignas(4) std::array<uint8_t,kInfoBytes> info{};
	int type=-1;
	bool copied=false;
	Shot(uint8_t *weapon,uint8_t *shooter)
	{
		// Even a nested NPC/projectile call hides the outer player's scope.
		gShot=this;
		if(weapon && shooter && game::api.FindPlayerPed && shooter==game::api.FindPlayerPed()) {
			const int held=game::WeaponType(weapon);
			if(Bullet(held))type=held;
		}
	}
	~Shot(){gShot=parent;}
};
uint8_t *Hook_Info(int32_t type)
{
	uint8_t *native=gInfo(type);
	if(!native || !gShot || type!=gShot->type)return native;
	int fireType;float range;
	std::memcpy(&fireType,native,4);std::memcpy(&range,native+4,4);
	if(fireType!=1 || !std::isfinite(range) || range<=0)return native;
	if(!gShot->copied) {
		std::memcpy(gShot->info.data(),native,kInfoBytes);
		range=VisibleRange(range);
		std::memcpy(gShot->info.data()+4,&range,4);
		gShot->copied=true;
	}
	return gShot->info.data();
}
bool Hook_Fire(uint8_t *weapon, uint8_t *shooter, CVector *source)
{
	const Shot shot(weapon,shooter);
	return gFire(weapon,shooter,source);
}
bool Hook_FireCar(uint8_t *weapon, uint8_t *car, bool left, bool right)
{
	const Shot shot(weapon,car?*reinterpret_cast<uint8_t **>(car+0x218):nullptr);
	return gFireCar(weapon,car,left,right);
}
bool Hook_AddBullet(uint8_t *creator,int32_t type,CVector position,CVector velocity)
{
	if(!gBullets || !creator || !game::api.FindPlayerPed || creator!=game::api.FindPlayerPed() || (type!=28 && type!=29))
		return gAddBullet(creator,type,position,velocity);
	std::array<bool,nativebullet::kCount> occupied;
	for(size_t i=0;i<occupied.size();++i)occupied[i]=nativebullet::Read(gBullets,i).active!=0;
	const bool result=gAddBullet(creator,type,position,velocity);
	if(!result || creator!=game::api.FindPlayerPed())return result;
	// Only the uniquely new matching record belongs to this native call.
	// Never rewrite an existing bullet or assume which free slot was selected.
	size_t found=nativebullet::kCount;
	for(size_t i=0;i<occupied.size();++i){
		if(occupied[i])continue;
		const auto record=nativebullet::Read(gBullets,i);
		if(record.active!=1 || record.creator!=creator || record.type!=type ||
		   record.position.x!=position.x || record.position.y!=position.y || record.position.z!=position.z ||
		   record.velocity.x!=velocity.x || record.velocity.y!=velocity.y || record.velocity.z!=velocity.z)continue;
		if(found!=nativebullet::kCount)return result;
		found=i;
	}
	if(found==nativebullet::kCount)return result;
	const auto record=nativebullet::Read(gBullets,found);
	// Native Update uses velocity * timeStep * 0.5, at 50 units/second.
	const float speed=std::sqrt(velocity.x*velocity.x+velocity.y*velocity.y+velocity.z*velocity.z)*25.0f;
	if(!std::isfinite(speed) || speed<=1e-4f || !std::isfinite(record.deadline))return result;
	const float duration=std::clamp(std::ceil(VisibleRange(speed)/speed*1000.0f),1000.0f,10000.0f);
	const float deadline=record.deadline+(duration-1000.0f);
	if(std::isfinite(deadline))std::memcpy(gBullets+found*nativebullet::kStride+offsetof(nativebullet::Record,deadline),&deadline,4);
	return result;
}
}
float VisibleRange(float nativeRange)
{
	if(!std::isfinite(nativeRange) || nativeRange<=0)return nativeRange;
	float visible=gFarClip?*gFarClip:1000.0f;
	if(!std::isfinite(visible) || visible<=0)visible=1000.0f;
	return std::max(nativeRange,std::min(visible,4000.0f));
}
bool Install()
{
	if(gInstalled)return true;
	gFarClip=reinterpret_cast<float *>(dlsym(RTLD_MAIN_ONLY,"_ZN5CDraw12ms_fFarClipZE"));
	gBullets=static_cast<uint8_t *>(dlsym(RTLD_MAIN_ONLY,"gaBulletInfo"));
	if(!gFarClip || !gBullets || !game::api.FindPlayerPed)return false;
	gInfo=reinterpret_cast<decltype(gInfo)>(hooks::Chain("_ZN11CWeaponInfo13GetWeaponInfoE11eWeaponType",reinterpret_cast<void *>(Hook_Info)));
	gFire=reinterpret_cast<Fire>(hooks::Chain("_ZN7CWeapon4FireEP7CEntityP7CVector",reinterpret_cast<void *>(Hook_Fire)));
	gFireCar=reinterpret_cast<FireCar>(hooks::Chain("_ZN7CWeapon21FireInstantHitFromCarEP8CVehiclebb",reinterpret_cast<void *>(Hook_FireCar)));
	gAddBullet=reinterpret_cast<decltype(gAddBullet)>(hooks::Chain("_ZN11CBulletInfo9AddBulletEP7CEntity11eWeaponType7CVectorS3_",reinterpret_cast<void *>(Hook_AddBullet)));
	gInstalled=gInfo && gFire && gFireCar && gAddBullet;
	if(gInstalled)Log("ballistics: player bullets trace to visible distance; native AI/weapon table retained");
	return gInstalled;
}
} // namespace ballistics
