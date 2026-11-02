#include "playerreact.h"
#include "aimcam.h"
#include "climb.h"
#include "crouch.h"
#include "fpshoot.h"
#include "game.h"
#include "shootdodge.h"
#include "swim.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <dlfcn.h>

using namespace game;
namespace playerreact {
namespace {
constexpr int kTags[kBones]={0,1,2,3,4,5,31,32,33,34,35,21,22,23,24,25,41,42,43,51,52,53};
constexpr size_t kLookingAtPlayer=0x63, kWideScreen=0x75; // CCamera: scripted camera ownership
Guard gGuard;
uint8_t *gOwner,*gClump;
int16_t gModel;
int gHandle=-1;
uint32_t gLastTime,gAdvancedAt;
CVector gOrigin{};
struct Saved {
	uint8_t *owner=nullptr,*clump=nullptr;
	int16_t model=0;
	int handle=-1;
	uint32_t at=0;
	CVector origin{};
	float *matrices=nullptr;
	int indices[kBones]={};
	Matrices original{};
	bool valid=false;
} gSaved;
Vec3 V(CVector p){return {p.x,p.y,p.z};}
float Distance(CVector a,CVector b){return std::hypot(std::hypot(a.x-b.x,a.y-b.y),a.z-b.z);}
bool IsPlayer(uint8_t *ped){return ped && api.FindPlayerPed && ped==api.FindPlayerPed();}
int Reference(uint8_t *ped)
{
	using NativeReference=int (*)(uint8_t *);
	static const auto reference=reinterpret_cast<NativeReference>(dlsym(RTLD_MAIN_ONLY,"_ZN6CPools9GetPedRefEP4CPed"));
	if(!reference||!api.PoolsGetPed||!ped)return -1;
	const int handle=reference(ped);
	return handle>=0&&api.PoolsGetPed(handle)==ped?handle:-1;
}
bool Eligible(uint8_t *ped)
{
	const uint8_t *pad=api.GetPad?api.GetPad(0):nullptr;
	if(!IsPlayer(ped)||!api.timeMs||!api.timeStep||!EntityClump(ped)||!std::isfinite(Health(ped))||Health(ped)<=0||
	   !pad||PadControlsDisabled(pad)||
	   PedInVehicle(ped)||PedDucking(ped)||swim::Swimming()||climb::Active()||shootdodge::Posing()||crouch::Posing()||
	   (EntityFlags(ped)&7)!=3 || !(EntityFlags(ped)&kEntityUsesCollision) ||
	   *reinterpret_cast<void *const *>(ped+0x720) ||
	   (api.theCamera&&(!api.theCamera[kLookingAtPlayer]||api.theCamera[kWideScreen]))) return false;
	const int state=PedState(ped);
	return state>0 && state<kPedJump;
}
bool Refresh(uint8_t *ped)
{
	if(!IsPlayer(ped)||!api.timeMs)return false;
	const int handle=Reference(ped);
	if(handle<0){gGuard.Reset();gSaved.valid=false;gHandle=-1;return false;}
	const uint32_t now=*api.timeMs;
	const bool replaced=handle!=gHandle||ped!=gOwner||EntityClump(ped)!=gClump||EntityModel(ped)!=gModel||
	                    now<gLastTime||(gLastTime&&now-gLastTime>750)||Distance(EntityPos(ped),gOrigin)>4;
	if(replaced){
		gGuard.Reset();gSaved.valid=false;gAdvancedAt=now;
		gOwner=ped;gClump=EntityClump(ped);gModel=EntityModel(ped);gHandle=handle;
	}
	gLastTime=now;gOrigin=EntityPos(ped);
	if(!Eligible(ped)){gGuard.Reset();return false;}
	return true;
}
float *BoneMatrices(uint8_t *ped)
{
	if(!api.GetAnimHierarchyFromSkinClump||!api.HAnimMatrixArray)return nullptr;
	uint8_t *clump=EntityClump(ped),*hierarchy=clump?api.GetAnimHierarchyFromSkinClump(clump):nullptr;
	return hierarchy?api.HAnimMatrixArray(hierarchy):nullptr;
}
void Restore(uint8_t *ped,bool release)
{
	if(!gSaved.valid||!IsPlayer(ped))return;
	if(ped!=gSaved.owner||Reference(ped)!=gSaved.handle||EntityClump(ped)!=gSaved.clump||EntityModel(ped)!=gSaved.model||!api.timeMs||
	   *api.timeMs<gSaved.at||*api.timeMs-gSaved.at>750||Distance(EntityPos(ped),gSaved.origin)>4||
	   BoneMatrices(ped)!=gSaved.matrices){gSaved.valid=false;return;}
	for(int b=0;b<kBones;++b)
		std::memcpy(gSaved.matrices+gSaved.indices[b]*16,gSaved.original[size_t(b)].data(),sizeof(float)*16);
	// Keep the snapshot through control: camera's next restore contains this guard pose.
	if(release)gSaved.valid=false;
}
bool Aiming(uint8_t *ped)
{
	const int state=PedState(ped);
	return aimcam::Aiming()||fpshoot::Shooting()||state==kPedAttack||state==kPedFight||state==kPedAimGun;
}
Vec3 ViewRight(){return api.theCamera?V(*reinterpret_cast<CVector *>(api.theCamera)):Vec3{1,0,0};}
Vec3 ViewForward(){return api.theCamera?V(*reinterpret_cast<CVector *>(api.theCamera+0x10)):Vec3{0,1,0};}
}
void Contact(uint8_t *ped,Vec3 point,Vec3 normal,Vec3 velocity)
{
	if(!Refresh(ped)||Distance(EntityPos(ped),{point.x,point.y,point.z})>2.5f)return;
	gGuard.Contact(point,normal,velocity);
}
bool CanKnockdown(uint8_t *ped)
{
	return Refresh(ped)&&PedState(ped)==kPedIdle&&!Aiming(ped)&&
	       !(PedFlags(ped)&(kPedInTheAir|kPedLanding));
}
bool CanResistContact(uint8_t *ped) { return Refresh(ped); }
bool CanRagdoll(uint8_t *ped) { return Refresh(ped); }
bool CanStayDown(uint8_t *ped) { return IsPlayer(ped) && !swim::Swimming(); }
void BeforeControl(uint8_t *ped)
{
	Restore(ped,false);
	Refresh(ped);
}
void AfterControl(uint8_t *ped)
{
	if(!Refresh(ped))return;
	const uint32_t now=*api.timeMs;
	if(now!=gAdvancedAt){gGuard.Advance(std::clamp(*api.timeStep/50.0f,0.0f,.10f));gAdvancedAt=now;}
}
void BeforePreRender(uint8_t *ped)
{
	Restore(ped,true);
	Refresh(ped);
}
void AfterPreRender(uint8_t *ped)
{
	if(!Refresh(ped)||gGuard.Current().weight<=0||!api.HAnimIDGetIndex||!api.GetAnimHierarchyFromSkinClump)return;
	uint8_t *hierarchy=api.GetAnimHierarchyFromSkinClump(EntityClump(ped));
	float *matrices=hierarchy&&api.HAnimMatrixArray?api.HAnimMatrixArray(hierarchy):nullptr;
	if(!matrices)return;
	Saved saved;
	for(int b=0;b<kBones;++b){
		const int i=api.HAnimIDGetIndex(hierarchy,kTags[b]);
		if(i<0||i>=128)return;
		saved.indices[b]=i;
		std::memcpy(saved.original[size_t(b)].data(),matrices+i*16,sizeof(float)*16);
	}
	saved.owner=ped;saved.clump=EntityClump(ped);saved.model=EntityModel(ped);saved.handle=gHandle;saved.at=*api.timeMs;
	saved.origin=EntityPos(ped);saved.matrices=matrices;saved.valid=true;
	const Matrices posed=Pose(saved.original,gGuard.Current(),V(EntityForward(ped)),Aiming(ped));
	for(int b=0;b<kBones;++b)std::memcpy(matrices+saved.indices[b]*16,posed[size_t(b)].data(),sizeof(float)*16);
	gSaved=saved;
}
Response Current(uint8_t *ped){return Refresh(ped)?gGuard.Current():Response{};}
vm::Frame ViewTransform(uint8_t *ped){return ViewTransform(Current(ped),ViewRight(),ViewForward(),ped&&Aiming(ped));}
vm::Frame GuardHand(uint8_t *ped,bool left){return GuardHand(Current(ped),ViewRight(),ViewForward(),left);}
} // namespace playerreact
