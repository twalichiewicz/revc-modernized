// Compiled with the host's exact feature flags. Failure blocks plugin loading.
#include "common.h"
#include "Pools.h"
#include "Plane.h"
#include "PlayerPed.h"
#include "PlayerInfo.h"
#include "Frontend.h"
#include "Camera.h"
#include "Pad.h"
#include "Radar.h"
#include <cstddef>
#define SIZE(T,N) static_assert(sizeof(T)==N,#T " size differs from mod ABI")
#define OFFSET(T,F,N) static_assert(offsetof(T,F)==N,#T "." #F " differs from mod ABI")
SIZE(CEntity,0x78);SIZE(CPhysical,0x160);SIZE(CVehicle,0x350);SIZE(CAutomobile,0x6f0);SIZE(CPlane,0x388);SIZE(CPlayerPed,0x8d0);SIZE(CPlayerInfo,0x1a0);SIZE(CCam,0x1e0);SIZE(CWeapon,0x18);SIZE(sRadarTrace,0x38);SIZE(CMenuScreenCustom,752);SIZE(CMenuScreenCustom::CMenuEntry,40);
OFFSET(CEntity,m_rwObject,0x58);OFFSET(CEntity,m_modelIndex,0x6a);
OFFSET(CPhysical,m_vecMoveSpeed,0x84);OFFSET(CPhysical,m_vecTurnSpeed,0x90);OFFSET(CPhysical,m_fMass,0xcc);
OFFSET(CVehicle,pDriver,0x218);OFFSET(CVehicle,m_fHealth,0x2a0);OFFSET(CVehicle,m_vehType,0x348);OFFSET(CVehicle,m_nRadioStation,0x2e8);
OFFSET(CPed,m_nPedState,0x2fc);OFFSET(CPed,m_nMoveState,0x304);OFFSET(CPed,m_objective,0x19c);OFFSET(CPed,m_nPedType,0x4f8);OFFSET(CPed,m_weapons,0x550);OFFSET(CPed,m_currentWeapon,0x64c);OFFSET(CPed,m_nearPeds,0x6c0);OFFSET(CPed,m_numNearPeds,0x710);OFFSET(CPed,m_pMyVehicle,0x4b8);OFFSET(CPed,m_fHealth,0x44c);OFFSET(CPed,m_fArmour,0x450);
OFFSET(CMenuManager,m_nCurrOption,0x30);OFFSET(CMenuManager,m_bMenuActive,0x38);OFFSET(CMenuManager,m_bGameNotLoaded,0x6c);OFFSET(CMenuManager,m_nCurrScreen,0x16c);OFFSET(CMenuManager,m_nMousePosX,0x1a0);OFFSET(CMenuManager,m_nMousePosY,0x1a4);
OFFSET(CPad,OldState,0x2a);OFFSET(CPad,PCTempJoyState,0x96);OFFSET(CPad,DisablePlayerControls,0xf0);
static_assert(MENUPAGES==45,"mod iterates 45 native menu slots");
extern "C" unsigned long long VCSourceABI(){return 0x5643535243414241ULL;}

// Writable hook pointers deliberately reserved by this host; no segment growth.
extern "C" { __attribute__((section("__DATA,__vchooks"),used,no_sanitize("address"))) uint64_t VCSourceHooks[256]={}; }
