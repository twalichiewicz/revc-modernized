#pragma once
#include <cmath>

namespace populationrules {
struct Spawn {
	float distance=70;
	int local=0, limit=32, nearby=0, freeSlots=20;
	bool cop=false, visible=false, ground=true, clear=true, loaded=true, path=true, outdoors=true;
};
inline bool Eligible(const Spawn &s)
{
	return s.loaded && s.outdoors && s.path && !s.visible && s.ground && s.clear &&
	       std::isfinite(s.distance) && s.distance>=(s.cop?45.0f:32.0f) && s.distance<=145.0f &&
	       s.local<s.limit && s.nearby<4 && s.freeSlots>8;
}
struct Release {
	float distance=200;
	bool visible=false, random=true, attached=false, inVehicle=false, deletable=true;
};
inline bool CanVirtualize(const Release &r)
{
	return r.distance>180 && !r.visible && r.random && !r.attached && !r.inVehicle && r.deletable;
}
inline bool SameOwner(int expected,int current,bool mission,bool attached)
{
	return expected>=0 && expected==current && !mission && !attached;
}
struct PathStep { int node=-1,next=-1; float along=0; };
inline PathStep Travel(PathStep before,PathStep proposed,bool wasOpen,bool staysOpen)
{
	return wasOpen && staysOpen ? proposed : before;
}
}
