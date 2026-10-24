#pragma once
// Runtime side of tools/patch_macvc.py: the patched binary carries a table of
// hook stubs; installing a hook means writing our function into its slot.
#include <cstdint>

namespace hooks {

// Finds the patch table in the main executable. `expectedUuid` is the LC_UUID
// of the build our offsets were derived from (null skips the check).
bool Init(const uint8_t expectedUuid[16]);

// Routes `mangledName` (no leading '_') to `replacement`. On success returns the
// entry point that runs the original function, otherwise nullptr.
void *Install(const char *mangledName, void *replacement);

// The slot of a skip patch: non-zero jumps over its block of game code.
// nullptr if the patch is missing.
volatile uint64_t *Flag(const char *name);

// Empties every slot, so all hooked functions run their original code again.
void UninstallAll();

// The running game's Mach-O header (= its __TEXT base); valid after Init.
const void *ImageBase();

// For hooks that recognise their caller by return address: true if the
// instruction before image base + `retOffset` is a `bl` to `mangledName`.
bool IsCallTo(uint32_t retOffset, const char *mangledName);

template <typename Fn>
bool Install(const char *mangledName, Fn replacement, Fn &original)
{
	original = reinterpret_cast<Fn>(Install(mangledName, reinterpret_cast<void *>(replacement)));
	return original != nullptr;
}

} // namespace hooks
