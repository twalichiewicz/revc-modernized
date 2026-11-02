#pragma once
#include "carlightrules.h"
#include "types.h"
#include <algorithm>
#include <vector>
namespace carmaterials {
struct Access {
	const CRGBA *(*Color)(const void *);
	void *(*SetColor)(void *, const CRGBA *);
	uint32_t (*Effects)(const void *);
	float (*Env)(const void *);
	void *(*SetEnv)(void *, float);
};
// The vehicle model's materials can be shared across atomics and cars. Apply
// only while one atomic draws, and restore in reverse order even if it throws.
class InteriorScope {
  public:
	explicit InteriorScope(const Access &access) : access_(access) {}
	InteriorScope(const InteriorScope &) = delete;
	InteriorScope &operator=(const InteriorScope &) = delete;
	void Adjust(void *material)
	{
		const CRGBA color = *access_.Color(material);
		if (color.a == 0 || color.a == 255)
			return;
		const uint32_t effect = access_.Effects ? access_.Effects(material) : 0;
		const bool reflection = (effect == 2 || effect == 3) && access_.Env && access_.SetEnv;
		const float env = reflection ? access_.Env(material) : 0;
		saved_.push_back({material, color, env, reflection});
		CRGBA clear = color;
		clear.a = carlightrules::InteriorAlpha(color.a);
		access_.SetColor(material, &clear);
		if (reflection)
			access_.SetEnv(material, std::min(env, 0.08f));
	}
	~InteriorScope()
	{
		for (auto it = saved_.rbegin(); it != saved_.rend(); ++it) {
			access_.SetColor(it->material, &it->color);
			if (it->reflection)
				access_.SetEnv(it->material, it->env);
		}
	}

  private:
	struct Saved {
		void *material;
		CRGBA color;
		float env;
		bool reflection;
	};
	const Access &access_;
	std::vector<Saved> saved_;
};
} // namespace carmaterials
