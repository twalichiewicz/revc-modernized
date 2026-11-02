// A routine car contact must not repeatedly postpone an owned native get-up.
#define main existing_reaction_tests
#include "test_reactionhook.cpp"
#undef main

int main()
{
	gNow += 1000;
	reactions::gLastFrame = gNow;
	reactions::gPeople.clear();
	reactions::gContacts = {};
	std::memset(gPed, 0, sizeof gPed);
	std::memset(gPlayer, 0, sizeof gPlayer);
	game::api.timeMs = &gNow;
	game::api.FindPlayerPed = Player;
	game::api.PoolsGetPed = Lookup;
	game::api.ProcessVerticalLine = NativeGround;
	reactions::gRef = Ref;
	reactions::gSetFall = Fall;
	reactions::gGetUp = GetUp;
	reactions::gUpdateMatrix = reactions::gUpdateFrame = reactions::gReindex = Noop;
	reactions::gInstalled = true;
	reactions::gCutscene = nullptr;
	gConfig.npcReactions = true;
	gLive = gPed;
	game::EntityFlags(gPed) = 3 | game::kEntityUsesCollision;
	*reinterpret_cast<uint8_t **>(gPed + 0x58) = gContactClump;
	*reinterpret_cast<float *>(gPed + 0x44c) = 100;
	*reinterpret_cast<uint32_t *>(gPed + 0x68c) = 1234;
	const auto standing = Standing();
	reactions::Person person;
	person.handle = 17;
	person.clump = gContactClump;
	person.pelvisOffset = {0, 0, -.09f};
	person.physical.Reset(standing);
	for (int i = 0; i < 4; i++) person.physical.Impulse(reactphysics::KneeL, {0, -1.4f, 0});
	reactions::gPeople.push_back(person);
	auto &body = reactions::gPeople[0];
	reactions::NativeMode(body, gPed);
	const int originalFalls = gFalls, originalGetUps = gGetUps;
	bool recovered = false;
	int contacts = 0;
	for (int frame = 0; frame < 900 && body.physical.Active(); frame++) {
		gNow += 17;
		body.physical.Advance(1.f / 60, standing, Ground, nullptr);
		reactions::NativeMode(body, gPed);
		reactions::Place(body, gPed);
		if (body.physical.State() == reactphysics::Mode::Recovering) {
			recovered = true;
			if (frame % 16 == 0) {
				reactcontact::NativePoint point{};
				point.point = body.physical.Positions()[reactphysics::Chest];
				point.normal = {1, 0, 0};
				point.depth = .02f;
				point.pieceA = 1;
				reactions::ContactReaction(gPed, reinterpret_cast<const uint8_t *>(&point), 1,
									   {-3, 3, 0}, {-3, 3, 0}, false);
				++contacts;
			}
		}
	}
	if (!recovered || !contacts || body.physical.Active() || body.physical.Positions()[reactphysics::Pelvis].z < .8f ||
		gFalls != originalFalls || gGetUps != originalGetUps + 1 || game::Health(gPed) != 100) {
		printf("FAIL: routine contacts postponed native recovery (contacts=%d active=%d pelvis=%.3f falls=%d getups=%d)\n",
			   contacts, body.physical.Active(), body.physical.Positions()[reactphysics::Pelvis].z,
			   gFalls - originalFalls, gGetUps - originalGetUps);
		return 1;
	}
	reactions::Frame();
	if (*reinterpret_cast<uint32_t *>(gPed + 0x68c) != 1234 || !reactions::gPeople.empty()) {
		puts("FAIL: completed contacted get-up did not release its native fall timer");
		return 1;
	}
	puts("recoveryhook: all passed");
	return 0;
}
