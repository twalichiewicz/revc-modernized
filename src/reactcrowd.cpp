#include "reactcrowd.h"
#include "reactshape.h"
#include <array>
#include <algorithm>
#include <cmath>
namespace reactcrowd {
namespace {
using namespace reactphysics;
using namespace reactshape;
constexpr int VolumeCount=Count+11, Passes=12;
constexpr float Slop=.0005f;
struct Actor {Entry entry;std::array<Capsule,VolumeCount> volumes;};
struct PairContact {Vec3 normal{};float depth=0;};
struct Bounds {Vec3 low{},high{};};
struct CachedPair {Vec3 a{},b{};PairContact contact{};bool blocked=false;};
bool Finite(Vec3 p) {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
bool Valid(const Body &b) {
 if(!b.Active()||b.State()==Mode::Balanced) return false;
 for(int j=0;j<Count;j++) if(!Finite(b.Positions()[size_t(j)])||!Finite(b.PreviousPositions()[size_t(j)])||!Finite(b.Reference()[size_t(j)])||!Finite(b.Velocity(Joint(j)))) return false;
 return true;
}
Vec3 Lerp(Vec3 a,Vec3 b,float t) {return Add(a,Mul(Sub(b,a),t));}
// Closest points of two finite segments, including zero-length spheres.
void Closest(Vec3 a,Vec3 b,Vec3 c,Vec3 d,float &s,float &t,Vec3 &difference) {
 const Vec3 u=Sub(b,a),v=Sub(d,c),w=Sub(a,c);
 const float aa=Dot(u,u),bb=Dot(u,v),cc=Dot(v,v),dd=Dot(u,w),ee=Dot(v,w);
 if(aa<1e-12f) {s=0;t=cc>1e-12f?std::clamp(ee/cc,0.f,1.f):0;}
 else if(cc<1e-12f) {t=0;s=std::clamp(-dd/aa,0.f,1.f);}
 else {
  const float denominator=aa*cc-bb*bb;
  s=denominator>1e-12f?std::clamp((bb*ee-cc*dd)/denominator,0.f,1.f):0;
  t=(bb*s+ee)/cc;
  if(t<0) {t=0;s=std::clamp(-dd/aa,0.f,1.f);}
  else if(t>1) {t=1;s=std::clamp((bb-dd)/aa,0.f,1.f);}
 }
 difference=Sub(Lerp(a,b,s),Lerp(c,d,t));
}
Vec3 Fallback(Vec3 a,Vec3 b,Vec3 c,Vec3 d,Vec3 roots) {
 const Vec3 u=Sub(b,a),v=Sub(d,c);
 const Vec3 cross={u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
 const float length=Length(cross);
 if(length>1e-5f) return Mul(cross,1/length);
 roots.z=0;
 const float rootLength=Length(roots);
 return rootLength>1e-5f?Mul(roots,1/rootLength):Vec3{0,-1,0};
}
Vec3 Endpoint(const Body &body,const Pose &pose,const Capsule &capsule,float t) {
 return capsule.a<0?Skull(pose,body.HeadAxis(pose)):At(pose,capsule,t);
}
Bounds Box(const Actor &actor,bool swept) {
 const Body &b=*actor.entry.body;
 Bounds box{{1e30f,1e30f,1e30f},{-1e30f,-1e30f,-1e30f}};
 for(const Capsule &capsule:actor.volumes) for(int end=0;end<2;end++) for(int previous=0;previous<=(swept?1:0);previous++) {
  const Vec3 p=Endpoint(b,previous?b.PreviousPositions():b.Positions(),capsule,float(end));
  box.low.x=std::min(box.low.x,p.x-capsule.radius);box.high.x=std::max(box.high.x,p.x+capsule.radius);
  box.low.y=std::min(box.low.y,p.y-capsule.radius);box.high.y=std::max(box.high.y,p.y+capsule.radius);
  box.low.z=std::min(box.low.z,p.z-capsule.radius);box.high.z=std::max(box.high.z,p.z+capsule.radius);
 }
 return box;
}
bool Overlap(const Bounds &a,const Bounds &b) {
 return a.low.x<=b.high.x&&a.high.x>=b.low.x&&a.low.y<=b.high.y&&a.high.y>=b.low.y&&a.low.z<=b.high.z&&a.high.z>=b.low.z;
}
PairContact Contact(const Actor &a,const Actor &b,bool swept,bool coincident) {
 const Body &ba=*a.entry.body,&bb=*b.entry.body;
 PairContact best,continuous;
 const Vec3 roots=Sub(ba.Positions()[Pelvis],bb.Positions()[Pelvis]);
 for(const Capsule &ca:a.volumes) for(const Capsule &cb:b.volumes) {
  const Vec3 a0=Endpoint(ba,ba.Positions(),ca,0),a1=Endpoint(ba,ba.Positions(),ca,1),b0=Endpoint(bb,bb.Positions(),cb,0),b1=Endpoint(bb,bb.Positions(),cb,1);
  const float radius=ca.radius+cb.radius;
  float s,t;Vec3 difference;
  Closest(a0,a1,b0,b1,s,t,difference);
  const float distance=Length(difference);
  if(radius-distance>Slop) {
   const Vec3 normal=coincident?Vec3{0,-1,0}:distance>1e-5f?Mul(difference,1/distance):Fallback(a0,a1,b0,b1,roots);
   const float depth=radius-Dot(difference,normal);
   if(depth>best.depth) best={normal,depth};
  }
  if(!swept) continue;
  const Vec3 oldA0=Endpoint(ba,ba.PreviousPositions(),ca,0),oldA1=Endpoint(ba,ba.PreviousPositions(),ca,1),oldB0=Endpoint(bb,bb.PreviousPositions(),cb,0),oldB1=Endpoint(bb,bb.PreviousPositions(),cb,1);
  const float speed=std::max(Length(Sub(a0,oldA0)),Length(Sub(a1,oldA1)))+std::max(Length(Sub(b0,oldB0)),Length(Sub(b1,oldB1)));
  if(speed<1e-6f) continue;
  float time=0;
  for(int step=0;step<64;step++) {
   const Vec3 x0=Lerp(oldA0,a0,time),x1=Lerp(oldA1,a1,time),y0=Lerp(oldB0,b0,time),y1=Lerp(oldB1,b1,time);
   Closest(x0,x1,y0,y1,s,t,difference);
   const float gap=Length(difference)-radius;
   // An already penetrating start has no certified contact entry plane.
   // Discrete geometry handles its end overlap; retaining that old plane
   // would re-block a successfully separated lateral escape.
   if(time==0&&gap<-Slop) break;
   if(gap<.00002f||step==63) {
    const float magnitude=Length(difference);
    const Vec3 n=magnitude>1e-5f?Mul(difference,1/magnitude):Fallback(x0,x1,y0,y1,Sub(ba.PreviousPositions()[Pelvis],bb.PreviousPositions()[Pelvis]));
    const float depth=radius-Dot(Sub(Lerp(a0,a1,s),Lerp(b0,b1,t)),n);
    if(depth>continuous.depth) continuous={n,depth};
    break;
   }
   const float increment=gap/speed*.95f;
   if(time+increment>1) break;
   time+=increment;
  }
 }
 return continuous.depth>best.depth?continuous:best;
}
// Test copied complete bodies so failed world queries cannot publish partial
// tangent experiments. A fixed horizontal search resolves feasible wall traps
// without lifting a pose or deriving velocity from a position repair.
bool Escape(Actor &a,Actor &b,Vec3 normal,Sweep sweep,Stats &stats) {
 const float baseline=Contact(a,b,false,false).depth;
 Vec3 tangent={-normal.y,normal.x,0};const float length=Length(tangent);
 tangent=length>1e-5f?Mul(tangent,1/length):Vec3{1,0,0};
 Body bestA=*a.entry.body,bestB=*b.entry.body;float best=baseline;
 constexpr float offsets[]={.32f,.16f,.08f,.04f,.64f,.96f};
 const std::array<Vec3,3> directions={tangent,Vec3{1,0,0},Vec3{0,1,0}};
 auto trial=[&](float offset,Vec3 direction,float sign) {
  ++stats.escapeTrials;
  Body trialA=*a.entry.body,trialB=*b.entry.body;
  const Vec3 delta=Mul(direction,offset*sign);
  const float first=trialA.Translate(delta,sweep,a.entry.context);
  const float second=trialB.Translate(Mul(delta,-(2-first)),sweep,b.entry.context);
  const float consumed=first+(2-first)*second;
  if(consumed<2-Slop) trialA.Translate(Mul(delta,2-consumed),sweep,a.entry.context);
  Actor candidateA=a,candidateB=b;candidateA.entry.body=&trialA;candidateB.entry.body=&trialB;
  const float remaining=Contact(candidateA,candidateB,false,false).depth;
  if(remaining+Slop<best||remaining<=Slop) {best=remaining;bestA=trialA;bestB=trialB;}
  return remaining;
 };
 for(float offset:offsets) for(Vec3 direction:directions) for(float sign:{1.f,-1.f}) {
  if(trial(offset,direction,sign)>Slop) continue;
  // Typical corridor clearance is near .32m. Start there, then narrow a
  // certified successful translation without testing every small direction.
  float low=0,high=offset;
  for(int refinement=0;refinement<3;refinement++) {
   const float middle=(low+high)*.5f;
   if(trial(middle,direction,sign)<=Slop) high=middle;else low=middle;
  }
  *a.entry.body=bestA;*b.entry.body=bestB;return true;
 }
 if(best+Slop<baseline) {*a.entry.body=bestA;*b.entry.body=bestB;return true;}
 return false;
}
float Speed(const Body &body,Vec3 n) {
 return Dot(body.CenterVelocity(),n);
}
}
Stats Solve(const Entry *entries,size_t count,Sweep sweep) {
 Stats stats;
 if(!entries) return stats;
 std::array<Actor,Capacity> actors{};
 std::array<std::array<CachedPair,Capacity>,Capacity> cache{};
 std::array<std::array<bool,Capacity>,Capacity> coincident{},pinnedPair{},escapeTried{};
 for(size_t i=0;i<std::min(count,Capacity);i++) {
  if(!entries[i].body||!Valid(*entries[i].body)) continue;
  bool duplicate=false;for(unsigned j=0;j<stats.bodies;j++) duplicate|=actors[j].entry.body==entries[i].body;
  if(duplicate) continue;
  Actor &a=actors[stats.bodies++];a.entry=entries[i];
  for(int j=0;j<Count;j++) a.volumes[size_t(j)]={j,j,Body::Radius(Joint(j))};
  const auto capsules=Capsules(a.entry.body->Reference());
  std::copy(capsules.begin(),capsules.end(),a.volumes.begin()+Count);
  a.volumes.back()={-1,-1,kSkullRadius};
 }
 for(unsigned i=0;i<stats.bodies;i++) for(unsigned j=i+1;j<stats.bodies;j++)
  coincident[i][j]=Length(Sub(actors[i].entry.body->Positions()[Pelvis],actors[j].entry.body->Positions()[Pelvis]))<.001f;
 for(int pass=0;pass<Passes;pass++) {
  bool moved=false;++stats.iterations;
  for(unsigned i=0;i<stats.bodies;i++) for(unsigned j=i+1;j<stats.bodies;j++) {
   Actor &a=actors[i],&b=actors[j];
   const bool swept=pass==0&&(a.entry.body->LastSteps()>0||b.entry.body->LastSteps()>0);
   if(!Overlap(Box(a,swept),Box(b,swept))) continue;
   ++stats.pairs;CachedPair &cached=cache[i][j];
   // Crowd corrections are rigid. A blocked pair with unchanged roots has
   // unchanged geometry; retry only after another pair moves either body.
   const bool reuse=pass>0&&cached.blocked&&Length(Sub(a.entry.body->Positions()[Pelvis],cached.a))<Slop&&Length(Sub(b.entry.body->Positions()[Pelvis],cached.b))<Slop;
   const PairContact contact=reuse?cached.contact:Contact(a,b,swept,coincident[i][j]);
   if(contact.depth<=Slop) continue;
   ++stats.contacts;const float amount=contact.depth+Slop;
   Vec3 entry=Sub(a.entry.body->PreviousPositions()[Pelvis],b.entry.body->PreviousPositions()[Pelvis]);
   entry.z=0;
   const Vec3 exit=Sub(a.entry.body->Positions()[Pelvis],b.entry.body->Positions()[Pelvis]);
   const bool crossed=swept&&Length(entry)>.001f&&Dot(entry,exit)<0;
   float first=0,second=0,accepted=0;
   if(!reuse) {
    const Body startA=*a.entry.body,startB=*b.entry.body;
    first=a.entry.body->Translate(Mul(contact.normal,amount*.5f),sweep,a.entry.context);
    second=b.entry.body->Translate(Mul(contact.normal,-amount*(1-.5f*first)),sweep,b.entry.context);
    accepted=amount*.5f*first+amount*(1-.5f*first)*second;
    if(accepted<amount-Slop) accepted+=(amount-accepted)*a.entry.body->Translate(Mul(contact.normal,amount-accepted),sweep,a.entry.context);
    if(accepted<amount-Slop) {
     ++stats.blocked;
     if(!escapeTried[i][j]&&stats.escapeSearches<4) {
      escapeTried[i][j]=true;++stats.escapeSearches;
      const Body repairedA=*a.entry.body,repairedB=*b.entry.body;
      *a.entry.body=startA;*b.entry.body=startB;
      if(Escape(a,b,contact.normal,sweep,stats)) {moved=true;++stats.escapes;}
      else {*a.entry.body=repairedA;*b.entry.body=repairedB;}
     }
    }
    if(accepted<amount-Slop&&accepted<=Slop) {
     cached={a.entry.body->Positions()[Pelvis],b.entry.body->Positions()[Pelvis],Contact(a,b,false,coincident[i][j]),true};
    }
   } else ++stats.blocked;
   moved|=accepted>1e-6f;
   const float av=Speed(*a.entry.body,contact.normal),bv=Speed(*b.entry.body,contact.normal);
   // A world-pinned participant acts as a stationary surface. An ordinary
   // pair shares closing momentum without restitution or position-derived speed.
   const bool pinned=pinnedPair[i][j]=pinnedPair[i][j]||first<.9999f||second<.9999f;
   const float surface=pinned?0:(av+bv)*.5f;
   a.entry.body->ResolveVelocity(contact.normal,surface);
   b.entry.body->ResolveVelocity(Mul(contact.normal,-1),-surface);
   // Once an entire root trajectory has crossed another body, retain the
   // entry side in velocity as well as position. This prevents residual
   // closing speed from oblique limb normals repeating the crossing.
   if(crossed) {
    entry=Mul(entry,1/Length(entry));
    const float speedA=Speed(*a.entry.body,entry),speedB=Speed(*b.entry.body,entry);
    if(speedA<speedB) {
     const float shared=pinned?0:(speedA+speedB)*.5f;
     a.entry.body->ResolveVelocity(entry,shared);b.entry.body->ResolveVelocity(Mul(entry,-1),-shared);
    }
   }
  }
  if(!moved) break;
 }
 return stats;
}
}
