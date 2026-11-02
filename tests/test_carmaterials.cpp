#include "carmaterials.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>
struct Material {
	CRGBA color;
	float env;
	uint32_t effect;
};
const CRGBA *Color(const void *m) { return &static_cast<const Material *>(m)->color; }
void *SetColor(void *m, const CRGBA *c)
{
	static_cast<Material *>(m)->color = *c;
	return m;
}
uint32_t Effects(const void *m) { return static_cast<const Material *>(m)->effect; }
float Env(const void *m) { return static_cast<const Material *>(m)->env; }
void *SetEnv(void *m, float f)
{
	static_cast<Material *>(m)->env = f;
	return m;
}
int main()
{
	carmaterials::Access access{Color, SetColor, Effects, Env, SetEnv};
	Material glass{{40, 60, 80, 128}, 0.7f, 2}, body{{255, 255, 255, 255}, 0.7f, 2}, missing{{0, 0, 0, 0}, 0.5f, 2};
	{
		carmaterials::InteriorScope scope(access);
		scope.Adjust(&glass);
		scope.Adjust(&body);
		scope.Adjust(&missing);
		assert(glass.color.a == 48 && glass.color.r == 40 && glass.env == 0.08f);
		assert(body.color.a == 255 && body.env == 0.7f && missing.color.a == 0);
		scope.Adjust(&glass); // shared material referenced more than once
		assert(glass.color.a == 48);
	}
	assert(glass.color.a == 128 && glass.env == 0.7f);
	try {
		carmaterials::InteriorScope scope(access);
		scope.Adjust(&glass);
		throw std::runtime_error("render failed");
	} catch (const std::runtime_error &) {
	}
	assert(glass.color.a == 128 && glass.env == 0.7f);
	puts("all scoped car material tests passed");
}
