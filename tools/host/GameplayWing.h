#pragma once
#include "common.h"
#include "WingPhysics.h"
class CEntity;
class CPhysical;
// Ordinary-world integration. No alternate scene or user-facing test controls.
void GameplayWingFrame();
bool GameplayWingOwns(const CEntity *);
bool GameplayWingSkipNativeThisPass(const CEntity *);
void GameplayWingReleaseForExternal(const CEntity *);
bool GameplayWingRejectNative(const CEntity *,const char *method);
bool GameplayWingSnapshot(WpState *,unsigned *blocked,unsigned *failed);
bool GameplayWingCaptureMove(CPhysical *,CVector impulse);
bool GameplayWingCaptureTurn(CPhysical *,CVector impulse,CVector lever);
void GameplayWingDeleting(CEntity *);
void GameplayWingShutdown();
bool GameplayWingGroundCoverage(CVector point,CVector *ground);
extern "C" double VCSourceWingIntegrity(const void *vehicle);
extern "C" bool VCSourceWingOwns(const void *entity);
extern "C" bool VCSourceWingGeometryOwned(const void *entity);
extern "C" void VCSourceRepairWing(void *vehicle);
