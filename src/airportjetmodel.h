#pragma once
#include "jetwheels.h"

namespace airportjetmodel {
bool Install();
// Runs on the independently loaded, owned native vehicle template, before any
// actors clone it. The original static-model geometry remains untouched.
bool Prepare(void *clump,void *policeMaverickClump=nullptr);
struct Pose {
 std::array<jetwheels::WheelPose,3> wheels{};
 float leftFlap=0,rightFlap=0,elevator=0,rudder=0,gearDown=1,doorOpen=0;
};
void Animate(void *clump,const Pose &pose);
void Animate(void *clump,const std::array<jetwheels::WheelPose,3> &wheels,float leftFlap,float rightFlap);
}
