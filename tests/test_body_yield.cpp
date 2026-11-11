// Yield-limited material contacts with the static world: the struck part
// carries at most its yield force; the rest of the native impulse becomes
// plastic deformation, and yielding contacts never hold the actor "stuck".
#define CRASH_TUNING_NO_MAIN
#include "test_vehicle_crash_tuning.cpp"
#undef CRASH_TUNING_NO_MAIN
#include <cmath>
#include <cstdio>
namespace yieldtest {
using namespace crashtuning;
int fail=0;void Check(bool ok,const char *name){if(!ok){std::printf("FAIL %s\n",name);++fail;}}
float Wheel(const Car &car){float z=0;for(auto w:car.definition.wheels)z+=w.z*.25f;return z;}
// Lowest non-wheel point of the front overhang, and the front-most point at wheel-centre height.
Vec3 Lip(const Car &car){Vec3 out{0,-1e9f,0};const float axle=std::max(car.definition.wheels[0].y,car.definition.wheels[2].y);for(const auto &s:car.surfaces)if(s.role!=Role::Wheel&&!s.glass)for(auto p:s.points)if(p.y>axle+.2f&&std::fabs(p.x)<.6f&&(out.y<-1e8f||p.z<out.z))out=p;return out;}
Vec3 Beam(const Car &car){const float z=Wheel(car);Vec3 out{0,-1e9f,0};for(const auto &s:car.surfaces)if(s.role!=Role::Wheel&&!s.glass)for(auto p:s.points)if(std::fabs(p.z-z-.05f)<.08f&&std::fabs(p.x)<.6f&&p.y>out.y)out=p;return out;}
}
int main(){
 using namespace yieldtest;
 // Native impulse units are kg x (m per 1/50 s); a force over one native step.
 Check(std::fabs(bodycontact::YieldCap(1000,1)-1000*(1.f/50)/50)<1e-6f,"the cap is the yield force over the native time step");
 Check(std::fabs(bodycontact::YieldCap(1000,.5f)-.5f*bodycontact::YieldCap(1000,1))<1e-6f,"the cap scales with the step length");
 bool yielded=false;
 Check(bodycontact::Applied(5,8,yielded)==5&&!yielded,"an impulse under the cap is the native rigid response");
 Check(bodycontact::Applied(20,8,yielded)==8&&yielded,"an impulse over the cap applies only the cap and yields");
 Check(bodycontact::Applied(20,0,yielded)==20&&!yielded,"no yield data keeps the native response");
 // Only rigid responses make native collision report the actor as stuck.
 Check(!bodycontact::Stuck(3,3),"contacts that all yielded do not hold the actor stuck");
 Check(bodycontact::Stuck(3,2)&&bodycontact::Stuck(1,0),"any rigid response keeps the native stuck path");
 Check(!bodycontact::Stuck(0,0),"no response, no collision");
 // A resolved rigid contact inside the contact skin is resting, not wedged: it
 // keeps its native impulse but does not reject the move. Penetration does.
 Check(bodycontact::Resting(bodycontact::kRestSkin*.5f,false,false)&&!bodycontact::Resting(bodycontact::kRestSkin*3,false,false),"touching within the skin rests; penetration stays rigid");
 Check(!bodycontact::Resting(.001f,true,false),"a yielded contact is handled by its push, not as resting");
 // The ground under the body is its support, not an obstacle it strikes: an
 // upward-facing contact never yields, so penetration keeps native handling.
 Check(!bodycontact::Yieldable({0,0,1})&&!bodycontact::Yieldable({.3f,0,.9f}),"support faces under the body never yield");
 Check(bodycontact::Yieldable({0,-1,0})&&bodycontact::Yieldable({0,-.51f,-.86f})&&bodycontact::Yieldable({0,-.6f,.6f}),"struck faces and edges yield");
 // A push is resolved in the body's own frame: material never moves toward the
 // body's underside, so a car pitched nose-down when struck cannot leave its
 // trim below the road once it settles on its wheels.
 {const float c=std::cos(.38f),sn=std::sin(.38f);const Vec3 world{0,-.1f,0};const Vec3 local{0,world.y*c,world.y*sn};const Vec3 body=bodycontact::BodyPush(local);
  Check(local.z<0&&body.z==0&&body.y==local.y,"a world push at a pitched pose keeps no downward body component");}
 Check(bodycontact::BodyPush({0,0,.02f}).z==.02f,"an upward lip deflection is kept");
 // The back exit covers this step's travel into the face as well as current depth.
 Check(std::fabs(bodycontact::BackExit(.004f,6,1.7f)-6*1.7f/50)<1e-6f&&bodycontact::BackExit(.05f,.1f,1)==.05f,"the back exit includes the step's closing travel");
 // The plastic push takes the shortest way out of the obstacle: a lip just
 // under a kerb top lifts over it; a wall face pushes it back.
 Vec3 push=bodycontact::Push({0,-1,0},.12f,.02f);Check(push.z==.02f&&push.y==0,"a lip just under a kerb top deflects up over it");
 push=bodycontact::Push({0,-1,0},.03f,-1);Check(push.y==-.03f&&push.z==0,"a face with no top within reach pushes back along its normal");
 push=bodycontact::Push({0,-1,0},.01f,.05f);Check(push.y==-.01f,"the smaller exit wins");
 // An edge struck from below has a downward normal; the road under the car stops
 // the material going down, so the push keeps only its horizontal part.
 push=bodycontact::Push({0,-.51f,-.86f},.1f,-1);if(!(push.z==0&&std::fabs(push.y+.051f)<1e-5f))std::printf("  push %.9f %.9f %.9f\n",push.x,push.y,push.z);Check(push.z==0&&std::fabs(push.y+.051f)<1e-5f,"a push never drives material down into the ground");
 // Struck-part yield forces from the kernel's own material and section data.
 for(int id:{211,130}){Car car;Check(Load(id,car),"vehicle loads");Model m;Check(m.Initialize(car.surfaces,car.mass,car.definition),"model initializes");
  const Vec3 lip=Lip(car),beam=Beam(car);size_t lipSurface=0;for(size_t i=0;i<car.surfaces.size();++i)for(auto p:car.surfaces[i].points)if(p.x==lip.x&&p.y==lip.y&&p.z==lip.z)lipSurface=i;
  const float lipForce=m.YieldForce(lipSurface,lip),beamForce=m.YieldForce(beam);const float radius=Wheel(car)-car.definition.bounds.lo.z;const bool low=lip.z<Wheel(car)-.25f*radius;
  std::printf("  model %d: lowest front z=%.3f (wheel %.3f, trim below %.3f) force=%.0f N; bumper-height z=%.3f force=%.0f N; mass %.0f\n",id,lip.z,Wheel(car),Wheel(car)-.25f*radius,lipForce,beam.z,beamForce,car.mass);
  if(low)Check(lipForce>0&&lipForce<5000,"a lip below the bumper zone yields at sheet strength");
  Check(beamForce/car.mass>40&&beamForce>40*2492,"structure at bumper height stops a car over a crumple distance");
  if(!low)continue;
  // Deformation booking: the struck patch moves by the pushed distance.
  const uint32_t group=car.surfaces[lipSurface].group;const Vec3 rest=m.Deform(lip,group);auto r=m.Yield(lipSurface,lip,{0,0,1},.03f,lipForce,1000);
  const Vec3 moved=m.Deform(lip,group);Check(r.valid&&r.changed&&std::fabs(moved.z-rest.z-.03f)<.004f,"a yield push moves the struck material by the pushed distance");
  Check(std::fabs(r.deposited-lipForce*.03f)<1,"the push books yield force times distance");
  r=m.Yield(lipSurface,lip,{0,0,1},.03f,lipForce,1000);Check(std::fabs(m.Deform(lip,group).z-moved.z)<.002f&&r.deposited==0,"contacts on one patch in the same frame push once");
  r=m.Yield(lipSurface,lip,{0,0,1},.03f,lipForce,1020);Check(m.Deform(lip,group).z>moved.z+.02f,"a later frame's penetration is pushed out again");
  for(uint32_t t=1040;t<3000;t+=20)m.Yield(lipSurface,lip,{0,0,1},.05f,lipForce,t);
  Check(m.YieldForce(lipSurface,lip)>20*lipForce,"a fully crushed stroke stands on the structure behind it");
  // The host reports the struck vertex where it is now (already pushed): the same
  // patch must take it, so the stroke stays bounded and saturates.
  {Model n;n.Initialize(car.surfaces,car.mass,car.definition);Vec3 at=lip;for(uint32_t t=1000;t<1060;t+=20){n.Yield(lipSurface,at,{0,0,1},.05f,lipForce,t);at=n.Deform(lip,group);}
   Check(n.YieldForce(lipSurface,at)>20*lipForce,"the moved vertex finds its saturated stroke");
   for(uint32_t t=1060;t<4000;t+=20){n.Yield(lipSurface,at,{0,0,1},.05f,lipForce,t);at=n.Deform(lip,group);}
   Check(at.z-lip.z<.152f,"repeated pushes reported at the moved vertex stay within one crush stroke");}
 }
 if(!fail)std::puts("Body yield cases pass");return fail?1:0;
}
