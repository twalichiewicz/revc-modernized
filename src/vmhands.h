#pragma once
// Where his hands are on the gun this frame, and which way his arms run from
// them: his right on its grip; his left on its handguard (a two-handed gun),
// or at its magazine through a reload, taking it out and away and bringing
// the new one in. His arms keep to the gun as it's framed, not as it moves:
// a reload's tilt or a draw turns his hands, never swings his shoulder up into
// view. All in the view's frame (x right, y ahead, z up). Shared by the game
// (fpgun) and the offline tool. Unit-tested (tests/test_vmhands.cpp).
#include "vmanim.h"
#include "vmpose.h"

#include <vector>

namespace vm {

struct HandPlace {
	Frame hand;
	ArmAngles angles; // its forearm's and upper arm's ways set
	bool shown;
	bool reaching;    // its fingers reach for their anchors on the gun (not while it holds the magazine)
	Frame anchors;    // how their anchors move with it, travelling from its hold (else none)
};

// The gun carried as he sprints (`framed`: as it's framed in the view): his
// arms keep to this.
Frame GunCarried(const Gun &gun, const Frame &framed, const Move &carry);
// The gun as the animation moves it from there (`carried`): turned about his
// grip, then moved; his hands go with it, his arms don't.
Frame GunMoved(const Gun &gun, const Frame &carried, const AnimOutput &out);

// `framed`: the gun as his arms keep to it (carried); `moved`: as the animation has it.
HandPlace RightPlace(const Gun &gun, const Frame &framed, const Frame &moved);
// `parts`: each part's move this frame (the gun's frame); `onPart`: the part
// his left fingers hold (a pump); `reload`: the reload's hand (HandForReload).
HandPlace LeftPlace(const Gun &gun, const Frame &framed, const Frame &moved, const std::vector<Frame> &parts,
                    uint32_t onPart, const ReloadHand &reload);

} // namespace vm
