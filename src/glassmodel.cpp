#include "glassmodel.h"
#include "vehiclebodylayout.h"
#include <cmath>
#include <cstring>
#include <strings.h>

namespace glassmodel {
// The source host admits material glazing by this same rule.
static_assert(int(Pane::Front)==int(aircraftbodymodel::GlassPane::Front)&&int(Pane::RightRear)==int(aircraftbodymodel::GlassPane::RightRear),"shared pane order");
Pane Classify(const char *name,Vec3 p,Vec3 n,uint8_t alpha,const char *texture)
{
 return Pane(aircraftbodymodel::CabinGlass(name,p,n,alpha,texture));
}
Pane DoorPane(int door)
{
 switch(door){case 1:return Pane::Rear;case 2:return Pane::LeftFront;case 3:return Pane::RightFront;
 case 4:return Pane::LeftRear;case 5:return Pane::RightRear;default:return Pane::None;}
}
}
