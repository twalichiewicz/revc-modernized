// The viewmodel's offline check: Tommy's arms cut and articulated from each of
// his outfits' models (the game's own, read from its IMG) by the same code
// the mod runs, his hands fitted to a gun, reported (how much of each finger
// ends up inside it) and written out as OBJs to render (tools/viewmodel/render.py).
//
//   vcmm-viewmodel <GameData> <out dir> [--gun <name>] [--level] [outfit ...]
//
// The gun is assets/guns/<name>.vmg (colt45 unless named), read from the file
// so one being built needn't be built into the mod yet.
#include "dff.h"
#include "gamefiles.h"
#include "vmarms.h"
#include "vmfit.h"
#include "vmanim.h"
#include "vmgun.h"
#include "vmhands.h"
#include "vmpose.h"
#include "vmskin.h"
#include "vmview.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

const char *const kOutfits[] = {"player",  "player2", "player3", "player4", "player5", "player6",
                                "player7", "player8", "player9", "play10",  "play11",  "play12"};
const char *const kRight[vm::kDigits] = {"r_index", "r_middle", "r_ring", "r_pinky", "r_thumb"};
const char *const kLeft[vm::kDigits] = {"l_index", "l_middle", "l_ring", "l_pinky", "l_thumb"};
const char *const kBoneNames[vm::kArmBones] = {"upper arm", "forearm", "hand", "index", "index tip", "middle", "middle tip",
                                               "ring", "ring tip", "pinky", "pinky tip", "thumb", "thumb tip"};

// `frame` takes skin space to the OBJ's (the hand's own frame, say).
void WriteObj(const std::string &path, const std::string &texture, const vm::Arm &arm, const vm::Frame &frame = vm::Identity())
{
	FILE *f = fopen(path.c_str(), "w");
	if (!f)
		return;
	const std::string mtl = path.substr(0, path.size() - 4) + ".mtl";
	fprintf(f, "mtllib %s\nusemtl skin\n", mtl.substr(mtl.find_last_of('/') + 1).c_str());
	for (const vm::SkinVertex &v : arm.vertices) {
		const vm::Vec3 p = vm::Apply(frame, v.pos), n = vm::Rotate(frame, v.normal);
		fprintf(f, "v %.5f %.5f %.5f\nvn %.4f %.4f %.4f\nvt %.5f %.5f\n", p.x, p.y, p.z, n.x, n.y, n.z, v.u, 1.0f - v.v);
	}
	for (const vm::Triangle &t : arm.triangles)
		fprintf(f, "f %d/%d/%d %d/%d/%d %d/%d/%d\n", t.v[0] + 1, t.v[0] + 1, t.v[0] + 1, t.v[1] + 1, t.v[1] + 1, t.v[1] + 1,
		        t.v[2] + 1, t.v[2] + 1, t.v[2] + 1);
	fclose(f);
	if (FILE *m = fopen(mtl.c_str(), "w")) {
		fprintf(m, "newmtl skin\nmap_Kd %s\n", texture.c_str());
		fclose(m);
	}
}

// The gun as `frame` puts it, wearing its atlas (<name>_atlas.png).
// The gun this frame: framed, carried (sprinting), moved as the animation has it, its parts moved.
struct Moment {
	vm::Frame framed, carried, moved;
	std::vector<vm::Frame> parts;
	vm::AnimOutput anim{};
	vm::ReloadHand reload{0.0f, 0.0f, false};
};

void WriteGun(const std::string &out, const std::string &file, const std::string &name, const vm::Gun &gun, const vm::Frame &frame,
              const Moment *moment = nullptr)
{
	FILE *f = fopen((out + "/" + file).c_str(), "w");
	if (!f)
		return;
	fprintf(f, "mtllib %s.mtl\nusemtl gun\n", name.c_str());
	for (const vm::GunVertex &v : gun.vertices) {
		const vm::Frame part = moment && v.part < moment->parts.size() ? moment->parts[v.part] : vm::Identity();
		const vm::Vec3 p = vm::Apply(frame, vm::Apply(part, v.pos)), n = vm::Rotate(frame, vm::Rotate(part, v.normal));
		fprintf(f, "v %.5f %.5f %.5f\nvn %.4f %.4f %.4f\nvt %.5f %.5f\n", p.x, p.y, p.z, n.x, n.y, n.z, v.u, 1.0f - v.v);
	}
	const int magazine = vm::FindPart(gun, "magazine");
	for (size_t i = 0; i + 2 < gun.indices.size(); i += 3)
		if (!moment || moment->anim.magazineShown || magazine < 0 || gun.vertices[gun.indices[i]].part != uint32_t(magazine))
			fprintf(f, "f %d/%d/%d %d/%d/%d %d/%d/%d\n", gun.indices[i] + 1, gun.indices[i] + 1, gun.indices[i] + 1,
		        gun.indices[i + 1] + 1, gun.indices[i + 1] + 1, gun.indices[i + 1] + 1, gun.indices[i + 2] + 1,
		        gun.indices[i + 2] + 1, gun.indices[i + 2] + 1);
	fclose(f);
	if (FILE *m = fopen((out + "/" + name + ".mtl").c_str(), "w")) {
		fprintf(m, "newmtl gun\nmap_Kd %s_atlas.png\n", name.c_str());
		fclose(m);
	}
}

// How much of each of an arm's bones is inside the gun (`gunView` places it), as a report.
std::string Inside(const vm::Gun &gun, const vm::Frame &gunView, const vm::Arm &arm, const std::vector<vm::Vec3> &pos)
{
	const vm::Frame toGun = vm::Inverse(gunView);
	int inside[vm::kArmBones] = {};
	for (size_t i = 0; i < pos.size(); i++)
		if (vm::Winding(gun, vm::Apply(toGun, pos[i])) > 0.5f)
			inside[arm.vertices[i].bone[0]]++;
	std::string into;
	for (int b = 0; b < vm::kArmBones; b++)
		if (inside[b])
			into += std::string(into.empty() ? "" : ", ") + kBoneNames[b] + " " + std::to_string(inside[b]);
	return into.empty() ? "nothing" : into;
}

// One arm holding the gun as the mod poses it (vmhands): in the view's frame
// (the eye at the origin, looking along y, z up). Returns the posed arm; its
// `shown` false for a left arm the mod wouldn't draw.
vm::Arm Posed(const vm::Arm &arm, const vm::Gun &gun, const Moment &now, bool left, std::string &report, bool &shown)
{
	const vm::Fit fit = vm::FitHand(arm, gun, left ? kLeft : kRight, left);
	const vm::HandPlace place = left ? vm::LeftPlace(gun, now.carried, now.moved, now.parts, fit.part[vm::kDigitIndex], now.reload)
	                                 : vm::RightPlace(gun, now.carried, now.moved);
	shown = place.shown;
	vm::Vec3 anchors[vm::kDigits], bends[vm::kDigits];
	bool reach[vm::kDigits];
	for (int d = 0; d < vm::kDigits; d++) {
		reach[d] = place.reaching && fit.reach[d] && !getenv("VM_NO_REACH");
		const vm::Frame &part = fit.part[d] < now.parts.size() ? now.parts[fit.part[d]] : now.parts[0];
		anchors[d] = vm::Apply(place.anchors, vm::Apply(now.moved, vm::Apply(part, fit.anchor[d])));
		bends[d] = vm::Rotate(place.anchors, vm::Rotate(now.moved, fit.bend[d]));
	}
	vm::Frame matrices[vm::kArmBones];
	vm::Pose(arm, place.hand, anchors, reach, place.angles, left, matrices, bends);
	const vm::Frame &gunView = now.moved;
	std::vector<vm::Vec3> pos, normal;
	vm::Skin(arm.vertices, matrices, pos, normal);
	report = Inside(gun, gunView, arm, pos);
	vm::Arm posed = arm;
	for (size_t i = 0; i < pos.size(); i++) {
		posed.vertices[i].pos = pos[i];
		posed.vertices[i].normal = normal[i];
	}
	return posed;
}

} // namespace

// How far a gun framed at the hip points off where he aims (degrees above the
// line from its muzzle to the crosshair 25 m out, and in across it), as
// test_vmgun's TestEveryGunPointsWhereItAims measures it.
void AimError(const vm::Gun &gun, const vm::Framing &framing, float &up, float &in)
{
	const float deg = 180.0f / 3.14159265f;
	const vm::Frame view = vm::GunInView(gun, framing, 0.0f);
	const vm::Vec3 barrel = vm::Normal(vm::Rotate(view, {0, 1, 0})), muzzle = vm::Apply(view, gun.muzzle);
	const vm::Vec3 aim = vm::Normal(vm::Sub({0, 25, 0}, muzzle));
	up = (std::asin(barrel.z) - std::asin(aim.z)) * deg;
	in = (std::atan2(-barrel.x, barrel.y) - std::atan2(-aim.x, aim.y)) * deg;
}

// --level: the gun's hip framing turned so its barrel points where he aims
// (no pitch; a yaw in across the view kept, up to 15°; one out dropped), the
// eye moved so the gun's middle stays where it was on screen, as near: its
// FRAMING line for the gun's script, and (a two-handed gun) its LEFT_FOREARM
// turned to run where it did in the view (else his shoulder comes into it).
void PrintLevelled(const std::string &name, const vm::Gun &gun)
{
	vm::Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
	for (const vm::GunVertex &v : gun.vertices) {
		lo = {std::min(lo.x, v.pos.x), std::min(lo.y, v.pos.y), std::min(lo.z, v.pos.z)};
		hi = {std::max(hi.x, v.pos.x), std::max(hi.y, v.pos.y), std::max(hi.z, v.pos.z)};
	}
	const vm::Vec3 middle = vm::Mul(vm::Add(lo, hi), 0.5f);
	auto look = [&](const vm::Framing &f) {
		const float d = 3.14159265f / 180.0f, yaw = f.hipYaw * d, pitch = f.hipPitch * d;
		const vm::Vec3 ahead{std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), std::sin(pitch)};
		return vm::Look(vm::Mul(f.hipEye, gun.scale), ahead, {0, 0, 1});
	};
	const vm::Framing was = gun.framing;
	const vm::Vec3 seen = vm::Apply(vm::Inverse(look(was)), middle); // the middle in the old view
	vm::Framing now = was;
	now.hipPitch = 0.0f;
	now.hipYaw = std::clamp(was.hipYaw, 0.0f, 15.0f);
	now.hipEye = {0, 0, 0};
	const vm::Vec3 eye = vm::Sub(middle, vm::Rotate(look(now), seen));
	now.hipEye = vm::Mul(eye, 1.0f / gun.scale);
	float up0, in0, up1, in1;
	AimError(gun, was, up0, in0);
	AimError(gun, now, up1, in1);
	printf("%s: was %.1f° up, %.1f° in; now %.1f° up, %.1f° in\n", name.c_str(), up0, in0, up1, in1);
	printf("FRAMING = ((%.3f, %.3f, %.3f), %.1f, %.1f, %.3g, %.3g)\n", now.hipEye.x, now.hipEye.y, now.hipEye.z, now.hipYaw,
	       now.hipPitch, now.aimBack, now.aimAbove);
	if (gun.twoHanded) {
		const vm::Vec3 f = vm::Rotate(look(now), vm::Rotate(vm::Inverse(look(was)), gun.leftForearm));
		printf("LEFT_FOREARM = (%.3f, %.3f, %.3f)\n", f.x, f.y, f.z);
	}
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s <GameData> <out dir> [--gun <name>] [--level] [outfit ...]\n", argv[0]);
		return 2;
	}
	const std::string gameData = argv[1], out = argv[2];
	std::string gunName = "colt45";
	std::vector<std::string> outfits;
	bool level = false;
	for (int i = 3; i < argc; i++) {
		if (std::string(argv[i]) == "--gun" && i + 1 < argc)
			gunName = argv[++i];
		else if (std::string(argv[i]) == "--level")
			level = true;
		else
			outfits.push_back(argv[i]);
	}
	if (outfits.empty())
		outfits.assign(std::begin(kOutfits), std::end(kOutfits));
	ImgArchive img;
	if (!img.Open(gameData + "/models/gta3.img")) {
		fprintf(stderr, "can't open %s/models/gta3.img\n", gameData.c_str());
		return 1;
	}
	std::ifstream file("assets/guns/" + gunName + ".vmg", std::ios::binary);
	const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	vm::Gun gun;
	std::string gunWhy;
	if (bytes.empty() || !vm::ParseGun(bytes.data(), bytes.size(), gun, gunWhy)) {
		fprintf(stderr, "assets/guns/%s.vmg didn't load: %s\n", gunName.c_str(), bytes.empty() ? "no file" : gunWhy.c_str());
		return 1;
	}
	printf("%s: %zu triangles, %s, scale %.3f\n", gunName.c_str(), gun.indices.size() / 3,
	       gun.twoHanded ? "two-handed" : "one-handed", gun.scale);
	if (const char *f = getenv("VM_FRAMING")) { // trying another: "eye x,y,z,yaw,pitch" (real metres, degrees)
		vm::Framing &fr = gun.framing;
		sscanf(f, "%f,%f,%f,%f,%f", &fr.hipEye.x, &fr.hipEye.y, &fr.hipEye.z, &fr.hipYaw, &fr.hipPitch);
	}
	if (level) {
		PrintLevelled(gunName, gun);
		return 0;
	}
	if (const char *f = getenv("VM_LEFT_FOREARM")) // trying another: "x,y,z" in the gun frame
		sscanf(f, "%f,%f,%f", &gun.leftForearm.x, &gun.leftForearm.y, &gun.leftForearm.z);
	// The moment drawn: held, `VM_RELOAD` (0..1) through a reload, or
	// `VM_SPRINT` (0..2) through a sprinting stride (the animation the game runs).
	Moment now;
	now.framed = vm::GunInView(gun, gun.framing, 0.0f);
	{
		const float reload = getenv("VM_RELOAD") ? float(atof(getenv("VM_RELOAD"))) : -1.0f;
		vm::Anim anim;
		vm::AnimInput in{};
		in.weapon = vm::WeaponNamed(gunName.c_str());
		in.covered = true;
		in.clip = reload >= 0.0f ? 0 : 7;
		in.dt = 0.05f;
		if (const char *sprint = getenv("VM_SPRINT")) {
			in.sprinting = true;
			in.stepSpeed = 7.0f;
			in.stepPhase = float(atof(sprint));
		}
		for (int i = 0; i < 40; i++)
			now.anim = anim.Update(in);
		in.reloading = reload >= 0.0f;
		in.reloadProgress = reload;
		in.dt = 0.0f;
		now.anim = anim.Update(in);
		now.reload = vm::HandForReload(in.reloading, reload, gun.twoHanded);
	}
	if (const char *c = getenv("VM_CARRY")) { // trying another sprint carry: "x,y,z,pitch,roll,yaw"
		vm::Move &m = now.anim.carry;
		sscanf(c, "%f,%f,%f,%f,%f,%f", &m.offset.x, &m.offset.y, &m.offset.z, &m.pitch, &m.roll, &m.yaw);
	}
	now.carried = vm::GunCarried(gun, now.framed, now.anim.carry);
	now.moved = vm::GunMoved(gun, now.carried, now.anim);
	now.parts.assign(std::max<size_t>(1, gun.parts.size()), vm::Identity());
	for (size_t i = 0; i < gun.parts.size(); i++)
		now.parts[i] = vm::PartMove(gun.parts[i], now.anim, gun.scale);
	const vm::Frame &gunView = now.moved;
	WriteGun(out, gunName + "_fp.obj", gunName, gun, gunView, &now);
	WriteGun(out, gunName + "_gun.obj", gunName, gun, vm::Identity());
	WritePng(out + "/" + gunName + "_atlas.png", gun.atlasWidth, gun.atlasHeight, gun.atlasRgba);
	if (FILE *f = fopen((out + "/gun.txt").c_str(), "w")) {
		fprintf(f, "%s\n", gunName.c_str());
		fclose(f);
	}
	int refused = 0;
	for (const std::string &name : outfits) {
		std::vector<uint8_t> dff, txd;
		vm::SkinnedMesh mesh;
		std::string why;
		if (!img.Read(name + ".dff", dff) || !ReadSkinnedDff(dff, mesh, why)) {
			printf("%s: unreadable: %s\n", name.c_str(), why.c_str());
			refused++;
			continue;
		}
		const vm::Arms arms = vm::Articulate(mesh);
		if (!arms.ok) {
			printf("%s: refused: %s\n", name.c_str(), arms.why.c_str());
			refused++;
			continue;
		}
		const std::string texture = name + ".png";
		int w = 0, h = 0;
		std::vector<uint8_t> rgba;
		if (img.Read(name + ".txd", txd) && DecodeTxdFirstTexture(txd, w, h, rgba))
			WritePng(out + "/" + texture, w, h, rgba);
		std::string right, left;
		bool rightShown = true, leftShown = false;
		const vm::Arm posedRight = Posed(arms.right, gun, now, false, right, rightShown);
		WriteObj(out + "/" + name + "_fp.obj", texture, posedRight);
		WriteObj(out + "/" + name + "_gun.obj", texture, posedRight, vm::Inverse(gunView));
		WriteObj(out + "/" + name + "_hand.obj", texture, arms.right, vm::Inverse(arms.right.bind[vm::kHand]));
		const vm::Arm posedLeft = Posed(arms.left, gun, now, true, left, leftShown);
		if (leftShown) {
			WriteObj(out + "/" + name + "_fp_left.obj", texture, posedLeft);
			WriteObj(out + "/" + name + "_gun_left.obj", texture, posedLeft, vm::Inverse(gunView));
		} else {
			std::remove((out + "/" + name + "_fp_left.obj").c_str());
			std::remove((out + "/" + name + "_gun_left.obj").c_str());
		}
		printf("%s: ok; inside the gun: right hand %s%s%s\n", name.c_str(), right.c_str(),
		       leftShown ? "; left hand " : "", leftShown ? left.c_str() : "");
	}
	return refused ? 1 : 0;
}
