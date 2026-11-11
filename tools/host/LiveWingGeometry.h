#pragma once
#include "common.h"
#include "WingPhysics.h"
#include <memory>
#include <string>
#include <vector>
class CPlane;
class CEntity;
// Main-thread publication of one material snapshot into ordinary game actors.
// Capture retains original geometry for repair; no shared model mesh is edited.
class LiveWingGeometry {
 struct Impl;std::unique_ptr<Impl> impl;
public:
 LiveWingGeometry();~LiveWingGeometry();
 bool Capture(CPlane *,int reference,const WpState &,CVector anchor,std::string &error);
 bool Publish(const WpState &,CVector anchor,std::string &error);
 bool Current()const;
 bool Owns(const CEntity *)const;
 int Fragment(const CEntity *)const;
 bool VerifyQueries(const WpState &,CVector anchor,int &checks,std::string &error)const;
 void Restore();
 void Abandon(CEntity *);
 std::vector<CEntity*> DetachDebris();
 std::vector<CEntity*> Participants()const;
};
