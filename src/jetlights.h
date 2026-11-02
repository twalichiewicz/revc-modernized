#pragma once
#include "types.h"
#include <array>

namespace jetlights {
struct Lens { Vec3 position; uint8_t red,green,blue; float radius; };
// Measured on the original Learjet mesh. The lens centers sit partly inside
// the skin; the model builder and light producer share these exact anchors.
inline constexpr std::array<Lens,5> kLenses{{
	{{-10.0356f,-.1682f,-.6268f},255,35,25,.055f},
	{{10.0356f,-.1682f,-.6268f},30,255,65,.055f},
	{{0,-8.50f,1.71f},255,255,240,.055f},
	{{0,.5516f,.61f},255,25,15,.07f},
	{{0,.5516f,-1.24f},255,25,15,.07f},
}};
bool Install();
}
