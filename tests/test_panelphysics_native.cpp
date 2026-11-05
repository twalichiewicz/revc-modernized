#define PANELPHYSICS_NO_MAIN
#include "test_panelphysics.cpp"
#include "nativejetshadowarm.h"
namespace {
Vec3 NoParentMotion(uint8_t *,const Vec3 &){return {};}
}
int main(){if(PanelPhysicsChecks())return 1;using namespace fixture;using namespace paneltest;using namespace nativejetshadowtest;
 Code code;code.Add(0x1001564c0,0x1001565ac);code.Add(0x100172e94,0x100172f48);code.Add(0x100270c48,0x100270e28);code.bindings[0x10015632c]=reinterpret_cast<void *>(NoParentMotion);code.Finish();Store(code.Page(0x1007f4560),uint8_t(1));
 panelphysics::n.move=reinterpret_cast<decltype(panelphysics::n.move)>(code.Address(0x1001564c0));panelphysics::n.turn=reinterpret_cast<decltype(panelphysics::n.turn)>(code.Address(0x1001564f4));
 Fresh();panelphysics::Apply(Load(),[](){cars[0x34c+9+2]=3;Detached();});const float mass=*reinterpret_cast<float *>(object+0xcc),speed=*reinterpret_cast<float *>(object+0x84);const auto spin=*reinterpret_cast<Vec3 *>(object+0x90);
 Check(speed>0&&std::fabs(speed*mass*50-std::sqrt(2*.008f*25*12))<1e-5f,"actual native CPhysical applies the material point impulse with derived mass");
 Check(std::isfinite(spin.x)&&std::isfinite(spin.y)&&std::isfinite(spin.z)&&std::fabs(spin.y)>1e-5f,"actual native CPhysical spins detached skin about its measured centre of mass");
 Fresh();Bullet();panelphysics::Apply(Load(),[](){cars[0x34c+9+2]=3;Detached();});Check(*reinterpret_cast<float *>(object+0xcc)<mass&&*reinterpret_cast<float *>(object+0x84)>speed,"removed glazing reduces mass and changes actual native impulse response");
 Fresh();panelphysics::Apply(Load(),[](){});auto door=cars.data()+0x368+2*0x24;const float before=*reinterpret_cast<float *>(door+0xc);reinterpret_cast<void (*)(uint8_t *,uint8_t *)>(code.Address(0x100270c48))(door,cars.data());Check(*reinterpret_cast<float *>(door+0xc)!=before,"actual native CDoor integrates the localized hinge momentum into panel motion");
 Reset();std::printf("panel physics native: %d failures\n",failed);return failed?1:0;
}
