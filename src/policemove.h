#pragma once
// A police foot chase's pace (#23). Native CPed::Seek gives every police
// objective a run (only OBJECTIVE_SPRINT_TO_AREA sprints), so a cop never
// keeps up with a sprinting Tommy; in the seek states it also only ever raises
// a ped's pace (0x1001a3954), so a sprint set here must be lowered here too.
#include <cstdint>

namespace policemove {

constexpr int32_t kMoveStill = 1, kMoveRun = 4, kMoveSprint = 5; // CPed::m_nMoveState
constexpr float kSprintFrom = 10.0f, kSprintUntil = 7.0f;        // m behind him (hysteresis)

enum class Pace {
	Native, // leave the game's pace
	Sprint, // raise its run to a sprint
	Run,    // back within reach: the game's run
	Repick, // no longer chasing: still, so the game re-picks its own pace next frame
};

// chasing: a cop on foot whose kill objective targets Tommy; promoted: its
// sprint was set here.
inline Pace Decide(bool chasing, bool promoted, int32_t moveState, float gap)
{
	if (!chasing)
		return promoted && moveState == kMoveSprint ? Pace::Repick : Pace::Native;
	if (moveState == kMoveRun && gap > kSprintFrom)
		return Pace::Sprint;
	if (promoted && moveState == kMoveSprint && gap < kSprintUntil)
		return Pace::Run;
	return Pace::Native;
}

} // namespace policemove
