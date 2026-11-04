#include "firemodel.h"
#include <algorithm>
#include <cmath>
namespace firemodel {
void Ignite(State &s){if(!s.spent){s.lit=true;s.suppressed=false;s.exposure=0;}}
void Extinguish(State &s){s.lit=false;s.age=s.exposure=0;s.suppressed=true;}
void Rearm(State &s){s.suppressed=false;s.exposure=0;}
float Intensity(const State &s){return s.lit?float(.3+.7*std::clamp(s.age/15.,0.,1.)):0;}
void Tick(State &s,const Input &i,float dt)
{
 if(!std::isfinite(dt)||dt<=0||dt>1||!std::isfinite(i.health)||!std::isfinite(i.heat)||!std::isfinite(i.fuel)||!std::isfinite(i.electrical)||!std::isfinite(i.breach))return;
 if(s.lit){s.age+=dt;if(s.age>=60-1e-4){s.age=60;s.spent=true;s.lit=false;}return;}
 if(s.spent||s.suppressed)return;
 const bool system=i.running&&i.fuel>.01f&&((i.heat>=1.1f&&i.breach>=.15f)||(i.electrical>=.8f&&i.breach>=.3f));
 s.exposure=i.health<250||system?s.exposure+dt:0;
 if(s.exposure>=2.5-1e-4)Ignite(s);
}
}
