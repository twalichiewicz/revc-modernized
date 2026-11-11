// Gaze smoke (VC_REGULAR_WING=gaze, #56). On the airport apron, five bystanders
// stand idle around Tommy:
// - two men and two women (the women's walks turn the head only) 3-6 m in front,
//   each turned 45 degrees away from him;
// - one woman beside him, turned 100 degrees away (beyond a head's reach).
// Tommy is unarmed for eight seconds, then holds a pistol for eight. Every frame
// logs whether each bystander's native head look is on Tommy and how far the
// head has turned, so a glance that the game drops at once, or that never turns
// the head, shows. Frames are captured for a look.
#include "common.h"
#include "CivilianPed.h"
#include "General.h"
#include "PlayerPed.h"
#include "Pools.h"
#include "Streaming.h"
#include "Timer.h"
#include "WeaponInfo.h"
#include "World.h"
#include <cmath>
#include <cstdio>
#include <string>

void RegularGameSmokeCapture(const char *);

namespace {
struct Watch {
	int ref = -1, model = 0;
	bool female = false, reachable = true;
	int looked = 0, frames = 0, glances = 0, run = 0, longest = 0, shortest = 1 << 30;
	float maxYaw = 0;
	bool was = false;
	int armedLongest = 0;
};
Watch gWatch[5];
int gPhase = 0;
uint32_t gStart = 0;
bool gCapturedCalm = false, gCapturedArmed = false;

void Sample(CPlayerPed *player, bool armed)
{
	for (auto &w : gWatch) {
		CPed *ped = CPools::GetPed(w.ref);
		if (!ped)
			continue;
		const bool looking = ped->bIsLooking && ped->m_pLookTarget == player;
		w.frames++;
		if (looking) {
			w.looked++;
			w.run++;
			if (!w.was)
				w.glances++;
			if (std::fabs(ped->m_pedIK.m_headOrient.yaw) > w.maxYaw)
				w.maxYaw = std::fabs(ped->m_pedIK.m_headOrient.yaw);
		} else if (w.was) {
			if (w.run < w.shortest)
				w.shortest = w.run;
			if (w.run > w.longest)
				w.longest = w.run;
			if (armed && w.run > w.armedLongest)
				w.armedLongest = w.run;
			w.run = 0;
		}
		w.was = looking;
	}
}
} // namespace

void GazeSmokeTick(unsigned frames, const char *capture)
{
	CPlayerPed *player = FindPlayerPed();
	if (!player || gPhase == 99)
		return;
	const uint32_t now = CTimer::GetTimeInMilliseconds();
	if (gPhase == 0 && frames >= 240) {
		const int models[5] = {12, 10, 15, 9, 14};
		for (int m : models)
			CStreaming::RequestModel(m, STREAMFLAGS_DONT_REMOVE);
		CStreaming::RequestModel(CWeaponInfo::GetWeaponInfo(WEAPONTYPE_COLT45)->m_nModelId, STREAMFLAGS_DONT_REMOVE);
		CStreaming::LoadAllRequestedModels(false);
		player->SetHeading(0);
		player->m_fRotationCur = player->m_fRotationDest = 0; // facing +y, the camera behind
		const CVector at = player->GetPosition();
		struct Place {
			float x, y, turn;
		} place[5] = {{-1.5f, 3, 45}, {1.5f, 3, -45}, {-2, 6, 45}, {2, 6, -45}, {2.2f, 0, 100}};
		for (int i = 0; i < 5; i++) {
			const bool female = models[i] == 10 || models[i] == 9 || models[i] == 14;
			auto ped = new CCivilianPed(female ? PEDTYPE_CIVFEMALE : PEDTYPE_CIVMALE, models[i]);
			const CVector p = at + CVector(place[i].x, place[i].y, 0);
			const float toTommy = CGeneral::GetRadianAngleBetweenPoints(at.x, at.y, p.x, p.y);
			const float heading = CGeneral::LimitRadianAngle(toTommy + DEGTORAD(place[i].turn));
			ped->SetPosition(p);
			ped->SetHeading(heading);
			ped->m_fRotationCur = ped->m_fRotationDest = heading;
			ped->SetObjective(OBJECTIVE_WAIT_ON_FOOT);
			CWorld::Add(ped);
			gWatch[i].ref = CPools::GetPedRef(ped);
			gWatch[i].model = models[i];
			gWatch[i].female = female;
			gWatch[i].reachable = std::fabs(place[i].turn) < 80;
		}
		std::fprintf(stderr, "[gaze-smoke] placed bystanders=5 at=%.1f %.1f %.1f\n", at.x, at.y, at.z);
		gPhase = 1;
		gStart = now;
		return;
	}
	if (gPhase == 1 || gPhase == 2) {
		const uint32_t t = now - gStart;
		const bool armed = gPhase == 2;
		Sample(player, armed);
		if (frames % 15 == 0)
			for (int i = 0; i < 5; i++)
				if (CPed *ped = CPools::GetPed(gWatch[i].ref))
					std::fprintf(stderr, "[gaze-smoke] t=%u ped=%d state=%d looking=%d at_tommy=%d head_yaw=%.2f\n", t, i,
						     int(ped->m_nPedState), int(ped->bIsLooking), int(ped->m_pLookTarget == player),
						     ped->m_pedIK.m_headOrient.yaw);
		if (!armed && !gCapturedCalm && t > 5000) {
			RegularGameSmokeCapture((std::string(capture) + ".calm.ppm").c_str());
			gCapturedCalm = true;
		}
		if (!armed && t > 8000) {
			player->GiveWeapon(WEAPONTYPE_COLT45, 100);
			player->SetCurrentWeapon(WEAPONTYPE_COLT45);
			std::fprintf(stderr, "[gaze-smoke] pistol drawn weapon=%d\n", int(player->GetWeapon()->m_eWeaponType));
			gPhase = 2;
			return;
		}
		if (armed && !gCapturedArmed && t > 12000) {
			RegularGameSmokeCapture((std::string(capture) + ".armed.ppm").c_str());
			gCapturedArmed = true;
		}
		if (armed && t > 16000) {
			Sample(player, true);
			for (auto &w : gWatch)
				if (w.was) { // a look still going at the end counts too
					w.shortest = w.run < w.shortest ? w.run : w.shortest;
					w.longest = w.run > w.longest ? w.run : w.longest;
					w.armedLongest = w.run > w.armedLongest ? w.run : w.armedLongest;
				}
			for (int i = 0; i < 5; i++) {
				const auto &w = gWatch[i];
				const float fps = w.frames > 0 ? w.frames / 16.f : 30;
				std::fprintf(stderr,
					     "[gaze-smoke] bystander=%d model=%d female=%d reachable=%d alive=%d glances=%d looked_s=%.2f "
					     "shortest_s=%.2f longest_s=%.2f armed_longest_s=%.2f max_head_yaw=%.2f\n",
					     i, w.model, int(w.female), int(w.reachable), CPools::GetPed(w.ref) != nil, w.glances,
					     w.looked / fps, w.glances ? w.shortest / fps : 0.f, w.longest / fps, w.armedLongest / fps,
					     w.maxYaw);
			}
			std::fprintf(stderr, "[gaze-smoke] complete\n");
			RegularGameSmokeCapture(capture);
			RsGlobal.quit = TRUE;
			gPhase = 99;
		}
	}
}
