#include "reactcrowd.h"
#include "reactshape.h"
#include "reactworld.h"
#include <limits>
using namespace reactphysics;
namespace {
int failures=0;
void Check(bool good,const char *name) { if(!good) {std::printf("FAIL %s\n",name); ++failures;} }
Pose Rig(float y=0,float z=0) {Pose p=reactrig::Capture(reactfixture::CapturedRig()); for(auto &v:p) {v.y+=y;v.z+=z;} return p;}
float Distance(Vec3 a,Vec3 b) {return reactfixture::Length(reactfixture::Sub(a,b));}
void Activate(Body &body,const Pose &p,Vec3 velocity={}) {
 body.Reset(p,velocity); body.Momentum({0,4,0}); body.Momentum({0,-4,0});
}
void Rigid(const Pose &before,const Body &body,const char *name) {
 const Vec3 delta=reactfixture::Sub(body.Positions()[Pelvis],before[Pelvis]);
 for(int j=0;j<Count;j++) Check(Distance(reactfixture::Sub(body.Positions()[size_t(j)],before[size_t(j)]),delta)<.00002f,name);
}
struct Limits {float minY=-1000,maxY=1000; unsigned calls=0;};
Contact World(Vec3,Vec3 p,float radius,void *context) {
 auto *limits=static_cast<Limits *>(context); ++limits->calls;
 Contact c{p,{},false};
 if(p.y-radius<limits->minY) {c.position.y=limits->minY+radius;c.normal={0,1,0};c.hit=true;}
 if(p.y+radius>limits->maxY) {c.position.y=limits->maxY-radius;c.normal={0,-1,0};c.hit=true;}
 return c;
}
void StaticOverlap() {
 Body a,b; Activate(a,Rig());Activate(b,Rig(.1f));
 const Pose pa=a.Positions(),pb=b.Positions();
 reactcrowd::Entry entries[]={{&a},{&b}};
 const auto stats=reactcrowd::Solve(entries,2,nullptr);
 Check(Distance(a.Positions()[Pelvis],b.Positions()[Pelvis])>=.318f,"static overlap separates pelvis volumes");
 Check(stats.contacts>0,"static overlap reports contact");
 Rigid(pa,a,"static correction preserves complete A shape");Rigid(pb,b,"static correction preserves complete B shape");
 Check(!a.Dead()&&!b.Dead(),"contact never damages living bodies");
 for(int j=0;j<Count;j++) Check(std::fabs(a.Velocity(Joint(j)).z)<1e-6f&&std::fabs(b.Velocity(Joint(j)).z)<1e-6f,"position repairs inject no upward velocity");
}
void Coincident() {
 Body a,b,c,d;Activate(a,Rig());Activate(b,Rig());Activate(c,Rig());Activate(d,Rig());
 reactcrowd::Entry ab[]={{&a},{&b}},cd[]={{&c},{&d}};
 reactcrowd::Solve(ab,2,nullptr);reactcrowd::Solve(cd,2,nullptr);
 Check(Distance(a.Positions()[Pelvis],b.Positions()[Pelvis])>=.318f,"coincident actors escape");
 Check(Distance(a.Positions()[Pelvis],c.Positions()[Pelvis])<1e-6f,"coincident normal is deterministic");
 Check(std::fabs(a.Positions()[Pelvis].z-Rig()[Pelvis].z)<1e-5f,"coincident escape does not lift bodies");
}
void WallPinned() {
 Limits left{-.25f,1000},right{}; Body a,b;Activate(a,Rig());Activate(b,Rig(.1f));
 const Pose pa=a.Positions(),pb=b.Positions();reactcrowd::Entry entries[]={{&a,&left},{&b,&right}};
 reactcrowd::Solve(entries,2,World);
 Check(b.Positions()[Pelvis].y-a.Positions()[Pelvis].y>=.318f,"wall pinned correction transfers to other actor");
 Check(left.calls>0&&right.calls>0,"corrections use individual world contexts");
 Check(a.Positions()[Pelvis].y-pa[Pelvis].y>-.08f&&b.Positions()[Pelvis].y-pb[Pelvis].y>.18f,"wall consumes A correction and B receives the remainder");
 for(int j=0;j<Count;j++) Check(a.Positions()[size_t(j)].y-Body::Radius(Joint(j))>=left.minY-.002f,"crowd cannot push joints through wall");
 Rigid(pa,a,"pinned A remains rigid");Rigid(pb,b,"pinned B remains rigid");
}
void ThinLimb() {
 Pose pa=Rig(-2,3),pb=Rig(2,3);
 pa[ElbowL]={-1,0,3};pa[HandL]={1,0,3};pb[ElbowL]={0,-1,3};pb[HandL]={0,1,3};
 Body a,b;Activate(a,pa);Activate(b,pb);reactcrowd::Entry entries[]={{&a},{&b}};
 const auto stats=reactcrowd::Solve(entries,2,nullptr);

 Check(stats.contacts>0,"long thin crossing forearms contact between endpoints");
 Check(std::fabs(a.Positions()[ElbowL].z-b.Positions()[ElbowL].z)>.16f,"crossing forearm interiors clear their measured combined diameter");
 Check(Distance(a.Positions()[Pelvis],pa[Pelvis])+Distance(b.Positions()[Pelvis],pb[Pelvis])>.1f,"capsule interior contact separates actors");
 Rigid(pa,a,"thin limb A remains rigid");Rigid(pb,b,"thin limb B remains rigid");
}
void ExtendedSkull() {
 Pose pa=Rig();const float offset=3-pa[Head].z;for(Vec3 &p:pa) p.z+=offset;
 Pose pb=pa;for(Vec3 &p:pb) p.z=6.3f-p.z;
 Body a,b;const Vec3 up{0,0,1},down{0,0,-1};
 a.Reset(pa,{},nullptr,&up);b.Reset(pb,{},nullptr,&down);
 a.Momentum({0,4,0});a.Momentum({0,-4,0});b.Momentum({0,4,0});b.Momentum({0,-4,0});
 Check(Distance(pa[Head],pb[Head])>.299f,"skull fixture has clear neck/joint spheres");
 reactcrowd::Entry entries[]={{&a},{&b}};const auto stats=reactcrowd::Solve(entries,2,nullptr);
 Check(stats.contacts>0,"longitudinal skull extensions contact beyond neck spheres");
 Check(Distance(a.Positions()[Head],b.Positions()[Head])>.418f,"longitudinal skull centers separate by their actual diameters");
 Rigid(pa,a,"skull correction A rigid");Rigid(pb,b,"skull correction B rigid");
}
void Approaching() {
 reactfixture::Reset();reactfixture::Plane({0,0,1});
 Body a,b;const Pose pa=Rig(-.6f),pb=Rig(.6f);Activate(a,pa,{0,2,0});Activate(b,pb,{0,-2,0});
 reactcrowd::Entry entries[]={{&a},{&b}};float minimum=10;
 for(int frame=0;frame<75;frame++) {
  a.Advance(1.f/60,pa,reactfixture::NativeSweep,nullptr);b.Advance(1.f/60,pb,reactfixture::NativeSweep,nullptr);
  const Pose aBefore=a.Positions(),bBefore=b.Positions();
  reactcrowd::Solve(entries,2,reactfixture::NativeSweep);
  minimum=std::min(minimum,Distance(a.Positions()[Pelvis],b.Positions()[Pelvis]));
  Rigid(aBefore,a,"approach A correction rigid");Rigid(bBefore,b,"approach B correction rigid");
 }
 std::printf("approach minimum pelvis distance %.6f\n",minimum);
 Check(minimum>=.318f,"2 m/s approaching pelvis volumes stay separated every frame");
 Check(!a.Dead()&&!b.Dead(),"approach leaves native health state intact");
}
void HighSpeed() {
 const Pose pa=Rig(-1,10),pb=Rig(1,10);Body a,b;Activate(a,pa,{0,100,0});Activate(b,pb,{0,-100,0});
 a.Advance(1.f/30,pa,nullptr,nullptr);b.Advance(1.f/30,pb,nullptr,nullptr);
 Check(a.Positions()[Pelvis].y>b.Positions()[Pelvis].y,"high speed fixture actually swaps sides before crowd");
 const Pose beforeA=a.Positions(),beforeB=b.Positions();reactcrowd::Entry entries[]={{&a},{&b}};
 reactcrowd::Solve(entries,2,nullptr);
 Check(a.Positions()[Pelvis].y+.318f<=b.Positions()[Pelvis].y,"swept capsules prevent fast bodies swapping sides");
 std::printf("crossing y %.4f %.4f center velocities %.4f %.4f\n",a.Positions()[Pelvis].y,b.Positions()[Pelvis].y,a.CenterVelocity().y,b.CenterVelocity().y);
 Check(a.CenterVelocity().y<=b.CenterVelocity().y+.001f,"contact cancels closing center velocity without bounce");
 Rigid(beforeA,a,"high speed A remains rigid");Rigid(beforeB,b,"high speed B remains rigid");
}
// Independent native mass table: compare physical invariants rather than the
// crowd's averaging helper or Body::CenterVelocity implementation.
struct Metrics {double energy=0,px=0,py=0,pz=0,mass=0;};
Metrics Measure(const Body &a,const Body &b) {
 constexpr double inverseMass[Count]={.14,.2,.5,1,1,2,1,1,2,.5,.6,1,.5,.6,1};
 Metrics result;
 for(const Body *body:{&a,&b}) for(int j=0;j<Count;j++) {
  const Vec3 v=body->Velocity(Joint(j));const double mass=1/inverseMass[j];
  result.mass+=mass;result.energy+=mass*(double(v.x)*v.x+double(v.y)*v.y+double(v.z)*v.z)*.5;
  result.px+=mass*v.x;result.py+=mass*v.y;result.pz+=mass*v.z;
 }
 return result;
}
void FreePairResponse() {
 double maximumGain=0,maximumMomentumChange=0;
 for(int joint=0;joint<Count;joint++) for(float uniform:{-10.f,-3.f,0.f,3.f,10.f}) for(float impulse:{-15.f,-5.f,5.f,15.f}) {
  Body a,b;Activate(a,Rig(),{0,uniform,0});Activate(b,Rig(.1f),{0,uniform,0});a.Impulse(Joint(joint),{0,impulse,0});
  const Metrics before=Measure(a,b);Pose va{},vb{};
  for(int j=0;j<Count;j++) {va[size_t(j)]=a.Velocity(Joint(j));vb[size_t(j)]=b.Velocity(Joint(j));}
  reactcrowd::Entry entries[]={{&a},{&b}};const auto stats=reactcrowd::Solve(entries,2,nullptr);
  const Metrics after=Measure(a,b);maximumGain=std::max(maximumGain,after.energy-before.energy);
  const double momentumChange=std::sqrt(std::pow(after.px-before.px,2)+std::pow(after.py-before.py,2)+std::pow(after.pz-before.pz,2));
  maximumMomentumChange=std::max(maximumMomentumChange,momentumChange);
  Check(stats.contacts>0,"free-pair energy fixture actually contacts");
  Check(after.energy<=before.energy+.002,"free-pair contact cannot increase kinetic energy");
  Check(momentumChange<.002,"free-pair contact conserves mass-weighted momentum");
  const Vec3 changeA=reactfixture::Sub(a.Velocity(Pelvis),va[Pelvis]),changeB=reactfixture::Sub(b.Velocity(Pelvis),vb[Pelvis]);
  for(int j=0;j<Count;j++) {
   Check(Distance(reactfixture::Sub(a.Velocity(Joint(j)),va[size_t(j)]),changeA)<.00001f,"free-pair A response preserves internal velocity differences");
   Check(Distance(reactfixture::Sub(b.Velocity(Joint(j)),vb[size_t(j)]),changeB)<.00001f,"free-pair B response preserves internal velocity differences");
  }
 }
 std::printf("free-pair maximum KE gain %.8f momentum change %.8f\n",maximumGain,maximumMomentumChange);
}
void PinnedResponse() {
 Limits left{-.25f,1000},right{};Body a,b;
 Pose pa=Rig(),pb=Rig(.1f);for(Vec3 &p:pa) p.y=0;for(Vec3 &p:pb) p.y=.1f;
 Activate(a,pa,{0,-10,0});Activate(b,pb,{0,-10,0});a.Impulse(Pelvis,{0,5,0});
 const Metrics before=Measure(a,b);reactcrowd::Entry entries[]={{&a,&left},{&b,&right}};
 reactcrowd::Solve(entries,2,World);const Metrics after=Measure(a,b),moving=Measure(b,b);
 Check(after.energy<=before.energy+.002,"static-world pinned response cannot create kinetic energy");
 Check(moving.py/moving.mass>=-.001,"world-pinned contact uses stationary surface speed");
}
void CorridorEscape() {
 Limits corridor{-.30f,.30f};Body a,b;Activate(a,Rig(-.05f,2));Activate(b,Rig(.05f,2));
 const Pose pa=a.Positions(),pb=b.Positions();reactcrowd::Entry entries[]={{&a,&corridor},{&b,&corridor}};
 for(int frame=0;frame<60;frame++) reactcrowd::Solve(entries,2,World);
 Check(Distance(a.Positions()[Pelvis],b.Positions()[Pelvis])>=.318f,"world-blocked overlap finds a feasible lateral corridor escape");
 Check(std::fabs(a.Positions()[Pelvis].z-pa[Pelvis].z)<.00001f&&std::fabs(b.Positions()[Pelvis].z-pb[Pelvis].z)<.00001f,"corridor escape cannot invent upward displacement");
 Rigid(pa,a,"corridor A escape stays rigid");Rigid(pb,b,"corridor B escape stays rigid");
 for(int j=0;j<Count;j++) Check(Distance(a.Velocity(Joint(j)),{})<.00001f&&Distance(b.Velocity(Joint(j)),{})<.00001f,"corridor escape injects no velocity");
}
void NativeCorridorEscape() {
 for(float width:{.60f,.70f}) {
  reactfixture::Reset();reactfixture::Plane({0,1,0},width*.5f);reactfixture::Plane({0,1,0},-width*.5f);
  Body a,b;Activate(a,Rig(-.05f,2));Activate(b,Rig(.05f,2));const Pose pa=a.Positions(),pb=b.Positions();
  auto clear=[](const Body &body) {
   bool valid=true;auto probe=[&](Vec3 p,float radius) {const Contact c=reactfixture::NativeSweep(p,p,radius,nullptr);valid&=c.resolved&&Distance(p,c.position)<.0005f;};
   reactshape::Cover(body.Reference(),[&](const reactshape::Sphere &sphere) {probe(sphere.Position(body.Positions()),sphere.radius);},&body.Positions());
   probe(reactshape::Skull(body.Positions(),body.HeadAxis()),reactshape::kSkullRadius);return valid;
  };
  Check(clear(a)&&clear(b),"native corridor starts with certified complete rig and skull volumes");
  reactcrowd::Entry entries[]={{&a},{&b}};
  for(int frame=0;frame<60;frame++) reactcrowd::Solve(entries,2,reactfixture::NativeSweep);
  Check(Distance(a.Positions()[Pelvis],b.Positions()[Pelvis])>=.318f,"native triangle corridor permits lateral escape from blocked pair overlap");
  Check(clear(a)&&clear(b),"native corridor escape certifies complete volumes against both walls");
  Check(std::fabs(a.Positions()[Pelvis].z-pa[Pelvis].z)<.00001f&&std::fabs(b.Positions()[Pelvis].z-pb[Pelvis].z)<.00001f,"native corridor escape keeps original height");
  Rigid(pa,a,"native corridor A remains rigid");Rigid(pb,b,"native corridor B remains rigid");
 }
}
void DenseWorkBound() {
 Limits corridor{-.30f,.30f};std::array<Body,12> bodies;std::array<reactcrowd::Entry,12> entries;
 for(size_t i=0;i<bodies.size();i++) {
  Pose p=Rig((i%2)*.1f-.05f,2);for(Vec3 &v:p) v.x+=float(i/2)*.04f;
  Activate(bodies[i],p);entries[i]={&bodies[i],&corridor};
 }
 const auto stats=reactcrowd::Solve(entries.data(),entries.size(),World);
 Check(stats.escapeSearches>0,"dense twelve-body fixture exercises blocked-pair escape search");
 Check(stats.escapeSearches<=4&&stats.escapeTrials<=156,"dense twelve-body lateral search has a global work bound");
}
void PartialOverlapCrossing() {
 for(float speed:{15.f,25.f}) {
  Pose pa=Rig(0,10),pb=Rig(-.15f,10);for(Vec3 &p:pa) p.x-=.25f;for(Vec3 &p:pb) p.x+=.25f;
  Check(Distance(pa[Pelvis],pb[Pelvis])>.52f,"partial-overlap crossing starts with separated core spheres");
  Check(Distance(pa[ElbowR],pb[ElbowL])<.033f,"partial-overlap crossing starts with overlapping elbow volumes");
  Body a,b;Activate(a,pa,{speed,0,0});Activate(b,pb,{-speed,0,0});
  a.Advance(1.f/30,pa,nullptr,nullptr);b.Advance(1.f/30,pb,nullptr,nullptr);
  Check(a.Positions()[Pelvis].x>b.Positions()[Pelvis].x,"partial-overlap crossing independently swaps root sides");
  reactcrowd::Entry entries[]={{&a},{&b}};const auto stats=reactcrowd::Solve(entries,2,nullptr);
  Check(stats.contacts>0,"overlapping elbows do not suppress continuous core contacts");
  Check(a.Positions()[Pelvis].x<b.Positions()[Pelvis].x,"continuous separated cores retain root order despite initial limb overlap");
  const Vec3 entry=reactfixture::Sub(pa[Pelvis],pb[Pelvis]),velocity=reactfixture::Sub(a.CenterVelocity(),b.CenterVelocity());
  Check(reactfixture::Dot(entry,velocity)>=-.001f,"partial-overlap crossing cancels closing center velocity");
 }
}
void Filtering() {
 Body a,passive,distant;Activate(a,Rig());passive.Reset(Rig());passive.Momentum({});Activate(distant,Rig(30));
 const Pose pa=a.Positions(),pp=passive.Positions(),pd=distant.Positions();
 reactcrowd::Entry entries[]={{nullptr},{&a},{&a},{&passive},{&distant}};
 const auto stats=reactcrowd::Solve(entries,5,nullptr);
 Check(Distance(pa[Pelvis],a.Positions()[Pelvis])==0&&Distance(pp[Pelvis],passive.Positions()[Pelvis])==0&&Distance(pd[Pelvis],distant.Positions()[Pelvis])==0,"null duplicate passive and distant entries cannot move bodies");
 Check(stats.bodies==2&&stats.contacts==0,"invalid and duplicate entries excluded from bounded solve");
 Check(reactcrowd::Solve(nullptr,99,nullptr).bodies==0,"null entry list safe");
 Body invalid;Pose bad=Rig();bad[Head].x=std::numeric_limits<float>::quiet_NaN();Activate(invalid,bad);
 reactcrowd::Entry invalidEntries[]={{&a},{&invalid}};
 Check(reactcrowd::Solve(invalidEntries,2,nullptr).contacts==0,"nonfinite body excluded");
 std::array<Body,13> many;std::array<reactcrowd::Entry,13> capped;
 for(size_t i=0;i<many.size();i++) {Activate(many[i],Rig(float(i)*3));capped[i]={&many[i]};}
 Activate(many[12],Rig());const Pose excluded=many[12].Positions();
 Check(reactcrowd::Solve(capped.data(),capped.size(),nullptr).bodies==12,"bounded input accepts only twelve actor slots");
 Check(Distance(excluded[Pelvis],many[12].Positions()[Pelvis])==0,"thirteenth actor cannot acquire crowd ownership");
}
}
int main() {StaticOverlap();Coincident();WallPinned();ThinLimb();ExtendedSkull();Approaching();HighSpeed();FreePairResponse();PinnedResponse();CorridorEscape();NativeCorridorEscape();DenseWorkBound();PartialOverlapCrossing();Filtering();std::printf("reactcrowd: %d failures\n",failures);return failures?1:0;}
