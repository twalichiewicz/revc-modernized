// Unit tests for the viewmodel's gun assets (.vmg): make test
#include "vmgun.h"
#include "vmview.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

using namespace vm;

// A writer for the format, as tools/guns/kit.py writes it.
struct Writer {
	std::vector<uint8_t> out;
	void U32(uint32_t v)
	{
		for (int i = 0; i < 4; i++)
			out.push_back(uint8_t(v >> (8 * i)));
	}
	void F32(float f)
	{
		uint32_t v;
		memcpy(&v, &f, 4);
		U32(v);
	}
	void U16(uint16_t v)
	{
		out.push_back(uint8_t(v));
		out.push_back(uint8_t(v >> 8));
	}
	void Name(const char *s)
	{
		char n[16] = {};
		strncpy(n, s, 15);
		out.insert(out.end(), n, n + 16);
	}
	void Vec(float x, float y, float z)
	{
		F32(x);
		F32(y);
		F32(z);
	}
};

static std::vector<uint8_t> HandMade(uint32_t magic = 0x32474d56 /* "VMG2" */, uint16_t badIndex = 0)
{
	Writer w;
	w.U32(magic);
	w.U32(3); // vertices
	w.U32(1); // triangles
	w.U32(2); // parts
	w.U32(1); // anchors
	w.F32(1.09f);
	w.Vec(0, -1, 0); // hold: x axis
	w.Vec(1, 0, 0);  // y axis
	w.Vec(0, 0, 1);  // z axis
	w.Vec(0.1f, 0.2f, 0.3f);
	w.Vec(0, 0.2f, 0.1f);  // muzzle
	w.Vec(0, 0.02f, 0.13f); // rear sight
	w.Vec(0, 0.2f, 0.13f);  // front sight
	w.U32(1);                // two-handed
	w.Vec(1, 0, 0);          // left hold: x axis
	w.Vec(0, 0, 1);          // y axis
	w.Vec(0, -1, 0);         // z axis
	w.Vec(0.3f, 0.1f, 0.0f);
	w.Vec(-0.2f, -1, -0.4f); // its forearm
	w.Vec(-0.15f, -0.2f, 0.16f); // framing: hip eye
	w.F32(13.0f);
	w.F32(-3.0f);
	w.F32(0.22f);
	w.F32(0.02f);
	w.Name("body");
	w.Vec(0, 0, 0);
	w.Vec(0, 0, 0);
	w.Name("slide");
	w.Vec(0, 0.1f, 0.1f);
	w.Vec(0, -1, 0);
	w.Name("r_index");
	w.Vec(-0.001f, 0.1f, 0.085f);
	w.U32(1);
	w.Vec(0.6f, 0.0f, 0.8f); // its bend hint
	w.Vec(0.0f, 0.01f, 0.0f); // its slack
	for (int i = 0; i < 3; i++) {
		w.Vec(float(i), 0, 0);
		w.Vec(0, 0, 1);
		w.F32(0.25f * float(i));
		w.F32(0.5f);
		w.U32(i == 2 ? 1 : 0);
	}
	w.U16(0);
	w.U16(1);
	w.U16(badIndex ? badIndex : 2);
	w.U16(0); // padding to 4
	w.U32(2);
	w.U32(2);
	for (int i = 0; i < 256; i++) {
		w.out.push_back(uint8_t(i));
		w.out.push_back(uint8_t(255 - i));
		w.out.push_back(7);
		w.out.push_back(255);
	}
	for (uint8_t p : {0, 1, 2, 255})
		w.out.push_back(p);
	return w.out;
}

static void TestParsesAHandMadeGun()
{
	const std::vector<uint8_t> data = HandMade();
	Gun gun;
	std::string why;
	CHECK(ParseGun(data.data(), data.size(), gun, why));
	CHECK(why.empty());
	CHECK(std::fabs(gun.scale - 1.09f) < 1e-6f);
	CHECK(gun.hold.x.y == -1.0f && gun.hold.y.x == 1.0f && gun.hold.pos.z == 0.3f);
	CHECK(gun.muzzle.y == 0.2f && gun.sightRear.z == 0.13f && gun.sightFront.y == 0.2f);
	CHECK(gun.twoHanded && gun.holdLeft.y.z == 1.0f && gun.holdLeft.pos.x == 0.3f && gun.leftForearm.z == -0.4f);
	CHECK(gun.framing.hipEye.x == -0.15f && gun.framing.hipYaw == 13.0f && gun.framing.aimAbove == 0.02f);
	CHECK(gun.parts.size() == 2 && FindPart(gun, "slide") == 1 && FindPart(gun, "bolt") == -1);
	CHECK(gun.parts[1].axis.y == -1.0f && gun.parts[1].pivot.z == 0.1f);
	const Anchor *a = FindAnchor(gun, "r_index");
	CHECK(a && a->part == 1 && a->pos.z == 0.085f);
	CHECK(a && a->bend.x == 0.6f && a->bend.z == 0.8f && a->slack.y == 0.01f);
	CHECK(!FindAnchor(gun, "r_thumb"));
	CHECK(gun.vertices.size() == 3 && gun.vertices[2].part == 1 && gun.vertices[1].u == 0.25f);
	CHECK(gun.indices.size() == 3 && gun.indices[2] == 2);
	CHECK(gun.atlasWidth == 2 && gun.atlasHeight == 2 && gun.atlasRgba.size() == 16);
	CHECK(gun.atlasRgba[4] == 1 && gun.atlasRgba[5] == 254 && gun.atlasRgba[12] == 255 && gun.atlasRgba[13] == 0);
}

static void TestRefusesBadData()
{
	Gun gun;
	std::string why;
	const std::vector<uint8_t> magic = HandMade(0x31474d56); // a VMG1, from before the framing went in
	CHECK(!ParseGun(magic.data(), magic.size(), gun, why) && why.find("make guns") != std::string::npos);
	const std::vector<uint8_t> whole = HandMade();
	why.clear();
	CHECK(!ParseGun(whole.data(), 200, gun, why) && !why.empty());
	const std::vector<uint8_t> index = HandMade(0x32474d56, 3);
	why.clear();
	CHECK(!ParseGun(index.data(), index.size(), gun, why) && why.find("index") != std::string::npos);
}

static void TestTheColt()
{
	const Embedded *colt = FindEmbedded(17);
	CHECK(colt != nullptr);
	if (!colt)
		return;
	Gun gun;
	std::string why;
	CHECK(ParseGun(colt->begin, size_t(colt->end - colt->begin), gun, why));
	if (!why.empty())
		printf("colt: %s\n", why.c_str());
	const size_t triangles = gun.indices.size() / 3;
	CHECK(triangles >= 250 && triangles <= 400);
	CHECK(gun.atlasWidth == 256 && gun.atlasHeight == 256);
	for (const char *p : {"body", "slide", "trigger", "magazine"})
		CHECK(FindPart(gun, p) >= 0);
	for (const char *a : {"r_index", "r_middle", "r_ring", "r_pinky", "r_thumb"})
		CHECK(FindAnchor(gun, a) != nullptr);
	CHECK(std::fabs(Length(gun.hold.x) - 1.0f) < 1e-4f && std::fabs(Length(gun.hold.y) - 1.0f) < 1e-4f &&
	      std::fabs(Length(gun.hold.z) - 1.0f) < 1e-4f);
	CHECK(std::fabs(Dot(gun.hold.x, gun.hold.y)) < 1e-4f && std::fabs(Dot(gun.hold.y, gun.hold.z)) < 1e-4f);
	CHECK(gun.scale > 1.0f && gun.scale < 1.2f);
	CHECK(!gun.twoHanded);
	// Seen from as far back and as high as the Uzi (it read large and high), level.
	CHECK(std::fabs(gun.framing.hipEye.x + 0.190f) < 1e-5f && std::fabs(gun.framing.hipEye.y + 0.300f) < 1e-5f &&
	      std::fabs(gun.framing.hipEye.z - 0.200f) < 1e-5f);
	CHECK(gun.framing.hipYaw == 12.0f && gun.framing.hipPitch == 0.0f);
	CHECK(std::fabs(gun.framing.aimBack - 0.22f) < 1e-6f && std::fabs(gun.framing.aimAbove - 0.02f) < 1e-6f);
	printf("colt: %zu triangles, scale %.3f\n", triangles, gun.scale);
}

// His left hand holding a magazine: its palm up under the magazine's base, the magazine's way up its palm side.
static void TestMagazineGripUnderTheBase()
{
	Gun gun;
	Part body{}, mag{};
	strcpy(body.name, "body");
	strcpy(mag.name, "magazine");
	mag.axis = {0, 0, -1}; // it drops straight down
	gun.parts = {body, mag};
	for (float x : {-0.01f, 0.01f})
		for (float y : {0.02f, 0.05f})
			for (float z : {-0.10f, 0.0f})
				gun.vertices.push_back({{x, y, z}, {0, 0, 1}, 0, 0, 1});
	gun.vertices.push_back({{0, 0, 0.2f}, {0, 0, 1}, 0, 0, 0}); // the body, higher
	Frame hand;
	CHECK(MagazineGrip(gun, hand));
	CHECK(Length(Sub(Normal(hand.y), {0, 0, 1})) < 1e-4f);           // palm side up the magazine
	const Vec3 palm = Add(Add(hand.pos, Mul(Normal(hand.x), 0.045f)), Mul(Normal(hand.y), 0.03f));
	CHECK(Length(Sub(palm, {0, 0.035f, -0.10f})) < 1e-4f);           // on the base's middle
	Gun none;
	none.parts = {body};
	CHECK(!MagazineGrip(none, hand));
}

// Framed at the hip, every gun's barrel points where he aims: within 3° up or
// down of the line from its muzzle to the crosshair 25 m out (the minigun's
// was 14° over it), and turned in across that line by no more than 15° (a
// pistol's Counter-Strike cant), never out.
static void TestEveryGunPointsWhereItAims()
{
	const float deg = 180.0f / 3.14159265f;
	for (int weapon = 17; weapon <= 33; weapon++) {
		const Embedded *e = FindEmbedded(weapon);
		if (!e)
			continue;
		Gun gun;
		std::string why;
		CHECK(ParseGun(e->begin, size_t(e->end - e->begin), gun, why));
		const Frame view = GunInView(gun, gun.framing, 0.0f);
		const Vec3 barrel = Normal(Rotate(view, {0, 1, 0})), muzzle = Apply(view, gun.muzzle);
		const Vec3 aim = Normal(Sub({0, 25, 0}, muzzle));
		const float up = (std::asin(barrel.z) - std::asin(aim.z)) * deg;
		const float in = (std::atan2(-barrel.x, barrel.y) - std::atan2(-aim.x, aim.y)) * deg; // towards the middle
		if (std::fabs(up) > 3.0f || in < -1.0f || in > 15.0f) {
			printf("FAIL %s: its barrel %.1f° above where he aims, %.1f° in across it\n", e->name, up, in);
			gFailures++;
		}
	}
}

int main()
{
	TestEveryGunPointsWhereItAims();
	TestMagazineGripUnderTheBase();
	TestParsesAHandMadeGun();
	TestRefusesBadData();
	TestTheColt();
	if (gFailures) {
		printf("%d vmgun test(s) failed\n", gFailures);
		return 1;
	}
	printf("all vmgun tests passed\n");
	return 0;
}
