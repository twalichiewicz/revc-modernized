#include "anims.h"
#include "hooks.h"
#include "log.h"

#include <cstddef>
#include <cstdint>
#include <dlfcn.h>

namespace anims {
namespace {

constexpr int kStreamOffsetAnim = 0x1eec;       // CStreaming: an animation block's index after the models, TXDs, cols
constexpr int kStreamDependency = 4;            // STREAMFLAGS_DEPENDENCY
constexpr size_t kGroupSize = 0x20, kDefinitionSize = 0x28;
constexpr int kGroupStandard = 0;               // ASSOCGRP_STD
constexpr int kIdle = 3, kGetOutLeft = 0x57, kGetOutRight = 0x7f; // in it
// Anims that get a ped out of a vehicle (vans' and buses' own; ANIM_STD_*), for the stand-in.
constexpr int kGetOuts[] = {0xb1, 0xb5, 0xba};

uint8_t *const *gGroups;      // CAnimManager::ms_aAnimAssocGroups: CAnimBlendAssocGroup[] (list at +8, count +0x10, first +0x14)
const uint8_t *gDefinitions;  // CAnimManager::ms_aAnimAssocDefinitions: AnimAssocDefinition[] (block name at +8)
int (*gBlockIndex)(const char *name);
void (*gRequestModel)(int id, int flags);
void (*gLoadAllRequested)(bool onlyPriority);
void (*gAddBlockRef)(int block);
uint8_t *(*orig_Add)(void *clump, int group, int anim);
uint8_t *(*orig_Blend)(void *clump, int group, int anim, float delta);

bool Loaded(int group, int anim)
{
	const uint8_t *g = *gGroups + size_t(group) * kGroupSize;
	if (!*reinterpret_cast<void *const *>(g + 8))
		return false;
	const int32_t count = *reinterpret_cast<const int32_t *>(g + 0x10), first = *reinterpret_cast<const int32_t *>(g + 0x14);
	return anim - first >= 0 && anim - first < count;
}

// Its block, loaded now and kept loaded (it's wanted while it can go).
void LoadGroup(int group)
{
	const char *block = *reinterpret_cast<const char *const *>(gDefinitions + size_t(group) * kDefinitionSize + 8);
	const int index = block ? gBlockIndex(block) : -1;
	if (index < 0)
		return;
	gRequestModel(index + kStreamOffsetAnim, kStreamDependency);
	gLoadAllRequested(false);
	gAddBlockRef(index);
	Log("anims: the \"%s\" animations were wanted before they were loaded; loaded them", block);
}

// The group and animation to play: as asked, else a stand-in.
void Make(int &group, int &anim)
{
	if (group <= kGroupStandard || Loaded(group, anim))
		return;
	LoadGroup(group);
	if (Loaded(group, anim))
		return;
	bool out = false;
	for (int a : kGetOuts)
		out |= anim == a;
	static int logged;
	if (logged++ < 8)
		Log("anims: animation %d of group %d isn't there; %s instead", anim, group, out ? "getting out of a car" : "standing");
	group = kGroupStandard;
	anim = out ? (anim == 0xb5 ? kGetOutRight : kGetOutLeft) : kIdle;
}

uint8_t *Hook_Add(void *clump, int group, int anim)
{
	Make(group, anim);
	return orig_Add(clump, group, anim);
}

uint8_t *Hook_Blend(void *clump, int group, int anim, float delta)
{
	Make(group, anim);
	return orig_Blend(clump, group, anim, delta);
}

template <typename T>
bool Bind(T &out, const char *name)
{
	out = reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY, name));
	return out != nullptr;
}

} // namespace

bool Install()
{
	if (!Bind(gGroups, "_ZN12CAnimManager19ms_aAnimAssocGroupsE") ||
	    !Bind(gDefinitions, "_ZN12CAnimManager24ms_aAnimAssocDefinitionsE") ||
	    !Bind(gBlockIndex, "_ZN12CAnimManager22GetAnimationBlockIndexEPKc") ||
	    !Bind(gRequestModel, "_ZN10CStreaming12RequestModelEii") ||
	    !Bind(gLoadAllRequested, "_ZN10CStreaming22LoadAllRequestedModelsEb") ||
	    !Bind(gAddBlockRef, "_ZN12CAnimManager15AddAnimBlockRefEi"))
		return false;
	return hooks::Install("_ZN12CAnimManager12AddAnimationEPN2rw5ClumpE12AssocGroupId11AnimationId", Hook_Add, orig_Add) &&
	       hooks::Install("_ZN12CAnimManager14BlendAnimationEPN2rw5ClumpE12AssocGroupId11AnimationIdf", Hook_Blend,
	                      orig_Blend);
}

} // namespace anims
