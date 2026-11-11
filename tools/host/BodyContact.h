#pragma once
#include "vec3.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
namespace bodycontact {
inline float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec3 Sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
// Native thin triangle walls are two-sided. The one recovery exception is a
// material vertex emerging through support below an already above-floor actor.
// Reversing that floor normal would drive the carrier into its supporting slab.
// The reference is the body's top at the sweep's start pose, not its current
// origin: a sinking carrier's origin passes below the floor before its roof.
inline bool Sweep(Vec3 previous,Vec3 current,Vec3 point,Vec3 &normal,Vec3 origin,bool stationary,float &depth){
 float approach=Dot(Sub(current,previous),normal);
 if(approach>0){if(stationary&&normal.z>.5f&&Dot(Sub(origin,point),normal)>0)return false;normal={-normal.x,-normal.y,-normal.z};}
 depth=Dot(Sub(point,current),normal);return depth>.00001f;
}
// A native shift pass moves the actor without CCD and shifts it by contact
// depth. A material vertex entering a face during that pass's own move is
// reported with this minimal depth: the pass finds the collision again and
// undoes the move rather than accepting a tunnelled pose or amplifying a shift.
// Leaving through a back face resolves earlier penetration and stays allowed,
// so a shift pass can never trap material that the frame sweep let in.
constexpr float kShiftDepth=.0001f;
// Writes a material contact into a native colpoints slot. The engine's
// CColPoint::operator= copies everything but depth (as the original game did),
// so plain assignment would hand ProcessShiftSectorList whatever depth the slot
// held: uninitialised stack in a shift pass. It translates by normal*depth.
template<class Point> inline void Store(Point &out,const Point &in){out=in;out.depth=in.depth;}
// Yield-limited static-world contact. A struck part carries at most its yield
// force F (N) for one native step: the cap, in native impulse units (kg x m per
// 1/50 s), is F x (timestep/50 s) / 50. The impulse above it becomes plastic
// deformation; contacts that yielded do not hold the actor stuck.
inline float YieldCap(float force,float timestep){return std::isfinite(force)&&force>0&&std::isfinite(timestep)&&timestep>0?force*(timestep/50)/50:0;}
inline float Applied(float impulse,float cap,bool &yielded){yielded=cap>0&&std::isfinite(impulse)&&impulse>cap;return yielded?cap:impulse;}
inline bool Stuck(int responses,int yielded){return responses>yielded;}
// Body-frame push: material never moves toward the body's underside.
inline Vec3 BodyPush(Vec3 local){return {local.x,local.y,std::max(0.f,local.z)};}
// Material contacts are probed with a 2 cm skin. A resolved rigid contact whose
// depth is within it is resting (touching), not interpenetration: native stuck
// handling, which undoes interpenetration, does not apply to it.
constexpr float kRestSkin=.02f;
// Only struck obstacles yield. A face under the body (world normal z > .7) is
// its support: it keeps the native rigid response and stuck handling.
inline bool Yieldable(Vec3 worldNormal){return !(worldNormal.z>.7f);}
// Rest-probe depth is the skin less the distance above the face; a crossing's
// depth is how far it went through. Only a probe touching from outside can be
// resting: a crossing, a probe inside a box or a vertex recovered from under a
// floor is penetration, even within the skin, and keeps native stuck handling
// (an accepted 2 cm crossing per frame ratchets a car on its roof into the street).
// A crossing within the penetration slop is accepted like contact: native stuck
// handling undoes the whole step, and a car pressed on a wall that crosses it by
// a millimetre each frame would freeze in place. What the slop admits is read
// from behind next frame and pushed back out (PassedThroughFace).
constexpr float kPenetrationSlop=.005f;
inline bool Resting(float depth,bool yielded,bool penetrating){return !yielded&&std::isfinite(depth)&&depth<=(penetrating?kPenetrationSlop:kRestSkin);}
// Distance back out of a struck face: the current depth or this step's travel
// into it at the closing speed (m/s), timestep in 1/50 s.
inline float BackExit(float depth,float closing,float timestep){return std::max(depth,std::max(0.f,closing)*timestep/50);}
// Plastic push out of the obstacle by the shortest exit: up onto a top face
// that lies within the penetration (a lip under a kerb edge), otherwise back
// along the struck face's normal. lift<0: no top face within reach.
// The ground under the car stops material going down: no downward part.
inline Vec3 Push(Vec3 normal,float depth,float lift){if(lift>=0&&lift<depth)return {0,0,lift};return {normal.x*depth,normal.y*depth,std::max(0.f,normal.z*depth)};}
// A probe whose centre is inside a solid box exits by the nearest face.
// Native CCollision::ProcessSphereBox, for a centre inside the box, takes the
// normal from the box centre, puts the point 1 m along it and reports the
// largest of the three closer-side distances as depth (the original game's).
// A yielding bumper corner 5 cm inside a palm trunk (a 1.4 m x 28 m box) then
// reads as 0.6 m deep with a straight-down normal, which a shift pass applies
// as a 0.4 m drop through the kerb. Box-local coordinates; like the native
// outside case, depth includes the probe radius.
inline bool InsideBox(Vec3 c,float radius,Vec3 lo,Vec3 hi,Vec3 &point,Vec3 &normal,float &depth){
 if(!(c.x>lo.x&&c.x<hi.x&&c.y>lo.y&&c.y<hi.y&&c.z>lo.z&&c.z<hi.z))return false;
 const float d[6]={c.x-lo.x,hi.x-c.x,c.y-lo.y,hi.y-c.y,c.z-lo.z,hi.z-c.z};int k=0;for(int i=1;i<6;++i)if(d[i]<d[k])k=i;
 const float out=k%2?1.f:-1.f;normal={0,0,0};point=c;
 if(k<2){normal.x=out;point.x=out>0?hi.x:lo.x;}else if(k<4){normal.y=out;point.y=out>0?hi.y:lo.y;}else{normal.z=out;point.z=out>0?hi.z:lo.z;}
 depth=d[k]+radius;return true;
}
// The body's highest world point at a pose: its bounding-box centre raised by
// the box's vertical world extent. The local top-centre is under a car resting
// on its roof, so floor tests keyed on it treat the street as overhead.
inline Vec3 WorldTop(Vec3 centre,Vec3 right,Vec3 forward,Vec3 up,Vec3 half){return {centre.x,centre.y,centre.z+std::fabs(right.z)*half.x+std::fabs(forward.z)*half.y+std::fabs(up.z)*half.z};}
// Native sphere-triangle tests are two-sided: a material vertex just behind a
// static face reads its back (normal flipped, depth = skin - distance), takes
// no response while it moves further in and is never pushed back. A car on its
// roof settles through the street that way, and a car pressed on a facade creeps
// into the building. When the body is in front of the face (front: its world
// top for a floor, its centre otherwise), that vertex went through: it exits
// back through the face, by its distance behind it plus the skin. Ceilings over
// a body, walls it is behind and decks it is under keep the two-sided reading.
inline bool PassedThroughFace(Vec3 hitNormal,Vec3 faceNormal,Vec3 hitPoint,Vec3 front,float depth,float radius,Vec3 &normal,float &exit){
 if(!std::isfinite(depth)||!(Dot(hitNormal,faceNormal)<0)||!(Dot(Sub(front,hitPoint),faceNormal)>0))return false;
 normal=faceNormal;exit=2*radius-std::clamp(depth,0.f,radius);return true;
}
inline bool ShiftCrossing(Vec3 start,Vec3 current,Vec3 point,Vec3 &normal,float &depth){if(Dot(Sub(current,start),normal)>=0)return false;depth=Dot(Sub(point,current),normal);if(depth<=.00001f)return false;depth=kShiftDepth;return true;}
// Native GTA bodies have no tyre or suspension compliance: a wheeled vehicle
// that bottoms out, or kerbs a tyre, receives a rigid impulse carrying energy
// its springs and tyres store. Native springs reach 4*m*g at full compression
// and rest at a quarter of it, so a stroke L stores ~1.9*m*g*L; GTA's g=20 and
// the catalog's 0.45-0.95 m suspension lines give sqrt(3.75*g*L) ~= 7 m/s.
constexpr float kComplianceSpeed=7,kWorkFloorSpeed=1;
// Accepted native impulse (kg*m/s), closing/outward normal speeds (m/s).
// The native single-point response is J = m_eff*(closing+outward).
inline float EffectiveMass(float impulse,float closing,float outward){const float dv=closing+outward;if(!std::isfinite(impulse)||impulse<=0||!std::isfinite(dv))return 0;return dv>1e-3f?impulse/dv:closing>1e-3f?impulse/closing:0;}
// Dissipated material work, less what compliant tyres/suspension absorb, with
// a floor scaled by the effective mass (a 1 m/s stop at that point).
inline float ChassisWork(float impulse,float closing,float outward,bool compliant){
 if(!std::isfinite(impulse)||impulse<=0||!std::isfinite(closing)||closing<0||!std::isfinite(outward))return 0;
 const float raw=std::max(0.f,.5f*impulse*(closing-outward)),mass=EffectiveMass(impulse,closing,outward),work=compliant?raw-.5f*mass*kComplianceSpeed*kComplianceSpeed:raw;
 return work>=.5f*mass*kWorkFloorSpeed*kWorkFloorSpeed&&work>0?work:0;
}
// Vehicle-local material point/normal; wheel centre height and tyre radius.
inline bool Underbody(Vec3 point,Vec3 normal,float wheelZ,float radius){return normal.z>.7f&&point.z<wheelZ+.5f*radius;}
inline bool Tyre(Vec3 point,const Vec3 *wheels,int count,float radius){for(int i=0;i<count;++i){const auto d=Sub(point,wheels[i]);if(Dot(d,d)<radius*radius*1.21f)return true;}return false;}
// A native building pass averages every contact's response. Its consumer keeps
// the strongest witness per body region, so the regions share that response.
class Pass {
 const void *a_=nullptr,*b_=nullptr;uint32_t frame_=0,regions_=0;std::vector<std::pair<size_t,float>> events_;
public:
 void Begin(const void *a,const void *b,uint32_t frame){a_=a;b_=b;frame_=frame;regions_=0;events_.clear();}
 bool Matches(const void *a,const void *b,uint32_t frame)const{return a_&&a==a_&&b==b_&&frame==frame_;}
 // Returns the divisor now applying to every event recorded in this pass.
 float Record(size_t index,float work,int region){if(region>=0&&region<32)regions_|=1u<<region;events_.push_back({index,work});int count=0;for(uint32_t r=regions_;r;r&=r-1)++count;return float(std::max(1,count));}
 const std::vector<std::pair<size_t,float>> &Events()const{return events_;}
};
}
