#include "vmskin.h"

namespace vm {

void Skin(const std::vector<SkinVertex> &in, const Frame *matrices, std::vector<Vec3> &pos, std::vector<Vec3> &normal)
{
	pos.resize(in.size());
	normal.resize(in.size());
	for (size_t i = 0; i < in.size(); i++) {
		const SkinVertex &v = in[i];
		Vec3 p{0, 0, 0}, n{0, 0, 0};
		for (int k = 0; k < 4; k++) {
			if (v.weight[k] <= 0.0f)
				continue;
			const Frame &m = matrices[v.bone[k]];
			p = Add(p, Mul(Apply(m, v.pos), v.weight[k]));
			n = Add(n, Mul(Rotate(m, v.normal), v.weight[k]));
		}
		pos[i] = p;
		normal[i] = Normal(n);
	}
}

} // namespace vm
