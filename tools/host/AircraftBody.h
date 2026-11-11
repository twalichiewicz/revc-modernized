#pragma once
#include "aircraftbodybridge.h"
#include <cstddef>
class CEntity;
class CPhysical;
class CVehicle;
class CVector;
struct CColPoint;
void AircraftBodyFrame();
void AircraftBodyDeleting(const CEntity *);
void AircraftBodyShutdown();
// Native shift passes request current overlap, never the frame's earlier CCD
// trajectory. Replaying that trajectory during correction can amplify shifts.
// An actor's own shift pass sweeps only that pass's move, from the pose the
// pass started at; crossings carry minimal depth so the native pass undoes it.
struct AircraftBodyOverlapQuery {
 AircraftBodyOverlapQuery();
 explicit AircraftBodyOverlapQuery(CPhysical *);
 ~AircraftBodyOverlapQuery();
 AircraftBodyOverlapQuery(const AircraftBodyOverlapQuery&)=delete;
 AircraftBodyOverlapQuery&operator=(const AircraftBodyOverlapQuery&)=delete;
private:
 bool shift=false;
};
// -1 leaves the pair to native collision. Otherwise these witnesses replace
// the old moving-aircraft proxy with sweeps of the committed material surface.
int AircraftBodyContacts(CPhysical *,CEntity *,CColPoint *);

bool AircraftBodyFirePosition(const CEntity *,CVector &);
// Called only after native rigid physics accepted an impulse. Speeds use m/s,
// impulse uses native kg*m/tick and is converted once at this boundary.
void AircraftBodyAcceptedImpulse(CPhysical *,CEntity *,const CColPoint &,float impulse,float closingBefore,float outwardAfter);
void AircraftBodyAcceptedWeapon(CVehicle *,int weapon,float damage,const CVector &point);

bool AircraftBodyBuoyancyBounds(CPhysical *,CVector &lo,CVector &hi);

CVector AircraftBodySurfaceSpeed(CEntity *,const CVector &worldPoint);

CVector AircraftBodyCOMOffset(CPhysical *);
// Yield-limited static-world response: the impulse applied to motion (native,
// or the struck part's yield force over the step); queues the plastic push.
float AircraftBodyYieldImpulse(CPhysical *,CEntity *,const CColPoint &,float impulse);
// Whether the last applied response yielded (read and clear).
bool AircraftBodyYielded();
// The impulse a just-applied response actually transmitted (yield cap or native).
float AircraftBodyAppliedImpulse(float native);
// Native collision reports A stuck only for rigid (non-yielding) responses.
bool AircraftBodyRigid(int responses,int yielded);
// Smoke only: velocity changes from static responses (kind 0) and shift damping (1).
void AircraftBodySmokeMomentum(CPhysical *,int kind,const CVector &delta);
void AircraftBodySmokeMomentumTake(int reference,float out[7],bool reset);
float AircraftBodySmokeYieldWork(CVehicle *);
CVector AircraftBodyComposeCOM(CPhysical *,const CVector &native);

// Last native/plug-in animation writer has completed before the renderer reads.
void AircraftBodyRenderBoundary();

void AircraftBodyPrepareClump(CEntity *);
void AircraftBodyClumpCreated(CEntity *);
void AircraftBodyAcceptedMelee(CVehicle *,CEntity *,int weapon,float work,const CVector &point);

// Native LOD callbacks may cull, but cannot replace committed private meshes.
bool AircraftBodyOwnsAtomic(const void *);
bool AircraftBodyOwnsClump(const void *);

// Called after native world insertion; publish the visible envelope before its
// first collision query, without waiting for the old proxy to report a hit.
void AircraftBodyAdded(CEntity *);
// Regular-game smoke only (VC_REGULAR_SMOKE): log this vehicle reference's
// material contacts and accepted impulses; -1 stops tracing.
void AircraftBodyTrace(int reference);
// Smoke only: lowest world z of the committed non-wheel material vertices.
bool AircraftBodySmokeLowest(CVehicle *,float &z);
bool AircraftBodySmokeLowestPart(CVehicle *,char *out,size_t size);
// Smoke only: leave a native actor unowned (stock) for A/B comparison.
void AircraftBodySmokeUnowned(const void *vehicle,bool unowned);
void AircraftBodyTraceAlso(int reference);
void AircraftBodySmokeWork(int reference,float &work,int &events,bool reset);
bool AircraftBodySmokeModules(CVehicle *,char *out,size_t size);
