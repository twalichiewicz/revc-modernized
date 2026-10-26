#include "watch.h"
#include "game.h"
#include "log.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <dlfcn.h>
#include <mach/mach.h>
#include <pthread.h>
#include <thread>

using namespace game;

namespace watch {
namespace {

// --- Events ------------------------------------------------------------------------------

constexpr int32_t kPedArrested = 62; // ePedState, after kPedDie and kPedDead
bool (*gOnAMission)();               // CTheScripts::IsPlayerOnAMission
bool *gCutscene;                     // CCutsceneMgr::ms_running
int32_t *gArea;                      // CGame::currArea
bool gWasDown, gWasOnMission, gWasCutscene;
int32_t gWasArea = -1;

void Where(char *out, size_t size, uint8_t *player)
{
	const bool driving = PedInVehicle(player) && PedVehicle(player);
	const CVector at = EntityPos(driving ? PedVehicle(player) : player);
	snprintf(out, size, "at %.0f,%.0f,%.0f, %s", at.x, at.y, at.z,
	         driving ? "in a vehicle" : "on foot");
	if (driving) {
		const size_t used = strlen(out);
		snprintf(out + used, size - used, " (model %d)", EntityModel(PedVehicle(player)));
	}
}

// --- The watchdog ------------------------------------------------------------------------

constexpr int kStallSeconds = 8;  // the loop not round this long: a freeze, or a long load
constexpr int kSamples = 3, kMostFrames = 28;
std::atomic<uint64_t> gBeats;
thread_act_t gMain = MACH_PORT_NULL;
uintptr_t gStackLo, gStackHi; // the main thread's stack

// The main thread's return addresses, from its frame pointers (stopped a moment).
int Backtrace(uintptr_t (&pcs)[kMostFrames])
{
	if (thread_suspend(gMain) != KERN_SUCCESS)
		return 0;
	int n = 0;
	arm_thread_state64_t st;
	mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
	if (thread_get_state(gMain, ARM_THREAD_STATE64, reinterpret_cast<thread_state_t>(&st), &count) == KERN_SUCCESS) {
		constexpr uintptr_t kAddress = 0x00007fffffffffffull; // no pointer signing bits
		pcs[n++] = uintptr_t(arm_thread_state64_get_pc(st)) & kAddress;
		pcs[n++] = uintptr_t(arm_thread_state64_get_lr(st)) & kAddress; // its caller, if it's a leaf
		uintptr_t fp = uintptr_t(arm_thread_state64_get_fp(st));
		while (n < kMostFrames && fp >= gStackLo && fp + 16 <= gStackHi && !(fp & 7)) {
			const uintptr_t next = reinterpret_cast<const uintptr_t *>(fp)[0];
			pcs[n++] = reinterpret_cast<const uintptr_t *>(fp)[1] & kAddress;
			if (next <= fp)
				break;
			fp = next;
		}
	}
	thread_resume(gMain);
	return n;
}

// Each as its image and offset, and the nearest symbol (mangled: nothing here
// allocates, in case the main thread is stuck holding malloc's lock).
void LogBacktrace(int sample)
{
	uintptr_t pcs[kMostFrames];
	const int n = Backtrace(pcs);
	Log("watch: main thread, sample %d of %d (%d frames):", sample, kSamples, n);
	for (int i = 0; i < n; i++) {
		Dl_info info{};
		if (!dladdr(reinterpret_cast<void *>(pcs[i] - (i > 0 ? 1 : 0)), &info) || !info.dli_fname) {
			Log("  %2d  0x%lx", i, static_cast<unsigned long>(pcs[i]));
			continue;
		}
		const char *image = strrchr(info.dli_fname, '/');
		image = image ? image + 1 : info.dli_fname;
		const uintptr_t base = reinterpret_cast<uintptr_t>(info.dli_fbase);
		if (info.dli_sname)
			Log("  %2d  %s+0x%lx  %s+%lu", i, image, static_cast<unsigned long>(pcs[i] - base), info.dli_sname,
			    static_cast<unsigned long>(pcs[i] - reinterpret_cast<uintptr_t>(info.dli_saddr)));
		else
			Log("  %2d  %s+0x%lx", i, image, static_cast<unsigned long>(pcs[i] - base));
	}
}

void Watchdog()
{
	using namespace std::chrono;
	uint64_t seen = 0;
	steady_clock::time_point since = steady_clock::now();
	bool reported = false;
	for (;;) {
		std::this_thread::sleep_for(milliseconds(500));
		const uint64_t beats = gBeats.load(std::memory_order_relaxed);
		const steady_clock::time_point now = steady_clock::now();
		if (beats != seen) {
			if (reported)
				Log("watch: the game came round again after %.1f s", duration<float>(now - since).count());
			seen = beats;
			since = now;
			reported = false;
			continue;
		}
		if (!seen || reported || now - since < seconds(kStallSeconds)) // (not before its first frame)
			continue;
		reported = true;
		Log("watch: the game hasn't come round for %d s (frozen, or loading); where it is:", kStallSeconds);
		for (int k = 1; k <= kSamples; k++) {
			LogBacktrace(k);
			std::this_thread::sleep_for(milliseconds(400));
		}
	}
}

} // namespace

void Install()
{
	auto sym = [](const char *name) { return dlsym(RTLD_MAIN_ONLY, name); };
	gOnAMission = reinterpret_cast<bool (*)()>(sym("_ZN11CTheScripts18IsPlayerOnAMissionEv"));
	gCutscene = static_cast<bool *>(sym("_ZN12CCutsceneMgr10ms_runningE"));
	gArea = static_cast<int32_t *>(sym("_ZN5CGame8currAreaE"));
	if (!pthread_main_np()) {
		Log("watch: not on the main thread; no watchdog");
		return;
	}
	gMain = mach_thread_self();
	const pthread_t self = pthread_self();
	gStackHi = reinterpret_cast<uintptr_t>(pthread_get_stackaddr_np(self));
	gStackLo = gStackHi - pthread_get_stacksize_np(self);
	std::thread(Watchdog).detach();
	Log("watch: logging deaths, arrests, missions and cutscenes; a freeze of %d s logs where the game is",
	    kStallSeconds);
}

void Beat() { gBeats.fetch_add(1, std::memory_order_relaxed); }

void Frame()
{
	uint8_t *player = api.FindPlayerPed();
	if (!player)
		return;
	char where[96];
	const int32_t state = PedState(player);
	const bool down = Health(player) <= 0.0f || state == kPedDie || state == kPedDead || state == kPedArrested;
	if (down && !gWasDown) {
		Where(where, sizeof where, player);
		Log("watch: %s %s%s", state == kPedArrested ? "busted" : "wasted", where,
		    gOnAMission && gOnAMission() ? ", on a mission" : "");
	}
	gWasDown = down;
	const bool onMission = gOnAMission && gOnAMission();
	if (onMission != gWasOnMission) {
		const uint8_t *newest = api.activeScripts ? *api.activeScripts : nullptr; // (scripts start at the head)
		char name[9] = {};
		if (newest)
			memcpy(name, ScriptName(newest), 8);
		if (onMission)
			Log("watch: a mission started (the newest script: %s)", name);
		else
			Log("watch: the mission ended (passed or failed)");
	}
	gWasOnMission = onMission;
	const bool cutscene = gCutscene && *gCutscene;
	if (cutscene != gWasCutscene)
		Log("watch: a cutscene %s", cutscene ? "started" : "ended");
	gWasCutscene = cutscene;
	if (gArea && *gArea != gWasArea) {
		if (gWasArea >= 0)
			Log("watch: into area %d%s", *gArea, *gArea == 0 ? " (outdoors)" : "");
		gWasArea = *gArea;
	}
}

} // namespace watch
