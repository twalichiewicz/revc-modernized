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

// Wrap the current target (or native trampoline when unclaimed), rather than
// replacing another feature's hook. Rejects chaining a function to itself.
void *Chain(const char *mangledName, void *replacement);

// The slot of a skip patch: non-zero jumps over its block of game code.
// nullptr if the patch is missing.
volatile uint64_t *Flag(const char *name);

// Empties every slot, so all hooked functions run their original code again.
void UninstallAll();

// The running game's Mach-O header (= its __TEXT base); valid after Init.
const void *ImageBase();

// Named caller/callee relocation for the separately compiled source host.
// Unknown source return addresses have no vanilla identity (UINT32_MAX).
#ifdef VC_SOURCE_UUID_BYTES
uint32_t ReturnOffset(const void *address);
const void *ReturnAddress(uint32_t vanillaOffset);
#else
inline uint32_t ReturnOffset(const void *address)
{
	const auto base = ImageBase();
	if (!base || !address) return UINT32_MAX;
	const uintptr_t offset = reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(base);
	return offset <= UINT32_MAX ? uint32_t(offset) : UINT32_MAX;
}
inline const void *ReturnAddress(uint32_t offset)
{
	const auto base = ImageBase();
	return base ? reinterpret_cast<const uint8_t *>(base) + offset : nullptr;
}
#endif

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
