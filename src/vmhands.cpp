#include "vmhands.h"

namespace vm {
namespace {

// His left arm holding a magazine below the gun: the forearm back, out to his
// left and down from the wrist, the upper arm back and up to his shoulder.
constexpr Vec3 kMagazineForearm{-0.5f, -1.0f, -0.6f}, kMagazineUpper{-0.3f, -1.0f, 0.3f};
// Gone: down and back, out of view.
constexpr Vec3 kAway{0.0f, -0.12f, -0.35f};
// His left upper arm from the forearm, in the gun's frame: out and up.
constexpr Vec3 kLeftUpper{-0.35f, 0.0f, 0.55f};

const Frame &PartAt(const std::vector<Frame> &parts, uint32_t i) { return i < parts.size() ? parts[i] : parts[0]; }

} // namespace

Frame GunCarried(const Gun &gun, const Frame &framed, const Move &m)
{
	// Turned about his grip: rolled, tipped, then its muzzle round; then moved.
	const Vec3 grip = HandFor(gun, framed).pos;
	const float d = 3.14159265f / 180.0f;
	Frame f = Compose(TurnAbout({0, 1, 0}, m.roll * d, grip), framed);
	f = Compose(TurnAbout({1, 0, 0}, m.pitch * d, grip), f);
	f = Compose(TurnAbout({0, 0, 1}, m.yaw * d, grip), f);
	f.pos = Add(f.pos, m.offset);
	return f;
}

Frame GunMoved(const Gun &gun, const Frame &carried, const AnimOutput &out)
{
	return GunCarried(gun, carried, Move{out.offset, out.pitch, out.roll, out.yaw});
}

HandPlace RightPlace(const Gun &gun, const Frame &framed, const Frame &moved)
{
	HandPlace place{HandFor(gun, moved), ArmAngles{}, true, true, Identity()};
	ArmDirections(HandFor(gun, framed), ArmAngles{}, false, place.angles.forearm, place.angles.upper);
	return place;
}

HandPlace LeftPlace(const Gun &gun, const Frame &framed, const Frame &moved, const std::vector<Frame> &parts,
                    uint32_t onPart, const ReloadHand &reload)
{
	HandPlace place{};
	place.shown = place.reaching = gun.twoHanded;
	place.anchors = Identity();
	if (gun.twoHanded) { // on its hold, moving with the part it's on
		place.hand = Compose(Compose(moved, PartAt(parts, onPart)), Inverse(gun.holdLeft));
		place.angles.forearm = Normal(Rotate(framed, gun.leftForearm));
		place.angles.upper = Normal(Add(place.angles.forearm, Rotate(framed, kLeftUpper)));
	}
	Frame grip;
	const int magazine = FindPart(gun, "magazine");
	if (!reload.shown || magazine < 0 || !MagazineGrip(gun, grip))
		return place;
	// At the magazine, holding its base as it moves.
	const float t = gun.twoHanded ? reload.atMagazine : 1.0f;
	const Frame atMagazine = Compose(Compose(moved, PartAt(parts, uint32_t(magazine))), grip);
	const Frame held = place.hand;
	place.hand = gun.twoHanded ? Blend(held, atMagazine, t) : atMagazine;
	// Till it lets go (halfway), its fingers keep their grip, their anchors travelling with it.
	place.reaching = gun.twoHanded && t < 0.5f;
	if (place.reaching)
		place.anchors = Compose(place.hand, Inverse(held));
	place.hand.pos = Add(place.hand.pos, Mul(kAway, reload.away));
	place.angles.forearm = gun.twoHanded ? Normal(Lerp(place.angles.forearm, Normal(kMagazineForearm), t)) : Normal(kMagazineForearm);
	place.angles.upper = gun.twoHanded ? Normal(Lerp(place.angles.upper, Normal(kMagazineUpper), t)) : Normal(kMagazineUpper);
	place.shown = true;
	return place;
}

} // namespace vm
