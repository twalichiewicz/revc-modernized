#include "vmgun.h"

#include <algorithm>
#include <cstring>

namespace vm {
namespace {

// His palm's middle in his hand bone's frame: along towards the knuckles, out on the palm side.
constexpr float kPalmAlong = 0.045f, kPalmOut = 0.03f;

class Reader {
public:
	Reader(const uint8_t *data, size_t size) : p_(data), end_(data + size) {}
	bool Has(size_t n) const { return size_t(end_ - p_) >= n; }
	bool Read(void *out, size_t n)
	{
		if (!Has(n))
			return false;
		memcpy(out, p_, n);
		p_ += n;
		return true;
	}
	bool U32(uint32_t &v) { return Read(&v, 4); }
	bool F32(float &v) { return Read(&v, 4); }
	bool V3(Vec3 &v) { return F32(v.x) && F32(v.y) && F32(v.z); }
	bool Name(char *out)
	{
		if (!Read(out, 16))
			return false;
		out[15] = 0;
		return true;
	}
	void Align4(const uint8_t *start)
	{
		while ((p_ - start) % 4 && p_ < end_)
			p_++;
	}

private:
	const uint8_t *p_, *end_;
};

} // namespace

bool ParseGun(const uint8_t *data, size_t size, Gun &out, std::string &why)
{
	Reader r(data, size);
	char magic[4] = {};
	uint32_t vertices, triangles, parts, anchors;
	if (!r.Read(magic, 4) || memcmp(magic, "VMG2", 4) != 0) {
		why = memcmp(magic, "VMG1", 4) == 0 ? "an old VMG1 gun (rebuild it with make guns)" : "not a VMG2 gun";
		return false;
	}
	auto truncated = [&](const char *what) {
		why = std::string("truncated in its ") + what;
		return false;
	};
	if (!r.U32(vertices) || !r.U32(triangles) || !r.U32(parts) || !r.U32(anchors) || !r.F32(out.scale))
		return truncated("header");
	if (!r.V3(out.hold.x) || !r.V3(out.hold.y) || !r.V3(out.hold.z) || !r.V3(out.hold.pos) || !r.V3(out.muzzle) ||
	    !r.V3(out.sightRear) || !r.V3(out.sightFront))
		return truncated("hold");
	uint32_t twoHanded;
	Framing &f = out.framing;
	if (!r.U32(twoHanded) || !r.V3(out.holdLeft.x) || !r.V3(out.holdLeft.y) || !r.V3(out.holdLeft.z) ||
	    !r.V3(out.holdLeft.pos) || !r.V3(out.leftForearm) || !r.V3(f.hipEye) || !r.F32(f.hipYaw) || !r.F32(f.hipPitch) ||
	    !r.F32(f.aimBack) || !r.F32(f.aimAbove))
		return truncated("left hand and framing");
	out.twoHanded = twoHanded != 0;
	if (vertices > 65535 || parts > 64 || anchors > 64) {
		why = "too many vertices, parts or anchors";
		return false;
	}
	out.parts.resize(parts);
	for (Part &p : out.parts)
		if (!r.Name(p.name) || !r.V3(p.pivot) || !r.V3(p.axis))
			return truncated("parts");
	out.anchors.resize(anchors);
	for (Anchor &a : out.anchors)
		if (!r.Name(a.name) || !r.V3(a.pos) || !r.U32(a.part) || !r.V3(a.bend) || !r.V3(a.slack))
			return truncated("anchors");
	if (!r.Has(size_t(vertices) * 36))
		return truncated("vertices");
	out.vertices.resize(vertices);
	for (GunVertex &v : out.vertices)
		if (!r.V3(v.pos) || !r.V3(v.normal) || !r.F32(v.u) || !r.F32(v.v) || !r.U32(v.part))
			return truncated("vertices");
	out.indices.resize(size_t(triangles) * 3);
	if (!r.Read(out.indices.data(), out.indices.size() * 2))
		return truncated("triangles");
	for (uint16_t i : out.indices)
		if (i >= vertices) {
			why = "a triangle's index is past the vertices";
			return false;
		}
	for (const GunVertex &v : out.vertices)
		if (v.part >= parts) {
			why = "a vertex's part is past the parts";
			return false;
		}
	r.Align4(data);
	uint32_t w, h;
	if (!r.U32(w) || !r.U32(h) || w == 0 || h == 0 || w > 1024 || h > 1024)
		return truncated("atlas");
	uint8_t palette[256 * 4];
	std::vector<uint8_t> pixels(size_t(w) * h);
	if (!r.Read(palette, sizeof palette) || !r.Read(pixels.data(), pixels.size()))
		return truncated("atlas");
	out.atlasWidth = int(w);
	out.atlasHeight = int(h);
	out.atlasRgba.resize(pixels.size() * 4);
	for (size_t i = 0; i < pixels.size(); i++)
		memcpy(&out.atlasRgba[i * 4], &palette[pixels[i] * 4], 4);
	why.clear();
	return true;
}

const Anchor *FindAnchor(const Gun &gun, const char *name)
{
	for (const Anchor &a : gun.anchors)
		if (strcmp(a.name, name) == 0)
			return &a;
	return nullptr;
}

bool MagazineGrip(const Gun &gun, Frame &hand)
{
	const int part = FindPart(gun, "magazine");
	if (part < 0)
		return false;
	const Vec3 down = Normal(gun.parts[part].axis); // the way it drops out
	Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
	float base = -1e9f;
	for (const GunVertex &v : gun.vertices) {
		if (v.part != static_cast<uint32_t>(part))
			continue;
		lo = {std::min(lo.x, v.pos.x), std::min(lo.y, v.pos.y), std::min(lo.z, v.pos.z)};
		hi = {std::max(hi.x, v.pos.x), std::max(hi.y, v.pos.y), std::max(hi.z, v.pos.z)};
		base = std::max(base, Dot(v.pos, down));
	}
	if (base < -1e8f)
		return false;
	const Vec3 middle = Mul(Add(lo, hi), 0.5f);
	const Vec3 under = Add(middle, Mul(down, base - Dot(middle, down)));
	// Fingers forward (along the barrel, square to the magazine), palm up it.
	Vec3 ahead = Sub(Vec3{0, 1, 0}, Mul(down, down.y));
	ahead = Length(ahead) > 1e-3f ? Normal(ahead) : Vec3{1, 0, 0};
	const Vec3 palm = Mul(down, -1.0f);
	hand.x = ahead;
	hand.y = palm;
	hand.z = Cross(ahead, palm);
	// The palm's middle (along the knuckles' way, out on the palm side) on it.
	hand.pos = Sub(Sub(under, Mul(ahead, kPalmAlong)), Mul(palm, kPalmOut));
	return true;
}

int FindPart(const Gun &gun, const char *name)
{
	for (size_t i = 0; i < gun.parts.size(); i++)
		if (strcmp(gun.parts[i].name, name) == 0)
			return int(i);
	return -1;
}

} // namespace vm
