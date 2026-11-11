#pragma once
class CPlane;
class CEntity;
void DodoRigFrame();
void DodoRigShutdown();
bool DodoRigReady(CPlane *);
void DodoRigNeutralWing(CPlane *);
void DodoRigDeleting(const CEntity *);
