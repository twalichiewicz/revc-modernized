#include "helizones.h"
#include <cmath>
#include <cstdio>
int main()
{
	int failures=0;
	for (Vec3 target: {Vec3{0,30,-30},Vec3{-10,4,-10},Vec3{0,0,-100},Vec3{10,40,0}}) {
		const helizones::Lamp lamp=helizones::Aim(target);
		helizones::Mesh housing,glow;
		helizones::LampMesh(lamp,true,false,housing,glow);
		Vec3 centre{};
		for (const auto &v:glow.v) { centre.x+=v.p.x; centre.y+=v.p.y; centre.z+=v.p.z; }
		const float n=float(glow.v.size());
		centre={centre.x/n,centre.y/n,centre.z/n};
		const Vec3 source=helizones::Lens(lamp);
		if (std::fabs(source.x-centre.x)>0.0001f || std::fabs(source.y-centre.y)>0.0001f ||
		    std::fabs(source.z-centre.z)>0.0001f) {
			std::puts("FAIL: emitted source differs from the physical lens centre"); failures++;
		}
	}
	if (!failures) std::puts("helilight: all passed");
	return failures?1:0;
}
