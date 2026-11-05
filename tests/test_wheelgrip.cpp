#include "vmtestarm.h"
#include "wheelgrip.h"
#include <cmath>
#include <cstdio>
static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)
using namespace vm;
int main()
{
	auto arms = Articulate(vmtest::Body());
	CHECK(arms.ok);
	for (float angle : {-1.8f, -1.05f, 0.0f, 1.05f, 1.8f})
		for (bool left : {false, true}) {
			auto grip =
			    wheelgrip::OnRim({0, 0.5f, -0.3f}, {0, -1, 0}, {0, 0, 1}, {-1, 0, 0}, .19f, .017f, angle);
			CHECK(std::fabs(Dot(grip.hand.x, grip.hand.y)) < 1e-5f);
			CHECK(Dot(grip.hand.x, grip.axis) < -.99f);
			Vec3 targets[kDigits], bends[kDigits];
			const Arm &arm = left ? arms.left : arms.right;
			wheelgrip::Contacts(arm, grip, targets, bends);
			bool reaches[kDigits] = {true, true, true, true, true};
			Frame matrices[kArmBones];
			Pose(arm, grip.hand, targets, reaches, ArmAngles{}, left, matrices, bends);
			for (int d = 0; d < kDigits; ++d) {
				Vec3 p = Sub(targets[d], grip.centre);
				float axial = Dot(p, grip.axis), radial = Length(Sub(p, Mul(grip.axis, axial)));
				CHECK(std::fabs(std::hypot(radial - grip.radius, axial) - grip.tube) < 1e-4f);
				CHECK(Length(Sub(Apply(matrices[DigitBase(d) + 1], arm.tip[d]), targets[d])) < .012f);
			}
		}
 for(bool left:{false,true}){
  const auto g=wheelgrip::OnStick({-.42f,1.98f,.28f},{0,0,1},{0,1,0},.035f,left);Vec3 targets[kDigits],bends[kDigits];const auto &arm=left?arms.left:arms.right;wheelgrip::Contacts(arm,g,targets,bends);bool reach[kDigits]={true,true,true,true,true};Frame posed[kArmBones];Pose(arm,g.hand,targets,reach,ArmAngles{},left,posed,bends);
  for(int d=0;d<kDigits;d++){const auto delta=Sub(targets[d],g.centre);const float axial=Dot(delta,g.axis);CHECK(std::fabs(Length(Sub(delta,Mul(g.axis,axial)))-g.tube)<1e-4f);CHECK(Length(Sub(Apply(posed[DigitBase(d)+1],arm.tip[d]),targets[d]))<.015f);}
 }
	if (failures)
		return 1;
	std::puts("all steering-wheel grip tests passed");
}
